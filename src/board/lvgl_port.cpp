#include "lvgl_port.h"

#include <Arduino.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "../config.h"
#include "display.h"
#include "touch.h"

namespace board {

static constexpr uint32_t LVGL_TICK_MS = 2;

#if LCD_ROTATE_180
// LVGL refuses sw_rotate in full-refresh mode, so rotate the finished frame
// ourselves: a 180 degree turn is just reversing the pixel order.  Two
// RGB565 pixels per 32-bit word -> reverse the words and swap their halves.
static void rotate180(uint16_t* px, size_t n) {
    uint32_t* lo = reinterpret_cast<uint32_t*>(px);
    uint32_t* hi = lo + n / 2 - 1;
    while (lo < hi) {
        uint32_t a = *lo, b = *hi;
        *lo++ = (b << 16) | (b >> 16);
        *hi-- = (a << 16) | (a >> 16);
    }
}
#endif

void LvglPort::flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
    LvglPort* self = static_cast<LvglPort*>(drv->user_data);
#if LCD_ROTATE_180
    // full_refresh guarantees the whole frame is redrawn every time, so an
    // in-place rotation of the buffer we were just handed is safe.
    rotate180(reinterpret_cast<uint16_t*>(px), (size_t)LCD_H_RES * LCD_V_RES);
    self->disp_->flush(0, 0, LCD_H_RES - 1, LCD_V_RES - 1, px);
#else
    self->disp_->flush(area->x1, area->y1, area->x2, area->y2, px);
#endif
    lv_disp_flush_ready(drv);
}

void LvglPort::touchCb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    LvglPort* self = static_cast<LvglPort*>(drv->user_data);
    TouchPoint tp = self->touch_->read();
    if (tp.pressed) {
#if LCD_ROTATE_180
        data->point.x = LCD_H_RES - 1 - tp.x;
        data->point.y = LCD_V_RES - 1 - tp.y;
#else
        data->point.x = tp.x;
        data->point.y = tp.y;
#endif
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void LvglPort::tickCb(void*) {
    lv_tick_inc(LVGL_TICK_MS);
}

bool LvglPort::begin(Display& disp, Touch& touch) {
    disp_ = &disp;
    touch_ = &touch;
    mutex_ = xSemaphoreCreateRecursiveMutex();

    lv_init();

    lv_disp_draw_buf_init(&drawBuf_, disp.frameBuffer(0), disp.frameBuffer(1),
                          LCD_H_RES * LCD_V_RES);

    lv_disp_drv_init(&dispDrv_);
    dispDrv_.hor_res = LCD_H_RES;
    dispDrv_.ver_res = LCD_V_RES;
    dispDrv_.flush_cb = flushCb;
    dispDrv_.draw_buf = &drawBuf_;
    dispDrv_.full_refresh = 1;   // whole frame each time -> pure buffer swap
    dispDrv_.user_data = this;
    lv_disp_drv_register(&dispDrv_);

    lv_indev_drv_init(&indevDrv_);
    indevDrv_.type = LV_INDEV_TYPE_POINTER;
    indevDrv_.read_cb = touchCb;
    indevDrv_.user_data = this;
    lv_indev_drv_register(&indevDrv_);

    esp_timer_create_args_t args = {};
    args.callback = tickCb;
    args.name = "lvgl_tick";
    esp_timer_handle_t t;
    if (esp_timer_create(&args, &t) != ESP_OK) return false;
    esp_timer_start_periodic(t, LVGL_TICK_MS * 1000);
    return true;
}

void LvglPort::loop() {
    if (lock(50)) {
        lv_timer_handler();
        unlock();
    }
}

bool LvglPort::lock(uint32_t timeoutMs) {
    return xSemaphoreTakeRecursive((SemaphoreHandle_t)mutex_, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void LvglPort::unlock() {
    xSemaphoreGiveRecursive((SemaphoreHandle_t)mutex_);
}

}  // namespace board
