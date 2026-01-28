/**
 * @file ui_lvgl.cpp
 * LVGL UI for CYD Smart Shunt — UX spec: 8px grid, 32px header, min tap 44×28, category list.
 * Design: black BG, dark grey cards, cyan accent, flat containers, left label / right value.
 */
#include "ui_lvgl.h"
#include "touch.h"
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <INA228.h>
#include <Arduino.h>

extern TFT_eSPI tft;
extern INA228 ina228;

extern void resetEnergyAccumulation(void);
extern void cycleAveraging(void);
extern String getAveragingString(void);
extern void performTouchCalibration(void);
extern void performShuntCalibration(void);
extern void performKnownLoadCalibration(void);

/* ─── UX constants (CYD: 320×240, 8px grid, resistive touch) ─── */
#define DISP_W    320
#define DISP_H    240
#define GRID      8
#define MARGIN    8
#define MARGIN_L  16
#define GAP       8
#define PAD       8
#define HEADER_H  32
#define MIN_TAP_W 44
#define MIN_TAP_H 28
#define BTN_W     56
#define BTN_H     32
#define CARD_R    6
#define ROW_H     36
#define LIST_ITEM_H 44

/* Colors: black BG, dark grey cards, cyan accent, red/green. Muted brighter for contrast. */
#define COL_BG      0x000000
#define COL_CARD    0x252525
#define COL_HEADER  0x1a1a1a
#define COL_ACCENT  0x00D4FF
#define COL_ERROR   0xE63946
#define COL_OK      0x00AA00
#define COL_TEXT    0xFFFFFF
#define COL_MUTED   0xB0B0B0

#define BUF_STRIDE  320
#define BUF_LINES   40
#define BUF_BYTES   (BUF_STRIDE * BUF_LINES * 2)

/* Set to 1 if colours look inverted/wrong; 0 if they look correct. */
#ifndef LVGL_FLUSH_SWAP_BYTES
#define LVGL_FLUSH_SWAP_BYTES 1
#endif

static lv_display_t *disp = NULL;
static lv_obj_t *scr_monitor = NULL;
static lv_obj_t *scr_settings_home = NULL;
static lv_obj_t *scr_measurement = NULL;
static lv_obj_t *scr_calibration = NULL;
static lv_obj_t *scr_data = NULL;
static lv_obj_t *scr_system = NULL;

static lv_obj_t *label_current = NULL;
static lv_obj_t *label_voltage = NULL;
static lv_obj_t *label_power = NULL;
static lv_obj_t *label_energy = NULL;
static lv_obj_t *label_temp = NULL;
static lv_obj_t *label_status = NULL;
static lv_obj_t *label_avg_val = NULL;

static uint8_t *draw_buf1 = NULL;
static uint8_t *draw_buf2 = NULL;

/* ─── Flush: swap RGB565 byte order for ILI9341, then push ─── */
static void my_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map) {
  (void)d;
  int32_t w = lv_area_get_width(area);
  int32_t h = lv_area_get_height(area);
  if (w <= 0 || h <= 0) { lv_display_flush_ready(d); return; }
  uint32_t n = (uint32_t)w * (uint32_t)h;
  uint16_t *p = (uint16_t *)px_map;
#if LVGL_FLUSH_SWAP_BYTES
  /* ILI9341 often expects high byte first. If colours still wrong, set LVGL_FLUSH_SWAP_BYTES to 0 in build. */
  for (uint32_t i = 0; i < n; i++) {
    uint16_t v = p[i];
    p[i] = (uint16_t)((v >> 8) | (v << 8));
  }
#endif
  tft.startWrite();
  tft.setAddrWindow((int32_t)area->x1, (int32_t)area->y1, (int32_t)w, (int32_t)h);
  tft.pushPixels(p, n);
  tft.endWrite();
  lv_display_flush_ready(d);
}

