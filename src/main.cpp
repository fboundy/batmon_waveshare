// BatMon display for the Waveshare ESP32-S3-Touch-LCD-2.1.
//
// Tasks:
//   loop()     (core 1)  LVGL rendering + touch, pulls a state snapshot 4x/s
//   batmon_ble (core 0)  NimBLE central: scan / connect / poll the BatMon
#include <Arduino.h>
#include <Wire.h>

#include "batmon/batmon_client.h"
#include "board/display.h"
#include "board/lvgl_port.h"
#include "board/tca9554.h"
#include "board/touch.h"
#include "config.h"
#include "history.h"
#include "settings.h"
#include "ui/ui.h"

static board::Tca9554  g_io(TCA9554_ADDR);
static board::Display  g_display;
static board::Touch    g_touch;
static board::LvglPort g_lvgl;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n%s %s\n", FW_NAME, FW_VERSION);

    g_settings.load();

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);

    // All expander pins as outputs; buzzer off, resets released.
    if (!g_io.begin(0x00)) Serial.println("TCA9554 not found!");
    g_io.setOutput(EXIO_BUZZER, false);
    g_io.setOutput(EXIO_SD_CS, true);

    if (!g_display.begin(g_io)) Serial.println("Display init failed!");
    g_display.setBacklight(g_settings.brightness);
    g_touch.begin(g_io);
    g_lvgl.begin(g_display, g_touch);

    history::begin();   // before the UI: the chart page reads it at build time

    if (g_lvgl.lock()) {
        ui::create(g_display);
        g_lvgl.unlock();
    }

    batmon::g_client.begin();
    Serial.println("setup done");
}

void loop() {
    static uint32_t lastUi = 0;
    g_lvgl.loop();

    uint32_t now = millis();
    if (now - lastUi >= UI_REFRESH_MS) {
        lastUi = now;
        batmon::State s = batmon::g_client.snapshot();
        if (g_lvgl.lock(20)) {
            ui::update(s);
            g_lvgl.unlock();
        }
    }
    delay(5);
}
