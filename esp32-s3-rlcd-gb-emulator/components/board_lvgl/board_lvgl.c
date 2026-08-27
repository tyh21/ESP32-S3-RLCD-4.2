#include "board_lvgl.h"

#include <stdint.h>

#include "board_rlcd.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "board_lvgl";

#define BOARD_LVGL_TICK_PERIOD_MS      5
#define BOARD_LVGL_TASK_STACK_SIZE     (8 * 1024)
#define BOARD_LVGL_TASK_PRIORITY       5
#define BOARD_LVGL_TASK_CORE           0
#define BOARD_LVGL_TASK_MIN_DELAY_MS   5
#define BOARD_LVGL_TASK_MAX_DELAY_MS   50

/*
 * 这里使用 40 行 partial buffer：
 *   I1 最大 stride 约 52 字节，40 行约 2KB。
 *
 * 本屏是 1-bit 黑白屏，LVGL 也直接使用 I1 渲染。
 * 这样可以避免先画 RGB565、再强制阈值转黑白造成的边缘发毛。
 */
#define BOARD_LVGL_BUFFER_LINES        40

static SemaphoreHandle_t s_lvgl_mutex = NULL;
static lv_display_t *s_display = NULL;
static esp_timer_handle_t s_tick_timer = NULL;
static bool s_initialized = false;

static const lv_font_t *board_lvgl_get_text_font(void)
{
#if LV_FONT_UNSCII_8
    return &lv_font_unscii_8;
#else
    return LV_FONT_DEFAULT;
#endif
}

static lv_obj_t *board_lvgl_create_panel(lv_obj_t *parent, const char *title_text)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_style_bg_color(panel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, 4, LV_PART_MAIN);

    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_color(title, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, board_lvgl_get_text_font(), LV_PART_MAIN);

    return panel;
}

static void board_lvgl_add_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, board_lvgl_get_text_font(), LV_PART_MAIN);
}

static void board_lvgl_tick_timer_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(BOARD_LVGL_TICK_PERIOD_MS);
}

bool board_lvgl_lock(int timeout_ms)
{
    if (s_lvgl_mutex == NULL) {
        return false;
    }

    TickType_t timeout_ticks = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_lvgl_mutex, timeout_ticks) == pdTRUE;
}

void board_lvgl_unlock(void)
{
    if (s_lvgl_mutex != NULL) {
        xSemaphoreGive(s_lvgl_mutex);
    }
}

/*
 * LVGL9 flush 回调。
 *
 * LVGL 现在直接使用 I1 颜色格式：
 *   1 个 bit 表示 1 个像素；
 *   bit=1 表示亮色，bit=0 表示暗色。
 *
 * I1 是 indexed 格式，px_map 开头有 2 个 palette 项，
 * 每个 palette 项 4 字节，所以真正像素数据从 px_map + 8 开始。
 */
static void board_lvgl_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    (void)display;

    const uint16_t width = (uint16_t)lv_area_get_width(area);
    const uint16_t height = (uint16_t)lv_area_get_height(area);
    const uint32_t palette_size = LV_COLOR_INDEXED_PALETTE_SIZE(LV_COLOR_FORMAT_I1) * 4;
    const uint32_t stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_I1);
    const uint8_t *bits = px_map + palette_size;

    for (uint16_t row = 0; row < height; row++) {
        const uint8_t *row_bits = bits + row * stride;

        for (uint16_t col = 0; col < width; col++) {
            bool pixel_on = (row_bits[col / 8] & (uint8_t)(1U << (7 - (col % 8)))) != 0;
            board_rlcd_color_t color = pixel_on ? BOARD_RLCD_COLOR_WHITE : BOARD_RLCD_COLOR_BLACK;

            (void)board_rlcd_set_pixel((uint16_t)(area->x1 + col), (uint16_t)(area->y1 + row), color);
        }
    }

    /*
     * 当前先继续使用整屏 flush，避开 ST7305 局部刷新打包问题。
     */
    (void)board_rlcd_flush();
    lv_display_flush_ready(display);
}

