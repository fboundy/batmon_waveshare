#include "ui.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../batmon/batmon_client.h"
#include "../board/board.h"
#include "../config.h"
#include "../history.h"
#include "../presence.h"
#include "../settings.h"
#include "ui_internal.h"

using batmon::LinkState;
using batmon::State;

namespace ui {

Widgets w;
static constexpr size_t MAX_PHONE_LIST = 256;

const char* const detailNames[D_COUNT] = {
    "Main voltage", "Aux voltage", "Current", "Power", "Amp hours", "Ah full ref",
    "Ah min", "Ext temp", "CPU temp", "RSSI", "Polls ok/err", "Address", "History saved"};

static lv_obj_t* tv = nullptr;
static constexpr int NPAGES = 5;
static lv_obj_t* pages[NPAGES] = {};
static constexpr int PAGE_CHART = 1;

// After a user toggle, stop syncing the switches from device state for a
// moment so they don't snap back before the BLE command has gone through.
static uint32_t switchHoldUntilMs = 0;
static constexpr uint32_t SWITCH_HOLD_MS = 3000;
static bool suppressSwitchEvents = false;

// Chart state
static int  chartOffset = 0;
static bool secIsAmps = false;      // right axis shows amps (SoC hidden)
static float ampMin = -1, ampMax = 1;
static uint32_t lastChartMs = 0;
static bool chartVisible = false;
static history::Sample chartPts[history::POINTS];   // static: too big for the loop stack

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
// Gauge colour: red at <= 10 %, yellow at 50 %, green at >= 90 %, with a
// linear fade between those stops.
static lv_color_t socColor(float soc) {
    if (soc < 0)   return col::dim();
    if (soc >= 90) return col::good();
    if (soc <= 10) return col::bad();
    if (soc >= 50) {
        uint8_t t = (uint8_t)((soc - 50.0f) / 40.0f * 255.0f);   // 0 = yellow, 255 = green
        return lv_color_mix(col::good(), col::warn(), t);
    }
    uint8_t t = (uint8_t)((soc - 10.0f) / 40.0f * 255.0f);       // 0 = red, 255 = yellow
    return lv_color_mix(col::warn(), col::bad(), t);
}

static float toDisplayTemp(float c) {
    return g_settings.fahrenheit ? c * 9.0f / 5.0f + 32.0f : c;
}
static const char* tempUnit() { return g_settings.fahrenheit ? "\xC2\xB0" "F" : "\xC2\xB0" "C"; }

static bool stale(const batmon::Reading& r) {
    return !r.valid() || (millis() - r.updatedMs) > DATA_STALE_MS;
}

lv_obj_t* mkLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "--");
    return l;
}

lv_obj_t* mkRow(lv_obj_t* parent, lv_coord_t gap) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, gap, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return row;
}

lv_obj_t* mkButton(lv_obj_t* parent, const char* text, int wd, int ht, lv_event_cb_t cb, void* ud,
                   const lv_font_t* font) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_size(b, wd, ht);
    lv_obj_set_style_bg_color(b, col::btn(), 0);
    lv_obj_set_style_bg_color(b, col::accent(), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

lv_obj_t* mkCheckButton(lv_obj_t* parent, const char* text, int wd, int ht, lv_color_t on,
                        lv_event_cb_t cb, void* ud) {
    lv_obj_t* b = mkButton(parent, text, wd, ht, nullptr, nullptr, &lv_font_montserrat_14);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(b, on, LV_STATE_CHECKED);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), lv_color_black(), LV_STATE_CHECKED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_VALUE_CHANGED, ud);
    return b;
}

void setCapacityLabel() {
    if (!w.lblCapacity) return;
    char buf[32];
    if (g_settings.capacityAh > 0)
        snprintf(buf, sizeof buf, "%.0f Ah", g_settings.capacityAh);
    else
        snprintf(buf, sizeof buf, "not set");
    lv_label_set_text(w.lblCapacity, buf);
}

