#include "ui.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>

#include "../batmon/batmon_client.h"
#include "../board/display.h"
#include "../config.h"
#include "../settings.h"

using batmon::LinkState;
using batmon::State;

namespace ui {

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------
static const lv_color_t C_BG       = lv_color_hex(0x000000);
static const lv_color_t C_TEXT     = lv_color_hex(0xF2F2F2);
static const lv_color_t C_DIM      = lv_color_hex(0x8A8A8A);
static const lv_color_t C_STALE    = lv_color_hex(0x555555);
static const lv_color_t C_TRACK    = lv_color_hex(0x202020);
static const lv_color_t C_GOOD     = lv_color_hex(0x2ECC71);
static const lv_color_t C_WARN     = lv_color_hex(0xF1C40F);
static const lv_color_t C_BAD      = lv_color_hex(0xE74C3C);
static const lv_color_t C_CHARGE   = lv_color_hex(0x3498DB);
static const lv_color_t C_ACCENT   = lv_color_hex(0x00BFA5);

static lv_color_t socColor(float soc) {
    if (soc < 0)  return C_DIM;
    if (soc < 20) return C_BAD;
    if (soc < 50) return C_WARN;
    return C_GOOD;
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------
static board::Display* g_disp = nullptr;
static lv_obj_t* tv = nullptr;

// Halo page
static lv_obj_t* arc;
static lv_obj_t* lblName;
static lv_obj_t* lblSoc;
static lv_obj_t* lblSocUnit;
static lv_obj_t* lblVolts;
static lv_obj_t* lblAmps;
static lv_obj_t* lblWatts;
static lv_obj_t* lblTemp;
static lv_obj_t* lblRuntime;
static lv_obj_t* lblLink;
static lv_obj_t* dotLink;

// Detail page
enum DetailRow {
    D_VOLTS, D_EXT_VOLTS, D_CURRENT, D_WATTS, D_AH, D_AH_MAX, D_AH_MIN,
    D_EXT_TEMP, D_INT_TEMP, D_RSSI, D_POLLS, D_ADDR, D_COUNT
};
static lv_obj_t* detVal[D_COUNT];
static lv_obj_t* swRelay;
static lv_obj_t* swSwitch;
static bool suppressSwitchEvents = false;

// Setup page
static lv_obj_t* lblCapacity;
static lv_obj_t* sliderBright;
static lv_obj_t* swFahrenheit;
static lv_obj_t* btnPauseLbl;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static float toDisplayTemp(float c) {
    return g_settings.fahrenheit ? c * 9.0f / 5.0f + 32.0f : c;
}
static const char* tempUnit() { return g_settings.fahrenheit ? "\xC2\xB0" "F" : "\xC2\xB0" "C"; }

static bool stale(const batmon::Reading& r) {
    return !r.valid() || (millis() - r.updatedMs) > DATA_STALE_MS;
}

static lv_obj_t* mkLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "--");
    return l;
}

static void setCapacityLabel() {
    char buf[32];
    if (g_settings.capacityAh > 0)
        snprintf(buf, sizeof buf, "%.0f Ah", g_settings.capacityAh);
    else
        snprintf(buf, sizeof buf, "not set");
    lv_label_set_text(lblCapacity, buf);
}

