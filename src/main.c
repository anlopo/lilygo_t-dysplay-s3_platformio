/*
Improve performance:
CONFIG_LV_USE_OBSERVER=y
CONFIG_LV_USE_SYSMON=y
CONFIG_LV_USE_PERF_MONITOR=y
*/

/*
LCD PINS

LCD_D0 39
LCD_D1 40
LCD_D2 41
LCD_D3 42
LCD_D4 45
LCD_D5 46
LCD_D6 47
LCD_D7 48
LCD_BL 38
LCD_WR  08
LCD_RD  09
LCD_DC  07
LCD_CS  06
LCD_RES 05
LCD_PowerOn 15

ST7789V
170(H)RGB x320(V) 8-Bit Parallel Interface
IO14 Button
*/

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lvgl_port.h"

#define DISP_WIDTH 170
#define DISP_HEIGHT 320

#define LCD_D0 39
#define LCD_D1 40
#define LCD_D2 41
#define LCD_D3 42
#define LCD_D4 45
#define LCD_D5 46
#define LCD_D6 47
#define LCD_D7 48
#define LCD_WR 8
#define LCD_RD 9
#define LCD_DC 7
#define LCD_CS 6
#define LCD_RES 5
#define LCD_BL 38
#define LCD_PowerOn 15
#define PIN_BUTTON_1 0
#define PIN_BUTTON_2 14
#define PIN_BAT_VOLT 4

#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY (4096)                // Set duty to 50%. (2 ** 13) * 50% = 4096
#define LEDC_FREQUENCY (4000)           // Frequency in Hertz. Set frequency at 4 kHz

#define EXAMPLE_LCD_PIXEL_CLOCK_HZ (10 * 1000 * 1000)

#define LVGL_LCD_BUF_SIZE (DISP_HEIGHT * DISP_WIDTH)

#define LCD_MODULE_ST7789V

#if defined(LCD_MODULE_ST7789V)
typedef struct
{
    uint8_t cmd;
    uint8_t data[14];
    uint8_t len;
} lcd_cmd_t;

lcd_cmd_t lcd_st7789v[] = {
    {0x11, {0}, 0 | 0x80},
    {0x3A, {0X05}, 1},
    {0xB2, {0X0B, 0X0B, 0X00, 0X33, 0X33}, 5},
    {0xB7, {0X75}, 1},
    {0xBB, {0X28}, 1},
    {0xC0, {0X2C}, 1},
    {0xC2, {0X01}, 1},
    {0xC3, {0X1F}, 1},
    {0xC6, {0X13}, 1},
    {0xD0, {0XA7}, 1},
    {0x21, {0}, 0}, // Inverze color
    {0xD0, {0XA4, 0XA1}, 2},
    {0xD6, {0XA1}, 1},
    {0xE0, {0XF0, 0X05, 0X0A, 0X06, 0X06, 0X03, 0X2B, 0X32, 0X43, 0X36, 0X11, 0X10, 0X2B, 0X32}, 14},
    {0xE1, {0XF0, 0X08, 0X0C, 0X0B, 0X09, 0X24, 0X2B, 0X22, 0X43, 0X38, 0X15, 0X16, 0X2F, 0X37}, 14},

};
#endif

esp_lcd_panel_io_handle_t io_handle = NULL;
esp_lcd_i80_bus_handle_t i80_bus = NULL;
static lv_disp_t *disp_handle;
esp_lcd_panel_handle_t lcd_panel_handle;

void lcd_init();
void gpio_init();
void lvgl_init();
bool display_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *);

void app_main()
{
    gpio_init();
    lcd_init();
    lvgl_init();

    lvgl_port_lock(0);

    /* Change the active screen's background color */
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x003a57), LV_PART_MAIN);

    /* Create a white label, set its text and align it to the center */
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Hello world");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    lvgl_port_unlock();

    ESP_LOGW("ESP", "Starting infinite loop");
    int pause = 0;
    while (true)
    {
        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
        pause++;
        if (pause > 200)
        {
            pause = 0;
            ESP_LOGI("while loop", "alive");
        }
    }
}

