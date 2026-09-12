// Shared between ui.cpp (logic) and the per-form-factor layout files.
// A layout builds the four pages into lv_obj_t tiles and fills `w` with
// whatever widgets it created; ui::update() only touches non-null handles,
// so a layout may leave out anything that doesn't fit its screen.
#pragma once

#include <lvgl.h>

namespace ui {

// ---- palette ---------------------------------------------------------------
namespace col {
inline lv_color_t bg()      { return lv_color_hex(0x000000); }
inline lv_color_t text()    { return lv_color_hex(0xF2F2F2); }
inline lv_color_t dim()     { return lv_color_hex(0x8A8A8A); }
inline lv_color_t stale()   { return lv_color_hex(0x555555); }
inline lv_color_t track()   { return lv_color_hex(0x202020); }
inline lv_color_t grid()    { return lv_color_hex(0x2A2A2A); }
inline lv_color_t good()    { return lv_color_hex(0x2ECC71); }
inline lv_color_t warn()    { return lv_color_hex(0xF1C40F); }
inline lv_color_t bad()     { return lv_color_hex(0xE74C3C); }
inline lv_color_t charge()  { return lv_color_hex(0x3498DB); }
inline lv_color_t accent()  { return lv_color_hex(0x00BFA5); }
inline lv_color_t btn()     { return lv_color_hex(0x2A2A2A); }
inline lv_color_t serMain() { return lv_color_hex(0x2ECC71); }
inline lv_color_t serAux()  { return lv_color_hex(0x00BFA5); }
inline lv_color_t serSoc()  { return lv_color_hex(0xF2F2F2); }
inline lv_color_t serAmps() { return lv_color_hex(0x3498DB); }
}  // namespace col

// ---- Details rows ------------------------------------------------------------
enum DetailRow {
    D_VOLTS, D_EXT_VOLTS, D_CURRENT, D_WATTS, D_AH, D_AH_MAX, D_AH_MIN,
    D_EXT_TEMP, D_INT_TEMP, D_RSSI, D_POLLS, D_ADDR, D_HISTORY, D_COUNT
};
extern const char* const detailNames[D_COUNT];

// ---- widget handles -------------------------------------------------------------
struct Widgets {
    // Halo
    lv_obj_t* arc = nullptr;          // round: 270 degree ring
    lv_obj_t* bar = nullptr;          // wide: horizontal bar
    lv_obj_t* lblName = nullptr;
    lv_obj_t* lblSoc = nullptr;
    lv_obj_t* lblSocUnit = nullptr;
    lv_obj_t* lblMainV = nullptr;
    lv_obj_t* lblAuxV = nullptr;
    lv_obj_t* lblAmps = nullptr;
    lv_obj_t* lblWatts = nullptr;
    lv_obj_t* lblTemp = nullptr;
    lv_obj_t* lblRuntime = nullptr;
    lv_obj_t* swHaloRelay = nullptr;     // touch boards
    lv_obj_t* rowHaloRelay = nullptr;    // the whole "Relay [toggle]" row, hidden when locked
    lv_obj_t* lblRelayState = nullptr;   // touch-less boards: "Relay ON"
    lv_obj_t* lblBt = nullptr;       // Bluetooth glyph, green/red
    lv_obj_t* lblCharge = nullptr;   // charge glyph, blue/green/yellow/red
    lv_obj_t* lblPhoneIcon = nullptr;  // phone glyph, green when a paired phone is present
    lv_obj_t* lblPhoneName = nullptr;  // name of the nearest present (or first paired) phone
    // Details
    lv_obj_t* detVal[D_COUNT] = {};
    lv_obj_t* swRelay = nullptr;
    lv_obj_t* swSwitch = nullptr;
    lv_obj_t* lblIoState = nullptr;      // touch-less: "Relay OFF  Switch ON"
    // Chart
    lv_obj_t* chart = nullptr;
    lv_chart_series_t* serMain = nullptr;
    lv_chart_series_t* serAux = nullptr;
    lv_chart_series_t* serSoc = nullptr;
    lv_chart_series_t* serAmps = nullptr;
    lv_obj_t* btnSeries[4] = {};
    lv_obj_t* btnRange[4] = {};
    lv_obj_t* lblWindow = nullptr;
    lv_obj_t* lblScale = nullptr;
    // Phones
    lv_obj_t* lblPhones = nullptr;       // multi-line list (touch-less layout)
    lv_obj_t* phoneRows[8] = {};         // selectable rows (touch layout)
    lv_obj_t* btnRename = nullptr;
    lv_obj_t* btnDelete = nullptr;
    lv_obj_t* lblPairStatus = nullptr;
    lv_obj_t* btnPairLbl = nullptr;      // label inside the Pair button
    lv_obj_t* lblTimeout = nullptr;      // "Away after 90 s"
    // Beacon picker (touch layout)
    lv_obj_t* beaconDlg = nullptr;
    lv_obj_t* beaconRows[6] = {};
    lv_obj_t* lblBeaconStatus = nullptr;
    // Name dialog (touch layout): shown after a pairing and for Rename
    lv_obj_t* nameDlg = nullptr;
    lv_obj_t* nameTitle = nullptr;
    lv_obj_t* nameTa = nullptr;
    lv_obj_t* nameKb = nullptr;
    // Setup
    lv_obj_t* swRelayPhone = nullptr;
    lv_obj_t* lblCapacity = nullptr;
    lv_obj_t* sliderBright = nullptr;
    lv_obj_t* swFahrenheit = nullptr;
    lv_obj_t* btnPauseLbl = nullptr;
    lv_obj_t* lblSetupInfo = nullptr;    // touch-less: settings summary text
};
extern Widgets w;

// ---- helpers for layouts ---------------------------------------------------------
lv_obj_t* mkLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color);
// Transparent flex row whose children share a bottom edge (for value + unit).
lv_obj_t* mkRow(lv_obj_t* parent, lv_coord_t gap);
lv_obj_t* mkButton(lv_obj_t* parent, const char* text, int w, int h, lv_event_cb_t cb, void* ud,
                   const lv_font_t* font = &lv_font_montserrat_16);
