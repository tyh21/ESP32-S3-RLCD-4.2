/**
 * Game Boy APU emulator.
 * Copyright (c) 2019 Mahyar Koshkouei <mk@deltabeard.com>
 * Copyright (c) 2017 Alex Baines <alex@abaines.me.uk>
 * minigb_apu is released under the terms of the MIT license.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "minigb_apu.h"

#define DMG_CLOCK_FREQ_U ((unsigned)DMG_CLOCK_FREQ)
#define AUDIO_NSAMPLES (AUDIO_SAMPLES_TOTAL)
#define MAX(a, b) (a > b ? a : b)
#define MIN(a, b) (a <= b ? a : b)

#ifndef FREQ_INC_MULT
#define FREQ_INC_MULT 105
#endif
#define FREQ_INC_REF (AUDIO_SAMPLE_RATE * FREQ_INC_MULT)
#define MAX_CHAN_VOLUME 15

static void set_note_freq(struct chan *c) {
  uint32_t freq = (DMG_CLOCK_FREQ_U / 4) / (2048 - c->freq);
  c->freq_inc = freq * (uint32_t)(FREQ_INC_REF / AUDIO_SAMPLE_RATE);
}

static void chan_enable(struct minigb_apu_ctx *ctx, uint_fast8_t i,
                        bool enable) {
  ctx->chans[i].enabled = enable;
  ctx->audio_mem[0xFF26 - AUDIO_ADDR_COMPENSATION] =
      (ctx->audio_mem[0xFF26 - AUDIO_ADDR_COMPENSATION] & 0x80) |
      (ctx->chans[3].enabled << 3) | (ctx->chans[2].enabled << 2) |
      (ctx->chans[1].enabled << 1) | ctx->chans[0].enabled;
}

static void update_env(struct chan *c) {
  c->env.counter += c->env.inc;
  while (c->env.counter > FREQ_INC_REF) {
    if (c->env.step) {
      c->volume += c->env.up ? 1 : -1;
      if (c->volume == 0 || c->volume == MAX_CHAN_VOLUME) {
        c->env.inc = 0;
      }
      c->volume = MAX(0, MIN(MAX_CHAN_VOLUME, c->volume));
    }
    c->env.counter -= FREQ_INC_REF;
  }
}

static void update_len(struct minigb_apu_ctx *ctx, struct chan *c) {
  if (!c->len.enabled) {
    return;
  }
  c->len.counter += c->len.inc;
  if (c->len.counter > FREQ_INC_REF) {
    chan_enable(ctx, (uint_fast8_t)(c - ctx->chans), false);
    c->len.counter = 0;
  }
}

static bool update_freq(struct chan *c, uint32_t *pos) {
  uint32_t inc = c->freq_inc - *pos;
  c->freq_counter += inc;
  if (c->freq_counter > FREQ_INC_REF) {
    *pos = c->freq_inc - (c->freq_counter - FREQ_INC_REF);
    c->freq_counter = 0;
    return true;
  }
  *pos = c->freq_inc;
  return false;
}

static void update_sweep(struct chan *c) {
  c->sweep.counter += c->sweep.inc;
  while (c->sweep.counter > FREQ_INC_REF) {
    if (c->sweep.shift) {
      uint16_t inc = c->sweep.freq >> c->sweep.shift;
      if (c->sweep.down) {
        inc = (uint16_t)-inc;
      }
      c->freq = c->sweep.freq + inc;
      if (c->freq > 2047) {
        c->enabled = 0;
      } else {
        set_note_freq(c);
        c->sweep.freq = c->freq;
      }
    } else if (c->sweep.rate) {
      c->enabled = 0;
    }
    c->sweep.counter -= FREQ_INC_REF;
  }
}

static void update_square(struct minigb_apu_ctx *ctx, audio_sample_t *samples,
                          bool ch2) {
  struct chan *c = &ctx->chans[ch2];
  if (!c->powered || !c->enabled) {
    return;
  }
  set_note_freq(c);
  for (uint_fast16_t i = 0; i < AUDIO_NSAMPLES; i += 2) {
    update_len(ctx, c);
    if (!c->enabled) {
      return;
    }
    update_env(c);
    if (!c->volume) {
      continue;
    }
    if (!ch2) {
      update_sweep(c);
    }
    uint32_t pos = 0;
    uint32_t prev_pos = 0;
    int32_t sample = 0;
    while (update_freq(c, &pos)) {
      c->square.duty_counter = (c->square.duty_counter + 1) & 7;
      sample += ((pos - prev_pos) / c->freq_inc) * c->val;
      c->val = (c->square.duty & (1 << c->square.duty_counter))
                   ? VOL_INIT_MAX / MAX_CHAN_VOLUME
                   : VOL_INIT_MIN / MAX_CHAN_VOLUME;
      prev_pos = pos;
    }
    sample += c->val;
    sample *= c->volume;
    sample /= 4;
    samples[i] += sample * c->on_left * ctx->vol_l;
    samples[i + 1] += sample * c->on_right * ctx->vol_r;
  }
}

static uint8_t wave_sample(struct minigb_apu_ctx *ctx, unsigned pos,
                           unsigned volume) {
  uint8_t sample =
      ctx->audio_mem[(0xFF30 + pos / 2) - AUDIO_ADDR_COMPENSATION];
  if (pos & 1) {
    sample &= 0x0F;
  } else {
    sample >>= 4;
  }
  return volume ? (sample >> (volume - 1)) : 0;
}

static void update_wave(struct minigb_apu_ctx *ctx, audio_sample_t *samples) {
  struct chan *c = &ctx->chans[2];
  if (!c->powered || !c->enabled || !c->volume) {
    return;
  }
  set_note_freq(c);
  c->freq_inc *= 2;
  for (uint_fast16_t i = 0; i < AUDIO_NSAMPLES; i += 2) {
    update_len(ctx, c);
    if (!c->enabled) {
      return;
    }
    uint32_t pos = 0;
    uint32_t prev_pos = 0;
    audio_sample_t sample = 0;
    c->wave.sample = wave_sample(ctx, (unsigned)c->val, c->volume);
    while (update_freq(c, &pos)) {
      c->val = (c->val + 1) & 31;
      sample += ((pos - prev_pos) / c->freq_inc) *
                ((audio_sample_t)c->wave.sample - 8) *
                (AUDIO_SAMPLE_MAX / 64);
      c->wave.sample = wave_sample(ctx, (unsigned)c->val, c->volume);
      prev_pos = pos;
    }
    sample += ((audio_sample_t)c->wave.sample - 8) *
              (audio_sample_t)(AUDIO_SAMPLE_MAX / 64);
    const audio_sample_t div[] = {AUDIO_SAMPLE_MAX, 1, 2, 4};
    sample /= div[c->volume];
    sample /= 4;
    samples[i] += sample * c->on_left * ctx->vol_l;
    samples[i + 1] += sample * c->on_right * ctx->vol_r;
  }
}

static void update_noise(struct minigb_apu_ctx *ctx, audio_sample_t *samples) {
  struct chan *c = &ctx->chans[3];
  if (c->freq >= 14) {
    c->enabled = 0;
  }
  if (!c->powered || !c->enabled) {
    return;
  }
  const uint32_t lfsr_div_lut[] = {8, 16, 32, 48, 64, 80, 96, 112};
  const uint32_t freq =
      DMG_CLOCK_FREQ_U / (lfsr_div_lut[c->noise.lfsr_div] << c->freq);
  c->freq_inc = freq * (uint32_t)(FREQ_INC_REF / AUDIO_SAMPLE_RATE);

  for (uint_fast16_t i = 0; i < AUDIO_NSAMPLES; i += 2) {
    update_len(ctx, c);
    if (!c->enabled) {
      return;
    }
    update_env(c);
    if (!c->volume) {
      continue;
    }
    uint32_t pos = 0;
    uint32_t prev_pos = 0;
    int32_t sample = 0;
    while (update_freq(c, &pos)) {
      c->noise.lfsr_reg =
          (c->noise.lfsr_reg << 1) |
          (c->val >= VOL_INIT_MAX / MAX_CHAN_VOLUME);
      if (c->noise.lfsr_wide) {
        c->val = !(((c->noise.lfsr_reg >> 14) & 1) ^
                   ((c->noise.lfsr_reg >> 13) & 1))
                     ? VOL_INIT_MAX / MAX_CHAN_VOLUME
                     : VOL_INIT_MIN / MAX_CHAN_VOLUME;
      } else {
        c->val = !(((c->noise.lfsr_reg >> 6) & 1) ^
                   ((c->noise.lfsr_reg >> 5) & 1))
                     ? VOL_INIT_MAX / MAX_CHAN_VOLUME
                     : VOL_INIT_MIN / MAX_CHAN_VOLUME;
      }
      sample += ((pos - prev_pos) / c->freq_inc) * c->val;
      prev_pos = pos;
    }
    sample += c->val;
    sample *= c->volume;
    sample /= 4;
    samples[i] += sample * c->on_left * ctx->vol_l;
    samples[i + 1] += sample * c->on_right * ctx->vol_r;
  }
}

void minigb_apu_audio_callback(struct minigb_apu_ctx *ctx,
                               audio_sample_t *stream) {
  memset(stream, 0, AUDIO_SAMPLES_TOTAL * sizeof(audio_sample_t));
  update_square(ctx, stream, false);
  update_square(ctx, stream, true);
  update_wave(ctx, stream);
  update_noise(ctx, stream);
}

static void chan_trigger(struct minigb_apu_ctx *ctx, uint_fast8_t i) {
  struct chan *c = &ctx->chans[i];
  chan_enable(ctx, i, true);
  c->volume = c->volume_init;
  const uint32_t inc_lut[8] = {
      840, 6720, 3360, 2240, 1680, 1344, 1120, 960,
  };
  uint8_t val =
      ctx->audio_mem[(0xFF12 + (i * 5)) - AUDIO_ADDR_COMPENSATION];
  c->env.step = val & 0x07;
  c->env.up = val & 0x08;
  c->env.inc = inc_lut[c->env.step];
  c->env.counter = 0;

  if (i == 0) {
    val = ctx->audio_mem[0xFF10 - AUDIO_ADDR_COMPENSATION];
    c->sweep.freq = c->freq;
    c->sweep.rate = (val >> 4) & 0x07;
    c->sweep.down = val & 0x08;
    c->sweep.shift = val & 0x07;
    c->sweep.inc = c->sweep.rate
                       ? ((128U * FREQ_INC_REF) /
                          (c->sweep.rate * AUDIO_SAMPLE_RATE))
                       : 0;
    c->sweep.counter = FREQ_INC_REF;
  }

  int len_max = 64;
  if (i == 2) {
    len_max = 256;
    c->val = 0;
  } else if (i == 3) {
    c->noise.lfsr_reg = 0xFFFF;
    c->val = VOL_INIT_MIN / MAX_CHAN_VOLUME;
  }
  c->len.inc = (256U * FREQ_INC_REF) /
               (AUDIO_SAMPLE_RATE * (unsigned)(len_max - c->len.load));
  c->len.counter = 0;
}

uint8_t minigb_apu_audio_read(struct minigb_apu_ctx *ctx, uint16_t addr) {
  static const uint8_t ortab[] = {
      0x80, 0x3f, 0x00, 0xff, 0xbf, 0xff, 0x3f, 0x00, 0xff, 0xbf,
      0x7f, 0xff, 0x9f, 0xff, 0xbf, 0xff, 0xff, 0x00, 0x00, 0xbf,
      0x00, 0x00, 0x70, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  return ctx->audio_mem[addr - AUDIO_ADDR_COMPENSATION] |
         ortab[addr - AUDIO_ADDR_COMPENSATION];
}

void minigb_apu_audio_write(struct minigb_apu_ctx *ctx, uint16_t addr,
                            uint8_t val) {
  if (addr == 0xFF26) {
    ctx->audio_mem[addr - AUDIO_ADDR_COMPENSATION] = val & 0x80;
    if ((val & 0x80) == 0) {
      memset(ctx->audio_mem, 0, 0xFF26 - AUDIO_ADDR_COMPENSATION);
      for (size_t i = 0; i < 4; i++) {
        ctx->chans[i].enabled = false;
      }
    }
    return;
  }
  if (ctx->audio_mem[0xFF26 - AUDIO_ADDR_COMPENSATION] == 0) {
    return;
  }

  ctx->audio_mem[addr - AUDIO_ADDR_COMPENSATION] = val;
  uint_fast8_t i = (addr - AUDIO_ADDR_COMPENSATION) / 5;
  switch (addr) {
  case 0xFF12:
  case 0xFF17:
  case 0xFF21:
    ctx->chans[i].volume_init = val >> 4;
    ctx->chans[i].powered = (val >> 3) != 0;
    if (ctx->chans[i].powered && ctx->chans[i].enabled) {
      if (ctx->chans[i].env.step == 0 && ctx->chans[i].env.inc != 0) {
        ctx->chans[i].volume += (val & 0x08) ? 1 : 2;
      } else {
        ctx->chans[i].volume = 16 - ctx->chans[i].volume;
      }
      ctx->chans[i].volume &= 0x0F;
      ctx->chans[i].env.step = val & 0x07;
    }
    break;
  case 0xFF1C:
    ctx->chans[i].volume = ctx->chans[i].volume_init = (val >> 5) & 0x03;
    break;
  case 0xFF11:
  case 0xFF16:
  case 0xFF20: {
    const uint8_t duty_lookup[] = {0x10, 0x30, 0x3C, 0xCF};
    ctx->chans[i].len.load = val & 0x3F;
    ctx->chans[i].square.duty = duty_lookup[val >> 6];
    break;
  }
  case 0xFF1B:
    ctx->chans[i].len.load = val;
    break;
  case 0xFF13:
  case 0xFF18:
  case 0xFF1D:
    ctx->chans[i].freq = (ctx->chans[i].freq & 0xFF00) | val;
    break;
  case 0xFF1A:
    ctx->chans[i].powered = (val & 0x80) != 0;
    chan_enable(ctx, i, val & 0x80);
    break;
  case 0xFF14:
  case 0xFF19:
  case 0xFF1E:
    ctx->chans[i].freq =
        (ctx->chans[i].freq & 0x00FF) | ((val & 0x07) << 8);
    /* Intentional fall-through. */
  case 0xFF23:
    ctx->chans[i].len.enabled = val & 0x40;
    if (val & 0x80) {
      chan_trigger(ctx, i);
    }
    break;
  case 0xFF22:
    ctx->chans[3].freq = val >> 4;
    ctx->chans[3].noise.lfsr_wide = !(val & 0x08);
    ctx->chans[3].noise.lfsr_div = val & 0x07;
    break;
  case 0xFF24:
    ctx->vol_l = (val >> 4) & 0x07;
    ctx->vol_r = val & 0x07;
    break;
  case 0xFF25:
    for (uint_fast8_t channel = 0; channel < 4; channel++) {
      ctx->chans[channel].on_left = (val >> (4 + channel)) & 1;
      ctx->chans[channel].on_right = (val >> channel) & 1;
    }
    break;
  default:
    break;
  }
}

void minigb_apu_audio_init(struct minigb_apu_ctx *ctx) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->chans[0].val = ctx->chans[1].val = -1;
  const uint8_t regs_init[] = {
      0x80, 0xBF, 0xF3, 0xFF, 0x3F, 0xFF, 0x3F, 0x00,
      0xFF, 0x3F, 0x7F, 0xFF, 0x9F, 0xFF, 0x3F, 0xFF,
      0xFF, 0x00, 0x00, 0x3F, 0x77, 0xF3, 0xF1,
  };
  for (uint_fast8_t i = 0; i < sizeof(regs_init); i++) {
    minigb_apu_audio_write(ctx, 0xFF10 + i, regs_init[i]);
  }
  const uint8_t wave_init[] = {
      0xAC, 0xDD, 0xDA, 0x48, 0x36, 0x02, 0xCF, 0x16,
      0x2C, 0x04, 0xE5, 0x2C, 0xAC, 0xDD, 0xDA, 0x48,
  };
  for (uint_fast8_t i = 0; i < sizeof(wave_init); i++) {
    minigb_apu_audio_write(ctx, 0xFF30 + i, wave_init[i]);
  }
}
