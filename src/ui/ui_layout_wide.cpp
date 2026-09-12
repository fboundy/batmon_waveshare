// Page layouts for the 320x170 landscape panel (Waveshare ESP32-S3-LCD-1.9).
// Designed for a board WITHOUT touch: everything is readable, nothing needs
// tapping.  Pages are cycled with the BOOT button, the chart range with a
// long press, and settings come from the USB serial console.  On the touch
// variant the same widgets are tappable.
#include "../config.h"
#if !BOARD_ROUND

#include "../settings.h"
#include "ui_internal.h"

namespace ui {
namespace layout {

static lv_obj_t* caption(lv_obj_t* parent, const char* text, const lv_font_t* font, int x, int y) {
    lv_obj_t* l = mkLabel(parent, font, col::dim());
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    return l;
}

// ---------------------------------------------------------------------------
// Page 0: Halo  (bar gauge instead of the ring)
// ---------------------------------------------------------------------------
void halo(lv_obj_t* page) {
    w.lblName = mkLabel(page, &lv_font_montserrat_14, col::dim());
    lv_obj_set_pos(w.lblName, 8, 4);
    lv_label_set_text(w.lblName, "BatMon");

    // Status icons top right: charge state, Bluetooth link
    w.lblBt = mkLabel(page, &lv_font_montserrat_24, col::bad());
    lv_label_set_text(w.lblBt, LV_SYMBOL_BLUETOOTH);
    lv_obj_align(w.lblBt, LV_ALIGN_TOP_RIGHT, -8, 0);
    w.lblCharge = mkLabel(page, &lv_font_montserrat_24, col::dim());
    lv_label_set_text(w.lblCharge, LV_SYMBOL_CHARGE);
    lv_obj_align(w.lblCharge, LV_ALIGN_TOP_RIGHT, -40, 0);

    // SoC bar: 8..232, with the percentage to its right
    w.bar = lv_bar_create(page);
    lv_obj_set_size(w.bar, 224, 26);
    lv_obj_set_pos(w.bar, 8, 26);
    lv_bar_set_range(w.bar, 0, 100);
    lv_bar_set_value(w.bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(w.bar, col::track(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(w.bar, col::good(), LV_PART_INDICATOR);
    lv_obj_set_style_radius(w.bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(w.bar, 4, LV_PART_INDICATOR);

    lv_obj_t* rowSoc = mkRow(page, 2);
    lv_obj_align(rowSoc, LV_ALIGN_TOP_RIGHT, -8, 20);
    w.lblSoc = mkLabel(rowSoc, &lv_font_montserrat_28, col::text());
    w.lblSocUnit = mkLabel(rowSoc, &lv_font_montserrat_16, col::dim());
    lv_label_set_text(w.lblSocUnit, "%");
    lv_obj_set_style_translate_y(w.lblSocUnit, -(5 - 3), 0);

    // Voltages
    lv_obj_t* rowMain = mkRow(page, 4);
    lv_obj_set_pos(rowMain, 8, 60);
    lv_obj_t* cm = mkLabel(rowMain, &lv_font_montserrat_12, col::dim());
    lv_label_set_text(cm, "Main");
    w.lblMainV = mkLabel(rowMain, &lv_font_montserrat_24, col::text());
    lv_obj_t* um = mkLabel(rowMain, &lv_font_montserrat_16, col::dim());
    lv_label_set_text(um, "V");

    lv_obj_t* rowAux = mkRow(page, 4);
    lv_obj_set_pos(rowAux, 168, 60);
    lv_obj_t* ca = mkLabel(rowAux, &lv_font_montserrat_12, col::dim());
    lv_label_set_text(ca, "Aux");
    w.lblAuxV = mkLabel(rowAux, &lv_font_montserrat_24, col::text());
    lv_obj_t* ua = mkLabel(rowAux, &lv_font_montserrat_16, col::dim());
    lv_label_set_text(ua, "V");

    // Current / power / temperature
    w.lblAmps = mkLabel(page, &lv_font_montserrat_20, col::text());
    lv_obj_set_pos(w.lblAmps, 8, 96);
    w.lblWatts = mkLabel(page, &lv_font_montserrat_20, col::text());
    lv_obj_set_pos(w.lblWatts, 118, 96);
    w.lblTemp = mkLabel(page, &lv_font_montserrat_16, col::dim());
    lv_obj_set_pos(w.lblTemp, 218, 99);

    w.lblRuntime = mkLabel(page, &lv_font_montserrat_14, col::dim());
    lv_obj_set_pos(w.lblRuntime, 8, 126);
    lv_label_set_text(w.lblRuntime, "");

    w.lblRelayState = mkLabel(page, &lv_font_montserrat_14, col::dim());
    lv_obj_align(w.lblRelayState, LV_ALIGN_TOP_RIGHT, -8, 126);
    lv_label_set_text(w.lblRelayState, "Relay --");

}

// ---------------------------------------------------------------------------
// Page 1: Chart
// ---------------------------------------------------------------------------
void chart(lv_obj_t* page) {
    // Top row: range buttons (left) and series toggles (right).  Without
    // touch they still show the current selection.
    static const char* rn[4] = {"Hour", "Day", "Week", "Mon"};
    static const char* sn[4] = {"Main", "Aux", "SoC", "A"};
    const lv_color_t cols[4] = {col::serMain(), col::serAux(), col::serSoc(), col::serAmps()};
    const int bw = 36, bh = 18, gap = 3;
    int x = 2;
    for (int i = 0; i < 4; i++) {
        w.btnRange[i] = mkButton(page, rn[i], bw, bh, onRange, (void*)(intptr_t)i, &lv_font_montserrat_12);
        lv_obj_add_flag(w.btnRange[i], LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_style_bg_color(w.btnRange[i], col::accent(), LV_STATE_CHECKED);
        lv_obj_set_style_text_color(lv_obj_get_child(w.btnRange[i], 0), lv_color_black(), LV_STATE_CHECKED);
        lv_obj_set_pos(w.btnRange[i], x, 1);
        x += bw + gap;
    }
    x = LCD_H_RES - 2 - 4 * bw - 3 * gap;
    for (int i = 0; i < 4; i++) {
        w.btnSeries[i] = mkCheckButton(page, sn[i], bw, bh, cols[i], onSeriesToggle, (void*)(intptr_t)i);
        lv_obj_set_pos(w.btnSeries[i], x, 1);
        x += bw + gap;
    }

    lv_obj_t* c = mkChart(page, 256, 124, &lv_font_montserrat_12, 28);
    lv_obj_set_pos(c, 32, 24);

    w.lblWindow = mkLabel(page, &lv_font_montserrat_12, col::text());
    lv_obj_align(w.lblWindow, LV_ALIGN_BOTTOM_LEFT, 4, -2);
    w.lblScale = mkLabel(page, &lv_font_montserrat_12, col::dim());
    lv_obj_align(w.lblScale, LV_ALIGN_BOTTOM_RIGHT, -4, -2);
    lv_label_set_text(w.lblScale, "");
}

// ---------------------------------------------------------------------------
// Page 2: Details  (two columns, no toggles: relay/switch shown as text)
// ---------------------------------------------------------------------------
void detail(lv_obj_t* page) {
    lv_obj_t* title = mkLabel(page, &lv_font_montserrat_14, col::accent());
    lv_obj_set_pos(title, 8, 2);
    lv_label_set_text(title, "Details");

    const int rowH = 16, y0 = 22, colW = 156;
    for (int i = 0; i < D_COUNT; i++) {
        int colX = (i < 7) ? 6 : 6 + colW + 8;
        int y = y0 + (i % 7) * rowH;
        caption(page, detailNames[i], &lv_font_montserrat_12, colX, y);
        w.detVal[i] = mkLabel(page, &lv_font_montserrat_12, col::text());
        lv_obj_set_width(w.detVal[i], 80);
        lv_obj_set_style_text_align(w.detVal[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(w.detVal[i], colX + colW - 80, y);
    }
    // Address is long: give it the whole width of its column
    lv_obj_set_width(w.detVal[D_ADDR], 120);
    lv_obj_set_pos(w.detVal[D_ADDR], 6 + colW + 8 + colW - 120, y0 + (D_ADDR % 7) * rowH);

    w.lblIoState = mkLabel(page, &lv_font_montserrat_14, col::text());
    lv_obj_align(w.lblIoState, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(w.lblIoState, "Relay --    Switch --");
}

// ---------------------------------------------------------------------------
// Page 3: Phones  (list + status; pairing is started from the serial console)
// ---------------------------------------------------------------------------
void phones(lv_obj_t* page) {
    lv_obj_t* title = mkLabel(page, &lv_font_montserrat_14, col::accent());
    lv_obj_set_pos(title, 8, 2);
    lv_label_set_text(title, "Phones");

    w.lblPhones = mkLabel(page, &lv_font_montserrat_14, col::text());
    lv_obj_set_width(w.lblPhones, LCD_H_RES - 16);
    lv_label_set_long_mode(w.lblPhones, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(w.lblPhones, 8, 22);

    w.lblPairStatus = mkLabel(page, &lv_font_montserrat_12, col::dim());
    lv_obj_set_width(w.lblPairStatus, LCD_H_RES - 16);
    lv_label_set_long_mode(w.lblPairStatus, LV_LABEL_LONG_WRAP);
    lv_obj_align(w.lblPairStatus, LV_ALIGN_BOTTOM_LEFT, 8, -20);

    lv_obj_t* help = mkLabel(page, &lv_font_montserrat_12, col::stale());
    lv_label_set_text(help, "serial: phone pair | phone forget <n>|all | phone name <n> <name>");
    lv_obj_align(help, LV_ALIGN_BOTTOM_LEFT, 8, -4);
}

// ---------------------------------------------------------------------------
// Page 4: Setup  (read-only summary; changes come from the serial console)
// ---------------------------------------------------------------------------
void setup(lv_obj_t* page) {
    lv_obj_t* title = mkLabel(page, &lv_font_montserrat_14, col::accent());
    lv_obj_set_pos(title, 8, 2);
    lv_label_set_text(title, "Setup");

    w.lblSetupInfo = mkLabel(page, &lv_font_montserrat_12, col::text());
    lv_obj_set_width(w.lblSetupInfo, LCD_H_RES - 16);
    lv_label_set_long_mode(w.lblSetupInfo, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(w.lblSetupInfo, 8, 24);

    lv_obj_t* help = mkLabel(page, &lv_font_montserrat_12, col::dim());
    lv_obj_set_width(help, LCD_H_RES - 16);
    lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(help, 8, 58);
    lv_label_set_text(help,
        "BOOT: next page, hold: chart range.\n"
        "USB serial 115200, type 'help':\n"
        "cap <Ah>  bright <0-100>  degf 0|1  poll <ms>\n"
        "switch on|off  relay on|off  pause  resume  forget\n"
        "chart <main,aux,soc,amps> <hour|day|week|month>\n"
        "relayphone 0|1  phone pair|list|forget|name");

    lv_obj_t* ver = mkLabel(page, &lv_font_montserrat_12, col::stale());
    lv_label_set_text(ver, FW_NAME " " FW_VERSION "  " BOARD_NAME);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_LEFT, 8, -4);
}

}  // namespace layout
}  // namespace ui

#endif  // !BOARD_ROUND
