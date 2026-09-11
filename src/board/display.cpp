#include "display.h"

#include <Arduino.h>
#include <string.h>

#include "driver/spi_master.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"

#include "../config.h"
#include "tca9554.h"

static const char* TAG = "display";

namespace board {

// ---------------------------------------------------------------------------
// 9-bit SPI helpers: the ST7701 3-wire mode prefixes each byte with a D/C bit.
// We let the SPI peripheral emit it as a 1-bit "command" phase followed by an
// 8-bit "address" phase, exactly as the Waveshare demo does.
// ---------------------------------------------------------------------------
void Display::spiCmd(uint8_t cmd) {
    spi_transaction_t t;
    memset(&t, 0, sizeof t);
    t.cmd  = 0;    // D/C = 0 -> command
    t.addr = cmd;
    spi_device_transmit((spi_device_handle_t)spi_, &t);
}

void Display::spiData(uint8_t data) {
    spi_transaction_t t;
    memset(&t, 0, sizeof t);
    t.cmd  = 1;    // D/C = 1 -> data
    t.addr = data;
    spi_device_transmit((spi_device_handle_t)spi_, &t);
}

// ---------------------------------------------------------------------------
// ST7701 register table (from Waveshare LVGL_Arduino/Display_ST7701.cpp).
// Each entry: command, parameter count, parameters.  A count of 0xFF marks a
// delay of `params[0]*10` ms instead.
// ---------------------------------------------------------------------------
struct InitCmd {
    uint8_t cmd;
    uint8_t n;
    uint8_t p[16];
};

static const InitCmd kInit[] = {
    // Command2 BK0
    {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x10}},
    {0xC0, 2, {0x3B, 0x00}},                      // scan lines
    {0xC1, 2, {0x0B, 0x02}},                      // VBP
    {0xC2, 2, {0x07, 0x02}},
    {0xCC, 1, {0x10}},
    {0xCD, 1, {0x08}},                            // RGB format
    {0xB0, 16, {0x00, 0x11, 0x16, 0x0e, 0x11, 0x06, 0x05, 0x09,
                0x08, 0x21, 0x06, 0x13, 0x10, 0x29, 0x31, 0x18}},  // gamma +
    {0xB1, 16, {0x00, 0x11, 0x16, 0x0e, 0x11, 0x07, 0x05, 0x09,
                0x09, 0x21, 0x05, 0x13, 0x11, 0x2a, 0x31, 0x18}},  // gamma -
    // Command2 BK1
    {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x11}},
    {0xB0, 1, {0x6d}},                            // VOP
    {0xB1, 1, {0x37}},                            // VCOM
    {0xB2, 1, {0x81}},                            // VGH 12 V
    {0xB3, 1, {0x80}},
    {0xB5, 1, {0x43}},                            // VGL -8.3 V
    {0xB7, 1, {0x85}},
    {0xB8, 1, {0x20}},
    {0xC1, 1, {0x78}},
    {0xC2, 1, {0x78}},
    {0xD0, 1, {0x88}},
    {0xE0, 3, {0x00, 0x00, 0x02}},
    {0xE1, 11, {0x03, 0xA0, 0x00, 0x00, 0x04, 0xA0, 0x00, 0x00, 0x00, 0x20, 0x20}},
    {0xE2, 13, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {0xE3, 4, {0x00, 0x00, 0x11, 0x00}},
    {0xE4, 2, {0x22, 0x00}},
    {0xE5, 16, {0x05, 0xEC, 0xA0, 0xA0, 0x07, 0xEE, 0xA0, 0xA0,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {0xE6, 4, {0x00, 0x00, 0x11, 0x00}},
    {0xE7, 2, {0x22, 0x00}},
    {0xE8, 16, {0x06, 0xED, 0xA0, 0xA0, 0x08, 0xEF, 0xA0, 0xA0,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {0xEB, 7, {0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00}},
    {0xED, 16, {0xFF, 0xFF, 0xFF, 0xBA, 0x0A, 0xBF, 0x45, 0xFF,
                0xFF, 0x54, 0xFB, 0xA0, 0xAB, 0xFF, 0xFF, 0xFF}},
    {0xEF, 6, {0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F}},
    // Command2 BK3
    {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x13}},
    {0xEF, 1, {0x08}},
    // Back to Command1
    {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x00}},
    {0x36, 1, {0x00}},                            // MADCTL
    {0x3A, 1, {0x66}},                            // COLMOD: 18-bit RGB interface
    {0x11, 0, {}},                                // sleep out
    {0x00, 0xFF, {48}},                           // 480 ms
    {0x20, 0, {}},                                // inversion off
    {0x00, 0xFF, {12}},                           // 120 ms
    {0x29, 0, {}},                                // display on
};

void Display::sendInitSequence() {
    for (const InitCmd& c : kInit) {
        if (c.n == 0xFF) {
            delay(c.p[0] * 10);
            continue;
        }
        spiCmd(c.cmd);
        for (uint8_t i = 0; i < c.n; i++) spiData(c.p[i]);
    }
}

bool Display::begin(Tca9554& io) {
    // --- hard reset via IO expander ---
    io.setOutput(EXIO_LCD_RST, false);
    delay(10);
    io.setOutput(EXIO_LCD_RST, true);
    delay(50);

    // --- 3-wire SPI for register init ---
    spi_bus_config_t bus;
    memset(&bus, 0, sizeof bus);
    bus.mosi_io_num = PIN_LCD_SPI_SDA;
    bus.miso_io_num = -1;
    bus.sclk_io_num = PIN_LCD_SPI_SCL;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 64;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed");
        return false;
    }
    spi_device_interface_config_t dev;
    memset(&dev, 0, sizeof dev);
    dev.command_bits = 1;
    dev.address_bits = 8;
    dev.mode = 0;
    dev.clock_speed_hz = 40 * 1000 * 1000;
    dev.spics_io_num = -1;       // CS is on the expander
    dev.queue_size = 1;
    spi_device_handle_t h;
    if (spi_bus_add_device(SPI2_HOST, &dev, &h) != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed");
        return false;
    }
    spi_ = h;

    io.setOutput(EXIO_LCD_CS, false);
    delay(10);
    sendInitSequence();
    io.setOutput(EXIO_LCD_CS, true);
    delay(10);

    // --- RGB peripheral ---
    esp_lcd_rgb_panel_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.clk_src = LCD_CLK_SRC_DEFAULT;
    cfg.timings.pclk_hz = LCD_PCLK_HZ;
    cfg.timings.h_res = LCD_H_RES;
    cfg.timings.v_res = LCD_V_RES;
    cfg.timings.hsync_pulse_width = LCD_HSYNC_PULSE;
    cfg.timings.hsync_back_porch  = LCD_HSYNC_BACK;
    cfg.timings.hsync_front_porch = LCD_HSYNC_FRONT;
    cfg.timings.vsync_pulse_width = LCD_VSYNC_PULSE;
    cfg.timings.vsync_back_porch  = LCD_VSYNC_BACK;
    cfg.timings.vsync_front_porch = LCD_VSYNC_FRONT;
    cfg.timings.flags.pclk_active_neg = 0;
    cfg.data_width = 16;
    cfg.bits_per_pixel = 16;
    cfg.num_fbs = 2;
    cfg.bounce_buffer_size_px = LCD_BOUNCE_PX;
    cfg.dma_burst_size = 64;     // IDF 5.4 name for psram_trans_align
    cfg.hsync_gpio_num = PIN_LCD_HSYNC;
    cfg.vsync_gpio_num = PIN_LCD_VSYNC;
    cfg.de_gpio_num    = PIN_LCD_DE;
    cfg.pclk_gpio_num  = PIN_LCD_PCLK;
    cfg.disp_gpio_num  = -1;
    const int data[16] = {PIN_LCD_D0,  PIN_LCD_D1,  PIN_LCD_D2,  PIN_LCD_D3,
                          PIN_LCD_D4,  PIN_LCD_D5,  PIN_LCD_D6,  PIN_LCD_D7,
                          PIN_LCD_D8,  PIN_LCD_D9,  PIN_LCD_D10, PIN_LCD_D11,
                          PIN_LCD_D12, PIN_LCD_D13, PIN_LCD_D14, PIN_LCD_D15};
    for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data[i];
    cfg.flags.fb_in_psram = 1;
    cfg.flags.double_fb = 1;

    if (esp_lcd_new_rgb_panel(&cfg, &panel_) != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_rgb_panel failed");
        return false;
    }
    esp_lcd_panel_reset(panel_);
    esp_lcd_panel_init(panel_);
    esp_lcd_rgb_panel_get_frame_buffer(panel_, 2, &fb_[0], &fb_[1]);
    ESP_LOGI(TAG, "RGB panel up, fb0=%p fb1=%p", fb_[0], fb_[1]);

    // --- backlight PWM ---
    ledcAttach(PIN_LCD_BL, BL_PWM_FREQ_HZ, BL_PWM_RES_BITS);
    setBacklight(BL_DEFAULT_PERCENT);
    return true;
}

void Display::flush(int x1, int y1, int x2, int y2, const void* pixels) {
    // esp_lcd_panel_draw_bitmap takes exclusive end coordinates.
    int xe = x2 + 1, ye = y2 + 1;
    if (xe > LCD_H_RES) xe = LCD_H_RES;
    if (ye > LCD_V_RES) ye = LCD_V_RES;
    esp_lcd_panel_draw_bitmap(panel_, x1, y1, xe, ye, pixels);
}

void Display::setBacklight(uint8_t percent) {
    if (percent > 100) percent = 100;
    backlight_ = percent;
    uint32_t maxDuty = (1u << BL_PWM_RES_BITS) - 1;
    ledcWrite(PIN_LCD_BL, (uint32_t)percent * maxDuty / 100);
}

}  // namespace board
