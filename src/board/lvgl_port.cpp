#include "lvgl_port.h"

#include <Arduino.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "board.h"

namespace board {

static constexpr uint32_t LVGL_TICK_MS = 2;
LvglPort* LvglPort::instance_ = nullptr;

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

void LvglPort::flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
    LvglPort* self = static_cast<LvglPort*>(drv->user_data);
    const LvglConfig& c = self->cfg_;
    if (c.direct) {
        // full_refresh guarantees the whole frame is redrawn every time, so an
        // in-place rotation of the buffer we were just handed is safe.
        if (c.rotate180) rotate180(reinterpret_cast<uint16_t*>(px), (size_t)c.width * c.height);
        c.flush(0, 0, c.width - 1, c.height - 1, px);
    } else {
        c.flush(area->x1, area->y1, area->x2, area->y2, px);
    }
    if (!c.flushAsync) lv_disp_flush_ready(drv);
}

void LvglPort::flushDone() {
    if (instance_) lv_disp_flush_ready(&instance_->dispDrv_);
}

void LvglPort::touchCb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    LvglPort* self = static_cast<LvglPort*>(drv->user_data);
    uint16_t x, y;
    if (self->cfg_.touchRead && self->cfg_.touchRead(x, y)) {
        noteInput();
        if (self->cfg_.rotate180) {
            x = self->cfg_.width - 1 - x;
            y = self->cfg_.height - 1 - y;
        }
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void LvglPort::tickCb(void*) {
    lv_tick_inc(LVGL_TICK_MS);
}

bool LvglPort::begin(const LvglConfig& cfg) {
    cfg_ = cfg;
    instance_ = this;
    mutex_ = xSemaphoreCreateRecursiveMutex();

    lv_init();

    lv_disp_draw_buf_init(&drawBuf_, cfg.buf0, cfg.buf1, cfg.bufPixels);

    lv_disp_drv_init(&dispDrv_);
    dispDrv_.hor_res = cfg.width;
    dispDrv_.ver_res = cfg.height;
    dispDrv_.flush_cb = flushCb;
    dispDrv_.draw_buf = &drawBuf_;
    dispDrv_.full_refresh = cfg.direct ? 1 : 0;   // whole frame each time -> pure buffer swap
    dispDrv_.user_data = this;
    lv_disp_drv_register(&dispDrv_);

    if (cfg.touchRead) {
        lv_indev_drv_init(&indevDrv_);
        indevDrv_.type = LV_INDEV_TYPE_POINTER;
        indevDrv_.read_cb = touchCb;
        indevDrv_.user_data = this;
        lv_indev_drv_register(&indevDrv_);
    }

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