static void board_lvgl_task(void *arg)
{
    (void)arg;

    while (1) {
        uint32_t delay_ms = BOARD_LVGL_TASK_MAX_DELAY_MS;

        if (board_lvgl_lock(-1)) {
            delay_ms = lv_timer_handler();
            board_lvgl_unlock();
        }

        if (delay_ms < BOARD_LVGL_TASK_MIN_DELAY_MS) {
            delay_ms = BOARD_LVGL_TASK_MIN_DELAY_MS;
        } else if (delay_ms > BOARD_LVGL_TASK_MAX_DELAY_MS) {
            delay_ms = BOARD_LVGL_TASK_MAX_DELAY_MS;
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t board_lvgl_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (!board_rlcd_is_initialized()) {
        ESP_LOGE(TAG, "RLCD is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_lvgl_mutex = xSemaphoreCreateMutex();
    if (s_lvgl_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_init();

    s_display = lv_display_create(board_rlcd_get_logical_width(), board_rlcd_get_logical_height());
    if (s_display == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_I1);
    lv_display_set_flush_cb(s_display, board_lvgl_flush_cb);

    uint32_t buffer_stride = lv_draw_buf_width_to_stride(BOARD_RLCD_MAX_LOGICAL_WIDTH, LV_COLOR_FORMAT_I1);
    uint32_t buffer_size = buffer_stride * BOARD_LVGL_BUFFER_LINES +
                           LV_COLOR_INDEXED_PALETTE_SIZE(LV_COLOR_FORMAT_I1) * 4;
    void *buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffer");
        return ESP_ERR_NO_MEM;
    }

    lv_display_set_buffers(s_display, buffer, NULL, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    const esp_timer_create_args_t tick_timer_args = {
        .callback = board_lvgl_tick_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick",
        .skip_unhandled_events = true,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_timer_args, &s_tick_timer), TAG, "create LVGL tick timer failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_tick_timer, BOARD_LVGL_TICK_PERIOD_MS * 1000), TAG, "start LVGL tick timer failed");

    BaseType_t task_ret = xTaskCreatePinnedToCore(
        board_lvgl_task,
        "board_lvgl",
        BOARD_LVGL_TASK_STACK_SIZE,
        NULL,
        BOARD_LVGL_TASK_PRIORITY,
        NULL,
        BOARD_LVGL_TASK_CORE
    );
    if (task_ret != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "LVGL9 initialized with I1 %lu-byte partial buffer", (unsigned long)buffer_size);
    return ESP_OK;
}

lv_display_t *board_lvgl_get_display(void)
{
    return s_display;
}

esp_err_t board_lvgl_set_rotation(board_rlcd_rotation_t rotation)
{
    if (!s_initialized || s_display == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!board_lvgl_lock(-1)) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = board_rlcd_set_rotation(rotation);
    if (ret == ESP_OK) {
        ESP_RETURN_ON_ERROR(board_rlcd_clear(BOARD_RLCD_COLOR_WHITE), TAG, "clear RLCD before rotation redraw failed");
        lv_display_set_resolution(s_display, board_rlcd_get_logical_width(), board_rlcd_get_logical_height());
        lv_obj_set_size(lv_screen_active(), lv_pct(100), lv_pct(100));
        lv_obj_update_layout(lv_screen_active());
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(s_display);
    }

    board_lvgl_unlock();
    return ret;
}

esp_err_t board_lvgl_create_test_screen(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!board_lvgl_lock(-1)) {
        return ESP_ERR_TIMEOUT;
    }

    static int32_t grid_cols[] = {LV_GRID_FR(2), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t grid_rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    ESP_RETURN_ON_ERROR(board_rlcd_clear(BOARD_RLCD_COLOR_WHITE), TAG, "clear RLCD before test screen failed");

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_font(screen, board_lvgl_get_text_font(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(screen, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(screen, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "LVGL FLEX + GRID ROTATION TEST");
    lv_obj_set_style_text_color(title, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, board_lvgl_get_text_font(), LV_PART_MAIN);

    lv_obj_t *meta = lv_label_create(screen);
    lv_label_set_text(meta, "GPIO18 rotates 90 deg");
    lv_obj_set_style_text_color(meta, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_font(meta, board_lvgl_get_text_font(), LV_PART_MAIN);

    lv_obj_t *flex_row = lv_obj_create(screen);
    lv_obj_set_width(flex_row, lv_pct(100));
    lv_obj_set_height(flex_row, lv_pct(34));
    lv_obj_set_style_bg_color(flex_row, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(flex_row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(flex_row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(flex_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(flex_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(flex_row, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(flex_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(flex_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *card_a = board_lvgl_create_panel(flex_row, "FLEX A");
    lv_obj_set_width(card_a, lv_pct(32));
    lv_obj_set_height(card_a, lv_pct(100));
    board_lvgl_add_label(card_a, "one");
    board_lvgl_add_label(card_a, "wide");

    lv_obj_t *card_b = board_lvgl_create_panel(flex_row, "FLEX B");
    lv_obj_set_width(card_b, lv_pct(32));
    lv_obj_set_height(card_b, lv_pct(100));
    board_lvgl_add_label(card_b, "two");
    board_lvgl_add_label(card_b, "auto wrap");

    lv_obj_t *card_c = board_lvgl_create_panel(flex_row, "FLEX C");
    lv_obj_set_width(card_c, lv_pct(32));
    lv_obj_set_height(card_c, lv_pct(100));
    board_lvgl_add_label(card_c, "three");
    board_lvgl_add_label(card_c, "resize");

    lv_obj_t *grid = lv_obj_create(screen);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_style_bg_color(grid, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(grid, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(grid, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(grid, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, 6, LV_PART_MAIN);
    lv_obj_set_grid_dsc_array(grid, grid_cols, grid_rows);

    lv_obj_t *main_cell = board_lvgl_create_panel(grid, "GRID 2FR");
    lv_obj_set_grid_cell(main_cell,
                         LV_GRID_ALIGN_STRETCH,
                         0,
                         1,
                         LV_GRID_ALIGN_STRETCH,
                         0,
                         2);
    board_lvgl_add_label(main_cell, "This tall cell spans two rows.");
    board_lvgl_add_label(main_cell, "It should become narrower in portrait.");

    lv_obj_t *top_cell = board_lvgl_create_panel(grid, "GRID 1FR");
    lv_obj_set_grid_cell(top_cell,
                         LV_GRID_ALIGN_STRETCH,
                         1,
                         1,
                         LV_GRID_ALIGN_STRETCH,
                         0,
                         1);
    board_lvgl_add_label(top_cell, "top");

    lv_obj_t *bottom_cell = board_lvgl_create_panel(grid, "GRID 1FR");
    lv_obj_set_grid_cell(bottom_cell,
                         LV_GRID_ALIGN_STRETCH,
                         1,
                         1,
                         LV_GRID_ALIGN_STRETCH,
                         1,
                         1);
    board_lvgl_add_label(bottom_cell, "bottom");

    lv_scr_load(screen);
    board_lvgl_unlock();

    return ESP_OK;
}