static void my_touchpad_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  int16_t x = 0, y = 0;
  bool pressed = false;
  TouchGetScreenPoint(&x, &y, &pressed);
  data->point.x = (int32_t)x;
  data->point.y = (int32_t)y;
  data->state   = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* ─── Navigation ─── */
static void to_monitor(lv_event_t *e) {
  (void)e;
  if (scr_monitor) lv_screen_load(scr_monitor);
}

static void to_settings_home(lv_event_t *e) {
  (void)e;
  if (scr_settings_home) lv_screen_load(scr_settings_home);
}

static void open_settings(lv_event_t *e) {
  (void)e;
  if (scr_settings_home) lv_screen_load(scr_settings_home);
}

static void to_measurement(lv_event_t *e) {
  (void)e;
  if (scr_measurement) lv_screen_load(scr_measurement);
}

static void to_calibration(lv_event_t *e) {
  (void)e;
  if (scr_calibration) lv_screen_load(scr_calibration);
}

static void to_data(lv_event_t *e) {
  (void)e;
  if (scr_data) lv_screen_load(scr_data);
}

static void to_system(lv_event_t *e) {
  (void)e;
  if (scr_system) lv_screen_load(scr_system);
}

/* ─── Persistent header: title left, one action right (Back or Settings) ─── */
static lv_obj_t *add_header(lv_obj_t *parent, const char *title, bool show_back) {
  lv_obj_t *bar = lv_obj_create(parent);
  lv_obj_set_size(bar, DISP_W, HEADER_H);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(COL_HEADER), 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *tit = lv_label_create(bar);
  lv_label_set_text(tit, title);
  lv_obj_set_style_text_color(tit, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_pos(tit, MARGIN, (HEADER_H - 14) / 2);

  lv_obj_t *btn = lv_btn_create(bar);
  lv_obj_set_size(btn, BTN_W, BTN_H);
  lv_obj_set_pos(btn, DISP_W - MARGIN - BTN_W, (HEADER_H - BTN_H) / 2);
  lv_obj_set_style_radius(btn, CARD_R, 0);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, show_back ? "Back" : "Set");
  lv_obj_center(lbl);
  if (show_back)
    lv_obj_add_event_cb(btn, to_settings_home, LV_EVENT_CLICKED, NULL);
  else
    lv_obj_add_event_cb(btn, open_settings, LV_EVENT_CLICKED, NULL);

  return bar;
}

/* Header for Settings home: Back returns to Monitor */
static lv_obj_t *add_header_back_to_monitor(lv_obj_t *parent, const char *title) {
  lv_obj_t *bar = lv_obj_create(parent);
  lv_obj_set_size(bar, DISP_W, HEADER_H);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(COL_HEADER), 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *tit = lv_label_create(bar);
  lv_label_set_text(tit, title);
  lv_obj_set_style_text_color(tit, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_pos(tit, MARGIN, (HEADER_H - 14) / 2);

  lv_obj_t *btn = lv_btn_create(bar);
  lv_obj_set_size(btn, BTN_W, BTN_H);
  lv_obj_set_pos(btn, DISP_W - MARGIN - BTN_W, (HEADER_H - BTN_H) / 2);
  lv_obj_set_style_radius(btn, CARD_R, 0);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, "Back");
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn, to_monitor, LV_EVENT_CLICKED, NULL);
  return bar;
}

/* Header for sub-screens: Back returns to Settings home */
static lv_obj_t *add_header_back_to_settings(lv_obj_t *parent, const char *title) {
  lv_obj_t *bar = lv_obj_create(parent);
  lv_obj_set_size(bar, DISP_W, HEADER_H);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(COL_HEADER), 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *tit = lv_label_create(bar);
  lv_label_set_text(tit, title);
  lv_obj_set_style_text_color(tit, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_pos(tit, MARGIN, (HEADER_H - 14) / 2);

  lv_obj_t *btn = lv_btn_create(bar);
  lv_obj_set_size(btn, BTN_W, BTN_H);
  lv_obj_set_pos(btn, DISP_W - MARGIN - BTN_W, (HEADER_H - BTN_H) / 2);
  lv_obj_set_style_radius(btn, CARD_R, 0);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, "Back");
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn, to_settings_home, LV_EVENT_CLICKED, NULL);
  return bar;
}

