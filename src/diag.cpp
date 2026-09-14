#include "diag.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <NimBLEDevice.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "board/board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char* TAG = "diag";

namespace diag {

static const char* FILE_PATH = "/diag.txt";
static const char* CHR_UUID = "b47a0003-9f21-4d9e-a1c3-5ab9d2f0c001";
static constexpr size_t READ_PAGE = 500;      // one ATT long read
static constexpr size_t NOTIFY_CHUNK = 180;   // fits the default MTU comfortably

static char* buf = nullptr;                   // the log, NUL-terminated
static size_t len = 0;
static bool dirty = false;
static SemaphoreHandle_t mtx;
static NimBLECharacteristic* chr = nullptr;
static size_t readPos = 0;                    // next page for a plain read
static volatile bool streamRequested = false;
static uint16_t streamConn = 0;

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------
static void load() {
    File f = LittleFS.open(FILE_PATH, FILE_READ);
    if (!f) return;
    len = f.read((uint8_t*)buf, MAX_BYTES - 1);
    buf[len] = 0;
    f.close();
}

static void flush() {
    xSemaphoreTake(mtx, portMAX_DELAY);
    if (!dirty) { xSemaphoreGive(mtx); return; }
    File f = LittleFS.open(FILE_PATH, FILE_WRITE);
    if (f) {
        f.write((const uint8_t*)buf, len);
        f.close();
        dirty = false;
    }
    xSemaphoreGive(mtx);
    board::displayResync();   // the flash write may have upset the RGB panel
}

size_t size() { return len; }

size_t read(size_t offset, char* out, size_t maxLen) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    size_t n = 0;
    if (offset < len) {
        n = len - offset;
        if (n > maxLen) n = maxLen;
        memcpy(out, buf + offset, n);
    }
    xSemaphoreGive(mtx);
    return n;
}

void clear() {
    xSemaphoreTake(mtx, portMAX_DELAY);
    len = 0;
    buf[0] = 0;
    dirty = true;
    readPos = 0;
    xSemaphoreGive(mtx);
}

void log(const char* fmt, ...) {
    char line[200];
    uint32_t t = millis();
    int n = snprintf(line, sizeof line, "%lu.%03lu ", (unsigned long)t / 1000, (unsigned long)t % 1000);
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(line + n, sizeof line - n - 1, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof line - 2) n = sizeof line - 2;
    ESP_LOGI(TAG, "%s", line);
    line[n++] = '\n';
    line[n] = 0;

    xSemaphoreTake(mtx, portMAX_DELAY);
    if (len + n >= MAX_BYTES) {
        // Drop the oldest quarter so the log keeps rolling.
        size_t cut = MAX_BYTES / 4;
        const char* nl = (const char*)memchr(buf + cut, '\n', len - cut);
        cut = nl ? (size_t)(nl - buf) + 1 : cut;
        memmove(buf, buf + cut, len - cut);
        len -= cut;
        if (readPos > cut) readPos -= cut; else readPos = 0;
    }
    memcpy(buf + len, line, n);
    len += n;
    buf[len] = 0;
    dirty = true;
    xSemaphoreGive(mtx);
}

// ---------------------------------------------------------------------------
// BLE access
// ---------------------------------------------------------------------------
class ChrCb : public NimBLECharacteristicCallbacks {
    // Every read hands out the next 500-byte page; the page after the end
    // is empty and rewinds, so "read until empty" gets the whole log.
    void onRead(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        char page[READ_PAGE];
        size_t n = read(readPos, page, sizeof page);
        c->setValue((uint8_t*)page, n);
        readPos = n ? readPos + n : 0;
    }
    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo& info, uint16_t subValue) override {
        if (subValue & 1) {   // notifications on: stream everything
            streamConn = info.getConnHandle();
            streamRequested = true;
        }
    }
};
static ChrCb chrCb;

void attach(NimBLEService* svc) {
    chr = svc->createCharacteristic(CHR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    chr->setCallbacks(&chrCb);
    chr->setValue("diag");
}

// Streams the log as notifications (own task: the subscribe callback runs
// on the NimBLE host task and must not block).  Also flushes the file.
static void task(void*) {
    for (;;) {
        if (streamRequested) {
            streamRequested = false;
            char chunk[NOTIFY_CHUNK];
            size_t pos = 0, n;
            while ((n = read(pos, chunk, sizeof chunk)) > 0 && chr) {
                chr->setValue((uint8_t*)chunk, n);
                if (!chr->notify(streamConn)) break;
                pos += n;
                vTaskDelay(pdMS_TO_TICKS(30));
            }
            if (chr) {
                chr->setValue("--end--");
                chr->notify(streamConn);
            }
            ESP_LOGI(TAG, "streamed %u bytes over BLE", (unsigned)pos);
        }
        if (dirty) flush();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void begin() {
    mtx = xSemaphoreCreateMutex();
    buf = (char*)ps_malloc(MAX_BYTES);
    if (!buf) buf = (char*)malloc(MAX_BYTES);
    buf[0] = 0;
    load();
    ESP_LOGI(TAG, "%u bytes of diagnostics log restored", (unsigned)len);
    xTaskCreatePinnedToCore(task, "diag", 4096, nullptr, 1, nullptr, 0);
}

}  // namespace diag
