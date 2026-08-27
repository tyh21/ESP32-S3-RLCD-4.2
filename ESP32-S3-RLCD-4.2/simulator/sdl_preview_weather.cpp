// 实现 SDL 天气看板主体和 QWeather 图标码到字体字符的转换。
#include "sdl_preview_weather.h"

#include <stdlib.h>

#include "core/app_constexpr.h"
#include "sdl_preview_widgets.h"

LV_FONT_DECLARE(qweather_icons_36);
LV_FONT_DECLARE(zh_font_16);

namespace {
using sdl_preview_widgets::make_bar;
using sdl_preview_widgets::make_label;
using sdl_preview_widgets::make_label_with_font;
using sdl_preview_widgets::set_obj_black;

uint32_t weather_icon_codepoint(const char *code)
{
    int icon = atoi(code);
    if (icon >= 100 && icon <= 104) return 0xF101 + static_cast<uint32_t>(icon - 100);
    if (icon >= 150 && icon <= 153) return 0xF106 + static_cast<uint32_t>(icon - 150);
    if (icon >= 300 && icon <= 318) return 0xF10A + static_cast<uint32_t>(icon - 300);
    if (icon >= 350 && icon <= 351) return 0xF11D + static_cast<uint32_t>(icon - 350);
    if (icon == 399) return 0xF11F;
    if (icon >= 400 && icon <= 410) return 0xF120 + static_cast<uint32_t>(icon - 400);
    if (icon >= 456 && icon <= 457) return 0xF12B + static_cast<uint32_t>(icon - 456);
    if (icon == 499) return 0xF12D;
    if (icon >= 500 && icon <= 504) return 0xF12E + static_cast<uint32_t>(icon - 500);
    if (icon >= 507 && icon <= 515) return 0xF133 + static_cast<uint32_t>(icon - 507);
    if (icon >= 800 && icon <= 807) return 0xF13C + static_cast<uint32_t>(icon - 800);
    if (icon == 900) return 0xF144;
    if (icon == 901) return 0xF145;
    if (icon == 9999) return 0xF1CB;
    return 0xF146;
}

void style_weather_preview_card(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
}
} // namespace

const char *preview_weather_icon_text(const char *code)
{
    static char text[5];
    uint32_t cp = weather_icon_codepoint(code);
    text[0] = static_cast<char>(0xE0 | (cp >> 12));
    text[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    text[2] = static_cast<char>(0x80 | (cp & 0x3F));
    text[3] = '\0';
    return text;
}

void build_weather_board_preview_body(lv_obj_t *screen)
{
    if (!screen) {
        return;
    }

    make_label(screen, 20, 66, 135, 24, "杭州");
    make_label_with_font(screen, 20, 86, 88, 54, "26", &lv_font_montserrat_48);
    make_label_with_font(screen, 88, 96, 24, 32, "C", &lv_font_montserrat_24);
    lv_obj_t *icon = make_label(screen, 20, 143, 42, 40, preview_weather_icon_text("100"));
    lv_obj_set_style_text_font(icon, &qweather_icons_36, LV_PART_MAIN);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    make_label(screen, 62, 151, 92, 24, "晴");
    make_label(screen, 20, 179, 134, 22, "今日 22/29C");

    static constexpr const char *kDays[] = {
        "周二\n23日", "周三\n24日", "周四\n25日", "周五\n26日", "周六\n27日", "周日\n28日",
    };
    static constexpr const char *kTexts[] = {"晴", "多云转晴", "小到中雨", "阴", "雨夹雪", "大到暴雨"};
    static constexpr const char *kIcons[] = {"100", "101", "305", "104", "404", "306"};
    static constexpr const char *kRanges[] = {"22/29", "23/30", "-3/2", "22/28", "-8/-2", "18/24"};
    static constexpr int kCardX[] = {138, 180, 222, 264, 306, 348};
    static_assert(array_count(kDays) == array_count(kTexts) &&
                      array_count(kDays) == array_count(kIcons) &&
                      array_count(kDays) == array_count(kRanges) &&
                      array_count(kDays) == array_count(kCardX),
                  "weather preview forecast columns must stay aligned");
    constexpr int kCardY = 66;
    for (size_t i = 0; i < array_count(kDays); ++i) {
        int x = kCardX[i];
        lv_obj_t *card = lv_obj_create(screen);
        lv_obj_set_pos(card, x, kCardY);
        lv_obj_set_size(card, 34, 126);
        style_weather_preview_card(card);
        lv_obj_t *date = make_label_with_font(screen, x, kCardY, 34, 30, kDays[i], &zh_font_16);
        lv_label_set_long_mode(date, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(date, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_t *small_icon = make_label(screen,
                                          x,
                                          kCardY + 35,
                                          34,
                                          36,
                                          preview_weather_icon_text(kIcons[i]));
        lv_obj_set_style_text_font(small_icon, &qweather_icons_36, LV_PART_MAIN);
        lv_obj_set_style_text_align(small_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_t *text = make_label_with_font(screen, x, kCardY + 72, 34, 34, kTexts[i], &zh_font_16);
        lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_t *range = make_label_with_font(screen,
                                               x,
                                               kCardY + 108,
                                               34,
                                               16,
                                               kRanges[i],
                                               &lv_font_montserrat_12);
        lv_obj_set_style_text_align(range, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }

    lv_obj_t *detail_line = make_bar(screen, 20, 196, 360, 2);
    set_obj_black(detail_line, true);
    make_label(screen, 20, 202, 110, 22, "AQI 42 优");
    make_label(screen, 132, 202, 86, 22, "湿度 58%");
    make_label(screen, 228, 202, 152, 22, "东北风 3级");
    make_label(screen, 20, 224, 110, 20, "日出 05:01");
    make_label(screen, 132, 224, 120, 20, "日落 19:06");
    make_label(screen, 20, 246, 360, 22, "预警 大风蓝 / 暴雨黄 / 雷电橙");
    lv_obj_t *advice = make_label(screen, 20, 272, 360, 20, "天气平稳，适合轻装出行。");
    lv_label_set_long_mode(advice, LV_LABEL_LONG_WRAP);
}
