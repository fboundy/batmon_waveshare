// BatMon display firmware (Waveshare ESP32-S3 boards; see src/boards/).
//
// Tasks:
//   loop()     (core 1)  LVGL rendering + input, pulls a state snapshot 4x/s,
//                        BOOT button and serial console
//   batmon_ble (core 0)  NimBLE central: scan / connect / poll the BatMon
//   hist_save  (core 0)  writes the history buffers to LittleFS
#include <Arduino.h>
#include <math.h>


#include "batmon/batmon_client.h"
#include "board/board.h"
#include "config.h"
#include "console.h"
#include "history.h"
#include "presence.h"
#include "settings.h"
#include "ui/ui.h"

bool g_screenOn = true;   // read by the console's status command

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
    static bool screenOn = true;
    g_screenOn = screenOn;
    uint32_t now = millis();
    board::LvglPort& lvgl = board::lvgl();
    lvgl.loop();

    presence::tick();

    // Orientation: automatic from the accelerometer (which way gravity
    // pulls along the board's vertical axis), re-checked every 2 s with a
    // dead band so a slight tilt never flips it; or forced by the setting.
    static uint32_t lastOrientMs = 0;
    if (millis() - lastOrientMs >= 2000) {
        lastOrientMs = millis();
        if (g_settings.orientation == 1) board::setFlipped(false);
        else if (g_settings.orientation == 2) board::setFlipped(true);
        else {
            float ax, ay, az;
            if (board::readAccel(ax, ay, az)) {
                // Use whichever in-plane axis carries most of gravity.
                float g = fabsf(ax) > fabsf(ay) ? ax : ay;
                if (fabsf(g) > 0.5f) {
                    bool down = g < 0;
                    if (g_settings.accelInvert) down = !down;
                    board::setFlipped(down);
                }
            }
        }
    }

    // RGB panels stream from PSRAM; whenever the DMA falls behind (PSRAM busy
    // with drawing, or blocked by a flash write) the picture rolls until the
    // DMA is re-aligned to a frame start.  Re-align periodically - it happens
    // inside the next VSYNC and is invisible when nothing was wrong - and
    // immediately after a history save.
    static uint32_t lastResyncMs = 0;
    static uint32_t lastSeenSave = 0;
    if (history::lastSaveMs() != lastSeenSave || now - lastResyncMs >= DISPLAY_RESYNC_MS) {
        lastSeenSave = history::lastSaveMs();
        lastResyncMs = now;
        board::displayResync();
    }
    // Standby: paired phones exist but none is here.  Screen off unless
    // someone touched it / pressed the button recently.
    bool wantOn = presence::gateOpen() || (millis() - board::lastInputMs() < WAKE_MS);
    if (wantOn != screenOn) {
        screenOn = wantOn;
        board::setBacklight(screenOn ? g_settings.brightness : 0);
    }

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
