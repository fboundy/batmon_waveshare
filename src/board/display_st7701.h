// ST7701 480x480 RGB panel bring-up for the Waveshare ESP32-S3-Touch-LCD-2.1.
//
// The panel is configured over a 9-bit 3-wire SPI link (D/C bit + 8 data
// bits, no MISO, CS on the IO expander) and then streamed pixels over the
// ESP32-S3 LCD peripheral in 16-bit RGB565 mode.  The register sequence is
// the one shipped in Waveshare's LVGL_Arduino demo; see docs/01-hardware.md.
#pragma once

#include <stdint.h>

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

namespace board {

class Tca9554;

class Display {
public:
    // Resets and initialises the panel; allocates two RGB565 frame buffers in
    // PSRAM.  Returns false if the LCD peripheral could not be created.
    bool begin(Tca9554& io);

    // Frame buffers owned by the RGB driver (PSRAM).  Drawing into one of
    // these and calling flush() with the same pointer performs a zero-copy
    // buffer swap on the next VSYNC.
    void* frameBuffer(int index) const { return fb_[index]; }

    // Blit / swap.  x2,y2 are inclusive.
    void flush(int x1, int y1, int x2, int y2, const void* pixels);

    // Blocks until the next VSYNC (i.e. until a flush() swap has taken
    // effect) or the timeout.  Returns false on timeout.
    bool waitVsync(uint32_t timeoutMs);

    // Turn the picture 180 degrees inside the panel (ST7701 source
    // direction register + MADCTL ML).  Can be called at any time.
    void setFlip(bool flipped);
    bool flipped() const { return flipped_; }

    // Restart the panel timing.  Flash writes stall PSRAM, which the panel
    // streams from, and can leave the picture scrambled/drifted; this
    // recovers it.  Call from a task after any flash write.
    void resync();

    // 0..100
    void setBacklight(uint8_t percent);
    uint8_t backlight() const { return backlight_; }

    esp_lcd_panel_handle_t handle() const { return panel_; }

private:
    void spiCmd(uint8_t cmd);
    void spiData(uint8_t data);
    void sendInitSequence();

    static bool onVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t* edata, void* user);

    esp_lcd_panel_handle_t panel_ = nullptr;
    void* vsyncSem_ = nullptr;   // SemaphoreHandle_t
    Tca9554* io_ = nullptr;
    bool flipped_ = false;
    void* fb_[2] = {nullptr, nullptr};
    void* spi_ = nullptr;   // spi_device_handle_t
    uint8_t backlight_ = 0;
};

}  // namespace board