lv_obj_t* mkCheckButton(lv_obj_t* parent, const char* text, int w, int h, lv_color_t on,
                        lv_event_cb_t cb, void* ud);
// Standard chart styling + the four series; the layout only sizes/places it.
lv_obj_t* mkChart(lv_obj_t* parent, int w, int h, const lv_font_t* tickFont, int tickLabelSize);
void setCapacityLabel();

// ---- event callbacks implemented in ui.cpp, attached by layouts --------------------
void onSwitchSwitch(lv_event_t* e);
void onSwitchRelay(lv_event_t* e);
void onSeriesToggle(lv_event_t* e);   // user data: series bit 0..3
void onRange(lv_event_t* e);          // user data: range 0..3
void onScroll(lv_event_t* e);         // user data: +1 older / -1 newer
void onCapacity(lv_event_t* e);       // user data: delta Ah
void onBrightness(lv_event_t* e);
void onFahrenheit(lv_event_t* e);
void onPause(lv_event_t* e);
void onForget(lv_event_t* e);
void onPairPhone(lv_event_t* e);      // start / stop the pairing window
void onForgetPhones(lv_event_t* e);
void onRelayPhone(lv_event_t* e);
void onPhoneRow(lv_event_t* e);       // user data: phone index; selects it
void onRenamePhone(lv_event_t* e);    // opens the name dialog for the selection
void onDeletePhone(lv_event_t* e);
void onNameKeyboard(lv_event_t* e);   // LV_EVENT_READY / LV_EVENT_CANCEL from the keyboard
void onAddBeacon(lv_event_t* e);      // opens the picker and starts a discovery window
void onBeaconRow(lv_event_t* e);      // user data: candidate index
void onBeaconCancel(lv_event_t* e);
void onTimeout(lv_event_t* e);        // user data: delta seconds
void setTimeoutLabel();

// ---- layout entry points (one .cpp per form factor) -----------------------------
namespace layout {
void halo(lv_obj_t* page);
void chart(lv_obj_t* page);
void detail(lv_obj_t* page);
void phones(lv_obj_t* page);
void setup(lv_obj_t* page);
}  // namespace layout

}  // namespace ui
