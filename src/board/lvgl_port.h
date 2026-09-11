// Glue between LVGL 8.3 and the display / touch drivers.
//
// LVGL draws straight into the RGB driver's two PSRAM frame buffers in
// full-refresh mode, so a flush is just a buffer swap at VSYNC (no memcpy).
#pragma once

#include <lvgl.h>

namespace board {

class Display;
class Touch;

class LvglPort {
public:
    bool begin(Display& disp, Touch& touch);

    // Call from the UI task as often as possible (>= every 5 ms).
    void loop();

    // LVGL is not thread-safe.  Any other task that touches LVGL objects
    // must hold this lock.
    bool lock(uint32_t timeoutMs = 1000);
    void unlock();

private:
    static void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px);
    static void touchCb(lv_indev_drv_t* drv, lv_indev_data_t* data);
    static void tickCb(void* arg);

    Display* disp_ = nullptr;
    Touch* touch_ = nullptr;
    lv_disp_drv_t dispDrv_;
    lv_disp_draw_buf_t drawBuf_;
    lv_indev_drv_t indevDrv_;
    void* mutex_ = nullptr;  // SemaphoreHandle_t
};

}  // namespace board