/* ─── Confirmation: destructive action (Reset energy) ─── */
static void confirm_reset_energy_cb(lv_event_t *e) {
  lv_obj_t *msgbox = (lv_obj_t *)lv_event_get_user_data(e);
  resetEnergyAccumulation();
  if (msgbox) lv_msgbox_close(msgbox);
  if (scr_data) lv_screen_load(scr_data);
}

static void confirm_reset_cancel_cb(lv_event_t *e) {
  lv_obj_t *msgbox = (lv_obj_t *)lv_event_get_user_data(e);
  if (msgbox) lv_msgbox_close(msgbox);
}

static void show_reset_energy_confirm(lv_event_t *e) {
  (void)e;
  lv_obj_t *msgbox = lv_msgbox_create(lv_screen_active());
  lv_msgbox_add_title(msgbox, "Reset energy?");
  lv_msgbox_add_text(msgbox, "This will clear accumulated energy and charge.");
  lv_obj_t *btn_cancel = lv_msgbox_add_footer_button(msgbox, "Cancel");
  lv_obj_t *btn_reset  = lv_msgbox_add_footer_button(msgbox, "Reset");
  lv_obj_set_style_bg_color(btn_reset, lv_color_hex(COL_ERROR), 0);
  lv_obj_add_event_cb(btn_cancel, confirm_reset_cancel_cb, LV_EVENT_CLICKED, msgbox);
  lv_obj_add_event_cb(btn_reset,  confirm_reset_energy_cb, LV_EVENT_CLICKED, msgbox);
}

/* ─── Calibration: confirm then run legacy full-screen flow (TFT take-over is intentional) ─── */
typedef enum { CAL_TOUCH = 0, CAL_SHUNT, CAL_KNOWN } cal_type_t;

static void cal_confirm_continue_cb(lv_event_t *e) {
  lv_obj_t *msgbox = (lv_obj_t *)lv_event_get_user_data(e);
  lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
  cal_type_t which = (cal_type_t)(intptr_t)lv_obj_get_user_data(btn);
  if (msgbox) lv_msgbox_close(msgbox);
  switch (which) {
    case CAL_TOUCH:  performTouchCalibration();  break;
    case CAL_SHUNT:  performShuntCalibration(); break;
    case CAL_KNOWN:  performKnownLoadCalibration(); break;
  }
  if (scr_calibration) lv_screen_load(scr_calibration);
}

static void cal_confirm_cancel_cb(lv_event_t *e) {
  lv_obj_t *msgbox = (lv_obj_t *)lv_event_get_user_data(e);
  if (msgbox) lv_msgbox_close(msgbox);
}

