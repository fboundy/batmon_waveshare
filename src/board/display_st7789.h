// ST7789 (ST7789T3) 170x320 panel over 4-wire SPI, as on the Waveshare
// ESP32-S3-LCD-1.9.  Driven in landscape (320x170) through the ESP-IDF
// esp_lcd ST7789 driver plus the panel-tuning registers from Waveshare's
// demo.  Pixels are streamed from two small DMA buffers; completion is
// reported asynchronously through a callback.  See docs/08-hardware-lcd-1-9.md.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

namespace board {

class DisplaySt7789 {
public:
    typedef void (*DoneCb)();

    // `done` is called (from ISR context) when a flush has been sent.
    bool begin(DoneCb done);

    // x2,y2 inclusive.  Returns immediately; `done` fires when the DMA is
    // finished.  The buffer must stay valid until then.
    void flush(int x1, int y1, int x2, int y2, const void* px);

    void* drawBuffer(int index) const { return buf_[index]; }
    size_t drawBufferPixels() const { return bufPixels_; }

    void setBacklight(uint8_t percent);
    uint8_t backlight() const { return backlight_; }

private:
    static bool onTransDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* ctx);
    void sendTuning();

    esp_lcd_panel_io_handle_t io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    void* buf_[2] = {nullptr, nullptr};
    size_t bufPixels_ = 0;
    DoneCb done_ = nullptr;
    uint8_t backlight_ = 0;
};

}  // namespace board