void lcd_init()
{
    esp_lcd_i80_bus_config_t bus_config = {
        .dc_gpio_num = LCD_DC,
        .wr_gpio_num = LCD_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {
            LCD_D0,
            LCD_D1,
            LCD_D2,
            LCD_D3,
            LCD_D4,
            LCD_D5,
            LCD_D6,
            LCD_D7},
        .bus_width = 8,
        .max_transfer_bytes = LVGL_LCD_BUF_SIZE * sizeof(uint16_t),
        .psram_trans_align = 64,
        .sram_trans_align = 4};
    ESP_LOGI("lcd_init()", "Before esp_lcd_new_i80_bus()");
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));

    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = LCD_CS,
        .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 20,
        //.on_color_trans_done = display_notify_lvgl_flush_ready,
        .on_color_trans_done = NULL,
        //.user_ctx = &disp_drv,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        }};
    ESP_LOGI("lcd_init()", "Before esp_lcd_new_panel_io_i80()");
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RES,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = NULL};
    ESP_LOGI("lcd_init()", "Before esp_lcd_new_panel_st7789()");
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &lcd_panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel_handle));

    // the gap is LCD panel specific, even panels with the same driver IC, can have different gap value
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(lcd_panel_handle, 35, 0));

#if defined(LCD_MODULE_ST7789V)
    for (uint8_t i = 0; i < (sizeof(lcd_st7789v) / sizeof(lcd_cmd_t)); i++)
    {
        esp_lcd_panel_io_tx_param(io_handle, lcd_st7789v[i].cmd, lcd_st7789v[i].data, lcd_st7789v[i].len & 0x7f);
        if (lcd_st7789v[i].len & 0x80)
            vTaskDelay(pdMS_TO_TICKS(120));
    }
#endif

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd_panel_handle, true));
}

void lvgl_init()
{
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_LOGI("lvgl_init()", "Before lvgl_port_init()");
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    /* Add LCD screen */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = lcd_panel_handle,
        .buffer_size = DISP_WIDTH * DISP_HEIGHT * sizeof(lv_color_t),
        .double_buffer = true,
        .hres = DISP_WIDTH,
        .vres = DISP_HEIGHT,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false},
        .flags = {.buff_dma = true, .buff_spiram = true, .swap_bytes = true}};
    ESP_LOGI("lvgl_init()", "Before lvgl_port_add_disp()");
    disp_handle = lvgl_port_add_disp(&disp_cfg);
}

void powerSave()
{
    // set task_max_sleep_ms to big value
    // wake up with lvgl_port_task_wake()

    // Stopping the timer
    ESP_ERROR_CHECK(lvgl_port_stop());

    // resume LVGL timer after wake
    ESP_ERROR_CHECK(lvgl_port_resume());
}

void gpio_init()
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY, // Set output frequency at 4 kHz
        .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = LCD_BL,
        .duty = 0, // Set duty to 0%
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    ESP_ERROR_CHECK(gpio_set_direction(LCD_PowerOn, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_direction(LCD_RD, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(LCD_PowerOn, 1));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
    ESP_ERROR_CHECK(gpio_set_level(LCD_RD, 1));
}

void deinit_restart()
{
    ESP_ERROR_CHECK(lvgl_port_stop());
    ESP_ERROR_CHECK(lvgl_port_remove_disp(disp_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_del(lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_io_del(io_handle));
    ESP_ERROR_CHECK(esp_lcd_del_i80_bus(i80_bus));
    ESP_ERROR_CHECK(gpio_set_level(LCD_BL, 0));
    ESP_ERROR_CHECK(gpio_set_level(LCD_RD, 0));
    ESP_ERROR_CHECK(gpio_set_level(LCD_PowerOn, 0));
    ESP_ERROR_CHECK(gpio_reset_pin(LCD_BL));
    ESP_ERROR_CHECK(gpio_reset_pin(LCD_RD));
    ESP_ERROR_CHECK(gpio_reset_pin(LCD_PowerOn));
    esp_restart();
}

/*bool display_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_flush_ready(&disp_drv);
    return false;
}*/