// ---------------------------------------------------------------------------
// Event callbacks (attached by the layouts)
// ---------------------------------------------------------------------------
void onSwitchSwitch(lv_event_t* e) {
    if (suppressSwitchEvents) return;
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    switchHoldUntilMs = millis() + SWITCH_HOLD_MS;
    batmon::g_client.requestSetIo(batmon::IoType::Switch, on);
}

void onSwitchRelay(lv_event_t* e) {
    if (suppressSwitchEvents) return;
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    switchHoldUntilMs = millis() + SWITCH_HOLD_MS;
    batmon::g_client.requestSetIo(batmon::IoType::Relay, on);
}

void onCapacity(lv_event_t* e) {
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    float v = g_settings.capacityAh + delta;
    if (v < 0) v = 0;
    if (v > 5000) v = 5000;
    g_settings.capacityAh = v;
    g_settings.save();
    setCapacityLabel();
}

void onBrightness(lv_event_t* e) {
    int v = lv_slider_get_value(lv_event_get_target(e));
    g_settings.brightness = v;
    board::setBacklight(v);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) g_settings.save();
}

void onFahrenheit(lv_event_t* e) {
    g_settings.fahrenheit = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    g_settings.save();
}

void onPause(lv_event_t*) {
    State s = batmon::g_client.snapshot();
    if (s.link == LinkState::Paused) batmon::g_client.resume();
    else batmon::g_client.pause(BLE_PAUSE_DEFAULT_MS);
}

void onForget(lv_event_t*) {
    batmon::g_client.forgetDevice();
}

void onPairPhone(lv_event_t*) {
    if (presence::pairing()) presence::stopPairing();
    else presence::startPairing(PAIRING_WINDOW_MS);
}

void onForgetPhones(lv_event_t*) {
    presence::forgetAll();
}

void onRelayPhone(lv_event_t* e) {
    g_settings.relayFollowsPhone = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    g_settings.save();
}

// ---- phone list selection / naming ----
static int selectedPhone = -1;
static int namingPhone = -1;
static int lastPhoneCount = -1;

static void applyPhoneSelection() {
    for (int i = 0; i < 8; i++) {
        if (!w.phoneRows[i]) continue;
        lv_obj_set_style_border_width(w.phoneRows[i], i == selectedPhone ? 2 : 0, 0);
        lv_obj_set_style_border_color(w.phoneRows[i], col::accent(), 0);
    }
    bool sel = selectedPhone >= 0 && selectedPhone < presence::count();
    for (lv_obj_t* b : {w.btnRename, w.btnDelete}) {
        if (!b) continue;
        if (sel) lv_obj_clear_state(b, LV_STATE_DISABLED); else lv_obj_add_state(b, LV_STATE_DISABLED);
    }
}

