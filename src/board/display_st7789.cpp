#include "display_st7789.h"

#include <Arduino.h>
#include <string.h>

#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

#include "../config.h"

static const char* TAG = "st7789";

namespace board {

bool DisplaySt7789::onTransDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* ctx) {
    DisplaySt7789* self = static_cast<DisplaySt7789*>(ctx);
    if (self->done_) self->done_();
    return false;
}

// Panel tuning from Waveshare's ESP32-S3-LCD-1.9 demo (08_LVGL_Test/lcd_bsp.c).
// Sleep-out / display-on / MADCTL / COLMOD are handled by the IDF driver.
struct TuneCmd { uint8_t cmd; uint8_t n; uint8_t p[14]; };
static const TuneCmd kTuning[] = {
    {0xB2, 5, {0x0c, 0x0c, 0x00, 0x33, 0x33}},                 // porch control
    {0xB7, 1, {0x35}},                                         // gate control
    {0xBB, 1, {0x13}},                                         // VCOM
    {0xC0, 1, {0x2c}},                                         // LCM control
    {0xC2, 1, {0x01}},                                         // VDV/VRH enable
    {0xC3, 1, {0x0b}},                                         // VRH
    {0xC4, 1, {0x20}},                                         // VDV
    {0xC6, 1, {0x0f}},                                         // frame rate 60 Hz
    {0xD0, 2, {0xa4, 0xa1}},                                   // power control 1
    {0xD6, 1, {0xa1}},
    {0xE0, 14, {0x00, 0x03, 0x07, 0x08, 0x07, 0x15, 0x2A, 0x44, 0x42, 0x0A, 0x17, 0x18, 0x25, 0x27}},  // gamma +
    {0xE1, 14, {0x00, 0x03, 0x08, 0x07, 0x07, 0x23, 0x2A, 0x43, 0x42, 0x09, 0x18, 0x17, 0x25, 0x27}},  // gamma -
};

void DisplaySt7789::sendTuning() {
    for (const TuneCmd& c : kTuning) esp_lcd_panel_io_tx_param(io_, c.cmd, c.p, c.n);
}

bool DisplaySt7789::begin(DoneCb done) {
    done_ = done;

    bufPixels_ = (size_t)LCD_H_RES * LCD_DRAW_BUF_LINES;
    for (int i = 0; i < 2; i++) {
        buf_[i] = heap_caps_malloc(bufPixels_ * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!buf_[i]) {
            ESP_LOGE(TAG, "draw buffer alloc failed");
            return false;
        }
    }

    spi_bus_config_t bus;
    memset(&bus, 0, sizeof bus);
    bus.mosi_io_num = PIN_LCD_MOSI;
    bus.miso_io_num = -1;
    bus.sclk_io_num = PIN_LCD_CLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = (int)(bufPixels_ * 2);
    if (spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed");
        return false;
    }

    esp_lcd_panel_io_spi_config_t io;
    memset(&io, 0, sizeof io);
    io.dc_gpio_num = PIN_LCD_DC;
    io.cs_gpio_num = PIN_LCD_CS;
    io.pclk_hz = LCD_SPI_HZ;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    io.spi_mode = 0;
    io.trans_queue_depth = 10;
    io.on_color_trans_done = onTransDone;
    io.user_ctx = this;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io, &io_) != ESP_OK) {
        ESP_LOGE(TAG, "panel io failed");
        return false;
    }

    esp_lcd_panel_dev_config_t pc;
    memset(&pc, 0, sizeof pc);
    pc.reset_gpio_num = PIN_LCD_RST;
    pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    pc.bits_per_pixel = 16;
    if (esp_lcd_new_panel_st7789(io_, &pc, &panel_) != ESP_OK) {
        ESP_LOGE(TAG, "st7789 panel failed");
        return false;
    }
    esp_lcd_panel_reset(panel_);
    esp_lcd_panel_init(panel_);
    sendTuning();
    esp_lcd_panel_invert_color(panel_, true);        // this panel needs INVON
    // Landscape: MV + MX (MADCTL 0x60), or MV + MY when mounted the other way.
    esp_lcd_panel_swap_xy(panel_, true);
    esp_lcd_panel_mirror(panel_, !LCD_ROTATE_180, LCD_ROTATE_180);
    esp_lcd_panel_set_gap(panel_, LCD_X_GAP, LCD_Y_GAP);
    esp_lcd_panel_disp_on_off(panel_, true);
    ESP_LOGI(TAG, "panel up, %dx%d", LCD_H_RES, LCD_V_RES);

    ledcAttach(PIN_LCD_BL, BL_PWM_FREQ_HZ, BL_PWM_RES_BITS);
    setBacklight(BL_DEFAULT_PERCENT);
    return true;
}

void DisplaySt7789::flush(int x1, int y1, int x2, int y2, const void* px) {
    // esp_lcd_panel_draw_bitmap takes exclusive end coordinates.
    esp_lcd_panel_draw_bitmap(panel_, x1, y1, x2 + 1, y2 + 1, px);
}

void DisplaySt7789::setBacklight(uint8_t percent) {
    if (percent > 100) percent = 100;
    backlight_ = percent;
    uint32_t maxDuty = (1u << BL_PWM_RES_BITS) - 1;
    ledcWrite(PIN_LCD_BL, (uint32_t)percent * maxDuty / 100);
}

}  // namespace board
