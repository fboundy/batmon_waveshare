#include "history.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <math.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "config.h"

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

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
static const char* FILE_PATH = "/history.bin";
static const char* TEMP_PATH = "/history.tmp";

struct __attribute__((packed)) Meta {
    uint32_t magic;       // 'BMH1'
    uint32_t t0Cap, t0Head, t0Count;
    uint32_t t1Cap, t1Head, t1Count;
    uint32_t acc1Count;
    double   acc1Sum[4];
    uint32_t acc1N[4];
    uint32_t savedClock;  // ClockFn seconds at save time, 0 = unknown
};
static constexpr uint32_t MAGIC = 0x31484D42;  // "BMH1"

static ClockFn clockFn = nullptr;
static bool fsOk = false;
static Sample* stage0 = nullptr;   // PSRAM copies written by the save task
static Sample* stage1 = nullptr;
static Meta stageMeta;
static SemaphoreHandle_t saveSignal;
static uint32_t lastSaveMs_ = 0;
static uint32_t nextSaveMs = 0;
static uint32_t restored_ = 0;

static bool writeAll(File& f, const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    while (n) {
        size_t chunk = n > 4096 ? 4096 : n;
        if (f.write(b, chunk) != chunk) return false;
        b += chunk;
        n -= chunk;
    }
    return true;
}

static bool readAll(File& f, void* p, size_t n) {
    return f.read((uint8_t*)p, n) == n;
}

// Runs on its own low-priority task so the BLE poll loop never waits on flash.
static void saveTask(void*) {
    for (;;) {
        xSemaphoreTake(saveSignal, portMAX_DELAY);
        if (!fsOk || !t0.cap) continue;

        // Snapshot under the lock, then write without it.
        xSemaphoreTake(mtx, portMAX_DELAY);
        memcpy(stage0, t0.buf, T0_CAP * sizeof(Sample));
        memcpy(stage1, t1.buf, T1_CAP * sizeof(Sample));
        stageMeta = Meta{};
        stageMeta.magic = MAGIC;
        stageMeta.t0Cap = T0_CAP; stageMeta.t0Head = t0.head; stageMeta.t0Count = t0.count;
        stageMeta.t1Cap = T1_CAP; stageMeta.t1Head = t1.head; stageMeta.t1Count = t1.count;
        stageMeta.acc1Count = acc1Count;
        for (int i = 0; i < 4; i++) { stageMeta.acc1Sum[i] = acc1.sum[i]; stageMeta.acc1N[i] = acc1.n[i]; }
        xSemaphoreGive(mtx);
        uint32_t now = 0;
        stageMeta.savedClock = (clockFn && clockFn(now)) ? now : 0;

        uint32_t t = millis();
        File f = LittleFS.open(TEMP_PATH, FILE_WRITE);
        bool ok = f && writeAll(f, &stageMeta, sizeof stageMeta) &&
                  writeAll(f, stage0, T0_CAP * sizeof(Sample)) &&
                  writeAll(f, stage1, T1_CAP * sizeof(Sample));
        if (f) f.close();
        if (ok) {
            if (LittleFS.exists(FILE_PATH)) LittleFS.remove(FILE_PATH);
            ok = LittleFS.rename(TEMP_PATH, FILE_PATH);
        }
        if (ok) {
            lastSaveMs_ = millis();
            ESP_LOGI(TAG, "saved %u+%u samples in %lu ms", (unsigned)stageMeta.t0Count,
                     (unsigned)stageMeta.t1Count, (unsigned long)(millis() - t));
        } else {
            ESP_LOGE(TAG, "save failed");
            LittleFS.remove(TEMP_PATH);
        }
    }
}

static void commitT0();

