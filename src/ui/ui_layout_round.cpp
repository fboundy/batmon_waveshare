// Page layouts for the 480x480 round panel (Waveshare ESP32-S3-Touch-LCD-2.1).
// Everything is kept inside the visible circle (~440 px diameter) and, on the
// Halo page, inside the SoC arc's bottom gap.  Touch is assumed.
#include "../config.h"
#if BOARD_ROUND

#include <initializer_list>

#include "../settings.h"
#include "ui_internal.h"

LV_FONT_DECLARE(lv_font_montserrat_72_digits)   // src/ui/font_montserrat_72_digits.c
LV_FONT_DECLARE(lv_font_icons_42)               // src/ui/font_icons_42.c: mobile phone glyph
#define SYMBOL_MOBILE "\xEF\x8F\x8D"             // U+F3CD

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
    // temp +50, runtime +78, relay +124, status icons +190, phone name +224.

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
    w.rowHaloRelay = rowSw;
    lv_obj_t* capSw = mkLabel(rowSw, &lv_font_montserrat_20, col::dim());
    lv_label_set_text(capSw, "Relay");
    w.swHaloRelay = lv_switch_create(rowSw);
    lv_obj_set_size(w.swHaloRelay, 100, 48);
    lv_obj_set_style_bg_color(w.swHaloRelay, col::accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(w.swHaloRelay, col::track(), LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(w.swHaloRelay, col::track(), LV_PART_INDICATOR | LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(w.swHaloRelay, col::stale(), LV_PART_KNOB | LV_STATE_DISABLED);
    lv_obj_add_event_cb(w.swHaloRelay, onSwitchRelay, LV_EVENT_VALUE_CHANGED, nullptr);

    // Status icons in the arc's bottom gap: Bluetooth link, phone presence,
    // charge state - all 42 px, on one line; the phone's name sits under it.
    w.lblBt = mkLabel(page, &lv_font_montserrat_42, col::bad());
    lv_label_set_text(w.lblBt, LV_SYMBOL_BLUETOOTH);
    lv_obj_align(w.lblBt, LV_ALIGN_CENTER, -84, 190);
    w.lblPhoneIcon = mkLabel(page, &lv_font_icons_42, col::bad());
    lv_label_set_text(w.lblPhoneIcon, SYMBOL_MOBILE);
    lv_obj_align(w.lblPhoneIcon, LV_ALIGN_CENTER, 0, 190);
    lv_obj_add_flag(w.lblPhoneIcon, LV_OBJ_FLAG_HIDDEN);
    w.lblCharge = mkLabel(page, &lv_font_montserrat_42, col::dim());
    lv_label_set_text(w.lblCharge, LV_SYMBOL_CHARGE);
    lv_obj_align(w.lblCharge, LV_ALIGN_CENTER, 84, 190);
    w.lblPhoneName = mkLabel(page, &lv_font_montserrat_12, col::dim());
    lv_obj_set_width(w.lblPhoneName, 84);
    lv_obj_set_style_text_align(w.lblPhoneName, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(w.lblPhoneName, LV_LABEL_LONG_DOT);
    lv_obj_align(w.lblPhoneName, LV_ALIGN_CENTER, 0, 224);
    lv_obj_add_flag(w.lblPhoneName, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------
// Page 1: Chart
// ---------------------------------------------------------------------------
void chart(lv_obj_t* page) {
    // Series toggles
    static const char* names[4] = {"Main V", "Aux V", "SoC", "Amps"};
    const lv_color_t cols[4] = {col::serMain(), col::serAux(), col::serSoc(), col::serAmps()};
    const int bw = 76, bh = 40, gap = 6;
    int bx = (LCD_H_RES - (4 * bw + 3 * gap)) / 2;
    for (int i = 0; i < 4; i++) {
        w.btnSeries[i] = mkCheckButton(page, names[i], bw, bh, cols[i], onSeriesToggle, (void*)(intptr_t)i);
        lv_obj_set_pos(w.btnSeries[i], bx + i * (bw + gap), 66);
    }

    lv_obj_t* c = mkChart(page, 316, 176, &lv_font_montserrat_12, 44);
    lv_obj_set_pos(c, (LCD_H_RES - 316) / 2, 122);

    w.lblWindow = mkLabel(page, &lv_font_montserrat_14, col::text());
    lv_obj_align(w.lblWindow, LV_ALIGN_TOP_MID, 0, 308);

    // Range + scroll row (thumb-sized)
    static const char* rn[4] = {"Hour", "Day", "Week", "Month"};
    const int aw = 56, rw = 60, rh = 42, rg = 6;
    int total = 2 * aw + 4 * rw + 5 * rg;
    int x = (LCD_H_RES - total) / 2, y = 332;
    lv_obj_t* bl = mkButton(page, LV_SYMBOL_LEFT, aw, rh, onScroll, (void*)(intptr_t)1, &lv_font_montserrat_20);
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
    lv_obj_t* br = mkButton(page, LV_SYMBOL_RIGHT, aw, rh, onScroll, (void*)(intptr_t)-1, &lv_font_montserrat_20);
    lv_obj_set_pos(br, x, y);

    w.lblScale = mkLabel(page, &lv_font_montserrat_12, col::dim());
    lv_obj_align(w.lblScale, LV_ALIGN_TOP_MID, 0, 380);
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

    // Selectable list: tap a phone to select it, then Rename / Delete
    // Scrollable (drag) list, 3 rows visible
    lv_obj_t* list = lv_obj_create(page);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 300, 112);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    for (int i = 0; i < 8; i++) {
        lv_obj_t* b = mkButton(list, "", 300, 34, onPhoneRow, (void*)(intptr_t)i);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
        w.phoneRows[i] = b;
    }

    lv_obj_t* bp = mkButton(page, "Pair new phone", 300, 36, onPairPhone, nullptr);
    lv_obj_align(bp, LV_ALIGN_TOP_MID, 0, 194);
    w.btnPairLbl = lv_obj_get_child(bp, 0);

    w.btnRename = mkButton(page, "Rename", 145, 36, onRenamePhone, nullptr);
    lv_obj_align(w.btnRename, LV_ALIGN_TOP_MID, -78, 236);
    w.btnDelete = mkButton(page, "Delete", 145, 36, onDeletePhone, nullptr);
    lv_obj_set_style_bg_color(w.btnDelete, col::bad(), LV_STATE_PRESSED);
    lv_obj_align(w.btnDelete, LV_ALIGN_TOP_MID, 78, 236);

    // Presence timeout: "Away after 90 s" with -/+ 15 s
    lv_obj_t* bm = mkButton(page, LV_SYMBOL_MINUS, 48, 34, onTimeout, (void*)(intptr_t)-15);
    lv_obj_align(bm, LV_ALIGN_TOP_MID, -110, 278);
    w.lblTimeout = mkLabel(page, &lv_font_montserrat_16, col::text());
    lv_obj_align(w.lblTimeout, LV_ALIGN_TOP_MID, 0, 286);
    lv_obj_t* bpl = mkButton(page, LV_SYMBOL_PLUS, 48, 34, onTimeout, (void*)(intptr_t)15);
    lv_obj_align(bpl, LV_ALIGN_TOP_MID, 110, 278);

    // Relay follows phone
    lv_obj_t* rl = mkLabel(page, &lv_font_montserrat_16, col::dim());
    lv_label_set_text(rl, "Relay follows phone");
    lv_obj_align(rl, LV_ALIGN_TOP_MID, -46, 326);
    w.swRelayPhone = lv_switch_create(page);
    lv_obj_align(w.swRelayPhone, LV_ALIGN_TOP_MID, 92, 320);
    if (g_settings.relayFollowsPhone) lv_obj_add_state(w.swRelayPhone, LV_STATE_CHECKED);
    lv_obj_add_event_cb(w.swRelayPhone, onRelayPhone, LV_EVENT_VALUE_CHANGED, nullptr);
    for (lv_obj_t* b : {w.btnRename, w.btnDelete}) {
        lv_obj_set_style_bg_color(b, col::track(), LV_STATE_DISABLED);
        lv_obj_set_style_text_color(lv_obj_get_child(b, 0), col::stale(), LV_STATE_DISABLED);
        lv_obj_add_state(b, LV_STATE_DISABLED);
    }

    w.lblPairStatus = mkLabel(page, &lv_font_montserrat_14, col::dim());
    lv_obj_set_width(w.lblPairStatus, 300);
    lv_obj_set_style_text_align(w.lblPairStatus, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(w.lblPairStatus, LV_LABEL_LONG_WRAP);
    lv_obj_align(w.lblPairStatus, LV_ALIGN_TOP_MID, 0, 360);

    // Name dialog: full-screen overlay on the screen (above the tileview)
    w.nameDlg = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(w.nameDlg);
    lv_obj_set_size(w.nameDlg, LCD_H_RES, LCD_V_RES);
    lv_obj_set_style_bg_color(w.nameDlg, col::bg(), 0);
    lv_obj_set_style_bg_opa(w.nameDlg, LV_OPA_COVER, 0);
    lv_obj_clear_flag(w.nameDlg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(w.nameDlg, LV_OBJ_FLAG_HIDDEN);

    w.nameTitle = mkLabel(w.nameDlg, &lv_font_montserrat_18, col::accent());
    lv_obj_align(w.nameTitle, LV_ALIGN_CENTER, 0, -150);
    lv_label_set_text(w.nameTitle, "Name this phone");

    w.nameTa = lv_textarea_create(w.nameDlg);
    lv_obj_set_size(w.nameTa, 300, 44);
    lv_obj_align(w.nameTa, LV_ALIGN_CENTER, 0, -100);
    lv_textarea_set_one_line(w.nameTa, true);
    lv_textarea_set_max_length(w.nameTa, 15);
    lv_obj_set_style_text_font(w.nameTa, &lv_font_montserrat_20, 0);

    w.nameKb = lv_keyboard_create(w.nameDlg);
    lv_obj_set_size(w.nameKb, 360, 200);
    lv_obj_align(w.nameKb, LV_ALIGN_CENTER, 0, 48);
    lv_keyboard_set_textarea(w.nameKb, w.nameTa);
    lv_obj_set_style_text_font(w.nameKb, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_add_event_cb(w.nameKb, onNameKeyboard, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(w.nameKb, onNameKeyboard, LV_EVENT_CANCEL, nullptr);
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

    // Units
    lv_obj_t* ul = mkLabel(page, &lv_font_montserrat_16, col::dim());
    lv_label_set_text(ul, "Fahrenheit");
    lv_obj_align(ul, LV_ALIGN_TOP_MID, -50, 276);
    w.swFahrenheit = lv_switch_create(page);
    lv_obj_align(w.swFahrenheit, LV_ALIGN_TOP_MID, 50, 270);
    if (g_settings.fahrenheit) lv_obj_add_state(w.swFahrenheit, LV_STATE_CHECKED);
    lv_obj_add_event_cb(w.swFahrenheit, onFahrenheit, LV_EVENT_VALUE_CHANGED, nullptr);

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
