// 集中发布关于本机页面状态，避免 UI、输入和 OTA 读取到混合字段。
#include "ui_info_page_state.h"

#include "freertos/FreeRTOS.h"

namespace {
portMUX_TYPE s_info_page_mux = portMUX_INITIALIZER_UNLOCKED;
InfoPageStateSnapshot s_info_page_state;
}

void info_page_state_load(InfoPageStateSnapshot *out)
{
    if (!out) {
        return;
    }
    portENTER_CRITICAL(&s_info_page_mux);
    *out = s_info_page_state;
    portEXIT_CRITICAL(&s_info_page_mux);
}

bool info_page_requested()
{
    InfoPageStateSnapshot state;
    info_page_state_load(&state);
    return state.requested;
}

void info_page_request(uint32_t hold_until_tick)
{
    portENTER_CRITICAL(&s_info_page_mux);
    s_info_page_state.requested = true;
    s_info_page_state.hold_until_tick = hold_until_tick;
    portEXIT_CRITICAL(&s_info_page_mux);
}

void info_page_clear()
{
    portENTER_CRITICAL(&s_info_page_mux);
    s_info_page_state = {};
    portEXIT_CRITICAL(&s_info_page_mux);
}

void info_page_hold_until_store(uint32_t hold_until_tick)
{
    portENTER_CRITICAL(&s_info_page_mux);
    s_info_page_state.hold_until_tick = hold_until_tick;
    portEXIT_CRITICAL(&s_info_page_mux);
}
