/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2022-2023 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "py/mphal.h"
#include "py/runtime.h"

#define SOLAR_OS_MICROPYTHON_HOOK_BRANCH_MASK 0x03ffU
#define SOLAR_OS_MICROPYTHON_YIELD_MS 5U

static const char *TAG = "mp_vm";

uint32_t solar_os_micropython_random_seed(void) {
    return esp_random();
}

__attribute__((weak)) bool solar_os_micropython_stop_requested(void) {
    return false;
}

__attribute__((weak)) void solar_os_micropython_stdout(const char *str, size_t len) {
    printf("%.*s", (int)len, str);
}

void solar_os_micropython_vm_hook(void) {
    static uint32_t branch_count;
    static TickType_t last_yield_tick;
    static bool stop_logged;

    if (solar_os_micropython_stop_requested()) {
        if (!stop_logged) {
            ESP_LOGI(TAG, "stop requested visible in VM hook");
            stop_logged = true;
        }
        mp_sched_keyboard_interrupt();
    } else {
        stop_logged = false;
    }

    branch_count++;
    if ((branch_count & SOLAR_OS_MICROPYTHON_HOOK_BRANCH_MASK) == 0) {
        const TickType_t now = xTaskGetTickCount();
        const TickType_t yield_ticks = pdMS_TO_TICKS(SOLAR_OS_MICROPYTHON_YIELD_MS);
        const TickType_t min_delta = yield_ticks > 0 ? yield_ticks : 1;
        if ((now - last_yield_tick) >= min_delta) {
            last_yield_tick = now;
            vTaskDelay(1);
        }
    }
}

// Send string of given length to stdout, converting \n to \r\n.
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    solar_os_micropython_stdout(str, len);
}