static void restore(bool clockValid) {
    File f = LittleFS.open(FILE_PATH, FILE_READ);
    if (!f) {
        ESP_LOGI(TAG, "no saved history");
        return;
    }
    Meta m;
    bool ok = readAll(f, &m, sizeof m) && m.magic == MAGIC && m.t0Cap == T0_CAP && m.t1Cap == T1_CAP &&
              m.t0Head < T0_CAP && m.t0Count <= T0_CAP && m.t1Head < T1_CAP && m.t1Count <= T1_CAP &&
              readAll(f, t0.buf, T0_CAP * sizeof(Sample)) && readAll(f, t1.buf, T1_CAP * sizeof(Sample));
    f.close();
    if (!ok) {
        ESP_LOGW(TAG, "saved history invalid, ignoring");
        t0.head = t0.count = t1.head = t1.count = 0;
        return;
    }
    t0.head = m.t0Head; t0.count = m.t0Count;
    t1.head = m.t1Head; t1.count = m.t1Count;
    acc1Count = m.acc1Count < T1_PER_T0 ? m.acc1Count : 0;
    for (int i = 0; i < 4; i++) { acc1.sum[i] = m.acc1Sum[i]; acc1.n[i] = m.acc1N[i]; }
    restored_ = t0.count;

    // Push the restored data back by the time we were off.
    uint32_t now = 0;
    uint32_t gapIntervals = 0;
    if (clockValid && m.savedClock && clockFn && clockFn(now) && now >= m.savedClock) {
        gapIntervals = (now - m.savedClock) / (T0_INTERVAL_MS / 1000);
        uint32_t maxGap = T0_CAP + T1_CAP * T1_PER_T0;
        if (gapIntervals > maxGap) gapIntervals = maxGap;
    }
    for (uint32_t i = 0; i < gapIntervals; i++) commitT0();   // acc0 empty -> NaN
    ESP_LOGI(TAG, "restored %u+%u samples, gap %lu s%s", (unsigned)m.t0Count, (unsigned)m.t1Count,
             (unsigned long)gapIntervals * 15, clockValid ? "" : " (clock reset, gap unknown)");
}

// ---------------------------------------------------------------------------
// Core
// ---------------------------------------------------------------------------
void begin(ClockFn clock, bool clockValid) {
    clockFn = clock;
    mtx = xSemaphoreCreateMutex();
    saveSignal = xSemaphoreCreateBinary();
    t0.cap = T0_CAP;
    t1.cap = T1_CAP;
    t0.buf = (Sample*)heap_caps_malloc(T0_CAP * sizeof(Sample), MALLOC_CAP_SPIRAM);
    t1.buf = (Sample*)heap_caps_malloc(T1_CAP * sizeof(Sample), MALLOC_CAP_SPIRAM);
    stage0 = (Sample*)heap_caps_malloc(T0_CAP * sizeof(Sample), MALLOC_CAP_SPIRAM);
    stage1 = (Sample*)heap_caps_malloc(T1_CAP * sizeof(Sample), MALLOC_CAP_SPIRAM);
    if (!t0.buf || !t1.buf || !stage0 || !stage1) {
        ESP_LOGE(TAG, "PSRAM allocation failed");
        t0.cap = t1.cap = 0;
        return;
    }
    ESP_LOGI(TAG, "history buffers: %u + %u samples in PSRAM", (unsigned)T0_CAP, (unsigned)T1_CAP);

    fsOk = LittleFS.begin(true);
    if (!fsOk) ESP_LOGE(TAG, "LittleFS mount failed; history will not persist");
    else {
        ESP_LOGI(TAG, "LittleFS %u/%u KB used", (unsigned)(LittleFS.usedBytes() / 1024),
                 (unsigned)(LittleFS.totalBytes() / 1024));
        restore(clockValid);
    }

    nextCommitMs = millis() + T0_INTERVAL_MS;
    nextSaveMs = millis() + HISTORY_SAVE_MS;
    xTaskCreatePinnedToCore(saveTask, "hist_save", 4096, nullptr, 1, nullptr, 0);
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
    if ((int32_t)(now - nextCommitMs) >= 0) {
        xSemaphoreTake(mtx, portMAX_DELAY);
        // Commit every interval that has elapsed; the first gets whatever was
        // accumulated, later ones (e.g. after a long scan) are NaN gaps.
        while ((int32_t)(now - nextCommitMs) >= 0) {
            commitT0();
            nextCommitMs += T0_INTERVAL_MS;
        }
        xSemaphoreGive(mtx);
    }
    if ((int32_t)(now - nextSaveMs) >= 0) {
        nextSaveMs = now + HISTORY_SAVE_MS;
        saveNow();
    }
}

void saveNow() {
    if (saveSignal) xSemaphoreGive(saveSignal);
}

uint32_t lastSaveMs() { return lastSaveMs_; }
uint32_t restoredSamples() { return restored_; }

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