static void show_cal_confirm(cal_type_t which, const char *title, const char *body) {
  lv_obj_t *msgbox = lv_msgbox_create(lv_screen_active());
  lv_msgbox_add_title(msgbox, title);
  lv_msgbox_add_text(msgbox, body);
  lv_obj_t *btn_cancel = lv_msgbox_add_footer_button(msgbox, "Cancel");
  lv_obj_t *btn_go = lv_msgbox_add_footer_button(msgbox, "Continue");
  lv_obj_set_style_bg_color(btn_go, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_user_data(btn_go, (void *)(intptr_t)which);
  lv_obj_add_event_cb(btn_cancel, cal_confirm_cancel_cb, LV_EVENT_CLICKED, msgbox);
  lv_obj_add_event_cb(btn_go, cal_confirm_continue_cb, LV_EVENT_CLICKED, msgbox);
}

static void act_touch_cal(lv_event_t *e) {
  (void)e;
  show_cal_confirm(CAL_TOUCH, "Touch calibration",
    "Full-screen prompts will appear. Touch the crosshairs. You will return here when done.");
}

static void act_shunt_cal(lv_event_t *e) {
  (void)e;
  show_cal_confirm(CAL_SHUNT, "Shunt calibration",
    "Full-screen prompts will appear. You will return here when done.");
}

static void act_known_load_cal(lv_event_t *e) {
  (void)e;
  show_cal_confirm(CAL_KNOWN, "Known load calibration",
    "Full-screen prompts will appear. You will return here when done.");
}

static void act_cycle_avg(lv_event_t *e) {
  (void)e;
  cycleAveraging();
  if (label_avg_val) lv_label_set_text(label_avg_val, getAveragingString().c_str());
}

/* ─── Standard row: label left, value right, tap opens action ─── */
static lv_obj_t *add_setting_row(lv_obj_t *parent, const char *name, const char *value,
    lv_coord_t y, lv_event_cb_t tap_cb) {
  lv_obj_t *row = lv_btn_create(parent);
  lv_obj_set_size(row, DISP_W - 2 * MARGIN, ROW_H);
  lv_obj_set_pos(row, MARGIN, y);
  lv_obj_set_style_radius(row, CARD_R, 0);
  lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, name);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_pos(lbl, PAD, (ROW_H - 14) / 2);

  lv_obj_t *val = lv_label_create(row);
  lv_label_set_text(val, value);
  lv_obj_set_style_text_color(val, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_pos(val, lv_obj_get_width(row) - PAD - 80, (ROW_H - 14) / 2);

  if (tap_cb) lv_obj_add_event_cb(row, tap_cb, LV_EVENT_CLICKED, NULL);
  return val;
}

/* ─── List-style category row: "Name  >" ─── */
static lv_obj_t *add_category_row(lv_obj_t *parent, const char *name, lv_coord_t y, lv_event_cb_t cb) {
  lv_obj_t *row = lv_btn_create(parent);
  lv_obj_set_size(row, DISP_W - 2 * MARGIN, LIST_ITEM_H);
  lv_obj_set_pos(row, MARGIN, y);
  lv_obj_set_style_radius(row, CARD_R, 0);
  lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, name);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_pos(lbl, PAD, (LIST_ITEM_H - 14) / 2);

  lv_obj_t *chev = lv_label_create(row);
  lv_label_set_text(chev, ">");
  lv_obj_set_style_text_color(chev, lv_color_hex(COL_ACCENT), 0);
  lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -PAD, 0);

  if (cb) lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, NULL);
  return row;
}

