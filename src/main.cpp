// BatMon display firmware (Waveshare ESP32-S3 boards; see src/boards/).
//
// Tasks:
//   loop()     (core 1)  LVGL rendering + input, pulls a state snapshot 4x/s,
//                        BOOT button and serial console
//   batmon_ble (core 0)  NimBLE central: scan / connect / poll the BatMon
//   hist_save  (core 0)  writes the history buffers to LittleFS
#include <Arduino.h>

#include "batmon/batmon_client.h"
#include "board/board.h"
#include "config.h"
#include "console.h"
#include "history.h"
#include "settings.h"
#include "ui/ui.h"

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n%s %s (%s)\n", FW_NAME, FW_VERSION, BOARD_NAME);

    g_settings.load();

    if (!board::init()) Serial.println("board init reported an error");
    board::setBacklight(g_settings.brightness);

    // Before the UI: the chart page reads history at build time.
    history::begin(board::clock, board::clockValid());

    if (board::lvgl().lock()) {
        ui::create();
        board::lvgl().unlock();
    }

    batmon::g_client.begin();
    console::begin();
    Serial.println("setup done; type 'help' for the console");
}

void loop() {
    static uint32_t lastUi = 0;
    board::LvglPort& lvgl = board::lvgl();
    lvgl.loop();

    switch (board::pollButton()) {
        case board::ButtonEvent::Short:
            if (lvgl.lock(50)) { ui::nextPage(); lvgl.unlock(); }
            break;
        case board::ButtonEvent::Long:
            if (lvgl.lock(50)) { ui::cycleChartRange(); lvgl.unlock(); }
            break;
        default:
            break;
    }
    console::poll();

    uint32_t now = millis();
    if (now - lastUi >= UI_REFRESH_MS) {
        lastUi = now;
        batmon::State s = batmon::g_client.snapshot();
        if (lvgl.lock(20)) {
            ui::update(s);
            lvgl.unlock();
        }
    }
    delay(5);
}