// ---------------------------------------------------------------------------
// Page 0: Halo
// ---------------------------------------------------------------------------
static void buildHalo(lv_obj_t* page) {
    // SoC ring around the rim
    arc = lv_arc_create(page);
    lv_obj_set_size(arc, 456, 456);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, C_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, C_GOOD, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);

    lblName = mkLabel(page, &lv_font_montserrat_18, C_DIM);
    lv_obj_align(lblName, LV_ALIGN_TOP_MID, 0, 62);
    lv_label_set_text(lblName, "BatMon");

    lblSoc = mkLabel(page, &lv_font_montserrat_48, C_TEXT);
    lv_obj_align(lblSoc, LV_ALIGN_CENTER, -8, -58);
    lblSocUnit = mkLabel(page, &lv_font_montserrat_24, C_DIM);
    lv_obj_align_to(lblSocUnit, lblSoc, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -6);
    lv_label_set_text(lblSocUnit, "%");

    lblVolts = mkLabel(page, &lv_font_montserrat_32, C_TEXT);
    lv_obj_align(lblVolts, LV_ALIGN_CENTER, 0, 0);

    lblAmps = mkLabel(page, &lv_font_montserrat_28, C_TEXT);
    lv_obj_align(lblAmps, LV_ALIGN_CENTER, -70, 48);
    lblWatts = mkLabel(page, &lv_font_montserrat_28, C_TEXT);
    lv_obj_align(lblWatts, LV_ALIGN_CENTER, 70, 48);

    lblTemp = mkLabel(page, &lv_font_montserrat_20, C_DIM);
    lv_obj_align(lblTemp, LV_ALIGN_CENTER, 0, 96);

    lblRuntime = mkLabel(page, &lv_font_montserrat_18, C_DIM);
    lv_obj_align(lblRuntime, LV_ALIGN_CENTER, 0, 128);
    lv_label_set_text(lblRuntime, "");

    dotLink = lv_obj_create(page);
    lv_obj_set_size(dotLink, 12, 12);
    lv_obj_set_style_radius(dotLink, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dotLink, 0, 0);
    lv_obj_set_style_bg_color(dotLink, C_DIM, 0);
    lv_obj_align(dotLink, LV_ALIGN_BOTTOM_MID, -46, -70);
    lv_obj_clear_flag(dotLink, LV_OBJ_FLAG_SCROLLABLE);

    lblLink = mkLabel(page, &lv_font_montserrat_16, C_DIM);
    lv_obj_align(lblLink, LV_ALIGN_BOTTOM_MID, 12, -68);
    lv_label_set_text(lblLink, "Starting");
}

// ---------------------------------------------------------------------------
// Page 1: Detail
// ---------------------------------------------------------------------------
static void onSwitchRelay(lv_event_t* e) {
    if (suppressSwitchEvents) return;
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    batmon::g_client.requestSetIo(batmon::IoType::Relay, on);
}
static void onSwitchSwitch(lv_event_t* e) {
    if (suppressSwitchEvents) return;
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    batmon::g_client.requestSetIo(batmon::IoType::Switch, on);
}

