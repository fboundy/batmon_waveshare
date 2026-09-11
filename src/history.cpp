#include "history.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char* TAG = "history";

namespace history {

// ---------------------------------------------------------------------------
// Ring buffer tier
// ---------------------------------------------------------------------------
struct Tier {
    Sample*  buf = nullptr;
    uint32_t cap = 0;
    uint32_t head = 0;    // next write index
    uint32_t count = 0;

    void push(const Sample& s) {
        buf[head] = s;
        head = (head + 1) % cap;
        if (count < cap) count++;
    }
    // age 0 = newest
    bool at(uint32_t age, Sample& out) const {
        if (age >= count) return false;
        out = buf[(head + cap - 1 - age) % cap];
        return true;
    }
};

// Running mean of pushed samples, NaN-aware per field.
struct Accum {
    double sum[4] = {0, 0, 0, 0};
    uint32_t n[4] = {0, 0, 0, 0};

    void add(const Sample& s) {
        const float v[4] = {s.mainV, s.auxV, s.soc, s.current};
        for (int i = 0; i < 4; i++)
            if (!isnan(v[i])) { sum[i] += v[i]; n[i]++; }
    }
    Sample mean() const {
        Sample s;
        float* f[4] = {&s.mainV, &s.auxV, &s.soc, &s.current};
        for (int i = 0; i < 4; i++) *f[i] = n[i] ? (float)(sum[i] / n[i]) : NAN;
        return s;
    }
    void reset() { *this = Accum{}; }
};

static constexpr uint32_t T0_INTERVAL_MS = 15 * 1000;
static constexpr uint32_t T0_CAP         = 24 * 3600 / 15;       // 5760
static constexpr uint32_t T1_PER_T0      = 24;                   // 6 min
static constexpr uint32_t T1_CAP         = 30 * 24 * 10;         // 7200

static Tier  t0, t1;
static Accum acc0, acc1;
static uint32_t acc1Count = 0;
static uint32_t nextCommitMs = 0;
static SemaphoreHandle_t mtx;

static const Sample NONE = {NAN, NAN, NAN, NAN};

void begin() {
    mtx = xSemaphoreCreateMutex();
    t0.cap = T0_CAP;
    t1.cap = T1_CAP;
    t0.buf = (Sample*)heap_caps_malloc(T0_CAP * sizeof(Sample), MALLOC_CAP_SPIRAM);
    t1.buf = (Sample*)heap_caps_malloc(T1_CAP * sizeof(Sample), MALLOC_CAP_SPIRAM);
    if (!t0.buf || !t1.buf) {
        ESP_LOGE(TAG, "PSRAM allocation failed");
        t0.cap = t1.cap = 0;
    } else {
        ESP_LOGI(TAG, "history buffers: %u + %u samples in PSRAM",
                 (unsigned)T0_CAP, (unsigned)T1_CAP);
    }
    nextCommitMs = millis() + T0_INTERVAL_MS;
}

void push(const Sample& s) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    acc0.add(s);
    xSemaphoreGive(mtx);
}

static void commitT0() {
    Sample s = acc0.mean();
    acc0.reset();
    if (t0.cap) t0.push(s);
    acc1.add(s);
    if (++acc1Count >= T1_PER_T0) {
        if (t1.cap) t1.push(acc1.mean());
        acc1.reset();
        acc1Count = 0;
    }
}

void tick() {
    uint32_t now = millis();
    if ((int32_t)(now - nextCommitMs) < 0) return;
    xSemaphoreTake(mtx, portMAX_DELAY);
    // Commit every interval that has elapsed; the first gets whatever was
    // accumulated, later ones (e.g. after a long scan) are NaN gaps.
    while ((int32_t)(now - nextCommitMs) >= 0) {
        commitT0();
        nextCommitMs += T0_INTERVAL_MS;
    }
    xSemaphoreGive(mtx);
}

// ---------------------------------------------------------------------------
// Window extraction
// ---------------------------------------------------------------------------
struct RangeSpec { const Tier* tier; uint32_t stride; uint32_t secPerSample; };

static RangeSpec spec(Range r) {
    switch (r) {
        case Range::Hour:  return {&t0, 1, 15};
        case Range::Day:   return {&t1, 1, 360};
        case Range::Week:  return {&t1, 7, 360};
        case Range::Month:
        default:           return {&t1, 30, 360};
    }
}

uint32_t secondsPerPoint(Range r) {
    RangeSpec sp = spec(r);
    return sp.stride * sp.secPerSample;
}

int maxOffset(Range r) {
    RangeSpec sp = spec(r);
    uint32_t span = sp.stride * POINTS;
    if (span == 0 || sp.tier->count == 0) return 0;
    // Last window that still contains at least one sample.
    return (int)((sp.tier->count - 1) / span);
}

void window(Range r, int offset, Sample out[POINTS]) {
    RangeSpec sp = spec(r);
    uint32_t span = sp.stride * POINTS;
    uint32_t base = (uint32_t)offset * span;   // age of the newest point

    xSemaphoreTake(mtx, portMAX_DELAY);
    for (int i = 0; i < POINTS; i++) {
        // out[POINTS-1] is the newest
        uint32_t age0 = base + (uint32_t)(POINTS - 1 - i) * sp.stride;
        Accum a;
        bool any = false;
        for (uint32_t k = 0; k < sp.stride; k++) {
            Sample s;
            if (sp.tier->at(age0 + k, s)) { a.add(s); any = true; }
        }
        out[i] = any ? a.mean() : NONE;
    }
    xSemaphoreGive(mtx);
}

}  // namespace history
