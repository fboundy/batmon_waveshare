// Glue between LVGL 8.3 and a board's display / touch drivers.
//
// Two rendering modes:
//   * Direct: LVGL draws straight into the display's own full frame buffers
//     (RGB panels) in full-refresh mode, so a flush is just a buffer swap.
//   * Partial: LVGL renders strips into two small DMA buffers and the driver
//     streams them out (SPI panels).  The driver reports completion through
//     flushDone(), which may be called from an ISR.
#pragma once

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

namespace board {

struct LvglConfig {
    int  width = 0;
    int  height = 0;
    bool direct = false;         // true: buf0/buf1 are full frame buffers, full_refresh
    void* buf0 = nullptr;
    void* buf1 = nullptr;
    size_t bufPixels = 0;
    bool rotate180 = false;      // software flip of the whole frame (direct mode only)
    bool flushAsync = false;     // driver calls LvglPort::flushDone() when finished
    // x2,y2 inclusive.  In direct mode always called with the whole frame.
    void (*flush)(int x1, int y1, int x2, int y2, const void* px) = nullptr;
    // Optional touch: return true while pressed with the point in LVGL coords.
    bool (*touchRead)(uint16_t& x, uint16_t& y) = nullptr;
};

class LvglPort {
public:
    bool begin(const LvglConfig& cfg);

    // Call from the UI task as often as possible (>= every 5 ms).
    void loop();

    // LVGL is not thread-safe.  Any other task that touches LVGL objects
    // must hold this lock.
    bool lock(uint32_t timeoutMs = 1000);
    void unlock();

    // For async flush drivers (ISR-safe).
    static void flushDone();

private:
    static void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px);
    static void touchCb(lv_indev_drv_t* drv, lv_indev_data_t* data);
    static void tickCb(void* arg);

    LvglConfig cfg_;
    lv_disp_drv_t dispDrv_;
    lv_disp_draw_buf_t drawBuf_;
    lv_indev_drv_t indevDrv_;
    void* mutex_ = nullptr;  // SemaphoreHandle_t
    static LvglPort* instance_;
};

}  // namespace board
