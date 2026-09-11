#include "ui.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../batmon/batmon_client.h"
#include "../board/display.h"
#include "../config.h"
#include "../history.h"
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
static const lv_color_t C_GRID     = lv_color_hex(0x2A2A2A);
static const lv_color_t C_GOOD     = lv_color_hex(0x2ECC71);
static const lv_color_t C_WARN     = lv_color_hex(0xF1C40F);
static const lv_color_t C_BAD      = lv_color_hex(0xE74C3C);
static const lv_color_t C_CHARGE   = lv_color_hex(0x3498DB);
static const lv_color_t C_ACCENT   = lv_color_hex(0x00BFA5);
static const lv_color_t C_BTN      = lv_color_hex(0x2A2A2A);

// Chart series colours
static const lv_color_t C_SER_MAIN = lv_color_hex(0x2ECC71);
static const lv_color_t C_SER_AUX  = lv_color_hex(0x00BFA5);
static const lv_color_t C_SER_SOC  = lv_color_hex(0xF2F2F2);
static const lv_color_t C_SER_AMPS = lv_color_hex(0x3498DB);

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
static lv_obj_t* pChart = nullptr;

// Halo page
static lv_obj_t* arc;
static lv_obj_t* lblName;
static lv_obj_t* lblSoc;
static lv_obj_t* lblSocUnit;
static lv_obj_t* lblMainV;
static lv_obj_t* lblAuxV;
static lv_obj_t* lblAmps;
static lv_obj_t* lblWatts;
static lv_obj_t* lblTemp;
static lv_obj_t* lblRuntime;
static lv_obj_t* swHaloSwitch;
static lv_obj_t* lblAlert;
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
// After a user toggle, stop syncing the switches from device state for a
// moment so they don't snap back before the BLE command has gone through.
static uint32_t switchHoldUntilMs = 0;
static constexpr uint32_t SWITCH_HOLD_MS = 3000;

// Chart page
static lv_obj_t* chart;
static lv_chart_series_t* serMain;
static lv_chart_series_t* serAux;
static lv_chart_series_t* serSoc;
static lv_chart_series_t* serAmps;
static lv_obj_t* btnSeries[4];
static lv_obj_t* btnRange[4];
static lv_obj_t* lblWindow;
static lv_obj_t* lblScale;
static int  chartOffset = 0;
static bool secIsAmps = false;      // right axis shows amps (SoC hidden)
static float ampMin = -1, ampMax = 1;
static uint32_t lastChartMs = 0;
static bool chartVisible = false;
static history::Sample chartPts[history::POINTS];   // static: too big for the loop stack

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

// Transparent flex row whose children sit on a common bottom edge.
// Used to keep a unit / caption glued to a value whose width changes.
static lv_obj_t* mkRow(lv_obj_t* parent, lv_coord_t gap) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, gap, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return row;
}

static lv_obj_t* mkButton(lv_obj_t* parent, const char* text, int w, int h,
                          lv_event_cb_t cb, void* ud, const lv_font_t* font = &lv_font_montserrat_16) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, C_BTN, 0);
    lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

static void setCapacityLabel() {
    char buf[32];
    if (g_settings.capacityAh > 0)
        snprintf(buf, sizeof buf, "%.0f Ah", g_settings.capacityAh);
    else
        snprintf(buf, sizeof buf, "not set");
    lv_label_set_text(lblCapacity, buf);
}

