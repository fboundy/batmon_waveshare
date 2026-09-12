#include "console.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "batmon/batmon_client.h"
#include "board/board.h"
#include "config.h"
#include "history.h"
#include "settings.h"
#include "ui/ui.h"

namespace console {

static char line[96];
static size_t len = 0;

static void help() {
    Serial.println(
        "Commands:\n"
        "  status                 link state and latest readings\n"
        "  cap <Ah>               battery capacity for SoC (0 = unknown)\n"
        "  bright <0-100>         backlight\n"
        "  degf <0|1>             temperature in Fahrenheit\n"
        "  poll <ms>              fast poll period (250-10000)\n"
        "  chart <series> <range> series: main,aux,soc,amps  range: hour|day|week|month\n"
        "  switch <on|off>        BatMon switch output\n"
        "  relay <on|off>         BatMon relay output\n"
        "  pause [minutes]        release the BatMon so the phone app can connect\n"
        "  resume\n"
        "  forget                 forget the preferred BatMon and rescan\n"
        "  save                   flush history to flash now\n"
        "  page                   next page\n"
        "  help");
}

static void status() {
    batmon::State s = batmon::g_client.snapshot();
    Serial.printf("%s %s on %s\n", FW_NAME, FW_VERSION, BOARD_NAME);
    Serial.printf("link: %s  device: %s (%s)  rssi %d  polls ok/err %lu/%lu\n",
                  batmon::linkStateName(s.link), s.deviceName, s.deviceAddr, s.rssi,
                  (unsigned long)s.pollOk, (unsigned long)s.pollErrors);
    Serial.printf("main %.2f V  aux %.2f V  %.2f A  %.1f W  ext %.1f C  cpu %.1f C\n",
                  s.volts.value, s.extVolts.value, s.current.value, s.watts, s.extTemp.value, s.intTemp.value);
    Serial.printf("Ah %.2f (max %.2f min %.2f)  soc %.1f %%  runtime %.1f h  relay %d switch %d\n",
                  s.ampHours.value, s.ampHoursMax.value, s.ampHoursMin.value, s.soc, s.hoursRemaining,
                  (int)s.relay.value, (int)s.sw.value);
    Serial.printf("settings: cap %.0f Ah  bright %u  degf %d  poll %u ms  chart mask 0x%x range %u\n",
                  g_settings.capacityAh, g_settings.brightness, g_settings.fahrenheit, g_settings.pollMs,
                  g_settings.chartMask, g_settings.chartRange);
    Serial.printf("history: last save %lu s ago, %lu restored at boot\n",
                  history::lastSaveMs() ? (unsigned long)((millis() - history::lastSaveMs()) / 1000) : 0UL,
                  (unsigned long)history::restoredSamples());
}

static bool onOff(const char* a, bool& out) {
    if (!a) return false;
    if (!strcasecmp(a, "on") || !strcmp(a, "1")) { out = true; return true; }
    if (!strcasecmp(a, "off") || !strcmp(a, "0")) { out = false; return true; }
    return false;
}

static void uiChanged() {
    if (board::lvgl().lock(200)) {
        ui::settingsChanged();
        board::lvgl().unlock();
    }
}

static void execute(char* l) {
    char* cmd = strtok(l, " \t");
    if (!cmd) return;
    char* a1 = strtok(nullptr, " \t");
    char* a2 = strtok(nullptr, " \t");
    bool on;

    if (!strcasecmp(cmd, "help") || !strcmp(cmd, "?")) {
        help();
    } else if (!strcasecmp(cmd, "status")) {
        status();
    } else if (!strcasecmp(cmd, "cap") && a1) {
        g_settings.capacityAh = atof(a1);
        g_settings.save();
        uiChanged();
        Serial.printf("capacity %.0f Ah\n", g_settings.capacityAh);
    } else if (!strcasecmp(cmd, "bright") && a1) {
        int v = atoi(a1);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        g_settings.brightness = v;
        g_settings.save();
        board::setBacklight(v);
        uiChanged();
        Serial.printf("brightness %d\n", v);
    } else if (!strcasecmp(cmd, "degf") && onOff(a1, on)) {
        g_settings.fahrenheit = on;
        g_settings.save();
        uiChanged();
        Serial.printf("units %s\n", on ? "F" : "C");
    } else if (!strcasecmp(cmd, "poll") && a1) {
        int v = atoi(a1);
        if (v < 250) v = 250;
        if (v > 10000) v = 10000;
        g_settings.pollMs = v;
        g_settings.save();
        Serial.printf("poll %d ms\n", v);
    } else if (!strcasecmp(cmd, "chart")) {
        if (a1) {
            uint8_t mask = 0;
            if (strstr(a1, "main")) mask |= 1;
            if (strstr(a1, "aux"))  mask |= 2;
            if (strstr(a1, "soc"))  mask |= 4;
            if (strstr(a1, "amp"))  mask |= 8;
            if (mask) g_settings.chartMask = mask;
        }
        if (a2) {
            static const char* rn[4] = {"hour", "day", "week", "month"};
            for (int i = 0; i < 4; i++) if (!strcasecmp(a2, rn[i])) g_settings.chartRange = i;
        }
        g_settings.save();
        uiChanged();
        Serial.printf("chart mask 0x%x range %u\n", g_settings.chartMask, g_settings.chartRange);
    } else if (!strcasecmp(cmd, "switch") && onOff(a1, on)) {
        batmon::g_client.requestSetIo(batmon::IoType::Switch, on);
        Serial.printf("switch %s requested\n", on ? "on" : "off");
    } else if (!strcasecmp(cmd, "relay") && onOff(a1, on)) {
        batmon::g_client.requestSetIo(batmon::IoType::Relay, on);
        Serial.printf("relay %s requested\n", on ? "on" : "off");
    } else if (!strcasecmp(cmd, "pause")) {
        uint32_t ms = a1 ? (uint32_t)atoi(a1) * 60000UL : BLE_PAUSE_DEFAULT_MS;
        batmon::g_client.pause(ms);
        Serial.printf("paused for %lu s\n", (unsigned long)ms / 1000);
    } else if (!strcasecmp(cmd, "resume")) {
        batmon::g_client.resume();
        Serial.println("resuming");
    } else if (!strcasecmp(cmd, "forget")) {
        batmon::g_client.forgetDevice();
        Serial.println("forgotten; rescanning");
    } else if (!strcasecmp(cmd, "save")) {
        history::saveNow();
        Serial.println("history save queued");
    } else if (!strcasecmp(cmd, "page")) {
        if (board::lvgl().lock(200)) {
            ui::nextPage();
            board::lvgl().unlock();
        }
    } else {
        Serial.printf("unknown: %s (try 'help')\n", cmd);
    }
}

void begin() {
    len = 0;
}

void poll() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r' || c == '\n') {
            if (len) {
                line[len] = 0;
                execute(line);
                len = 0;
            }
        } else if (len < sizeof line - 1) {
            line[len++] = c;
        }
    }
}

}  // namespace console