static void buildDetail(lv_obj_t* page) {
    static const char* names[D_COUNT] = {
        "Voltage", "Ext voltage", "Current", "Power", "Amp hours", "Ah full ref",
        "Ah min", "Ext temp", "CPU temp", "RSSI", "Polls ok/err", "Address"};

    lv_obj_t* title = mkLabel(page, &lv_font_montserrat_20, C_ACCENT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 46);
    lv_label_set_text(title, "Details");

    // Two-column list kept inside the visible circle.
    const int x0 = 96, y0 = 84, rowH = 24;
    for (int i = 0; i < D_COUNT; i++) {
        lv_obj_t* n = mkLabel(page, &lv_font_montserrat_16, C_DIM);
        lv_label_set_text(n, names[i]);
        lv_obj_set_pos(n, x0, y0 + i * rowH);
        detVal[i] = mkLabel(page, &lv_font_montserrat_16, C_TEXT);
        lv_obj_set_width(detVal[i], 180);
        lv_obj_set_style_text_align(detVal[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(detVal[i], LCD_H_RES - x0 - 180, y0 + i * rowH);
    }

    int y = y0 + D_COUNT * rowH + 8;
    lv_obj_t* lr = mkLabel(page, &lv_font_montserrat_16, C_DIM);
    lv_label_set_text(lr, "Relay");
    lv_obj_set_pos(lr, x0 + 40, y + 6);
    swRelay = lv_switch_create(page);
    lv_obj_set_pos(swRelay, x0 + 100, y);
    lv_obj_add_event_cb(swRelay, onSwitchRelay, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* ls = mkLabel(page, &lv_font_montserrat_16, C_DIM);
    lv_label_set_text(ls, "Switch");
    lv_obj_set_pos(ls, x0 + 180, y + 6);
    swSwitch = lv_switch_create(page);
    lv_obj_set_pos(swSwitch, x0 + 244, y);
    lv_obj_add_event_cb(swSwitch, onSwitchSwitch, LV_EVENT_VALUE_CHANGED, nullptr);
}

// ---------------------------------------------------------------------------
// Page 2: Setup
// ---------------------------------------------------------------------------
static void onCapacity(lv_event_t* e) {
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    float v = g_settings.capacityAh + delta;
    if (v < 0) v = 0;
    if (v > 5000) v = 5000;
    g_settings.capacityAh = v;
    g_settings.save();
    setCapacityLabel();
}

static void onBrightness(lv_event_t* e) {
    int v = lv_slider_get_value(lv_event_get_target(e));
    g_settings.brightness = v;
    if (g_disp) g_disp->setBacklight(v);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) g_settings.save();
}

static void onFahrenheit(lv_event_t* e) {
    g_settings.fahrenheit = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    g_settings.save();
}

static void onPause(lv_event_t*) {
    State s = batmon::g_client.snapshot();
    if (s.link == LinkState::Paused) batmon::g_client.resume();
    else batmon::g_client.pause(BLE_PAUSE_DEFAULT_MS);
}

static void onForget(lv_event_t*) {
    batmon::g_client.forgetDevice();
}

static lv_obj_t* mkButton(lv_obj_t* parent, const char* text, int w, int h, lv_event_cb_t cb, void* ud) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

static void buildSetup(lv_obj_t* page) {
    lv_obj_t* title = mkLabel(page, &lv_font_montserrat_20, C_ACCENT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 46);
    lv_label_set_text(title, "Setup");

    // Capacity
    lv_obj_t* cl = mkLabel(page, &lv_font_montserrat_16, C_DIM);
    lv_label_set_text(cl, "Battery capacity");
    lv_obj_align(cl, LV_ALIGN_TOP_MID, 0, 86);
    lblCapacity = mkLabel(page, &lv_font_montserrat_28, C_TEXT);
    lv_obj_align(lblCapacity, LV_ALIGN_TOP_MID, 0, 108);
    setCapacityLabel();

    const int by = 150, bw = 64, bh = 40, gap = 8;
    int bx = (LCD_H_RES - (4 * bw + 3 * gap)) / 2;
    const char* txt[4] = {"-10", "-1", "+1", "+10"};
    const int dl[4] = {-10, -1, 1, 10};
    for (int i = 0; i < 4; i++) {
        lv_obj_t* b = mkButton(page, txt[i], bw, bh, onCapacity, (void*)(intptr_t)dl[i]);
        lv_obj_set_pos(b, bx + i * (bw + gap), by);
    }

    // Brightness
    lv_obj_t* bl = mkLabel(page, &lv_font_montserrat_16, C_DIM);
    lv_label_set_text(bl, "Brightness");
    lv_obj_align(bl, LV_ALIGN_TOP_MID, 0, 206);
    sliderBright = lv_slider_create(page);
    lv_obj_set_size(sliderBright, 260, 14);
    lv_obj_align(sliderBright, LV_ALIGN_TOP_MID, 0, 234);
    lv_slider_set_range(sliderBright, 5, 100);
    lv_slider_set_value(sliderBright, g_settings.brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sliderBright, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sliderBright, C_ACCENT, LV_PART_KNOB);
    lv_obj_add_event_cb(sliderBright, onBrightness, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(sliderBright, onBrightness, LV_EVENT_RELEASED, nullptr);

    // Units
    lv_obj_t* ul = mkLabel(page, &lv_font_montserrat_16, C_DIM);
    lv_label_set_text(ul, "Fahrenheit");
    lv_obj_align(ul, LV_ALIGN_TOP_MID, -50, 276);
    swFahrenheit = lv_switch_create(page);
    lv_obj_align(swFahrenheit, LV_ALIGN_TOP_MID, 50, 270);
    if (g_settings.fahrenheit) lv_obj_add_state(swFahrenheit, LV_STATE_CHECKED);
    lv_obj_add_event_cb(swFahrenheit, onFahrenheit, LV_EVENT_VALUE_CHANGED, nullptr);

    // BLE controls
    lv_obj_t* bp = mkButton(page, "Pause BLE 5 min", 200, 40, onPause, nullptr);
    lv_obj_align(bp, LV_ALIGN_TOP_MID, 0, 318);
    btnPauseLbl = lv_obj_get_child(bp, 0);
    lv_obj_t* bf = mkButton(page, "Forget device", 200, 40, onForget, nullptr);
    lv_obj_align(bf, LV_ALIGN_TOP_MID, 0, 366);

    lv_obj_t* ver = mkLabel(page, &lv_font_montserrat_14, C_STALE);
    lv_label_set_text(ver, FW_NAME " " FW_VERSION);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_MID, 0, -52);
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
void create(board::Display& display) {
    g_disp = &display;

    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, C_BG, 0);

    tv = lv_tileview_create(scr);
    lv_obj_set_style_bg_color(tv, C_BG, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* pHalo   = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    lv_obj_t* pDetail = lv_tileview_add_tile(tv, 1, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    lv_obj_t* pSetup  = lv_tileview_add_tile(tv, 2, 0, LV_DIR_LEFT);
    for (lv_obj_t* p : {pHalo, pDetail, pSetup}) {
        lv_obj_set_style_bg_color(p, C_BG, 0);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    }

    buildHalo(pHalo);
    buildDetail(pDetail);
    buildSetup(pSetup);
}

void update(const State& s) {
    char buf[48];

    // ---- Halo page ----
    lv_label_set_text(lblName, s.deviceName[0] ? s.deviceName : "BatMon");

    bool vStale = stale(s.volts);
    lv_color_t txt = vStale ? C_STALE : C_TEXT;

    if (s.soc >= 0) {
        snprintf(buf, sizeof buf, "%.0f", s.soc);
        lv_label_set_text(lblSoc, buf);
        lv_obj_clear_flag(lblSocUnit, LV_OBJ_FLAG_HIDDEN);
        lv_arc_set_value(arc, (int)lroundf(s.soc));
        lv_obj_set_style_arc_color(arc, vStale ? C_STALE : socColor(s.soc), LV_PART_INDICATOR);
    } else {
        lv_label_set_text(lblSoc, s.volts.valid() ? "" : "--");
        lv_obj_add_flag(lblSocUnit, LV_OBJ_FLAG_HIDDEN);
        lv_arc_set_value(arc, 0);
    }
    lv_obj_set_style_text_color(lblSoc, txt, 0);

    if (s.volts.valid()) snprintf(buf, sizeof buf, "%.2f V", s.volts.value);
    else snprintf(buf, sizeof buf, "-- V");
    lv_label_set_text(lblVolts, buf);
    lv_obj_set_style_text_color(lblVolts, txt, 0);

    if (s.current.valid()) {
        float a = s.current.value;
        snprintf(buf, sizeof buf, "%s%.1f A", a > 0.05f ? LV_SYMBOL_UP " " : (a < -0.05f ? LV_SYMBOL_DOWN " " : ""), fabsf(a));
        lv_label_set_text(lblAmps, buf);
        lv_obj_set_style_text_color(lblAmps, vStale ? C_STALE : (a > 0.05f ? C_CHARGE : (a < -0.05f ? C_WARN : C_TEXT)), 0);
        snprintf(buf, sizeof buf, "%.0f W", fabsf(s.watts));
        lv_label_set_text(lblWatts, buf);
    } else {
        lv_label_set_text(lblAmps, "-- A");
        lv_label_set_text(lblWatts, "-- W");
    }
    lv_obj_set_style_text_color(lblWatts, txt, 0);

    if (s.extTemp.valid()) snprintf(buf, sizeof buf, "%.1f %s", toDisplayTemp(s.extTemp.value), tempUnit());
    else snprintf(buf, sizeof buf, "-- %s", tempUnit());
    lv_label_set_text(lblTemp, buf);

    if (s.hoursRemaining > 0 && s.hoursRemaining < 1000 && !vStale) {
        int h = (int)s.hoursRemaining;
        int m = (int)((s.hoursRemaining - h) * 60);
        snprintf(buf, sizeof buf, "%dh %02dm to %s", h, m, s.current.value > 0 ? "full" : "empty");
        lv_label_set_text(lblRuntime, buf);
    } else if (s.soc < 0 && s.link == LinkState::Connected) {
        lv_label_set_text(lblRuntime, "Set capacity for SoC");
    } else {
        lv_label_set_text(lblRuntime, "");
    }

    // Link status
    lv_color_t dot = C_DIM;
    const char* linkTxt = batmon::linkStateName(s.link);
    switch (s.link) {
        case LinkState::Connected:    dot = vStale ? C_WARN : C_GOOD; break;
        case LinkState::Scanning:
        case LinkState::Connecting:
        case LinkState::Reconnecting: dot = C_WARN; break;
        case LinkState::Paused:       dot = C_CHARGE; break;
        default: break;
    }
    if (s.link == LinkState::Paused) {
        int32_t left = (int32_t)(s.pauseUntilMs - millis());
        if (left < 0) left = 0;
        snprintf(buf, sizeof buf, "Paused %d:%02d", left / 60000, (left / 1000) % 60);
        linkTxt = buf;
    }
    lv_obj_set_style_bg_color(dotLink, dot, 0);
    lv_label_set_text(lblLink, linkTxt);

    // ---- Detail page ----
    auto fmt = [&](DetailRow r, const batmon::Reading& rd, const char* f, float scale = 1.0f) {
        if (rd.valid()) snprintf(buf, sizeof buf, f, rd.value * scale);
        else snprintf(buf, sizeof buf, "--");
        lv_label_set_text(detVal[r], buf);
        lv_obj_set_style_text_color(detVal[r], stale(rd) ? C_STALE : C_TEXT, 0);
    };
    fmt(D_VOLTS, s.volts, "%.2f V");
    fmt(D_EXT_VOLTS, s.extVolts, "%.2f V");
    fmt(D_CURRENT, s.current, "%.2f A");
    snprintf(buf, sizeof buf, "%.1f W", s.watts);
    lv_label_set_text(detVal[D_WATTS], buf);
    fmt(D_AH, s.ampHours, "%.2f Ah");
    fmt(D_AH_MAX, s.ampHoursMax, "%.2f Ah");
    fmt(D_AH_MIN, s.ampHoursMin, "%.2f Ah");
    if (s.extTemp.valid()) snprintf(buf, sizeof buf, "%.1f %s", toDisplayTemp(s.extTemp.value), tempUnit()); else snprintf(buf, sizeof buf, "--");
    lv_label_set_text(detVal[D_EXT_TEMP], buf);
    if (s.intTemp.valid()) snprintf(buf, sizeof buf, "%.1f %s", toDisplayTemp(s.intTemp.value), tempUnit()); else snprintf(buf, sizeof buf, "--");
    lv_label_set_text(detVal[D_INT_TEMP], buf);
    snprintf(buf, sizeof buf, "%d dBm", s.rssi);
    lv_label_set_text(detVal[D_RSSI], buf);
    snprintf(buf, sizeof buf, "%lu / %lu", (unsigned long)s.pollOk, (unsigned long)s.pollErrors);
    lv_label_set_text(detVal[D_POLLS], buf);
    lv_label_set_text(detVal[D_ADDR], s.deviceAddr[0] ? s.deviceAddr : "--");

    // Reflect the device's real pin state without firing our own handlers.
    suppressSwitchEvents = true;
    if (s.relay.valid() && s.relay.value > 0.5f) lv_obj_add_state(swRelay, LV_STATE_CHECKED);
    else lv_obj_clear_state(swRelay, LV_STATE_CHECKED);
    if (s.sw.valid() && s.sw.value > 0.5f) lv_obj_add_state(swSwitch, LV_STATE_CHECKED);
    else lv_obj_clear_state(swSwitch, LV_STATE_CHECKED);
    suppressSwitchEvents = false;

    // ---- Setup page ----
    lv_label_set_text(btnPauseLbl, s.link == LinkState::Paused ? "Resume BLE" : "Pause BLE 5 min");
}

}  // namespace ui