static void onSwitchSwitch(lv_event_t* e) {
    if (suppressSwitchEvents) return;
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    switchHoldUntilMs = millis() + SWITCH_HOLD_MS;
    batmon::g_client.requestSetIo(batmon::IoType::Switch, on);
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

    // SoC: digits + unit on a shared baseline
    lv_obj_t* rowSoc = mkRow(page, 4);
    lv_obj_align(rowSoc, LV_ALIGN_CENTER, 0, -100);
    lblSoc = mkLabel(rowSoc, &lv_font_montserrat_48, C_TEXT);
    lblSocUnit = mkLabel(rowSoc, &lv_font_montserrat_32, C_DIM);
    lv_label_set_text(lblSocUnit, "%");
    // Montserrat 48 sits 10 px below its baseline, 32 sits 7 px: lift by 3.
    lv_obj_set_style_translate_y(lblSocUnit, -3, 0);

    // Voltages: "Main 13.19 V   Aux 12.62 V"
    lv_obj_t* rowV = mkRow(page, 6);
    lv_obj_align(rowV, LV_ALIGN_CENTER, 0, -38);
    lv_obj_t* capMain = mkLabel(rowV, &lv_font_montserrat_14, C_DIM);
    lv_label_set_text(capMain, "Main");
    lv_obj_set_style_translate_y(capMain, -3, 0);
    lblMainV = mkLabel(rowV, &lv_font_montserrat_28, C_TEXT);
    lv_obj_t* spacer = lv_obj_create(rowV);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 18, 1);
    lv_obj_t* capAux = mkLabel(rowV, &lv_font_montserrat_14, C_DIM);
    lv_label_set_text(capAux, "Aux");
    lv_obj_set_style_translate_y(capAux, -3, 0);
    lblAuxV = mkLabel(rowV, &lv_font_montserrat_28, C_TEXT);

    lblAmps = mkLabel(page, &lv_font_montserrat_28, C_TEXT);
    lv_obj_align(lblAmps, LV_ALIGN_CENTER, -70, 10);
    lblWatts = mkLabel(page, &lv_font_montserrat_28, C_TEXT);
    lv_obj_align(lblWatts, LV_ALIGN_CENTER, 70, 10);

    lblTemp = mkLabel(page, &lv_font_montserrat_20, C_DIM);
    lv_obj_align(lblTemp, LV_ALIGN_CENTER, 0, 50);

    lblRuntime = mkLabel(page, &lv_font_montserrat_18, C_DIM);
    lv_obj_align(lblRuntime, LV_ALIGN_CENTER, 0, 80);
    lv_label_set_text(lblRuntime, "");

    // Switch output control
    lv_obj_t* rowSw = mkRow(page, 10);
    lv_obj_set_flex_align(rowSw, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(rowSw, LV_ALIGN_CENTER, 0, 114);
    lv_obj_t* capSw = mkLabel(rowSw, &lv_font_montserrat_16, C_DIM);
    lv_label_set_text(capSw, "Switch");
    swHaloSwitch = lv_switch_create(rowSw);
    lv_obj_set_size(swHaloSwitch, 52, 26);
    lv_obj_set_style_bg_color(swHaloSwitch, C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(swHaloSwitch, onSwitchSwitch, LV_EVENT_VALUE_CHANGED, nullptr);

    // Alert line (hidden unless active)
    lblAlert = mkLabel(page, &lv_font_montserrat_18, C_BAD);
    lv_obj_align(lblAlert, LV_ALIGN_CENTER, 0, 150);
    lv_label_set_text(lblAlert, "");
    lv_obj_add_flag(lblAlert, LV_OBJ_FLAG_HIDDEN);

    // Link status
    dotLink = lv_obj_create(page);
    lv_obj_set_size(dotLink, 12, 12);
    lv_obj_set_style_radius(dotLink, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dotLink, 0, 0);
    lv_obj_set_style_bg_color(dotLink, C_DIM, 0);
    lv_obj_align(dotLink, LV_ALIGN_BOTTOM_MID, -46, -50);
    lv_obj_clear_flag(dotLink, LV_OBJ_FLAG_SCROLLABLE);

    lblLink = mkLabel(page, &lv_font_montserrat_16, C_DIM);
    lv_obj_align(lblLink, LV_ALIGN_BOTTOM_MID, 12, -48);
    lv_label_set_text(lblLink, "Starting");
}

// ---------------------------------------------------------------------------
// Page 1: Detail
// ---------------------------------------------------------------------------
static void onSwitchRelay(lv_event_t* e) {
    if (suppressSwitchEvents) return;
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    switchHoldUntilMs = millis() + SWITCH_HOLD_MS;
    batmon::g_client.requestSetIo(batmon::IoType::Relay, on);
}

static void buildDetail(lv_obj_t* page) {
    static const char* names[D_COUNT] = {
        "Main voltage", "Aux voltage", "Current", "Power", "Amp hours", "Ah full ref",
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
// Page 2: Chart
// ---------------------------------------------------------------------------
static history::Range chartRange() {
    uint8_t r = g_settings.chartRange;
    if (r > 3) r = 0;
    return (history::Range)r;
}

static void setWindowLabel() {
    static const char* unit[4] = {"h", "d", "w", "mo"};
    static const char* whole[4] = {"last hour", "last 24 h", "last 7 days", "last 30 days"};
    int r = (int)chartRange();
    char buf[40];
    if (chartOffset == 0) snprintf(buf, sizeof buf, "%s", whole[r]);
    else snprintf(buf, sizeof buf, "-%d%s to -%d%s", chartOffset + 1, unit[r], chartOffset, unit[r]);
    lv_label_set_text(lblWindow, buf);
}

static void rebuildChart() {
    lastChartMs = millis();
    history::window(chartRange(), chartOffset, chartPts);
    uint8_t mask = g_settings.chartMask;

    // --- volt axis (left) from the visible voltage series ---
    float vmin = 1e9f, vmax = -1e9f;
    for (int i = 0; i < history::POINTS; i++) {
        const history::Sample& s = chartPts[i];
        if ((mask & 1) && !isnan(s.mainV)) { vmin = fminf(vmin, s.mainV); vmax = fmaxf(vmax, s.mainV); }
        if ((mask & 2) && !isnan(s.auxV))  { vmin = fminf(vmin, s.auxV);  vmax = fmaxf(vmax, s.auxV); }
    }
    if (vmin > vmax) { vmin = 10.0f; vmax = 15.0f; }
    vmin = floorf((vmin - 0.15f) * 2.0f) / 2.0f;
    vmax = ceilf((vmax + 0.15f) * 2.0f) / 2.0f;
    if (vmax - vmin < 1.0f) vmax = vmin + 1.0f;
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, (lv_coord_t)lroundf(vmin * 100), (lv_coord_t)lroundf(vmax * 100));

    // --- amps: auto range, mapped onto the 0..100 right axis ---
    float amin = 1e9f, amax = -1e9f;
    for (int i = 0; i < history::POINTS; i++) {
        const history::Sample& s = chartPts[i];
        if (!isnan(s.current)) { amin = fminf(amin, s.current); amax = fmaxf(amax, s.current); }
    }
    if (amin > amax) { amin = -1; amax = 1; }
    amin = floorf(amin - 0.2f);
    amax = ceilf(amax + 0.2f);
    if (amax - amin < 2.0f) amax = amin + 2.0f;
    ampMin = amin;
    ampMax = amax;
    secIsAmps = !(mask & 4) && (mask & 8);
    lv_chart_set_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, 100);

    lv_coord_t* aMain = lv_chart_get_y_array(chart, serMain);
    lv_coord_t* aAux  = lv_chart_get_y_array(chart, serAux);
    lv_coord_t* aSoc  = lv_chart_get_y_array(chart, serSoc);
    lv_coord_t* aAmp  = lv_chart_get_y_array(chart, serAmps);
    for (int i = 0; i < history::POINTS; i++) {
        const history::Sample& s = chartPts[i];
        aMain[i] = isnan(s.mainV)   ? LV_CHART_POINT_NONE : (lv_coord_t)lroundf(s.mainV * 100);
        aAux[i]  = isnan(s.auxV)    ? LV_CHART_POINT_NONE : (lv_coord_t)lroundf(s.auxV * 100);
        aSoc[i]  = isnan(s.soc)     ? LV_CHART_POINT_NONE : (lv_coord_t)lroundf(s.soc);
        aAmp[i]  = isnan(s.current) ? LV_CHART_POINT_NONE
                                    : (lv_coord_t)lroundf((s.current - amin) / (amax - amin) * 100.0f);
    }
    lv_chart_hide_series(chart, serMain, !(mask & 1));
    lv_chart_hide_series(chart, serAux,  !(mask & 2));
    lv_chart_hide_series(chart, serSoc,  !(mask & 4));
    lv_chart_hide_series(chart, serAmps, !(mask & 8));
    lv_chart_refresh(chart);

    setWindowLabel();
    char buf[48];
    if ((mask & 8) && !secIsAmps)
        snprintf(buf, sizeof buf, "amps scaled %.0f to %.0f A (right axis = SoC)", amin, amax);
    else if (secIsAmps)
        snprintf(buf, sizeof buf, "left: volts   right: amps");
    else if (mask & 4)
        snprintf(buf, sizeof buf, "left: volts   right: SoC %%");
    else
        snprintf(buf, sizeof buf, "left: volts");
    lv_label_set_text(lblScale, buf);
}

// Axis tick labels: values are stored x100 for volts; right axis is either
// SoC (0..100 %) or amps mapped onto 0..100.
static void onChartDraw(lv_event_t* e) {
    lv_obj_draw_part_dsc_t* d = lv_event_get_draw_part_dsc(e);
    if (!lv_obj_draw_part_check_type(d, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL)) return;
    if (!d->text) return;
    if (d->id == LV_CHART_AXIS_PRIMARY_Y) {
        int v = d->value;
        lv_snprintf(d->text, d->text_length, "%d.%d", v / 100, (abs(v) % 100) / 10);
    } else if (d->id == LV_CHART_AXIS_SECONDARY_Y) {
        if (secIsAmps) {
            float a = ampMin + d->value / 100.0f * (ampMax - ampMin);
            lv_snprintf(d->text, d->text_length, "%d", (int)lroundf(a));
        } else {
            lv_snprintf(d->text, d->text_length, "%d%%", d->value);
        }
    }
}

static void onSeriesToggle(lv_event_t* e) {
    int bit = (int)(intptr_t)lv_event_get_user_data(e);
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if (on) g_settings.chartMask |= (1 << bit);
    else    g_settings.chartMask &= ~(1 << bit);
    g_settings.save();
    rebuildChart();
}

static void applyRangeButtons() {
    for (int i = 0; i < 4; i++) {
        if (i == (int)g_settings.chartRange) lv_obj_add_state(btnRange[i], LV_STATE_CHECKED);
        else lv_obj_clear_state(btnRange[i], LV_STATE_CHECKED);
    }
}

static void onRange(lv_event_t* e) {
    g_settings.chartRange = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    g_settings.save();
    chartOffset = 0;
    applyRangeButtons();
    rebuildChart();
}

static void onScroll(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);   // +1 = older
    int mx = history::maxOffset(chartRange());
    chartOffset += dir;
    if (chartOffset < 0) chartOffset = 0;
    if (chartOffset > mx) chartOffset = mx;
    rebuildChart();
}