/* ─── Screen 1: Monitor ─── */
static void build_monitor(void) {
  scr_monitor = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_monitor, lv_color_hex(COL_BG), 0);
  lv_obj_remove_flag(scr_monitor, LV_OBJ_FLAG_SCROLLABLE);

  add_header(scr_monitor, "Smart Shunt", false);  /* Settings on right */

  lv_coord_t top = HEADER_H + GAP;
  lv_coord_t half = (DISP_W - 2 * MARGIN - GAP) / 2;
  lv_coord_t card_w = half;
  lv_coord_t card_h = 56;

  /* Primary cards: Current, Voltage */
  lv_obj_t *card_i = lv_obj_create(scr_monitor);
  lv_obj_set_size(card_i, card_w, card_h);
  lv_obj_set_pos(card_i, MARGIN, top);
  lv_obj_set_style_bg_color(card_i, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_radius(card_i, CARD_R, 0);
  lv_obj_remove_flag(card_i, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *lbl_i = lv_label_create(card_i);
  lv_label_set_text(lbl_i, "Current");
  lv_obj_set_style_text_color(lbl_i, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_pos(lbl_i, PAD, 2);
  label_current = lv_label_create(card_i);
  lv_label_set_text(label_current, "0.000 A");
  lv_obj_set_style_text_color(label_current, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_pos(label_current, PAD, 20);

  lv_obj_t *card_v = lv_obj_create(scr_monitor);
  lv_obj_set_size(card_v, card_w, card_h);
  lv_obj_set_pos(card_v, MARGIN + card_w + GAP, top);
  lv_obj_set_style_bg_color(card_v, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_radius(card_v, CARD_R, 0);
  lv_obj_remove_flag(card_v, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *lbl_v = lv_label_create(card_v);
  lv_label_set_text(lbl_v, "Voltage");
  lv_obj_set_style_text_color(lbl_v, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_pos(lbl_v, PAD, 2);
  label_voltage = lv_label_create(card_v);
  lv_label_set_text(label_voltage, "0.00 V");
  lv_obj_set_style_text_color(label_voltage, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_pos(label_voltage, PAD, 20);

  top += card_h + GAP;
  /* Secondary rows: Power, Energy, Temp */
  label_power = add_setting_row(scr_monitor, "Power", "0.0 W",   top, NULL); top += ROW_H + GAP;
  label_energy = add_setting_row(scr_monitor, "Energy", "0.0 Wh", top, NULL); top += ROW_H + GAP;
  label_temp  = add_setting_row(scr_monitor, "Temp", "0.0 \xC2\xB0""C", top, NULL); top += ROW_H + GAP;

  /* Status strip */
  lv_obj_t *strip = lv_obj_create(scr_monitor);
  lv_obj_set_size(strip, DISP_W - 2 * MARGIN, ROW_H);
  lv_obj_set_pos(strip, MARGIN, DISP_H - MARGIN - ROW_H);
  lv_obj_set_style_bg_color(strip, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_radius(strip, CARD_R, 0);
  lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
  label_status = lv_label_create(strip);
  lv_label_set_text(label_status, "INA228 --");
  lv_obj_set_style_text_color(label_status, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_pos(label_status, PAD, (ROW_H - 14) / 2);
}

/* ─── Screen 2: Settings home (category list) ─── */
static void build_settings_home(void) {
  scr_settings_home = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_settings_home, lv_color_hex(COL_BG), 0);
  lv_obj_remove_flag(scr_settings_home, LV_OBJ_FLAG_SCROLLABLE);

  add_header_back_to_monitor(scr_settings_home, "Settings");

  lv_coord_t y = HEADER_H + GAP;
  add_category_row(scr_settings_home, "Measurement  >", y, to_measurement);   y += LIST_ITEM_H + GAP;
  add_category_row(scr_settings_home, "Calibration  >", y, to_calibration);   y += LIST_ITEM_H + GAP;
  add_category_row(scr_settings_home, "Data  >",        y, to_data);          y += LIST_ITEM_H + GAP;
  add_category_row(scr_settings_home, "System  >",     y, to_system);
}

/* ─── Screen 3: Measurement ─── */
static void build_measurement(void) {
  scr_measurement = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_measurement, lv_color_hex(COL_BG), 0);
  lv_obj_remove_flag(scr_measurement, LV_OBJ_FLAG_SCROLLABLE);

  add_header_back_to_settings(scr_measurement, "Measurement");

  lv_coord_t y = HEADER_H + GAP;
  label_avg_val = add_setting_row(scr_measurement, "Averaging", getAveragingString().c_str(), y, act_cycle_avg);
}

/* ─── Screen 4: Calibration ─── */
static void build_calibration(void) {
  scr_calibration = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_calibration, lv_color_hex(COL_BG), 0);
  lv_obj_remove_flag(scr_calibration, LV_OBJ_FLAG_SCROLLABLE);

  add_header_back_to_settings(scr_calibration, "Calibration");

  lv_coord_t y = HEADER_H + GAP;
  add_category_row(scr_calibration, "Touch calibration",  y, act_touch_cal);   y += LIST_ITEM_H + GAP;
  add_category_row(scr_calibration, "Shunt calibration",  y, act_shunt_cal);   y += LIST_ITEM_H + GAP;
  add_category_row(scr_calibration, "Known load cal",     y, act_known_load_cal);
}

/* ─── Screen 5: Data ─── */
static void build_data(void) {
  scr_data = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_data, lv_color_hex(COL_BG), 0);
  lv_obj_remove_flag(scr_data, LV_OBJ_FLAG_SCROLLABLE);

  add_header_back_to_settings(scr_data, "Data");

  lv_coord_t y = HEADER_H + GAP;
  lv_obj_t *row = lv_btn_create(scr_data);
  lv_obj_set_size(row, DISP_W - 2 * MARGIN, LIST_ITEM_H);
  lv_obj_set_pos(row, MARGIN, y);
  lv_obj_set_style_radius(row, CARD_R, 0);
  lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);
  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, "Reset energy / charge");
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_pos(lbl, PAD, (LIST_ITEM_H - 14) / 2);
  lv_obj_add_event_cb(row, show_reset_energy_confirm, LV_EVENT_CLICKED, NULL);
}

/* ─── Screen 6: System ─── */
static void build_system(void) {
  scr_system = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_system, lv_color_hex(COL_BG), 0);
  lv_obj_remove_flag(scr_system, LV_OBJ_FLAG_SCROLLABLE);

  add_header_back_to_settings(scr_system, "System");

  lv_obj_t *info = lv_label_create(scr_system);
  lv_label_set_text(info, "INA228 Smart Shunt\nSensor info on next update");
  lv_obj_set_style_text_color(info, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_pos(info, MARGIN, HEADER_H + GAP);
}

/* ─── INA228 update timer: only update value labels, no redraw ─── */
static void update_timer_cb(lv_timer_t *timer) {
  (void)timer;
  if (!label_current || !label_voltage || !label_power || !label_energy || !label_temp || !label_status) return;
  float current     = ina228.getCurrent();
  float voltage     = ina228.getBusVoltage();
  float power       = ina228.getPower();
  double energy     = ina228.getWattHour();
  float temperature = ina228.getTemperature();
  bool  connected   = ina228.isConnected();

  char buf[32];
  snprintf(buf, sizeof(buf), "%.3f A", (double)current);
  lv_label_set_text(label_current, buf);
  snprintf(buf, sizeof(buf), "%.2f V", (double)voltage);
  lv_label_set_text(label_voltage, buf);
  snprintf(buf, sizeof(buf), "%.1f W", (double)power);
  lv_label_set_text(label_power, buf);
  if (energy >= 1000.0)
    snprintf(buf, sizeof(buf), "%.3f kWh", (double)(energy / 1000.0));
  else
    snprintf(buf, sizeof(buf), "%.1f Wh", (double)energy);
  lv_label_set_text(label_energy, buf);
  snprintf(buf, sizeof(buf), "%.1f \xC2\xB0""C", (double)temperature);
  lv_label_set_text(label_temp, buf);
  lv_label_set_text(label_status, connected ? "INA228 OK" : "INA228 ERR");
}

void ui_lvgl_init(void) {
  draw_buf1 = (uint8_t *)malloc(BUF_BYTES);
  draw_buf2 = (uint8_t *)malloc(BUF_BYTES);
  if (!draw_buf1 || !draw_buf2) {
    if (draw_buf1) { free(draw_buf1); draw_buf1 = NULL; }
    if (draw_buf2) { free(draw_buf2); draw_buf2 = NULL; }
    return;
  }

  lv_init();
  disp = lv_display_create(DISP_W, DISP_H);
  lv_display_set_flush_cb(disp, my_flush_cb);
  lv_display_set_buffers(disp, draw_buf1, draw_buf2, BUF_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read_cb);

  build_monitor();
  build_settings_home();
  build_measurement();
  build_calibration();
  build_data();
  build_system();

  lv_screen_load(scr_monitor);

  lv_timer_t *t = lv_timer_create(update_timer_cb, 200, NULL);
  lv_timer_set_repeat_count(t, -1);
}

void ui_lvgl_on_touch_calibration_done(void) {
  (void)0;
}

void ui_lvgl_poll(void) {
  static uint32_t last = 0;
  uint32_t now = millis();
  lv_tick_inc(now - last);
  last = now;
  lv_timer_handler();
}
