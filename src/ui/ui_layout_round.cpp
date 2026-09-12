// Page layouts for the 480x480 round panel (Waveshare ESP32-S3-Touch-LCD-2.1).
// Everything is kept inside the visible circle (~440 px diameter) and, on the
// Halo page, inside the SoC arc's bottom gap.  Touch is assumed.
#include "../config.h"
#if BOARD_ROUND

#include "../settings.h"
#include "ui_internal.h"

LV_FONT_DECLARE(lv_font_montserrat_72_digits)   // src/ui/font_montserrat_72_digits.c

namespace ui {
namespace layout {

// ---------------------------------------------------------------------------
// Page 0: Halo
// ---------------------------------------------------------------------------
void halo(lv_obj_t* page) {
    // SoC ring around the rim
    w.arc = lv_arc_create(page);
    lv_obj_set_size(w.arc, 456, 456);
    lv_obj_center(w.arc);
    lv_arc_set_rotation(w.arc, 135);
    lv_arc_set_bg_angles(w.arc, 0, 270);
    lv_arc_set_range(w.arc, 0, 100);
    lv_arc_set_value(w.arc, 0);
    lv_obj_remove_style(w.arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(w.arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(w.arc, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_width(w.arc, 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(w.arc, col::track(), LV_PART_MAIN);
    lv_obj_set_style_arc_color(w.arc, col::good(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(w.arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(w.arc, true, LV_PART_MAIN);

    w.lblName = mkLabel(page, &lv_font_montserrat_18, col::dim());
    lv_obj_align(w.lblName, LV_ALIGN_TOP_MID, 0, 62);
    lv_label_set_text(w.lblName, "BatMon");

    // Vertical layout (centre-relative): SoC -108, volts -36, A/W +14,
    // temp +50, runtime +78, relay +124, status icons +192.

    // SoC: 72 px digits + 32 px unit on a shared baseline
    lv_obj_t* rowSoc = mkRow(page, 6);
    lv_obj_align(rowSoc, LV_ALIGN_CENTER, 0, -108);
    w.lblSoc = mkLabel(rowSoc, &lv_font_montserrat_72_digits, col::text());
    w.lblSocUnit = mkLabel(rowSoc, &lv_font_montserrat_32, col::dim());
    lv_label_set_text(w.lblSocUnit, "%");
    // baseline correction = big font base_line - small font base_line
    lv_obj_set_style_translate_y(w.lblSocUnit, -(15 - 6), 0);

    // Voltages: "Main 13.19 V   Aux 12.62 V" (40 px values, 28 px units)
    lv_obj_t* rowV = mkRow(page, 5);
    lv_obj_align(rowV, LV_ALIGN_CENTER, 0, -36);
    lv_obj_t* capMain = mkLabel(rowV, &lv_font_montserrat_14, col::dim());
    lv_label_set_text(capMain, "Main");
    lv_obj_set_style_translate_y(capMain, -(8 - 3), 0);
    w.lblMainV = mkLabel(rowV, &lv_font_montserrat_40, col::text());
    lv_obj_t* uMain = mkLabel(rowV, &lv_font_montserrat_28, col::dim());
    lv_label_set_text(uMain, "V");
    lv_obj_set_style_translate_y(uMain, -(8 - 5), 0);
    lv_obj_t* spacer = lv_obj_create(rowV);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 14, 1);
    lv_obj_t* capAux = mkLabel(rowV, &lv_font_montserrat_14, col::dim());
    lv_label_set_text(capAux, "Aux");
    lv_obj_set_style_translate_y(capAux, -(8 - 3), 0);
    w.lblAuxV = mkLabel(rowV, &lv_font_montserrat_40, col::text());
    lv_obj_t* uAux = mkLabel(rowV, &lv_font_montserrat_28, col::dim());
    lv_label_set_text(uAux, "V");
    lv_obj_set_style_translate_y(uAux, -(8 - 5), 0);

    w.lblAmps = mkLabel(page, &lv_font_montserrat_28, col::text());
    lv_obj_align(w.lblAmps, LV_ALIGN_CENTER, -72, 14);
    w.lblWatts = mkLabel(page, &lv_font_montserrat_28, col::text());
    lv_obj_align(w.lblWatts, LV_ALIGN_CENTER, 72, 14);

    w.lblTemp = mkLabel(page, &lv_font_montserrat_20, col::dim());
    lv_obj_align(w.lblTemp, LV_ALIGN_CENTER, 0, 50);

    w.lblRuntime = mkLabel(page, &lv_font_montserrat_18, col::dim());
    lv_obj_align(w.lblRuntime, LV_ALIGN_CENTER, 0, 78);
    lv_label_set_text(w.lblRuntime, "");

    // Relay output control - big enough to hit with a thumb
    lv_obj_t* rowSw = mkRow(page, 14);
    lv_obj_set_flex_align(rowSw, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(rowSw, LV_ALIGN_CENTER, 0, 124);
    lv_obj_t* capSw = mkLabel(rowSw, &lv_font_montserrat_20, col::dim());
    lv_label_set_text(capSw, "Relay");
    w.swHaloRelay = lv_switch_create(rowSw);
    lv_obj_set_size(w.swHaloRelay, 100, 48);
    lv_obj_set_style_bg_color(w.swHaloRelay, col::accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(w.swHaloRelay, onSwitchRelay, LV_EVENT_VALUE_CHANGED, nullptr);

    // Status icons in the arc's bottom gap: Bluetooth link and charge state
    w.lblBt = mkLabel(page, &lv_font_montserrat_42, col::bad());
    lv_label_set_text(w.lblBt, LV_SYMBOL_BLUETOOTH);
    lv_obj_align(w.lblBt, LV_ALIGN_CENTER, -36, 192);
    w.lblCharge = mkLabel(page, &lv_font_montserrat_42, col::dim());
    lv_label_set_text(w.lblCharge, LV_SYMBOL_CHARGE);
    lv_obj_align(w.lblCharge, LV_ALIGN_CENTER, 36, 192);
}

// ---------------------------------------------------------------------------
// Page 1: Chart
// ---------------------------------------------------------------------------
void chart(lv_obj_t* page) {
    // Series toggles
    static const char* names[4] = {"Main V", "Aux V", "SoC", "Amps"};
    const lv_color_t cols[4] = {col::serMain(), col::serAux(), col::serSoc(), col::serAmps()};
    const int bw = 74, bh = 30, gap = 6;
    int bx = (LCD_H_RES - (4 * bw + 3 * gap)) / 2;
    for (int i = 0; i < 4; i++) {
        w.btnSeries[i] = mkCheckButton(page, names[i], bw, bh, cols[i], onSeriesToggle, (void*)(intptr_t)i);
        lv_obj_set_pos(w.btnSeries[i], bx + i * (bw + gap), 64);
    }

    lv_obj_t* c = mkChart(page, 316, 186, &lv_font_montserrat_12, 44);
    lv_obj_set_pos(c, (LCD_H_RES - 316) / 2, 114);

    w.lblWindow = mkLabel(page, &lv_font_montserrat_14, col::text());
    lv_obj_align(w.lblWindow, LV_ALIGN_TOP_MID, 0, 310);

    // Range + scroll row
    static const char* rn[4] = {"Hour", "Day", "Week", "Month"};
    const int aw = 40, rw = 62, rh = 32, rg = 6;
    int total = 2 * aw + 4 * rw + 5 * rg;
    int x = (LCD_H_RES - total) / 2, y = 336;
    lv_obj_t* bl = mkButton(page, LV_SYMBOL_LEFT, aw, rh, onScroll, (void*)(intptr_t)1);
    lv_obj_set_pos(bl, x, y);
    x += aw + rg;
    for (int i = 0; i < 4; i++) {
        w.btnRange[i] = mkButton(page, rn[i], rw, rh, onRange, (void*)(intptr_t)i, &lv_font_montserrat_14);
        lv_obj_add_flag(w.btnRange[i], LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_style_bg_color(w.btnRange[i], col::accent(), LV_STATE_CHECKED);
        lv_obj_set_style_text_color(lv_obj_get_child(w.btnRange[i], 0), lv_color_black(), LV_STATE_CHECKED);
        lv_obj_set_pos(w.btnRange[i], x, y);
        x += rw + rg;
    }
    lv_obj_t* br = mkButton(page, LV_SYMBOL_RIGHT, aw, rh, onScroll, (void*)(intptr_t)-1);
    lv_obj_set_pos(br, x, y);

    w.lblScale = mkLabel(page, &lv_font_montserrat_12, col::dim());
    lv_obj_align(w.lblScale, LV_ALIGN_TOP_MID, 0, 378);
    lv_label_set_text(w.lblScale, "");
}

// ---------------------------------------------------------------------------
// Page 2: Details
// ---------------------------------------------------------------------------
void detail(lv_obj_t* page) {
    lv_obj_t* title = mkLabel(page, &lv_font_montserrat_20, col::accent());
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 46);
    lv_label_set_text(title, "Details");

    // Two-column list kept inside the visible circle.
    const int x0 = 96, y0 = 84, rowH = 24;
    for (int i = 0; i < D_COUNT; i++) {
        lv_obj_t* n = mkLabel(page, &lv_font_montserrat_16, col::dim());
        lv_label_set_text(n, detailNames[i]);
        lv_obj_set_pos(n, x0, y0 + i * rowH);
        w.detVal[i] = mkLabel(page, &lv_font_montserrat_16, col::text());
        lv_obj_set_width(w.detVal[i], 180);
        lv_obj_set_style_text_align(w.detVal[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(w.detVal[i], LCD_H_RES - x0 - 180, y0 + i * rowH);
    }

    int y = y0 + D_COUNT * rowH + 8;
    lv_obj_t* lr = mkLabel(page, &lv_font_montserrat_16, col::dim());
    lv_label_set_text(lr, "Relay");
    lv_obj_set_pos(lr, x0 + 40, y + 6);
    w.swRelay = lv_switch_create(page);
    lv_obj_set_pos(w.swRelay, x0 + 100, y);
    lv_obj_add_event_cb(w.swRelay, onSwitchRelay, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* ls = mkLabel(page, &lv_font_montserrat_16, col::dim());
    lv_label_set_text(ls, "Switch");
    lv_obj_set_pos(ls, x0 + 180, y + 6);
    w.swSwitch = lv_switch_create(page);
    lv_obj_set_pos(w.swSwitch, x0 + 244, y);
    lv_obj_add_event_cb(w.swSwitch, onSwitchSwitch, LV_EVENT_VALUE_CHANGED, nullptr);
}

// ---------------------------------------------------------------------------
// Page 3: Phones
// ---------------------------------------------------------------------------
void phones(lv_obj_t* page) {
    lv_obj_t* title = mkLabel(page, &lv_font_montserrat_20, col::accent());
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 46);
    lv_label_set_text(title, "Phones");

    w.lblPhones = mkLabel(page, &lv_font_montserrat_18, col::text());
    lv_obj_set_width(w.lblPhones, 300);
    lv_obj_set_style_text_align(w.lblPhones, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(w.lblPhones, LV_LABEL_LONG_WRAP);
    lv_obj_align(w.lblPhones, LV_ALIGN_TOP_MID, 0, 84);

    lv_obj_t* bp = mkButton(page, "Pair new phone", 220, 44, onPairPhone, nullptr);
    lv_obj_align(bp, LV_ALIGN_CENTER, 0, 30);
    w.btnPairLbl = lv_obj_get_child(bp, 0);

    w.lblPairStatus = mkLabel(page, &lv_font_montserrat_14, col::dim());
    lv_obj_set_width(w.lblPairStatus, 340);
    lv_obj_set_style_text_align(w.lblPairStatus, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(w.lblPairStatus, LV_LABEL_LONG_WRAP);
    lv_obj_align(w.lblPairStatus, LV_ALIGN_CENTER, 0, 88);

    lv_obj_t* bf = mkButton(page, "Forget all phones", 220, 40, onForgetPhones, nullptr);
    lv_obj_align(bf, LV_ALIGN_CENTER, 0, 150);
}

// ---------------------------------------------------------------------------
// Page 4: Setup
// ---------------------------------------------------------------------------
void setup(lv_obj_t* page) {
    lv_obj_t* title = mkLabel(page, &lv_font_montserrat_20, col::accent());
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 46);
    lv_label_set_text(title, "Setup");

    // Capacity
    lv_obj_t* cl = mkLabel(page, &lv_font_montserrat_16, col::dim());
    lv_label_set_text(cl, "Battery capacity");
    lv_obj_align(cl, LV_ALIGN_TOP_MID, 0, 86);
    w.lblCapacity = mkLabel(page, &lv_font_montserrat_28, col::text());
    lv_obj_align(w.lblCapacity, LV_ALIGN_TOP_MID, 0, 108);

    const int by = 150, bw = 64, bh = 40, gap = 8;
    int bx = (LCD_H_RES - (4 * bw + 3 * gap)) / 2;
    const char* txt[4] = {"-10", "-1", "+1", "+10"};
    const int dl[4] = {-10, -1, 1, 10};
    for (int i = 0; i < 4; i++) {
        lv_obj_t* b = mkButton(page, txt[i], bw, bh, onCapacity, (void*)(intptr_t)dl[i]);
        lv_obj_set_pos(b, bx + i * (bw + gap), by);
    }

    // Brightness
    lv_obj_t* bl = mkLabel(page, &lv_font_montserrat_16, col::dim());
    lv_label_set_text(bl, "Brightness");
    lv_obj_align(bl, LV_ALIGN_TOP_MID, 0, 206);
    w.sliderBright = lv_slider_create(page);
    lv_obj_set_size(w.sliderBright, 260, 14);
    lv_obj_align(w.sliderBright, LV_ALIGN_TOP_MID, 0, 234);
    lv_slider_set_range(w.sliderBright, 5, 100);
    lv_slider_set_value(w.sliderBright, g_settings.brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(w.sliderBright, col::accent(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(w.sliderBright, col::accent(), LV_PART_KNOB);
    lv_obj_add_event_cb(w.sliderBright, onBrightness, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(w.sliderBright, onBrightness, LV_EVENT_RELEASED, nullptr);

    // Units and relay-follows-phone, side by side
    lv_obj_t* ul = mkLabel(page, &lv_font_montserrat_16, col::dim());
    lv_label_set_text(ul, "Fahrenheit");
    lv_obj_align(ul, LV_ALIGN_TOP_MID, -140, 276);
    w.swFahrenheit = lv_switch_create(page);
    lv_obj_align(w.swFahrenheit, LV_ALIGN_TOP_MID, -60, 270);
    if (g_settings.fahrenheit) lv_obj_add_state(w.swFahrenheit, LV_STATE_CHECKED);
    lv_obj_add_event_cb(w.swFahrenheit, onFahrenheit, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* rl = mkLabel(page, &lv_font_montserrat_16, col::dim());
    lv_label_set_text(rl, "Relay w/ phone");
    lv_obj_align(rl, LV_ALIGN_TOP_MID, 60, 276);
    w.swRelayPhone = lv_switch_create(page);
    lv_obj_align(w.swRelayPhone, LV_ALIGN_TOP_MID, 160, 270);
    if (g_settings.relayFollowsPhone) lv_obj_add_state(w.swRelayPhone, LV_STATE_CHECKED);
    lv_obj_add_event_cb(w.swRelayPhone, onRelayPhone, LV_EVENT_VALUE_CHANGED, nullptr);

    // BLE controls
    lv_obj_t* bp = mkButton(page, "Pause BLE 5 min", 200, 40, onPause, nullptr);
    lv_obj_align(bp, LV_ALIGN_TOP_MID, 0, 318);
    w.btnPauseLbl = lv_obj_get_child(bp, 0);
    lv_obj_t* bf = mkButton(page, "Forget device", 200, 40, onForget, nullptr);
    lv_obj_align(bf, LV_ALIGN_TOP_MID, 0, 366);

    lv_obj_t* ver = mkLabel(page, &lv_font_montserrat_14, col::stale());
    lv_label_set_text(ver, FW_NAME " " FW_VERSION);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_MID, 0, -52);
}

}  // namespace layout
}  // namespace ui

#endif  // BOARD_ROUND