static void openNameDialog(int idx) {
    if (!w.nameDlg || idx < 0 || idx >= presence::count()) return;
    namingPhone = idx;
    char buf[48];
    snprintf(buf, sizeof buf, "Name for %s", presence::phone(idx).name);
    if (w.nameTitle) lv_label_set_text(w.nameTitle, buf);
    lv_textarea_set_text(w.nameTa, presence::phone(idx).name);
    lv_obj_clear_flag(w.nameDlg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(w.nameDlg);
}

void onPhoneRow(lv_event_t* e) {
    selectedPhone = (int)(intptr_t)lv_event_get_user_data(e);
    applyPhoneSelection();
}

void onRenamePhone(lv_event_t*) {
    openNameDialog(selectedPhone);
}

void onDeletePhone(lv_event_t*) {
    if (selectedPhone < 0) return;
    presence::forget(selectedPhone);
    selectedPhone = -1;
    lastPhoneCount = presence::count();   // a deletion is not a new pairing
    applyPhoneSelection();
}

void setTimeoutLabel() {
    if (!w.lblTimeout) return;
    char buf[32];
    snprintf(buf, sizeof buf, "Away after %u s", g_settings.presenceTimeoutS);
    lv_label_set_text(w.lblTimeout, buf);
}

void onTimeout(lv_event_t* e) {
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    int v = (int)g_settings.presenceTimeoutS + delta;
    if (v < PRESENCE_TIMEOUT_MIN_S) v = PRESENCE_TIMEOUT_MIN_S;
    if (v > PRESENCE_TIMEOUT_MAX_S) v = PRESENCE_TIMEOUT_MAX_S;
    g_settings.presenceTimeoutS = v;
    g_settings.save();
    setTimeoutLabel();
}

void onNameKeyboard(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY && namingPhone >= 0) {
        const char* t = lv_textarea_get_text(w.nameTa);
        if (t && t[0]) presence::rename(namingPhone, t);
    }
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        namingPhone = -1;
        lv_obj_add_flag(w.nameDlg, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---------------------------------------------------------------------------
// Chart
// ---------------------------------------------------------------------------
static history::Range chartRange() {
    uint8_t r = g_settings.chartRange;
    if (r > 3) r = 0;
    return (history::Range)r;
}

static void setWindowLabel() {
    if (!w.lblWindow) return;
    static const char* unit[4] = {"h", "d", "w", "mo"};
    static const char* whole[4] = {"last hour", "last 24 h", "last 7 days", "last 30 days"};
    int r = (int)chartRange();
    char buf[40];
    if (chartOffset == 0) snprintf(buf, sizeof buf, "%s", whole[r]);
    else snprintf(buf, sizeof buf, "-%d%s to -%d%s", chartOffset + 1, unit[r], chartOffset, unit[r]);
    lv_label_set_text(w.lblWindow, buf);
}

static void applyRangeButtons() {
    for (int i = 0; i < 4; i++) {
        if (!w.btnRange[i]) continue;
        if (i == (int)g_settings.chartRange) lv_obj_add_state(w.btnRange[i], LV_STATE_CHECKED);
        else lv_obj_clear_state(w.btnRange[i], LV_STATE_CHECKED);
    }
}

static void applySeriesButtons() {
    for (int i = 0; i < 4; i++) {
        if (!w.btnSeries[i]) continue;
        if (g_settings.chartMask & (1 << i)) lv_obj_add_state(w.btnSeries[i], LV_STATE_CHECKED);
        else lv_obj_clear_state(w.btnSeries[i], LV_STATE_CHECKED);
    }
}

static void rebuildChart() {
    if (!w.chart) return;
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
    lv_chart_set_range(w.chart, LV_CHART_AXIS_PRIMARY_Y, (lv_coord_t)lroundf(vmin * 100), (lv_coord_t)lroundf(vmax * 100));

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
    lv_chart_set_range(w.chart, LV_CHART_AXIS_SECONDARY_Y, 0, 100);

    lv_coord_t* aMain = lv_chart_get_y_array(w.chart, w.serMain);
    lv_coord_t* aAux  = lv_chart_get_y_array(w.chart, w.serAux);
    lv_coord_t* aSoc  = lv_chart_get_y_array(w.chart, w.serSoc);
    lv_coord_t* aAmp  = lv_chart_get_y_array(w.chart, w.serAmps);
    for (int i = 0; i < history::POINTS; i++) {
        const history::Sample& s = chartPts[i];
        aMain[i] = isnan(s.mainV)   ? LV_CHART_POINT_NONE : (lv_coord_t)lroundf(s.mainV * 100);
        aAux[i]  = isnan(s.auxV)    ? LV_CHART_POINT_NONE : (lv_coord_t)lroundf(s.auxV * 100);
        aSoc[i]  = isnan(s.soc)     ? LV_CHART_POINT_NONE : (lv_coord_t)lroundf(s.soc);
        aAmp[i]  = isnan(s.current) ? LV_CHART_POINT_NONE
                                    : (lv_coord_t)lroundf((s.current - amin) / (amax - amin) * 100.0f);
    }
    lv_chart_hide_series(w.chart, w.serMain, !(mask & 1));
    lv_chart_hide_series(w.chart, w.serAux,  !(mask & 2));
    lv_chart_hide_series(w.chart, w.serSoc,  !(mask & 4));
    lv_chart_hide_series(w.chart, w.serAmps, !(mask & 8));
    lv_chart_refresh(w.chart);

    setWindowLabel();
    if (w.lblScale) {
        char buf[64];
        if ((mask & 8) && !secIsAmps)
            snprintf(buf, sizeof buf, "amps scaled %.0f to %.0f A (right axis = SoC)", amin, amax);
        else if (secIsAmps)
            snprintf(buf, sizeof buf, "left: volts   right: amps");
        else if (mask & 4)
            snprintf(buf, sizeof buf, "left: volts   right: SoC %%");
        else
            snprintf(buf, sizeof buf, "left: volts");
        lv_label_set_text(w.lblScale, buf);
    }
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

lv_obj_t* mkChart(lv_obj_t* parent, int wd, int ht, const lv_font_t* tickFont, int tickLabelSize) {
    lv_obj_t* c = lv_chart_create(parent);
    lv_obj_set_size(c, wd, ht);
    lv_chart_set_type(c, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(c, history::POINTS);
    lv_chart_set_div_line_count(c, 5, 5);
    lv_chart_set_axis_tick(c, LV_CHART_AXIS_PRIMARY_Y, 6, 3, 5, 2, true, tickLabelSize);
    lv_chart_set_axis_tick(c, LV_CHART_AXIS_SECONDARY_Y, 6, 3, 5, 2, true, tickLabelSize);
    lv_chart_set_axis_tick(c, LV_CHART_AXIS_PRIMARY_X, 6, 3, 5, 2, false, 10);
    lv_obj_set_style_bg_color(c, col::bg(), 0);
    lv_obj_set_style_border_color(c, col::grid(), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, 0, 0);
    lv_obj_set_style_pad_all(c, 2, 0);
    lv_obj_set_style_line_color(c, col::grid(), LV_PART_MAIN);
    lv_obj_set_style_line_width(c, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(c, 0, LV_PART_INDICATOR);   // no point markers
    lv_obj_set_style_text_font(c, tickFont, LV_PART_TICKS);
    lv_obj_set_style_text_color(c, col::dim(), LV_PART_TICKS);
    lv_obj_set_style_line_color(c, col::dim(), LV_PART_TICKS);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(c, onChartDraw, LV_EVENT_DRAW_PART_BEGIN, nullptr);

    w.chart = c;
    w.serMain = lv_chart_add_series(c, col::serMain(), LV_CHART_AXIS_PRIMARY_Y);
    w.serAux  = lv_chart_add_series(c, col::serAux(),  LV_CHART_AXIS_PRIMARY_Y);
    w.serSoc  = lv_chart_add_series(c, col::serSoc(),  LV_CHART_AXIS_SECONDARY_Y);
    w.serAmps = lv_chart_add_series(c, col::serAmps(), LV_CHART_AXIS_SECONDARY_Y);
    return c;
}

void onSeriesToggle(lv_event_t* e) {
    int bit = (int)(intptr_t)lv_event_get_user_data(e);
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if (on) g_settings.chartMask |= (1 << bit);
    else    g_settings.chartMask &= ~(1 << bit);
    g_settings.save();
    rebuildChart();
}

void onRange(lv_event_t* e) {
    g_settings.chartRange = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    g_settings.save();
    chartOffset = 0;
    applyRangeButtons();
    rebuildChart();
}

void onScroll(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);   // +1 = older
    int mx = history::maxOffset(chartRange());
    chartOffset += dir;
    if (chartOffset < 0) chartOffset = 0;
    if (chartOffset > mx) chartOffset = mx;
    rebuildChart();
}

void cycleChartRange() {
    g_settings.chartRange = (g_settings.chartRange + 1) % 4;
    g_settings.save();
    chartOffset = 0;
    applyRangeButtons();
    rebuildChart();
}

void settingsChanged() {
    setCapacityLabel();
    setTimeoutLabel();
    if (w.swRelayPhone) {
        if (g_settings.relayFollowsPhone) lv_obj_add_state(w.swRelayPhone, LV_STATE_CHECKED);
        else lv_obj_clear_state(w.swRelayPhone, LV_STATE_CHECKED);
    }
    if (w.sliderBright) lv_slider_set_value(w.sliderBright, g_settings.brightness, LV_ANIM_OFF);
    if (w.swFahrenheit) {
        if (g_settings.fahrenheit) lv_obj_add_state(w.swFahrenheit, LV_STATE_CHECKED);
        else lv_obj_clear_state(w.swFahrenheit, LV_STATE_CHECKED);
    }
    applyRangeButtons();
    applySeriesButtons();
    chartOffset = 0;
    rebuildChart();
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------
static void onTileChanged(lv_event_t*) {
    chartVisible = (lv_tileview_get_tile_act(tv) == pages[PAGE_CHART]);
    if (chartVisible) rebuildChart();
}

void nextPage() {
    lv_obj_t* act = lv_tileview_get_tile_act(tv);
    int i = 0;
    for (; i < NPAGES; i++) if (pages[i] == act) break;
    i = (i + 1) % NPAGES;
    lv_obj_set_tile(tv, pages[i], LV_ANIM_ON);
    chartVisible = (i == PAGE_CHART);
    if (chartVisible) rebuildChart();
}

void create() {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, col::bg(), 0);

    tv = lv_tileview_create(scr);
    lv_obj_set_style_bg_color(tv, col::bg(), 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    pages[0] = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    for (int i = 1; i < NPAGES - 1; i++)
        pages[i] = lv_tileview_add_tile(tv, i, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    pages[NPAGES - 1] = lv_tileview_add_tile(tv, NPAGES - 1, 0, LV_DIR_LEFT);
    for (lv_obj_t* p : pages) {
        lv_obj_set_style_bg_color(p, col::bg(), 0);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_add_event_cb(tv, onTileChanged, LV_EVENT_VALUE_CHANGED, nullptr);

    layout::halo(pages[0]);
    layout::chart(pages[1]);
    layout::detail(pages[2]);
    layout::phones(pages[3]);
    layout::setup(pages[4]);

    applyRangeButtons();
    applySeriesButtons();
    setCapacityLabel();
    setTimeoutLabel();
    rebuildChart();
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
static void setText(lv_obj_t* o, const char* t) { if (o) lv_label_set_text(o, t); }
static void setColor(lv_obj_t* o, lv_color_t c) { if (o) lv_obj_set_style_text_color(o, c, 0); }

void update(const State& s) {
    char buf[160];

    // ---- Halo page ----
    setText(w.lblName, s.deviceName[0] ? s.deviceName : "BatMon");

    bool vStale = stale(s.volts);
    lv_color_t txt = vStale ? col::stale() : col::text();

    if (s.soc >= 0) {
        snprintf(buf, sizeof buf, "%.0f", s.soc);
        setText(w.lblSoc, buf);
        if (w.lblSocUnit) lv_obj_clear_flag(w.lblSocUnit, LV_OBJ_FLAG_HIDDEN);
        lv_color_t gc = vStale ? col::stale() : socColor(s.soc);
        if (w.arc) {
            lv_arc_set_value(w.arc, (int)lroundf(s.soc));
            lv_obj_set_style_arc_color(w.arc, gc, LV_PART_INDICATOR);
        }
        if (w.bar) {
            lv_bar_set_value(w.bar, (int)lroundf(s.soc), LV_ANIM_OFF);
            lv_obj_set_style_bg_color(w.bar, gc, LV_PART_INDICATOR);
        }
    } else {
        setText(w.lblSoc, "--");
        if (w.lblSocUnit) lv_obj_add_flag(w.lblSocUnit, LV_OBJ_FLAG_HIDDEN);
        if (w.arc) lv_arc_set_value(w.arc, 0);
        if (w.bar) lv_bar_set_value(w.bar, 0, LV_ANIM_OFF);
    }
    setColor(w.lblSoc, txt);

    if (s.volts.valid()) snprintf(buf, sizeof buf, "%.2f", s.volts.value);
    else snprintf(buf, sizeof buf, "--");
    setText(w.lblMainV, buf);
    setColor(w.lblMainV, txt);

    if (s.extVolts.valid()) snprintf(buf, sizeof buf, "%.2f", s.extVolts.value);
    else snprintf(buf, sizeof buf, "--");
    setText(w.lblAuxV, buf);
    setColor(w.lblAuxV, stale(s.extVolts) ? col::stale() : col::text());

    if (s.current.valid()) {
        float a = s.current.value;
        snprintf(buf, sizeof buf, "%s%.1f A", a > 0.05f ? LV_SYMBOL_UP " " : (a < -0.05f ? LV_SYMBOL_DOWN " " : ""), fabsf(a));
        setText(w.lblAmps, buf);
        setColor(w.lblAmps, vStale ? col::stale() : (a > 0.05f ? col::charge() : (a < -0.05f ? col::warn() : col::text())));
        snprintf(buf, sizeof buf, "%.0f W", fabsf(s.watts));
        setText(w.lblWatts, buf);
    } else {
        setText(w.lblAmps, "-- A");
        setText(w.lblWatts, "-- W");
    }
    setColor(w.lblWatts, txt);

    if (s.extTemp.valid()) snprintf(buf, sizeof buf, "%.1f %s", toDisplayTemp(s.extTemp.value), tempUnit());
    else snprintf(buf, sizeof buf, "-- %s", tempUnit());
    setText(w.lblTemp, buf);

    if (s.hoursRemaining > 0 && s.hoursRemaining < 1000 && !vStale) {
        int h = (int)s.hoursRemaining;
        int m = (int)((s.hoursRemaining - h) * 60);
        snprintf(buf, sizeof buf, "%dh %02dm to %s", h, m, s.current.value > 0 ? "full" : "empty");
        setText(w.lblRuntime, buf);
    } else if (s.soc < 0 && s.link == LinkState::Connected) {
        setText(w.lblRuntime, "Set capacity for SoC");
    } else {
        setText(w.lblRuntime, "");
    }

    // Charge icon: is a charger running (aux voltage), and is the main
    // battery getting any of it?
    //   blue    aux < 13.2 V                     nothing charging
    //   green   aux >= 13.2 V and main current +  main charging
    //   yellow  aux >= 13.2 V, main not charging, but main is full (SoC > 95 % or > 14.4 V)
    //   red     aux >= 13.2 V, main not charging and not full  -> split charge / DC-DC fault
    if (w.lblCharge) {
        lv_color_t c = col::dim();
        if (!stale(s.extVolts)) {
            if (s.extVolts.value < CHG_AUX_CHARGING_V) {
                c = col::charge();
            } else if (!stale(s.current) && s.current.value > CHG_MAIN_CURRENT_A) {
                c = col::good();
            } else if (s.soc > CHG_FULL_SOC || (s.volts.valid() && s.volts.value > CHG_FULL_MAIN_V)) {
                c = col::warn();
            } else {
                c = col::bad();
            }
        }
        lv_obj_set_style_text_color(w.lblCharge, c, 0);
    }

    // Link status: green only when connected with fresh data, else red
    bool linked = s.link == LinkState::Connected && !vStale;
    setColor(w.lblBt, linked ? col::good() : col::bad());
    if (linked) board::setStatusLed(0, 255, 0);
    else if (s.link == LinkState::Paused) board::setStatusLed(0, 0, 255);
    else if (s.link == LinkState::Standby) board::setStatusLed(0, 0, 0);
    else board::setStatusLed(255, 0, 0);
    if (s.link == LinkState::Standby) setText(w.lblRuntime, "Standby: no phone present");

    bool relayOn = s.relay.valid() && s.relay.value > 0.5f;
    bool swOn = s.sw.valid() && s.sw.value > 0.5f;
    if (w.lblRelayState) {
        snprintf(buf, sizeof buf, "Relay %s", s.relay.valid() ? (relayOn ? "ON" : "OFF") : "--");
        lv_label_set_text(w.lblRelayState, buf);
        lv_obj_set_style_text_color(w.lblRelayState, relayOn ? col::accent() : col::dim(), 0);
    }

    // ---- Detail page ----
    auto fmt = [&](DetailRow r, const batmon::Reading& rd, const char* f, float scale = 1.0f) {
        if (!w.detVal[r]) return;
        if (rd.valid()) snprintf(buf, sizeof buf, f, rd.value * scale);
        else snprintf(buf, sizeof buf, "--");
        lv_label_set_text(w.detVal[r], buf);
        lv_obj_set_style_text_color(w.detVal[r], stale(rd) ? col::stale() : col::text(), 0);
    };
    fmt(D_VOLTS, s.volts, "%.2f V");
    fmt(D_EXT_VOLTS, s.extVolts, "%.2f V");
    fmt(D_CURRENT, s.current, "%.2f A");
    snprintf(buf, sizeof buf, "%.1f W", s.watts);
    setText(w.detVal[D_WATTS], buf);
    fmt(D_AH, s.ampHours, "%.2f Ah");
    fmt(D_AH_MAX, s.ampHoursMax, "%.2f Ah");
    fmt(D_AH_MIN, s.ampHoursMin, "%.2f Ah");
    if (s.extTemp.valid()) snprintf(buf, sizeof buf, "%.1f %s", toDisplayTemp(s.extTemp.value), tempUnit()); else snprintf(buf, sizeof buf, "--");
    setText(w.detVal[D_EXT_TEMP], buf);
    if (s.intTemp.valid()) snprintf(buf, sizeof buf, "%.1f %s", toDisplayTemp(s.intTemp.value), tempUnit()); else snprintf(buf, sizeof buf, "--");
    setText(w.detVal[D_INT_TEMP], buf);
    snprintf(buf, sizeof buf, "%d dBm", s.rssi);
    setText(w.detVal[D_RSSI], buf);
    snprintf(buf, sizeof buf, "%lu / %lu", (unsigned long)s.pollOk, (unsigned long)s.pollErrors);
    setText(w.detVal[D_POLLS], buf);
    setText(w.detVal[D_ADDR], s.deviceAddr[0] ? s.deviceAddr : "--");
    if (history::lastSaveMs()) {
        uint32_t ago = (millis() - history::lastSaveMs()) / 1000;
        snprintf(buf, sizeof buf, "%lum %02lus ago", (unsigned long)ago / 60, (unsigned long)ago % 60);
    } else {
        snprintf(buf, sizeof buf, "not yet (%lu restored)", (unsigned long)history::restoredSamples());
    }
    setText(w.detVal[D_HISTORY], buf);

    if (w.lblIoState) {
        snprintf(buf, sizeof buf, "Relay %s    Switch %s",
                 s.relay.valid() ? (relayOn ? "ON" : "OFF") : "--",
                 s.sw.valid() ? (swOn ? "ON" : "OFF") : "--");
        lv_label_set_text(w.lblIoState, buf);
    }

    // Reflect the device's real pin state without firing our own handlers.
    if ((int32_t)(millis() - switchHoldUntilMs) >= 0) {
        suppressSwitchEvents = true;
        if (w.swSwitch) {
            if (swOn) lv_obj_add_state(w.swSwitch, LV_STATE_CHECKED); else lv_obj_clear_state(w.swSwitch, LV_STATE_CHECKED);
        }
        for (lv_obj_t* o : {w.swRelay, w.swHaloRelay}) {
            if (!o) continue;
            if (relayOn) lv_obj_add_state(o, LV_STATE_CHECKED); else lv_obj_clear_state(o, LV_STATE_CHECKED);
        }
        suppressSwitchEvents = false;
    }

    // ---- Chart page ----
    if (chartVisible && millis() - lastChartMs >= CHART_REFRESH_MS) rebuildChart();

    // ---- Phones page ----
    int nPhones = presence::count();
    if (lastPhoneCount >= 0 && nPhones > lastPhoneCount) {
        // A phone just paired: ask for its name (touch layouts only).
        openNameDialog(nPhones - 1);
    }
    lastPhoneCount = nPhones;
    if (w.phoneRows[0]) {
        for (int i = 0; i < 8; i++) {
            if (!w.phoneRows[i]) continue;
            if (i < nPhones) {
                const presence::Phone& p = presence::phone(i);
                if (presence::present(i)) snprintf(buf, sizeof buf, "%s   %d dBm", p.name, p.rssi);
                else snprintf(buf, sizeof buf, "%s   away", p.name);
                lv_label_set_text(lv_obj_get_child(w.phoneRows[i], 0), buf);
                lv_obj_set_style_text_color(lv_obj_get_child(w.phoneRows[i], 0),
                                            presence::present(i) ? col::good() : col::dim(), 0);
                lv_obj_clear_flag(w.phoneRows[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(w.phoneRows[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (selectedPhone >= nPhones) { selectedPhone = -1; applyPhoneSelection(); }
    }
    if (w.lblPhones) {
        char list[MAX_PHONE_LIST];
        int n = presence::count();
        if (n == 0) {
            snprintf(list, sizeof list, "No phones paired.\nThe display works normally.");
        } else {
            size_t used = 0;
            for (int i = 0; i < n && used < sizeof list - 1; i++) {
                const presence::Phone& p = presence::phone(i);
                bool here = presence::present(i);
                int wrote;
                if (here)
                    wrote = snprintf(list + used, sizeof list - used, "%s%s  %d dBm", used ? "\n" : "",
                                     p.name, p.rssi);
                else
                    wrote = snprintf(list + used, sizeof list - used, "%s%s  away", used ? "\n" : "", p.name);
                if (wrote > 0) used += wrote;
            }
        }
        lv_label_set_text(w.lblPhones, list);
    }
    if (w.lblPairStatus) {
        if (nPhones == 0 && !presence::pairing()) {
            snprintf(buf, sizeof buf, "No phones paired: the display works normally.");
        } else if (presence::pairing()) {
            uint32_t left = presence::pairingRemainingMs() / 1000;
            snprintf(buf, sizeof buf, "Pairing: %lu:%02lu left. On the phone open nRF Connect,\n"
                     "connect to 'BatMon Display' and read the characteristic.",
                     (unsigned long)left / 60, (unsigned long)left % 60);
        } else {
            snprintf(buf, sizeof buf, "Screen and BatMon link stay off until a paired phone is near.");
        }
        lv_label_set_text(w.lblPairStatus, buf);
    }
    // Phone icon on the Halo page: nearest present phone, else first paired
    if (w.lblPhoneIcon) {
        if (nPhones == 0) {
            lv_obj_add_flag(w.lblPhoneIcon, LV_OBJ_FLAG_HIDDEN);
            if (w.lblPhoneName) lv_obj_add_flag(w.lblPhoneName, LV_OBJ_FLAG_HIDDEN);
        } else {
            int near = presence::nearestPresent();
            lv_obj_clear_flag(w.lblPhoneIcon, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(w.lblPhoneIcon, near >= 0 ? col::good() : col::bad(), 0);
            if (w.lblPhoneName) {
                lv_obj_clear_flag(w.lblPhoneName, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(w.lblPhoneName, presence::phone(near >= 0 ? near : 0).name);
                lv_obj_set_style_text_color(w.lblPhoneName, near >= 0 ? col::text() : col::dim(), 0);
            }
        }
    }
    setText(w.btnPairLbl, presence::pairing() ? "Stop pairing" : "Pair new phone");

    // ---- Setup page ----
    setText(w.btnPauseLbl, s.link == LinkState::Paused ? "Resume BLE" : "Pause BLE 5 min");
    if (w.lblSetupInfo) {
        char cap[24];
        if (g_settings.capacityAh > 0) snprintf(cap, sizeof cap, "%.0f Ah", g_settings.capacityAh);
        else snprintf(cap, sizeof cap, "not set");
        snprintf(buf, sizeof buf, "Capacity %s   Bright %u%%   %s   Poll %u ms   Relay follows phone: %s",
                 cap, g_settings.brightness, g_settings.fahrenheit ? "F" : "C", g_settings.pollMs,
                 g_settings.relayFollowsPhone ? "on" : "off");
        lv_label_set_text(w.lblSetupInfo, buf);
    }
}

}  // namespace ui