static lv_obj_t* mkCheckButton(lv_obj_t* parent, const char* text, int w, int h, lv_color_t on,
                               lv_event_cb_t cb, void* ud) {
    lv_obj_t* b = mkButton(parent, text, w, h, nullptr, nullptr, &lv_font_montserrat_14);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(b, on, LV_STATE_CHECKED);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), lv_color_black(), LV_STATE_CHECKED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_VALUE_CHANGED, ud);
    return b;
}

static void buildChart(lv_obj_t* page) {
    // Series toggles
    static const char* names[4] = {"Main V", "Aux V", "SoC", "Amps"};
    static const lv_color_t cols[4] = {C_SER_MAIN, C_SER_AUX, C_SER_SOC, C_SER_AMPS};
    const int bw = 74, bh = 30, gap = 6;
    int bx = (LCD_H_RES - (4 * bw + 3 * gap)) / 2;
    for (int i = 0; i < 4; i++) {
        btnSeries[i] = mkCheckButton(page, names[i], bw, bh, cols[i], onSeriesToggle, (void*)(intptr_t)i);
        lv_obj_set_pos(btnSeries[i], bx + i * (bw + gap), 64);
        if (g_settings.chartMask & (1 << i)) lv_obj_add_state(btnSeries[i], LV_STATE_CHECKED);
    }

    // Chart
    chart = lv_chart_create(page);
    lv_obj_set_size(chart, 316, 186);
    lv_obj_set_pos(chart, (LCD_H_RES - 316) / 2, 114);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, history::POINTS);
    lv_chart_set_div_line_count(chart, 5, 5);
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 6, 3, 5, 2, true, 44);
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_SECONDARY_Y, 6, 3, 5, 2, true, 44);
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_X, 6, 3, 5, 2, false, 10);
    lv_obj_set_style_bg_color(chart, C_BG, 0);
    lv_obj_set_style_border_color(chart, C_GRID, 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_radius(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 2, 0);
    lv_obj_set_style_line_color(chart, C_GRID, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);   // no point markers
    lv_obj_set_style_text_font(chart, &lv_font_montserrat_12, LV_PART_TICKS);
    lv_obj_set_style_text_color(chart, C_DIM, LV_PART_TICKS);
    lv_obj_set_style_line_color(chart, C_DIM, LV_PART_TICKS);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(chart, onChartDraw, LV_EVENT_DRAW_PART_BEGIN, nullptr);

    serMain = lv_chart_add_series(chart, C_SER_MAIN, LV_CHART_AXIS_PRIMARY_Y);
    serAux  = lv_chart_add_series(chart, C_SER_AUX,  LV_CHART_AXIS_PRIMARY_Y);
    serSoc  = lv_chart_add_series(chart, C_SER_SOC,  LV_CHART_AXIS_SECONDARY_Y);
    serAmps = lv_chart_add_series(chart, C_SER_AMPS, LV_CHART_AXIS_SECONDARY_Y);

    lblWindow = mkLabel(page, &lv_font_montserrat_14, C_TEXT);
    lv_obj_align(lblWindow, LV_ALIGN_TOP_MID, 0, 310);

    // Range + scroll row
    static const char* rn[4] = {"Hour", "Day", "Week", "Month"};
    const int aw = 40, rw = 62, rh = 32, rg = 6;
    int total = 2 * aw + 4 * rw + 5 * rg;
    int x = (LCD_H_RES - total) / 2, y = 336;
    lv_obj_t* bl = mkButton(page, LV_SYMBOL_LEFT, aw, rh, onScroll, (void*)(intptr_t)1);
    lv_obj_set_pos(bl, x, y);
    x += aw + rg;
    for (int i = 0; i < 4; i++) {
        btnRange[i] = mkButton(page, rn[i], rw, rh, onRange, (void*)(intptr_t)i, &lv_font_montserrat_14);
        lv_obj_add_flag(btnRange[i], LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_style_bg_color(btnRange[i], C_ACCENT, LV_STATE_CHECKED);
        lv_obj_set_style_text_color(lv_obj_get_child(btnRange[i], 0), lv_color_black(), LV_STATE_CHECKED);
        lv_obj_set_pos(btnRange[i], x, y);
        x += rw + rg;
    }
    lv_obj_t* br = mkButton(page, LV_SYMBOL_RIGHT, aw, rh, onScroll, (void*)(intptr_t)-1);
    lv_obj_set_pos(br, x, y);
    applyRangeButtons();

    lblScale = mkLabel(page, &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lblScale, LV_ALIGN_TOP_MID, 0, 378);
    lv_label_set_text(lblScale, "");

    rebuildChart();
}

// ---------------------------------------------------------------------------
// Page 3: Setup
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
static void onTileChanged(lv_event_t*) {
    chartVisible = (lv_tileview_get_tile_act(tv) == pChart);
    if (chartVisible) rebuildChart();
}

void create(board::Display& display) {
    g_disp = &display;

    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, C_BG, 0);

    tv = lv_tileview_create(scr);
    lv_obj_set_style_bg_color(tv, C_BG, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* pHalo   = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    lv_obj_t* pDetail = lv_tileview_add_tile(tv, 1, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    pChart            = lv_tileview_add_tile(tv, 2, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    lv_obj_t* pSetup  = lv_tileview_add_tile(tv, 3, 0, LV_DIR_LEFT);
    for (lv_obj_t* p : {pHalo, pDetail, pChart, pSetup}) {
        lv_obj_set_style_bg_color(p, C_BG, 0);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_add_event_cb(tv, onTileChanged, LV_EVENT_VALUE_CHANGED, nullptr);

    buildHalo(pHalo);
    buildDetail(pDetail);
    buildChart(pChart);
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
        lv_label_set_text(lblSoc, "--");
        lv_obj_add_flag(lblSocUnit, LV_OBJ_FLAG_HIDDEN);
        lv_arc_set_value(arc, 0);
    }
    lv_obj_set_style_text_color(lblSoc, txt, 0);

    if (s.volts.valid()) snprintf(buf, sizeof buf, "%.2f V", s.volts.value);
    else snprintf(buf, sizeof buf, "-- V");
    lv_label_set_text(lblMainV, buf);
    lv_obj_set_style_text_color(lblMainV, txt, 0);

    if (s.extVolts.valid()) snprintf(buf, sizeof buf, "%.2f V", s.extVolts.value);
    else snprintf(buf, sizeof buf, "-- V");
    lv_label_set_text(lblAuxV, buf);
    lv_obj_set_style_text_color(lblAuxV, stale(s.extVolts) ? C_STALE : C_TEXT, 0);

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

    // Alert: aux battery is being charged but the main one is not.
    bool alert = !stale(s.extVolts) && !stale(s.current) &&
                 s.extVolts.value > ALERT_AUX_CHARGING_V &&
                 s.current.value < ALERT_MAIN_CHARGING_A;
    if (alert) {
        lv_label_set_text(lblAlert, LV_SYMBOL_WARNING " Aux charging, main is not");
        lv_obj_clear_flag(lblAlert, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(lblAlert, LV_OBJ_FLAG_HIDDEN);
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
    if ((int32_t)(millis() - switchHoldUntilMs) >= 0) {
        suppressSwitchEvents = true;
        bool relayOn = s.relay.valid() && s.relay.value > 0.5f;
        bool swOn = s.sw.valid() && s.sw.value > 0.5f;
        for (lv_obj_t* o : {swSwitch, swHaloSwitch}) {
            if (swOn) lv_obj_add_state(o, LV_STATE_CHECKED); else lv_obj_clear_state(o, LV_STATE_CHECKED);
        }
        if (relayOn) lv_obj_add_state(swRelay, LV_STATE_CHECKED); else lv_obj_clear_state(swRelay, LV_STATE_CHECKED);
        suppressSwitchEvents = false;
    }

    // ---- Chart page ----
    if (chartVisible && millis() - lastChartMs >= CHART_REFRESH_MS) rebuildChart();

    // ---- Setup page ----
    lv_label_set_text(btnPauseLbl, s.link == LinkState::Paused ? "Resume BLE" : "Pause BLE 5 min");
}

}  // namespace ui
