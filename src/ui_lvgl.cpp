/**
 * @file ui_lvgl.cpp
 * LVGL UI for CYD Smart Shunt — UX spec: 8px grid, 32px header, min tap 44×28, category list.
 * Design: black BG, dark grey cards, cyan accent, flat containers, left label / right value.
 */
#include "ui_lvgl.h"
#include "touch.h"
#include "sensor.h"
#include "telemetry_victron.h"
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <Arduino.h>
#include <math.h>

extern TFT_eSPI tft;

extern void resetEnergyAccumulation(void);
extern void cycleAveraging(void);
extern String getAveragingString(void);
extern void performTouchCalibration(void);
extern void saveShuntCalibration(void);
extern float getDefaultMaxCurrent(void);
extern float getDefaultShuntResistance(void);
extern float maxCurrent;
extern float shuntResistance;
extern bool get_vedirect_enabled(void);
extern void set_vedirect_enabled(bool on);

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
static lv_obj_t *scr_integration = NULL;
static lv_obj_t *scr_shunt_calibration = NULL;
static lv_obj_t *scr_shunt_standard = NULL;
static lv_obj_t *scr_known_load = NULL;
static lv_obj_t *scr_calc_mv = NULL;

static lv_obj_t *label_current = NULL;
static lv_obj_t *label_voltage = NULL;
static lv_obj_t *label_power = NULL;
static lv_obj_t *label_energy = NULL;
static lv_obj_t *label_status = NULL;
static lv_obj_t *label_avg_val = NULL;
static lv_obj_t *label_shunt_max = NULL;
static lv_obj_t *label_shunt_res = NULL;
static lv_obj_t *label_known_current = NULL;
static lv_obj_t *label_known_voltage = NULL;
static lv_obj_t *label_known_measured = NULL;
static lv_obj_t *label_known_corrected = NULL;
static lv_obj_t *label_calc_mv_voltage = NULL;
static lv_obj_t *label_calc_mv_current = NULL;
static lv_obj_t *label_calc_mv_result = NULL;

static uint8_t *draw_buf1 = NULL;
static uint8_t *draw_buf2 = NULL;

/* ─── History buffer for histogram (since start or last reset) ─── */
#define HISTORY_LEN 256
static float s_history_v[HISTORY_LEN];
static float s_history_i[HISTORY_LEN];
static float s_history_p[HISTORY_LEN];
static float s_history_e[HISTORY_LEN];
static uint16_t s_history_write_idx = 0;
static uint16_t s_history_count = 0;  /* samples written so far */

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

static void to_integration(lv_event_t *e) {
  (void)e;
  if (scr_integration) lv_screen_load(scr_integration);
}

static void to_shunt_calibration(lv_event_t *e) {
  (void)e;
  if (scr_shunt_calibration) lv_screen_load(scr_shunt_calibration);
}

static void to_shunt_standard(lv_event_t *e) {
  (void)e;
  if (scr_shunt_standard) lv_screen_load(scr_shunt_standard);
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

  if (title) {
    lv_obj_t *tit = lv_label_create(bar);
    lv_label_set_text(tit, title);
    lv_obj_set_style_text_color(tit, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(tit, LV_ALIGN_LEFT_MID, MARGIN, 0);
  }

  lv_obj_t *btn = lv_btn_create(bar);
  lv_obj_set_size(btn, BTN_W, BTN_H);
  lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -MARGIN, 0);
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
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *tit = lv_label_create(bar);
  lv_label_set_text(tit, title);
  lv_obj_set_style_text_color(tit, lv_color_hex(COL_TEXT), 0);
  lv_obj_align(tit, LV_ALIGN_LEFT_MID, MARGIN, 0);

  lv_obj_t *btn = lv_btn_create(bar);
  lv_obj_set_size(btn, BTN_W, BTN_H);
  lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -MARGIN, 0);
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
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *tit = lv_label_create(bar);
  lv_label_set_text(tit, title);
  lv_obj_set_style_text_color(tit, lv_color_hex(COL_TEXT), 0);
  lv_obj_align(tit, LV_ALIGN_LEFT_MID, MARGIN, 0);

  lv_obj_t *btn = lv_btn_create(bar);
  lv_obj_set_size(btn, BTN_W, BTN_H);
  lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -MARGIN, 0);
  lv_obj_set_style_radius(btn, CARD_R, 0);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, "Back");
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn, to_settings_home, LV_EVENT_CLICKED, NULL);
  return bar;
}

/* Header for calibration sub-screens: Back returns to Calibration */
static lv_obj_t *add_header_back_to_calibration(lv_obj_t *parent, const char *title) {
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
  lv_obj_align(tit, LV_ALIGN_LEFT_MID, MARGIN, 0);

  lv_obj_t *btn = lv_btn_create(bar);
  lv_obj_set_size(btn, BTN_W, BTN_H);
  lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -MARGIN, 0);
  lv_obj_set_style_radius(btn, CARD_R, 0);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, "Back");
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn, to_calibration, LV_EVENT_CLICKED, NULL);
  return bar;
}

/* Header for standard shunt list: Back returns to Shunt calibration */
static lv_obj_t *add_header_back_to_shunt(lv_obj_t *parent, const char *title) {
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
  lv_obj_align(tit, LV_ALIGN_LEFT_MID, MARGIN, 0);

  lv_obj_t *btn = lv_btn_create(bar);
  lv_obj_set_size(btn, BTN_W, BTN_H);
  lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -MARGIN, 0);
  lv_obj_set_style_radius(btn, CARD_R, 0);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, "Back");
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn, to_shunt_calibration, LV_EVENT_CLICKED, NULL);
  return bar;
}

/* ─── Confirmation: destructive action (Reset energy) ─── */
static void confirm_reset_energy_cb(lv_event_t *e) {
  lv_obj_t *msgbox = (lv_obj_t *)lv_event_get_user_data(e);
  if (msgbox) lv_msgbox_close(msgbox);
  resetEnergyAccumulation();
  ui_history_clear();
  if (scr_data) lv_screen_load(scr_data);
}

/* Reset from dashboard: stay on monitor after reset */
static void confirm_reset_energy_dashboard_cb(lv_event_t *e) {
  lv_obj_t *msgbox = (lv_obj_t *)lv_event_get_user_data(e);
  if (msgbox) lv_msgbox_close(msgbox);
  resetEnergyAccumulation();
  ui_history_clear();
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

/* Same dialog, but Reset keeps you on current screen (used from dashboard long-press) */
static void show_reset_energy_confirm_from_dashboard(lv_event_t *e) {
  (void)e;
  lv_obj_t *msgbox = lv_msgbox_create(lv_screen_active());
  lv_msgbox_add_title(msgbox, "Reset energy?");
  lv_msgbox_add_text(msgbox, "This will clear accumulated energy and charge.");
  lv_obj_t *btn_cancel = lv_msgbox_add_footer_button(msgbox, "Cancel");
  lv_obj_t *btn_reset  = lv_msgbox_add_footer_button(msgbox, "Reset");
  lv_obj_set_style_bg_color(btn_reset, lv_color_hex(COL_ERROR), 0);
  lv_obj_add_event_cb(btn_cancel, confirm_reset_cancel_cb, LV_EVENT_CLICKED, msgbox);
  lv_obj_add_event_cb(btn_reset,  confirm_reset_energy_dashboard_cb, LV_EVENT_CLICKED, msgbox);
}

/* ─── History histogram popup (short tap on V/I/P/E) ─── */
typedef enum { HIST_V = 0, HIST_I = 1, HIST_P = 2, HIST_E = 3 } hist_metric_t;

#define HIST_PAN_HOLD_MS 30000  /* after 30s no input, resume auto-scroll */

typedef struct {
  lv_obj_t *modal;
  lv_obj_t *chart;
  lv_chart_series_t *series;
  int32_t chart_data[HISTORY_LEN];
  hist_metric_t metric;
  uint8_t zoom;      /* 1, 2, 4 */
  uint16_t scroll;   /* start index */
  int32_t last_x;
  bool user_has_panned_or_zoomed;
  unsigned long last_user_action_time;
} hist_popup_t;

static hist_popup_t *s_active_hist_popup = NULL;  /* non-NULL while popup is open */

/* Map logical index (0=oldest) to circular buffer physical index */
static uint16_t hist_phys_idx(uint16_t logical) {
  if (s_history_count < HISTORY_LEN)
    return logical;  /* not wrapped yet */
  return (s_history_write_idx + logical) % HISTORY_LEN;
}

/* Clamp to int32 range to avoid overflow and LVGL issues */
static int32_t clamp_chart_val(int32_t v) {
  if (v > 2000000000) return 2000000000;
  if (v < -2000000000) return -2000000000;
  return v;
}

static int32_t safe_scale(float v, float scale) {
  if (isnan(v) || isinf(v)) return 0;
  double d = (double)v * (double)scale;
  if (d > 2000000000.0) return 2000000000;
  if (d < -2000000000.0) return -2000000000;
  return (int32_t)d;
}

#define HIST_CHART_MAX_POINTS 128  /* some LVGL builds cap chart points */
static void hist_refresh_chart(hist_popup_t *hp) {
  if (!hp || !hp->chart || !hp->series) return;
  uint16_t pts = HISTORY_LEN / hp->zoom;
  if (pts < 4) pts = 4;
  if (pts > HIST_CHART_MAX_POINTS) pts = HIST_CHART_MAX_POINTS;
  uint16_t max_scroll = (s_history_count > pts) ? (s_history_count - pts) : 0;
  if (hp->scroll > max_scroll) hp->scroll = max_scroll;

  float *src = NULL;
  float scale = 1.0f;
  switch (hp->metric) {
    case HIST_V: src = s_history_v; scale = 1000.0f; break;  /* mV */
    case HIST_I: src = s_history_i; scale = 1000.0f; break;  /* mA */
    case HIST_P: src = s_history_p; scale = 1.0f; break;
    case HIST_E: src = s_history_e; scale = 1.0f; break;    /* Wh (avoid int32 overflow with scale 10) */
  }
  if (!src) return;

  float vmin = 1e9f, vmax = -1e9f;
  for (uint16_t i = 0; i < pts; i++) {
    if (hp->scroll + i >= s_history_count) continue;
    uint16_t idx = hist_phys_idx(hp->scroll + i);
    float v = src[idx];
    if (isnan(v) || isinf(v)) continue;
    if (v < vmin) vmin = v;
    if (v > vmax) vmax = v;
  }
  if (vmin > vmax) { vmin = 0; vmax = 100; }
  float margin = (vmax - vmin) * 0.05f;
  if (margin < 0.001f) margin = 0.001f;
  int32_t ymin = safe_scale(vmin - margin, scale);
  int32_t ymax = safe_scale(vmax + margin, scale);
  if (ymin >= ymax) ymax = ymin + 1;

  lv_chart_set_range(hp->chart, LV_CHART_AXIS_PRIMARY_Y, clamp_chart_val(ymin), clamp_chart_val(ymax));
  lv_chart_set_point_count(hp->chart, pts);
  lv_chart_set_x_start_point(hp->chart, hp->series, 0);

  for (uint16_t i = 0; i < pts; i++) {
    int32_t val = ymin;
    if (hp->scroll + i < s_history_count) {
      uint16_t idx = hist_phys_idx(hp->scroll + i);
      val = safe_scale(src[idx], scale);
    }
    lv_chart_set_value_by_id(hp->chart, hp->series, i, clamp_chart_val(val));
  }
  lv_chart_refresh(hp->chart);
}

static void hist_mark_user_action(hist_popup_t *hp) {
  if (hp) {
    hp->user_has_panned_or_zoomed = true;
    hp->last_user_action_time = (unsigned long)millis();
  }
}

/** Apply scroll policy when new data arrives: auto-scroll by default; if user panned/zoomed, hold for 30s unless already at newest (then keep following). */
static void hist_apply_scroll_policy_and_refresh(hist_popup_t *hp) {
  if (!hp || !hp->chart || !hp->series) return;
  uint16_t pts = HISTORY_LEN / hp->zoom;
  if (pts < 4) pts = 4;
  if (pts > HIST_CHART_MAX_POINTS) pts = HIST_CHART_MAX_POINTS;
  uint16_t max_scroll = (s_history_count > pts) ? (s_history_count - pts) : 0;
  /* previous max (before this sample): if user was at this, they were "at newest" and we keep following */
  uint16_t max_scroll_prev = (s_history_count > pts && s_history_count > 0) ? (s_history_count - 1 - pts) : 0;
  unsigned long now = (unsigned long)millis();

  if (!hp->user_has_panned_or_zoomed) {
    hp->scroll = max_scroll;  /* default: follow newest */
  } else if (hp->scroll >= max_scroll_prev) {
    /* user had panned to the right (was at newest); keep following new data */
    hp->scroll = max_scroll;
  } else if ((now - hp->last_user_action_time) < (unsigned long)HIST_PAN_HOLD_MS) {
    /* user panned/zoomed and is viewing older data; hold position for 30s */
    /* hp->scroll unchanged */
  } else {
    /* 30s passed with no input; resume auto-scroll */
    hp->user_has_panned_or_zoomed = false;
    hp->scroll = max_scroll;
  }

  hist_refresh_chart(hp);
}

static void hist_zoom_plus_cb(lv_event_t *e) {
  hist_popup_t *hp = (hist_popup_t *)lv_event_get_user_data(e);
  hist_mark_user_action(hp);
  if (hp->zoom < 4) { hp->zoom *= 2; hist_refresh_chart(hp); }
}

static void hist_zoom_minus_cb(lv_event_t *e) {
  hist_popup_t *hp = (hist_popup_t *)lv_event_get_user_data(e);
  hist_mark_user_action(hp);
  if (hp->zoom > 1) { hp->zoom /= 2; hist_refresh_chart(hp); }
}

static void hist_scroll_left_cb(lv_event_t *e) {
  hist_popup_t *hp = (hist_popup_t *)lv_event_get_user_data(e);
  hist_mark_user_action(hp);
  uint16_t pts = HISTORY_LEN / hp->zoom;
  if (hp->scroll + pts < s_history_count) hp->scroll += pts / 4;
  if (hp->scroll + pts > s_history_count) hp->scroll = (s_history_count > pts) ? (s_history_count - pts) : 0;
  hist_refresh_chart(hp);
}

static void hist_scroll_right_cb(lv_event_t *e) {
  hist_popup_t *hp = (hist_popup_t *)lv_event_get_user_data(e);
  hist_mark_user_action(hp);
  if (hp->scroll >= 16) hp->scroll -= 16; else hp->scroll = 0;
  hist_refresh_chart(hp);
}

static void hist_modal_deleted_cb(lv_event_t *e) {
  (void)e;
  s_active_hist_popup = NULL;
}

static void hist_close_cb(lv_event_t *e) {
  hist_popup_t *hp = (hist_popup_t *)lv_event_get_user_data(e);
  s_active_hist_popup = NULL;
  if (hp && hp->modal) lv_obj_delete(hp->modal);
}

static void hist_chart_gesture_cb(lv_event_t *e) {
  hist_popup_t *hp = (hist_popup_t *)lv_event_get_user_data(e);
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSING) {
    hist_mark_user_action(hp);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int32_t dx = p.x - hp->last_x;
    hp->last_x = p.x;
    uint16_t pts = HISTORY_LEN / hp->zoom;
    if (dx > 8) { /* swipe right = scroll to older */
      if (hp->scroll + pts < s_history_count) hp->scroll += 4;
      if (hp->scroll + pts > s_history_count) hp->scroll = (s_history_count > pts) ? (s_history_count - pts) : 0;
      hist_refresh_chart(hp);
    } else if (dx < -8) { /* swipe left = scroll to newer */
      if (hp->scroll >= 4) hp->scroll -= 4; else hp->scroll = 0;
      hist_refresh_chart(hp);
    }
  } else if (code == LV_EVENT_PRESSED) {
    lv_indev_t *indev = lv_indev_get_act();
    if (indev) { lv_point_t p; lv_indev_get_point(indev, &p); hp->last_x = p.x; }
  }
}

static void show_history_popup(hist_metric_t metric) {
  static hist_popup_t hp;
  hp.metric = metric;
  hp.zoom = 1;
  /* Start at newest (same as max_scroll so graph scrolls with new data by default) */
  {
    uint16_t pts = HISTORY_LEN / hp.zoom;
    if (pts > HIST_CHART_MAX_POINTS) pts = HIST_CHART_MAX_POINTS;
    hp.scroll = (s_history_count > pts) ? (s_history_count - pts) : 0;
  }
  hp.last_x = 0;
  hp.user_has_panned_or_zoomed = false;  /* start in auto-scroll mode */
  hp.last_user_action_time = 0;
  s_active_hist_popup = &hp;

  const char *titles[] = { "Voltage", "Current", "Power", "Energy" };
  const char *units[] = { "V", "A", "W", "Wh" };

  hp.modal = lv_obj_create(lv_screen_active());
  lv_obj_set_size(hp.modal, DISP_W - 2 * MARGIN, DISP_H - 2 * MARGIN);
  lv_obj_align(hp.modal, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(hp.modal, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_radius(hp.modal, CARD_R, 0);
  lv_obj_set_style_pad_all(hp.modal, PAD, 0);
  lv_obj_clear_flag(hp.modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(hp.modal, LV_OBJ_FLAG_CLICKABLE);  /* absorb taps so they don't pass through to monitor */
  lv_obj_add_event_cb(hp.modal, hist_modal_deleted_cb, LV_EVENT_DELETE, NULL);

  lv_obj_t *tit = lv_label_create(hp.modal);
  char buf[32];
  snprintf(buf, sizeof(buf), "%s history (%s)", titles[metric], units[metric]);
  lv_label_set_text(tit, buf);
  lv_obj_set_style_text_color(tit, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_pos(tit, 0, 0);

  lv_coord_t ch_h = 120;
  hp.chart = lv_chart_create(hp.modal);
  lv_obj_set_size(hp.chart, DISP_W - 2 * MARGIN - 2 * PAD, ch_h);
  lv_obj_set_pos(hp.chart, 0, 24);
  lv_obj_set_style_bg_color(hp.chart, lv_color_hex(COL_BG), 0);
  lv_chart_set_type(hp.chart, LV_CHART_TYPE_LINE);
  lv_chart_set_div_line_count(hp.chart, 2, 4);
  hp.series = lv_chart_add_series(hp.chart, lv_color_hex(COL_ACCENT), LV_CHART_AXIS_PRIMARY_Y);
  lv_obj_add_flag(hp.chart, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(hp.chart, LV_DIR_NONE);
  lv_obj_add_event_cb(hp.chart, hist_chart_gesture_cb, LV_EVENT_PRESSING, &hp);
  lv_obj_add_event_cb(hp.chart, hist_chart_gesture_cb, LV_EVENT_PRESSED, &hp);

  lv_obj_t *btn_row = lv_obj_create(hp.modal);
  lv_obj_set_size(btn_row, DISP_W - 2 * MARGIN - 2 * PAD, 36);
  lv_obj_set_pos(btn_row, 0, 24 + ch_h + GAP);
  lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *btn_minus = lv_btn_create(btn_row);
  lv_obj_set_size(btn_minus, 40, 28);
  lv_obj_t *lbl = lv_label_create(btn_minus);
  lv_label_set_text(lbl, "-");
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn_minus, hist_zoom_minus_cb, LV_EVENT_CLICKED, &hp);

  lv_obj_t *btn_plus = lv_btn_create(btn_row);
  lv_obj_set_size(btn_plus, 40, 28);
  lbl = lv_label_create(btn_plus);
  lv_label_set_text(lbl, "+");
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn_plus, hist_zoom_plus_cb, LV_EVENT_CLICKED, &hp);

  lv_obj_t *btn_left = lv_btn_create(btn_row);
  lv_obj_set_size(btn_left, 40, 28);
  lbl = lv_label_create(btn_left);
  lv_label_set_text(lbl, "<");
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn_left, hist_scroll_right_cb, LV_EVENT_CLICKED, &hp);  /* < = older = decrease scroll */

  lv_obj_t *btn_right = lv_btn_create(btn_row);
  lv_obj_set_size(btn_right, 40, 28);
  lbl = lv_label_create(btn_right);
  lv_label_set_text(lbl, ">");
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn_right, hist_scroll_left_cb, LV_EVENT_CLICKED, &hp);  /* > = newer = increase scroll */

  lv_obj_t *btn_close = lv_btn_create(btn_row);
  lv_obj_set_size(btn_close, 56, 28);
  lbl = lv_label_create(btn_close);
  lv_label_set_text(lbl, "Close");
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn_close, hist_close_cb, LV_EVENT_CLICKED, &hp);

  hist_refresh_chart(&hp);
}

static void hist_card_click_cb(lv_event_t *e) {
  hist_metric_t m = (hist_metric_t)(intptr_t)lv_event_get_user_data(e);
  show_history_popup(m);
}

/* ─── Calibration: confirm then run legacy full-screen flow (TFT take-over is intentional) ─── */
typedef enum { CAL_TOUCH = 0 } cal_type_t;

static void cal_confirm_continue_cb(lv_event_t *e) {
  lv_obj_t *msgbox = (lv_obj_t *)lv_event_get_user_data(e);
  lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
  cal_type_t which = (cal_type_t)(intptr_t)lv_obj_get_user_data(btn);
  if (msgbox) lv_msgbox_close(msgbox);
  switch (which) {
    case CAL_TOUCH:  performTouchCalibration();  break;
  }
  if (scr_calibration) {
    lv_screen_load(scr_calibration);
    /* TFT was drawn by performTouchCalibration(); force full redraw so menu isn’t left half‑green */
    lv_obj_invalidate(scr_calibration);
    lv_refr_now(disp);
  }
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


static void update_shunt_labels(void) {
  if (!label_shunt_max || !label_shunt_res) return;
  char buf[24];
  snprintf(buf, sizeof(buf), "%.1f A", (double)maxCurrent);
  lv_label_set_text(label_shunt_max, buf);
  /* ASCII only (avoid Ω / mΩ glyph issues on embedded font build) */
  snprintf(buf, sizeof(buf), "%.3f mOhm", (double)(shuntResistance * 1000.0f));
  lv_label_set_text(label_shunt_res, buf);
}

typedef enum {
  EDIT_MAX_CURRENT = 0,
  EDIT_SHUNT_RESISTANCE,
  EDIT_KNOWN_CURRENT,
  EDIT_KNOWN_VOLTAGE,
  EDIT_CALC_MV_VOLTAGE,
  EDIT_CALC_MV_CURRENT
} edit_field_t;
static edit_field_t edit_field = EDIT_MAX_CURRENT;
static lv_obj_t *edit_modal = NULL;
static float edit_value = 0.0f;
static float edit_min = 0.0f, edit_max = 999.0f;
static float edit_step = 1.0f;
/* cursor_pos: 0 = ones, -1 = tenths, -2 = hundredths, -3 = thousandths; edit_step = 10^cursor_pos */
static int8_t edit_cursor_pos = 0;
static uint8_t edit_decimals = 1;
static const char *edit_unit = "";
static const char *edit_title = "";

static lv_obj_t *edit_spangroup = NULL;
static lv_span_t *edit_span_before = NULL;
static lv_span_t *edit_span_digit = NULL;
static lv_span_t *edit_span_after = NULL;
static lv_obj_t *edit_step_label = NULL;
static lv_obj_t *keypad_modal = NULL;
static lv_obj_t *keypad_display = NULL;

static float known_load_current = 10.0f;
static float known_load_voltage = 12.0f;
static float calc_mv_voltage_mv = 75.0f;
static float calc_mv_current_a = 50.0f;

static void close_edit_modal(void) {
  if (edit_modal) {
    lv_obj_del(edit_modal);
    edit_modal = NULL;
    edit_spangroup = NULL;
    edit_span_before = NULL;
    edit_span_digit = NULL;
    edit_span_after = NULL;
    edit_step_label = NULL;
  }
  if (keypad_modal) {
    lv_obj_del(keypad_modal);
    keypad_modal = NULL;
    keypad_display = NULL;
  }
}

static void edit_cancel_cb(lv_event_t *e) {
  (void)e;
  close_edit_modal();
}

static void edit_refresh_value_label(void) {
  if (!edit_spangroup || !edit_span_before || !edit_span_digit || !edit_span_after) return;
  char num[24];
  char fmt[12];
  snprintf(fmt, sizeof(fmt), "%%.%uf", (unsigned)edit_decimals);
  snprintf(num, sizeof(num), fmt, (double)edit_value);
  size_t len = strlen(num);
  const bool neg = (num[0] == '-');
  const char *dot = strchr(num, '.');
  int before_decimal = dot ? (int)(dot - num) : (int)len; /* includes '-' if present */

  /* Integer digit count (excluding '-') */
  int int_digits = before_decimal - (neg ? 1 : 0);
  if (int_digits < 1) int_digits = 1;
  int8_t max_int_cursor = (int8_t)(int_digits - 1); /* 0=ones, 1=tens, 2=hundreds... */

  /* Clamp cursor into valid range for the current value shape */
  if (edit_cursor_pos > max_int_cursor) edit_cursor_pos = max_int_cursor;
  if (edit_cursor_pos < -(int8_t)edit_decimals) edit_cursor_pos = -(int8_t)edit_decimals;

  /* digit index:
   *  cursor_pos >= 0: ones=toward left, tens/hundreds... move left from ones
   *  cursor_pos <  0: tenths/hundredths... move right from decimal point
   */
  int digit_index;
  if (edit_cursor_pos >= 0) {
    /* Ones digit is (before_decimal-1), tens is (before_decimal-2), etc. */
    digit_index = (before_decimal - 1) - edit_cursor_pos;
  } else {
    /* Tenths is right after '.', i.e. before_decimal+1 */
    digit_index = before_decimal + (-edit_cursor_pos);
  }
  if (digit_index < 0) digit_index = 0;
  if (digit_index >= (int)len) digit_index = (int)len - 1;

  /* Split into before, digit, after */
  char before_buf[20], digit_buf[4], after_buf[20];
  before_buf[0] = '\0';
  digit_buf[0] = '\0';
  after_buf[0] = '\0';
  if (digit_index > 0) {
    memcpy(before_buf, num, (size_t)digit_index);
    before_buf[digit_index] = '\0';
  }
  digit_buf[0] = num[digit_index];
  digit_buf[1] = '\0';
  if (digit_index + 1 < (int)len) {
    strncpy(after_buf, num + digit_index + 1, sizeof(after_buf) - 1);
    after_buf[sizeof(after_buf) - 1] = '\0';
  }
  /* Append unit to after (so " 50.25 A" style) */
  if (edit_unit && edit_unit[0]) {
    size_t alen = strlen(after_buf);
    if (alen + 2 < sizeof(after_buf)) {
      after_buf[alen] = ' ';
      strncpy(after_buf + alen + 1, edit_unit, sizeof(after_buf) - alen - 2);
      after_buf[sizeof(after_buf) - 1] = '\0';
    }
  }

  lv_span_set_text(edit_span_before, before_buf);
  lv_span_set_text(edit_span_digit, digit_buf);
  lv_span_set_text(edit_span_after, after_buf);
  /* Selected digit: white-only highlight (no bold/size change) */
  lv_style_set_text_color(lv_span_get_style(edit_span_before), lv_color_hex(COL_MUTED));
  lv_style_set_text_color(lv_span_get_style(edit_span_after), lv_color_hex(COL_MUTED));
  lv_style_set_text_color(lv_span_get_style(edit_span_digit), lv_color_hex(COL_TEXT));
  lv_spangroup_refresh(edit_spangroup);

  if (edit_step_label) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Step: %.3g %s", (double)edit_step, edit_unit ? edit_unit : "");
    lv_label_set_text(edit_step_label, buf);
  }
}

static void edit_nudge(float dir) {
  edit_value += dir * edit_step;
  if (edit_value < edit_min) edit_value = edit_min;
  if (edit_value > edit_max) edit_value = edit_max;
  edit_refresh_value_label();
}

static void edit_plus_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED || code == LV_EVENT_LONG_PRESSED_REPEAT) edit_nudge(+1.0f);
}

static void edit_minus_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED || code == LV_EVENT_LONG_PRESSED_REPEAT) edit_nudge(-1.0f);
}

/* < moves cursor left (bigger magnitude); > moves right (smaller, e.g. ones -> tenths) */
static void edit_cursor_left_cb(lv_event_t *e) {
  (void)e;
  /* Move toward larger place values: tenths -> ones -> tens -> hundreds... */
  char num[24];
  char fmt[12];
  snprintf(fmt, sizeof(fmt), "%%.%uf", (unsigned)edit_decimals);
  snprintf(num, sizeof(num), fmt, (double)edit_value);
  const bool neg = (num[0] == '-');
  const char *dot = strchr(num, '.');
  int before_decimal = dot ? (int)(dot - num) : (int)strlen(num);
  int int_digits = before_decimal - (neg ? 1 : 0);
  if (int_digits < 1) int_digits = 1;
  int8_t max_int_cursor = (int8_t)(int_digits - 1);

  if (edit_cursor_pos < max_int_cursor) {
    edit_cursor_pos++;
    edit_step = (edit_cursor_pos >= 0) ? (float)pow(10, edit_cursor_pos)
                                       : (1.0f / (float)pow(10, -edit_cursor_pos));
  }
  edit_refresh_value_label();
}

static void edit_cursor_right_cb(lv_event_t *e) {
  (void)e;
  if (edit_cursor_pos > -(int8_t)edit_decimals) {
    edit_cursor_pos--;
    edit_step = (edit_cursor_pos >= 0) ? (float)pow(10, edit_cursor_pos)
                                       : (1.0f / (float)pow(10, -edit_cursor_pos));
  }
  edit_refresh_value_label();
}

/* ─── Keypad fallback (direct entry) ─── */
static void keypad_close(void) {
  if (keypad_modal) {
    lv_obj_del(keypad_modal);
    keypad_modal = NULL;
    keypad_display = NULL;
  }
}

static void keypad_apply_to_edit(void) {
  if (!keypad_display) return;
  const char *txt = lv_label_get_text(keypad_display);
  if (!txt) return;
  char *end = NULL;
  float v = (float)strtof(txt, &end);
  if (end == txt) return;
  if (v < edit_min) v = edit_min;
  if (v > edit_max) v = edit_max;
  edit_value = v;
  edit_refresh_value_label();
}

static void keypad_btn_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t *m = (lv_obj_t *)lv_event_get_target(e);
  uint32_t id = lv_btnmatrix_get_selected_btn(m);
  const char *txt = lv_btnmatrix_get_btn_text(m, id);
  if (!txt || !keypad_display) return;

  const char *cur = lv_label_get_text(keypad_display);
  char buf[24];
  strncpy(buf, cur ? cur : "", sizeof(buf));
  buf[sizeof(buf) - 1] = '\0';

  if (strcmp(txt, "C") == 0) {
    buf[0] = '\0';
  } else if (strcmp(txt, "<") == 0) {
    size_t n = strlen(buf);
    if (n) buf[n - 1] = '\0';
  } else if (strcmp(txt, "OK") == 0) {
    keypad_apply_to_edit();
    keypad_close();
    return;
  } else if (strcmp(txt, "X") == 0) {
    keypad_close();
    return;
  } else {
    /* digit or '.' or '-' */
    size_t n = strlen(buf);
    if (n + 1 < sizeof(buf)) {
      /* only allow one '.' */
      if (txt[0] == '.' && strchr(buf, '.') != NULL) {
        /* ignore */
      } else if (txt[0] == '-' ) {
        /* toggle sign */
        if (buf[0] == '-') memmove(buf, buf + 1, strlen(buf));
        else {
          if (n + 1 < sizeof(buf)) {
            memmove(buf + 1, buf, n + 1);
            buf[0] = '-';
          }
        }
      } else {
        buf[n] = txt[0];
        buf[n + 1] = '\0';
      }
    }
  }

  if (buf[0] == '\0') {
    lv_label_set_text(keypad_display, "");
  } else {
    lv_label_set_text(keypad_display, buf);
  }
}

static void open_keypad_modal(void) {
  if (keypad_modal) return;
  keypad_modal = lv_obj_create(lv_screen_active());
  lv_obj_set_size(keypad_modal, DISP_W - 2 * MARGIN, 200);
  lv_obj_center(keypad_modal);
  lv_obj_set_style_bg_color(keypad_modal, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_radius(keypad_modal, CARD_R, 0);
  lv_obj_set_style_pad_all(keypad_modal, PAD, 0);
  lv_obj_remove_flag(keypad_modal, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl = lv_label_create(keypad_modal);
  lv_label_set_text(lbl, "Enter value");
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

  keypad_display = lv_label_create(keypad_modal);
  lv_obj_set_style_text_color(keypad_display, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_text_font(keypad_display, LV_FONT_DEFAULT, 0);
  lv_obj_set_width(keypad_display, DISP_W - 2 * MARGIN - 2 * PAD);
  lv_label_set_long_mode(keypad_display, LV_LABEL_LONG_CLIP);
  lv_obj_align(keypad_display, LV_ALIGN_TOP_LEFT, 0, 24);

  /* prefill with current value */
  {
    char fmt[12];
    snprintf(fmt, sizeof(fmt), "%%.%uf", (unsigned)edit_decimals);
    char num[24];
    snprintf(num, sizeof(num), fmt, (double)edit_value);
    lv_label_set_text(keypad_display, num);
  }

  static const char *map[] = {
    "7", "8", "9", "<", "\n",
    "4", "5", "6", "-", "\n",
    "1", "2", "3", "C", "\n",
    "0", ".", "X", "OK", ""
  };

  lv_obj_t *bm = lv_btnmatrix_create(keypad_modal);
  lv_btnmatrix_set_map(bm, map);
  lv_obj_set_size(bm, DISP_W - 2 * MARGIN - 2 * PAD, 140);
  lv_obj_align(bm, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(bm, keypad_btn_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void update_known_load_labels(void) {
  if (label_known_current) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.2f A", (double)known_load_current);
    lv_label_set_text(label_known_current, buf);
  }
  if (label_known_voltage) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.2f V", (double)known_load_voltage);
    lv_label_set_text(label_known_voltage, buf);
  }
}

static void update_calc_mv_labels(void) {
  if (label_calc_mv_voltage) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.1f mV", (double)calc_mv_voltage_mv);
    lv_label_set_text(label_calc_mv_voltage, buf);
  }
  if (label_calc_mv_current) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.1f A", (double)calc_mv_current_a);
    lv_label_set_text(label_calc_mv_current, buf);
  }
  if (label_calc_mv_result && calc_mv_current_a > 0.0f) {
    float mOhm = calc_mv_voltage_mv / calc_mv_current_a;
    char buf[24];
    snprintf(buf, sizeof(buf), "%.3f mOhm", (double)mOhm);
    lv_label_set_text(label_calc_mv_result, buf);
  }
}

static void edit_confirm_cb(lv_event_t *e) {
  (void)e;
  switch (edit_field) {
    case EDIT_MAX_CURRENT:
      maxCurrent = edit_value;
      update_shunt_labels();
      break;
    case EDIT_SHUNT_RESISTANCE:
      shuntResistance = edit_value / 1000.0f;
      update_shunt_labels();
      break;
    case EDIT_KNOWN_CURRENT:
      known_load_current = edit_value;
      update_known_load_labels();
      break;
    case EDIT_KNOWN_VOLTAGE:
      known_load_voltage = edit_value;
      update_known_load_labels();
      break;
    case EDIT_CALC_MV_VOLTAGE:
      calc_mv_voltage_mv = edit_value;
      update_calc_mv_labels();
      break;
    case EDIT_CALC_MV_CURRENT:
      calc_mv_current_a = edit_value;
      update_calc_mv_labels();
      break;
  }
  close_edit_modal();
}

static void open_edit_modal(edit_field_t field) {
  edit_field = field;
  edit_modal = lv_obj_create(lv_screen_active());
  lv_obj_set_size(edit_modal, DISP_W - 2 * MARGIN, 200);
  lv_obj_center(edit_modal);
  lv_obj_set_style_bg_color(edit_modal, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_radius(edit_modal, CARD_R, 0);
  lv_obj_set_style_pad_all(edit_modal, PAD, 0);
  lv_obj_remove_flag(edit_modal, LV_OBJ_FLAG_SCROLLABLE);

  if (field == EDIT_MAX_CURRENT) {
    edit_title = "Max current";
    edit_unit = "A";
    edit_min = 1.0f; edit_max = 200.0f;
    edit_value = maxCurrent;
    edit_decimals = 1;
    edit_cursor_pos = 0;
    edit_step = 1.0f;
  } else if (field == EDIT_SHUNT_RESISTANCE) {
    edit_title = "Shunt resistance";
    edit_unit = "mOhm";
    edit_min = 0.1f; edit_max = 100.0f;
    edit_value = shuntResistance * 1000.0f;
    edit_decimals = 3;
    edit_cursor_pos = -1;
    edit_step = 0.1f;
  } else if (field == EDIT_KNOWN_CURRENT) {
    edit_title = "Known current";
    edit_unit = "A";
    edit_min = 0.1f; edit_max = 200.0f;
    edit_value = known_load_current;
    edit_decimals = 2;
    edit_cursor_pos = -1;
    edit_step = 0.1f;
  } else if (field == EDIT_KNOWN_VOLTAGE) {
    edit_title = "Known voltage";
    edit_unit = "V";
    edit_min = 0.01f; edit_max = 100.0f;
    edit_value = known_load_voltage;
    edit_decimals = 2;
    edit_cursor_pos = -1;
    edit_step = 0.1f;
  } else if (field == EDIT_CALC_MV_VOLTAGE) {
    edit_title = "Shunt voltage";
    edit_unit = "mV";
    edit_min = 1.0f; edit_max = 200.0f;
    edit_value = calc_mv_voltage_mv;
    edit_decimals = 1;
    edit_cursor_pos = 0;
    edit_step = 1.0f;
  } else {
    edit_title = "Max current";
    edit_unit = "A";
    edit_min = 1.0f; edit_max = 200.0f;
    edit_value = calc_mv_current_a;
    edit_decimals = 1;
    edit_cursor_pos = 0;
    edit_step = 1.0f;
  }

  lv_obj_t *lbl = lv_label_create(edit_modal);
  lv_label_set_text(lbl, edit_title);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

  /* Value row: [ < ]  before digit after  [ > ] — selected digit highlighted + underlined */
  lv_obj_t *value_row = lv_obj_create(edit_modal);
  lv_obj_set_size(value_row, (DISP_W - 2 * MARGIN) - 2 * PAD, 56);
  lv_obj_align(value_row, LV_ALIGN_TOP_LEFT, 0, 28);
  lv_obj_set_style_bg_color(value_row, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_radius(value_row, 4, 0);
  lv_obj_set_style_border_color(value_row, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_border_width(value_row, 2, 0);
  lv_obj_set_style_pad_all(value_row, 4, 0);
  lv_obj_set_flex_flow(value_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(value_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(value_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *btn_left = lv_btn_create(value_row);
  lv_obj_set_size(btn_left, 40, 44);
  lv_obj_set_style_radius(btn_left, 4, 0);
  lv_obj_set_style_bg_color(btn_left, lv_color_hex(COL_CARD), 0);
  lv_obj_t *lbl_left = lv_label_create(btn_left);
  lv_label_set_text(lbl_left, "<");
  lv_obj_set_style_text_color(lbl_left, lv_color_hex(COL_TEXT), 0);
  lv_obj_center(lbl_left);
  lv_obj_add_event_cb(btn_left, edit_cursor_left_cb, LV_EVENT_CLICKED, NULL);

  /* Single spangroup: before + digit + after — no gaps, digit in accent + optional bold font */
  edit_spangroup = lv_spangroup_create(value_row);
  lv_obj_set_flex_grow(edit_spangroup, 1);
  lv_obj_set_height(edit_spangroup, 44);
  lv_obj_set_style_bg_opa(edit_spangroup, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(edit_spangroup, 0, 0);
  lv_spangroup_set_mode(edit_spangroup, LV_SPAN_MODE_EXPAND);
  lv_spangroup_set_align(edit_spangroup, LV_TEXT_ALIGN_CENTER);
  lv_obj_remove_flag(edit_spangroup, LV_OBJ_FLAG_SCROLLABLE);

  edit_span_before = lv_spangroup_add_span(edit_spangroup);
  lv_span_set_text(edit_span_before, "");
  lv_style_set_text_color(lv_span_get_style(edit_span_before), lv_color_hex(COL_MUTED));
  lv_style_set_text_font(lv_span_get_style(edit_span_before), &lv_font_montserrat_20);

  edit_span_digit = lv_spangroup_add_span(edit_spangroup);
  lv_span_set_text(edit_span_digit, "");
  lv_style_set_text_color(lv_span_get_style(edit_span_digit), lv_color_hex(COL_TEXT));
  lv_style_set_text_font(lv_span_get_style(edit_span_digit), &lv_font_montserrat_20);

  edit_span_after = lv_spangroup_add_span(edit_spangroup);
  lv_span_set_text(edit_span_after, "");
  lv_style_set_text_color(lv_span_get_style(edit_span_after), lv_color_hex(COL_MUTED));
  lv_style_set_text_font(lv_span_get_style(edit_span_after), &lv_font_montserrat_20);

  lv_obj_t *btn_right = lv_btn_create(value_row);
  lv_obj_set_size(btn_right, 40, 44);
  lv_obj_set_style_radius(btn_right, 4, 0);
  lv_obj_set_style_bg_color(btn_right, lv_color_hex(COL_CARD), 0);
  lv_obj_t *lbl_right = lv_label_create(btn_right);
  lv_label_set_text(lbl_right, ">");
  lv_obj_set_style_text_color(lbl_right, lv_color_hex(COL_TEXT), 0);
  lv_obj_center(lbl_right);
  lv_obj_add_event_cb(btn_right, edit_cursor_right_cb, LV_EVENT_CLICKED, NULL);

  edit_step_label = lv_label_create(edit_modal);
  lv_obj_set_style_text_color(edit_step_label, lv_color_hex(COL_MUTED), 0);
  lv_obj_align(edit_step_label, LV_ALIGN_TOP_LEFT, 0, 88);

  edit_refresh_value_label();

  /* Big +/- buttons + keypad */
  lv_obj_t *btn_minus = lv_btn_create(edit_modal);
  lv_obj_set_size(btn_minus, 88, 48);
  lv_obj_align(btn_minus, LV_ALIGN_BOTTOM_LEFT, 0, -44);
  lv_obj_set_style_bg_color(btn_minus, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_color(btn_minus, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_border_width(btn_minus, 2, 0);
  lv_obj_t *lbl_minus = lv_label_create(btn_minus);
  lv_label_set_text(lbl_minus, "-");
  lv_obj_set_style_text_color(lbl_minus, lv_color_hex(COL_TEXT), 0);
  lv_obj_center(lbl_minus);
  lv_obj_add_event_cb(btn_minus, edit_minus_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(btn_minus, edit_minus_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);

  lv_obj_t *btn_plus = lv_btn_create(edit_modal);
  lv_obj_set_size(btn_plus, 88, 48);
  lv_obj_align(btn_plus, LV_ALIGN_BOTTOM_RIGHT, 0, -44);
  lv_obj_set_style_bg_color(btn_plus, lv_color_hex(COL_ACCENT), 0);
  lv_obj_t *lbl_plus = lv_label_create(btn_plus);
  lv_label_set_text(lbl_plus, "+");
  lv_obj_set_style_text_color(lbl_plus, lv_color_hex(COL_BG), 0);
  lv_obj_center(lbl_plus);
  lv_obj_add_event_cb(btn_plus, edit_plus_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(btn_plus, edit_plus_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);

  lv_obj_t *btn_keypad = lv_btn_create(edit_modal);
  lv_obj_set_size(btn_keypad, 88, 36);
  lv_obj_align(btn_keypad, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_color(btn_keypad, lv_color_hex(COL_CARD), 0);
  lv_obj_t *lbl_kp = lv_label_create(btn_keypad);
  lv_label_set_text(lbl_kp, "123");
  lv_obj_set_style_text_color(lbl_kp, lv_color_hex(COL_TEXT), 0);
  lv_obj_center(lbl_kp);
  lv_obj_add_event_cb(btn_keypad, (lv_event_cb_t)open_keypad_modal, LV_EVENT_CLICKED, NULL);

  lv_obj_t *btn_cancel = lv_btn_create(edit_modal);
  lv_obj_set_size(btn_cancel, 88, 36);
  lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(COL_CARD), 0);
  lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
  lv_label_set_text(lbl_cancel, "Cancel");
  lv_obj_set_style_text_color(lbl_cancel, lv_color_hex(COL_TEXT), 0);
  lv_obj_center(lbl_cancel);
  lv_obj_add_event_cb(btn_cancel, edit_cancel_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *btn_ok = lv_btn_create(edit_modal);
  lv_obj_set_size(btn_ok, 88, 36);
  lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(btn_ok, lv_color_hex(COL_ACCENT), 0);
  lv_obj_t *lbl_ok = lv_label_create(btn_ok);
  lv_label_set_text(lbl_ok, "Save");
  lv_obj_set_style_text_color(lbl_ok, lv_color_hex(COL_BG), 0);
  lv_obj_center(lbl_ok);
  lv_obj_add_event_cb(btn_ok, edit_confirm_cb, LV_EVENT_CLICKED, NULL);
}

static void edit_max_current_cb(lv_event_t *e) {
  (void)e;
  open_edit_modal(EDIT_MAX_CURRENT);
}

static void edit_shunt_res_cb(lv_event_t *e) {
  (void)e;
  open_edit_modal(EDIT_SHUNT_RESISTANCE);
}

static void reset_shunt_confirm_cb(lv_event_t *e) {
  lv_obj_t *msgbox = (lv_obj_t *)lv_event_get_user_data(e);
  maxCurrent = getDefaultMaxCurrent();
  shuntResistance = getDefaultShuntResistance();
  update_shunt_labels();
  if (msgbox) lv_msgbox_close(msgbox);
}

static void reset_shunt_cancel_cb(lv_event_t *e) {
  lv_obj_t *msgbox = (lv_obj_t *)lv_event_get_user_data(e);
  if (msgbox) lv_msgbox_close(msgbox);
}

static void reset_shunt_cb(lv_event_t *e) {
  (void)e;
  lv_obj_t *msgbox = lv_msgbox_create(lv_screen_active());
  lv_msgbox_add_title(msgbox, "Reset defaults?");
  lv_msgbox_add_text(msgbox, "This will restore the default shunt values.");
  lv_obj_t *btn_cancel = lv_msgbox_add_footer_button(msgbox, "Cancel");
  lv_obj_t *btn_reset = lv_msgbox_add_footer_button(msgbox, "Reset");
  lv_obj_set_style_bg_color(btn_reset, lv_color_hex(COL_ERROR), 0);
  lv_obj_add_event_cb(btn_cancel, reset_shunt_cancel_cb, LV_EVENT_CLICKED, msgbox);
  lv_obj_add_event_cb(btn_reset, reset_shunt_confirm_cb, LV_EVENT_CLICKED, msgbox);
}

static void calibration_result_ok_cb(lv_event_t *e) {
  lv_obj_t *msgbox = (lv_obj_t *)lv_event_get_user_data(e);
  if (msgbox) lv_msgbox_close(msgbox);
}

static void apply_shunt_save_cb(lv_event_t *e) {
  (void)e;
  saveShuntCalibration();
  int result = SensorSetShunt(maxCurrent, shuntResistance);
  lv_obj_t *msgbox = lv_msgbox_create(lv_screen_active());
  if (result == 0) {
    lv_msgbox_add_title(msgbox, "Calibration saved");
    lv_msgbox_add_text(msgbox, "Shunt values applied.");
  } else {
    lv_msgbox_add_title(msgbox, "Calibration error");
    char buf[40];
    snprintf(buf, sizeof(buf), "%s error: %d", SensorGetDriverName(), result);
    lv_msgbox_add_text(msgbox, buf);
  }
  lv_obj_t *btn_ok = lv_msgbox_add_footer_button(msgbox, "OK");
  lv_obj_add_event_cb(btn_ok, calibration_result_ok_cb, LV_EVENT_CLICKED, msgbox);
}

static void edit_known_current_cb(lv_event_t *e) { (void)e; open_edit_modal(EDIT_KNOWN_CURRENT); }
static void edit_known_voltage_cb(lv_event_t *e) { (void)e; open_edit_modal(EDIT_KNOWN_VOLTAGE); }
static void edit_calc_mv_voltage_cb(lv_event_t *e) { (void)e; open_edit_modal(EDIT_CALC_MV_VOLTAGE); }
static void edit_calc_mv_current_cb(lv_event_t *e) { (void)e; open_edit_modal(EDIT_CALC_MV_CURRENT); }

static void apply_known_load_cb(lv_event_t *e) {
  (void)e;
  float measuredCurrent = SensorGetCurrent();
  float measuredVoltage = SensorGetBusVoltage();
  if (measuredCurrent == 0.0f || measuredVoltage == 0.0f) {
    lv_obj_t *msgbox = lv_msgbox_create(lv_screen_active());
    lv_msgbox_add_title(msgbox, "No measurement");
    lv_msgbox_add_text(msgbox, "Apply a load and ensure the sensor is reading current and voltage.");
    lv_obj_t *btn = lv_msgbox_add_footer_button(msgbox, "OK");
    lv_obj_add_event_cb(btn, calibration_result_ok_cb, LV_EVENT_CLICKED, msgbox);
    return;
  }
  float correctedShunt = shuntResistance * (measuredCurrent / known_load_current);
  float correctedMaxCurrent = maxCurrent * (known_load_current / measuredCurrent);
  maxCurrent = correctedMaxCurrent;
  shuntResistance = correctedShunt;
  update_shunt_labels();
  if (scr_shunt_calibration) lv_screen_load(scr_shunt_calibration);
  lv_obj_t *msgbox = lv_msgbox_create(lv_screen_active());
  lv_msgbox_add_title(msgbox, "Corrections applied");
  lv_msgbox_add_text(msgbox, "Shunt values updated from known load.");
  lv_obj_t *btn = lv_msgbox_add_footer_button(msgbox, "OK");
  lv_obj_add_event_cb(btn, calibration_result_ok_cb, LV_EVENT_CLICKED, msgbox);
}

static void apply_calc_mv_cb(lv_event_t *e) {
  (void)e;
  if (calc_mv_current_a <= 0.0f) return;
  maxCurrent = calc_mv_current_a;
  shuntResistance = (calc_mv_voltage_mv / 1000.0f) / calc_mv_current_a;
  update_shunt_labels();
  if (scr_shunt_calibration) lv_screen_load(scr_shunt_calibration);
  lv_obj_t *msgbox = lv_msgbox_create(lv_screen_active());
  lv_msgbox_add_title(msgbox, "Values applied");
  lv_msgbox_add_text(msgbox, "Shunt values set from mV/A.");
  lv_obj_t *btn = lv_msgbox_add_footer_button(msgbox, "OK");
  lv_obj_add_event_cb(btn, calibration_result_ok_cb, LV_EVENT_CLICKED, msgbox);
}

static void shunt_calibration_warning_ok_cb(lv_event_t *e) {
  lv_obj_t *msgbox = (lv_obj_t *)lv_event_get_user_data(e);
  if (msgbox) lv_msgbox_close(msgbox);
}

static void open_known_load_cb(lv_event_t *e) {
  (void)e;
  if (scr_known_load) lv_screen_load(scr_known_load);
  lv_obj_t *msgbox = lv_msgbox_create(lv_screen_active());
  lv_msgbox_add_title(msgbox, "Shunt calibration");
  lv_msgbox_add_text(msgbox, "For a reliable calibration: use a stable known load and an accurate reference meter.");
  lv_obj_t *btn = lv_msgbox_add_footer_button(msgbox, "OK");
  lv_obj_add_event_cb(btn, shunt_calibration_warning_ok_cb, LV_EVENT_CLICKED, msgbox);
}

static void open_calc_mv_cb(lv_event_t *e) {
  (void)e;
  if (scr_calc_mv) lv_screen_load(scr_calc_mv);
}

typedef struct {
  float max_current;
  float shunt_milliohm;
  const char *label;
} shunt_standard_t;

static const shunt_standard_t k_shunt_standards[] = {
  {10.0f, 15.0f, "10A / 15.000 mOhm"},
  {20.0f, 7.5f,  "20A / 7.500 mOhm"},
  {30.0f, 5.0f,  "30A / 5.000 mOhm"},
  {50.0f, 1.5f,  "50A / 1.500 mOhm"},
  {75.0f, 1.0f,  "75A / 1.000 mOhm"},
  {100.0f, 0.75f, "100A / 0.750 mOhm"},
  {150.0f, 0.5f, "150A / 0.500 mOhm"},
  {200.0f, 0.375f, "200A / 0.375 mOhm"},
};

static const shunt_standard_t *pending_standard_for_confirm = NULL;

static void standard_confirm_use_cb(lv_event_t *e) {
  lv_obj_t *msgbox = (lv_obj_t *)lv_event_get_user_data(e);
  if (pending_standard_for_confirm) {
    maxCurrent = pending_standard_for_confirm->max_current;
    shuntResistance = pending_standard_for_confirm->shunt_milliohm / 1000.0f;
    update_shunt_labels();
    pending_standard_for_confirm = NULL;
  }
  if (msgbox) lv_msgbox_close(msgbox);
  if (scr_shunt_calibration) lv_screen_load(scr_shunt_calibration);
}

static void standard_confirm_cancel_cb(lv_event_t *e) {
  lv_obj_t *msgbox = (lv_obj_t *)lv_event_get_user_data(e);
  pending_standard_for_confirm = NULL;
  if (msgbox) lv_msgbox_close(msgbox);
}

static void select_standard_cb(lv_event_t *e) {
  lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
  const shunt_standard_t *standard = (const shunt_standard_t *)lv_obj_get_user_data(btn);
  if (!standard) return;
  pending_standard_for_confirm = standard;
  lv_obj_t *msgbox = lv_msgbox_create(lv_screen_active());
  lv_msgbox_add_title(msgbox, "Use this shunt?");
  char buf[120];
  snprintf(buf, sizeof(buf), "%s\n\nThis will set Max current and Shunt resistance. Use Save & apply in Shunt calibration to write to the sensor.", standard->label);
  lv_msgbox_add_text(msgbox, buf);
  lv_obj_t *btn_cancel = lv_msgbox_add_footer_button(msgbox, "Cancel");
  lv_obj_t *btn_use = lv_msgbox_add_footer_button(msgbox, "Use");
  lv_obj_set_style_bg_color(btn_use, lv_color_hex(COL_ACCENT), 0);
  lv_obj_add_event_cb(btn_cancel, standard_confirm_cancel_cb, LV_EVENT_CLICKED, msgbox);
  lv_obj_add_event_cb(btn_use, standard_confirm_use_cb, LV_EVENT_CLICKED, msgbox);
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
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, PAD, 0);

  lv_obj_t *val = lv_label_create(row);
  lv_label_set_text(val, value);
  lv_obj_set_style_text_color(val, lv_color_hex(COL_TEXT), 0);
  lv_obj_align(val, LV_ALIGN_RIGHT_MID, -PAD, 0);

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

static lv_obj_t *add_setting_row_flex(lv_obj_t *parent, const char *name, const char *value,
    lv_event_cb_t tap_cb) {
  lv_obj_t *row = lv_btn_create(parent);
  lv_obj_set_size(row, DISP_W - 2 * MARGIN, ROW_H);
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
  lv_obj_align(val, LV_ALIGN_RIGHT_MID, -PAD, 0);

  if (tap_cb) lv_obj_add_event_cb(row, tap_cb, LV_EVENT_CLICKED, NULL);
  return val;
}

static lv_obj_t *add_category_row_flex(lv_obj_t *parent, const char *name, lv_event_cb_t cb) {
  lv_obj_t *row = lv_btn_create(parent);
  lv_obj_set_size(row, DISP_W - 2 * MARGIN, LIST_ITEM_H);
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

  lv_obj_t *hdr = add_header(scr_monitor, NULL, false);  /* No static title; status shows "SmartShunt INA228 27.8C" */
  label_status = lv_label_create(hdr);
  lv_label_set_text(label_status, "CYD SmartShunt INA? N/A");
  lv_obj_set_style_text_color(label_status, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_width(label_status, DISP_W - 2 * MARGIN - BTN_W - GAP);
  lv_label_set_long_mode(label_status, LV_LABEL_LONG_CLIP);
  lv_obj_align(label_status, LV_ALIGN_LEFT_MID, MARGIN, 0);

  lv_coord_t top = HEADER_H + GAP;
  lv_coord_t half = (DISP_W - 2 * MARGIN - GAP) / 2;
  lv_coord_t card_w = half;
  /* Split remaining height in half (row 1 + row 2), leave MARGIN at bottom like sides */
  lv_coord_t card_h = (DISP_H - HEADER_H - 2 * GAP - MARGIN) / 2;

  /* Primary cards: Current, Voltage */
  lv_obj_t *card_i = lv_obj_create(scr_monitor);
  lv_obj_set_size(card_i, card_w, card_h);
  lv_obj_set_pos(card_i, MARGIN, top);
  lv_obj_set_style_bg_color(card_i, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_radius(card_i, CARD_R, 0);
  lv_obj_remove_flag(card_i, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(card_i, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(card_i, hist_card_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)HIST_I);
  lv_obj_t *lbl_i = lv_label_create(card_i);
  lv_label_set_text(lbl_i, "Current");
  lv_obj_set_style_text_color(lbl_i, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_pos(lbl_i, PAD, 2);
  label_current = lv_label_create(card_i);
  lv_label_set_text(label_current, "0.000 A");
  lv_obj_set_style_text_color(label_current, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_pos(label_current, PAD, 22);
#if LV_FONT_MONTSERRAT_20
  lv_obj_set_style_text_font(label_current, &lv_font_montserrat_20, 0);
#endif

  lv_obj_t *card_v = lv_obj_create(scr_monitor);
  lv_obj_set_size(card_v, card_w, card_h);
  lv_obj_set_pos(card_v, MARGIN + card_w + GAP, top);
  lv_obj_set_style_bg_color(card_v, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_radius(card_v, CARD_R, 0);
  lv_obj_remove_flag(card_v, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(card_v, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(card_v, hist_card_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)HIST_V);
  lv_obj_t *lbl_v = lv_label_create(card_v);
  lv_label_set_text(lbl_v, "Voltage");
  lv_obj_set_style_text_color(lbl_v, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_pos(lbl_v, PAD, 2);
  label_voltage = lv_label_create(card_v);
  lv_label_set_text(label_voltage, "0.00 V");
  lv_obj_set_style_text_color(label_voltage, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_pos(label_voltage, PAD, 22);
#if LV_FONT_MONTSERRAT_20
  lv_obj_set_style_text_font(label_voltage, &lv_font_montserrat_20, 0);
#endif

  top += card_h + GAP;
  /* Secondary: Power + Energy – same card layout as Current/Voltage (label top, value below), white value text */
  {
    lv_obj_t *card_p = lv_btn_create(scr_monitor);
    lv_obj_set_size(card_p, card_w, card_h);
    lv_obj_set_pos(card_p, MARGIN, top);
    lv_obj_set_style_radius(card_p, CARD_R, 0);
    lv_obj_set_style_bg_color(card_p, lv_color_hex(COL_CARD), 0);
    lv_obj_clear_flag(card_p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card_p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card_p, hist_card_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)HIST_P);
    lv_obj_t *lbl = lv_label_create(card_p);
    lv_label_set_text(lbl, "Power");
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_pos(lbl, PAD, 2);
    label_power = lv_label_create(card_p);
    lv_label_set_text(label_power, "0.0 W");
    lv_obj_set_style_text_color(label_power, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_pos(label_power, PAD, 22);
#if LV_FONT_MONTSERRAT_20
    lv_obj_set_style_text_font(label_power, &lv_font_montserrat_20, 0);
#endif
  }
  {
    lv_obj_t *card_e = lv_btn_create(scr_monitor);
    lv_obj_set_size(card_e, card_w, card_h);
    lv_obj_set_pos(card_e, MARGIN + card_w + GAP, top);
    lv_obj_set_style_radius(card_e, CARD_R, 0);
    lv_obj_set_style_bg_color(card_e, lv_color_hex(COL_CARD), 0);
    lv_obj_clear_flag(card_e, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card_e, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card_e, hist_card_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)HIST_E);
    lv_obj_add_event_cb(card_e, show_reset_energy_confirm_from_dashboard, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_t *lbl = lv_label_create(card_e);
    lv_label_set_text(lbl, "Energy");
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_pos(lbl, PAD, 2);
    label_energy = lv_label_create(card_e);
    lv_label_set_text(label_energy, "0.0 Wh");
    lv_obj_set_style_text_color(label_energy, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_pos(label_energy, PAD, 22);
#if LV_FONT_MONTSERRAT_20
    lv_obj_set_style_text_font(label_energy, &lv_font_montserrat_20, 0);
#endif
  }
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
  add_category_row(scr_settings_home, "Integration  >", y, to_integration);   y += LIST_ITEM_H + GAP;
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
  add_category_row(scr_calibration, "Touch calibration",  y, act_touch_cal);     y += LIST_ITEM_H + GAP;
  add_category_row(scr_calibration, "Shunt calibration",  y, to_shunt_calibration);
}

static void build_shunt_calibration(void) {
  scr_shunt_calibration = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_shunt_calibration, lv_color_hex(COL_BG), 0);
  lv_obj_remove_flag(scr_shunt_calibration, LV_OBJ_FLAG_SCROLLABLE);

  add_header_back_to_calibration(scr_shunt_calibration, "Shunt calibration");

  lv_obj_t *list = lv_obj_create(scr_shunt_calibration);
  lv_obj_set_size(list, DISP_W, DISP_H - HEADER_H);
  lv_obj_set_pos(list, 0, HEADER_H);
  lv_obj_set_style_bg_color(list, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_pad_all(list, MARGIN, 0);
  lv_obj_set_style_pad_row(list, GAP, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

  label_shunt_max = add_setting_row_flex(list, "Max current", "--", edit_max_current_cb);
  label_shunt_res = add_setting_row_flex(list, "Shunt resistance", "--", edit_shunt_res_cb);
  add_category_row_flex(list, "Choose shunt", to_shunt_standard);  /* predefined + custom mV/A */
  add_category_row_flex(list, "Shunt calibration", open_known_load_cb);  /* known-load reference flow */
  add_category_row_flex(list, "Reset defaults", reset_shunt_cb);
  add_category_row_flex(list, "Save & apply", apply_shunt_save_cb);

  update_shunt_labels();
}

static void build_shunt_standard(void) {
  scr_shunt_standard = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_shunt_standard, lv_color_hex(COL_BG), 0);
  lv_obj_remove_flag(scr_shunt_standard, LV_OBJ_FLAG_SCROLLABLE);

  add_header_back_to_shunt(scr_shunt_standard, "Choose shunt");

  lv_obj_t *list = lv_list_create(scr_shunt_standard);
  lv_obj_set_size(list, DISP_W, DISP_H - HEADER_H);
  lv_obj_set_pos(list, 0, HEADER_H);
  lv_obj_set_style_bg_color(list, lv_color_hex(COL_BG), 0);

  for (size_t i = 0; i < sizeof(k_shunt_standards) / sizeof(k_shunt_standards[0]); i++) {
    const shunt_standard_t *standard = &k_shunt_standards[i];
    lv_obj_t *btn = lv_list_add_btn(list, NULL, standard->label);
    lv_obj_set_user_data(btn, (void *)standard);
    lv_obj_add_event_cb(btn, select_standard_cb, LV_EVENT_CLICKED, NULL);
  }
  lv_obj_t *custom_btn = lv_list_add_btn(list, NULL, "Custom (mV/A)  >");
  lv_obj_add_event_cb(custom_btn, open_calc_mv_cb, LV_EVENT_CLICKED, NULL);
}

static void build_known_load(void) {
  scr_known_load = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_known_load, lv_color_hex(COL_BG), 0);
  lv_obj_remove_flag(scr_known_load, LV_OBJ_FLAG_SCROLLABLE);

  add_header_back_to_shunt(scr_known_load, "Shunt calibration");

  lv_obj_t *hint = lv_label_create(scr_known_load);
  lv_label_set_text(hint, "Apply known load, enter reference values.");
  lv_obj_set_style_text_color(hint, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_width(hint, DISP_W - 2 * MARGIN);
  lv_obj_set_pos(hint, MARGIN, HEADER_H + GAP);

  lv_obj_t *list = lv_obj_create(scr_known_load);
  lv_obj_set_size(list, DISP_W, DISP_H - HEADER_H - ROW_H - GAP);
  lv_obj_set_pos(list, 0, HEADER_H + ROW_H + GAP);
  lv_obj_set_style_bg_color(list, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_pad_all(list, MARGIN, 0);
  lv_obj_set_style_pad_row(list, GAP, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

  char buf[24];
  snprintf(buf, sizeof(buf), "%.2f A", (double)known_load_current);
  label_known_current = add_setting_row_flex(list, "Known current", buf, edit_known_current_cb);
  snprintf(buf, sizeof(buf), "%.2f V", (double)known_load_voltage);
  label_known_voltage = add_setting_row_flex(list, "Known voltage", buf, edit_known_voltage_cb);
  label_known_measured = add_setting_row_flex(list, "Measured", "-- A / -- V", NULL);
  label_known_corrected = add_setting_row_flex(list, "Corrected", "-- A / -- mOhm", NULL);
  add_category_row_flex(list, "Apply corrections", apply_known_load_cb);
}

static void build_calc_mv(void) {
  scr_calc_mv = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_calc_mv, lv_color_hex(COL_BG), 0);
  lv_obj_remove_flag(scr_calc_mv, LV_OBJ_FLAG_SCROLLABLE);

  add_header_back_to_shunt(scr_calc_mv, "Calc from mV/A");

  lv_obj_t *list = lv_obj_create(scr_calc_mv);
  lv_obj_set_size(list, DISP_W, DISP_H - HEADER_H);
  lv_obj_set_pos(list, 0, HEADER_H);
  lv_obj_set_style_bg_color(list, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_pad_all(list, MARGIN, 0);
  lv_obj_set_style_pad_row(list, GAP, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

  char buf[24];
  snprintf(buf, sizeof(buf), "%.1f mV", (double)calc_mv_voltage_mv);
  label_calc_mv_voltage = add_setting_row_flex(list, "Shunt voltage", buf, edit_calc_mv_voltage_cb);
  snprintf(buf, sizeof(buf), "%.1f A", (double)calc_mv_current_a);
  label_calc_mv_current = add_setting_row_flex(list, "Max current", buf, edit_calc_mv_current_cb);
  if (calc_mv_current_a > 0.0f) {
    float mOhm = calc_mv_voltage_mv / calc_mv_current_a;
    snprintf(buf, sizeof(buf), "%.3f mOhm", (double)mOhm);
    label_calc_mv_result = add_setting_row_flex(list, "Calculated shunt", buf, NULL);
  } else {
    label_calc_mv_result = add_setting_row_flex(list, "Calculated shunt", "-- mOhm", NULL);
  }
  add_category_row_flex(list, "Apply values", apply_calc_mv_cb);
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
  {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s Smart Shunt\nSensor info on next update", SensorGetDriverName());
    lv_label_set_text(info, buf);
  }
  lv_obj_set_style_text_color(info, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_pos(info, MARGIN, HEADER_H + GAP);
}

/* ─── Screen: Integration (VE.Direct, UART info) ─── */
static void vedirect_switch_cb(lv_event_t *e) {
  lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
  bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
  set_vedirect_enabled(on);
}

static void build_integration(void) {
  scr_integration = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_integration, lv_color_hex(COL_BG), 0);
  lv_obj_remove_flag(scr_integration, LV_OBJ_FLAG_SCROLLABLE);

  add_header_back_to_settings(scr_integration, "Integration");

  lv_coord_t y = HEADER_H + GAP;

  /* VE.Direct row: label + switch */
  lv_obj_t *row_ve = lv_btn_create(scr_integration);
  lv_obj_set_size(row_ve, DISP_W - 2 * MARGIN, LIST_ITEM_H);
  lv_obj_set_pos(row_ve, MARGIN, y);
  lv_obj_set_style_radius(row_ve, CARD_R, 0);
  lv_obj_set_style_bg_color(row_ve, lv_color_hex(COL_CARD), 0);
  lv_obj_t *lbl_ve = lv_label_create(row_ve);
  lv_label_set_text(lbl_ve, "VE.Direct");
  lv_obj_set_style_text_color(lbl_ve, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_pos(lbl_ve, PAD, (LIST_ITEM_H - 14) / 2);
  lv_obj_t *sw = lv_switch_create(row_ve);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -PAD, 0);
  if (get_vedirect_enabled())
    lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, vedirect_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
  y += LIST_ITEM_H + GAP;

  /* UART info (read-only) */
  char uart_buf[64];
  TelemetryVictronGetUartInfo(uart_buf, sizeof(uart_buf));
  add_setting_row(scr_integration, "UART", uart_buf, y, NULL);
}

/* ─── History push (called from update_timer) ─── */
static void history_push(float v, float i, float p, double e) {
  s_history_v[s_history_write_idx] = v;
  s_history_i[s_history_write_idx] = i;
  s_history_p[s_history_write_idx] = (float)p;
  s_history_e[s_history_write_idx] = (float)e;
  s_history_write_idx = (s_history_write_idx + 1) % HISTORY_LEN;
  if (s_history_count < HISTORY_LEN) s_history_count++;
}

/* ─── Sensor update timer: only update value labels, no redraw ─── */
static void update_timer_cb(lv_timer_t *timer) {
  (void)timer;
  float current     = SensorGetCurrent();
  float voltage     = SensorGetBusVoltage();
  float power       = SensorGetPower();
  double energy     = SensorGetWattHour();
  float temperature = SensorGetTemperature();
  bool  connected   = SensorIsConnected();

  history_push(voltage, current, power, energy);

  if (s_active_hist_popup)
    hist_apply_scroll_policy_and_refresh(s_active_hist_popup);

  if (label_current && label_voltage && label_power && label_energy && label_status) {
    char buf[48];
    if (connected) {
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
      snprintf(buf, sizeof(buf), "CYD SmartShunt %s %.1fC", SensorGetDriverName(), (double)temperature);
      lv_label_set_text(label_status, buf);
      lv_obj_set_style_text_color(label_status, lv_color_hex(COL_MUTED), 0);
    } else {
      lv_label_set_text(label_current, "--");
      lv_label_set_text(label_voltage, "--");
      lv_label_set_text(label_power, "--");
      lv_label_set_text(label_energy, "--");
      snprintf(buf, sizeof(buf), "CYD SmartShunt INA? N/A");
      lv_label_set_text(label_status, buf);
      lv_obj_set_style_text_color(label_status, lv_color_hex(COL_ERROR), 0);
    }
  }

  if (lv_screen_active() == scr_known_load && label_known_measured && label_known_corrected) {
    char buf[40];
    snprintf(buf, sizeof(buf), "%.3f A / %.2f V", (double)current, (double)voltage);
    lv_label_set_text(label_known_measured, buf);
    if (current != 0.0f && voltage != 0.0f && known_load_current > 0.0f) {
      float corr_shunt = shuntResistance * (current / known_load_current);
      float corr_max = maxCurrent * (known_load_current / current);
      snprintf(buf, sizeof(buf), "%.2f A / %.3f mOhm", (double)corr_max, (double)(corr_shunt * 1000.0f));
      lv_label_set_text(label_known_corrected, buf);
    } else {
      lv_label_set_text(label_known_corrected, "--");
    }
  }

  if (lv_screen_active() == scr_calc_mv && label_calc_mv_result && calc_mv_current_a > 0.0f) {
    float mOhm = calc_mv_voltage_mv / calc_mv_current_a;
    char buf[24];
    snprintf(buf, sizeof(buf), "%.3f mOhm", (double)mOhm);
    lv_label_set_text(label_calc_mv_result, buf);
  }
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
  build_shunt_calibration();
  build_shunt_standard();
  build_known_load();
  build_calc_mv();
  build_data();
  build_system();
  build_integration();

  lv_screen_load(scr_monitor);

  lv_timer_t *t = lv_timer_create(update_timer_cb, 200, NULL);
  lv_timer_set_repeat_count(t, -1);
}

void ui_lvgl_on_touch_calibration_done(void) {
  (void)0;
}

void ui_lvgl_poll(void) {
  if (!disp) return;  /* init failed (e.g. buffer alloc), avoid calling LVGL */
  static uint32_t last = 0;
  uint32_t now = millis();
  lv_tick_inc(now - last);
  last = now;
  lv_timer_handler();
} left of lv_timer_handler

void ui_history_clear(void) {
  s_history_write_idx = 0;
  s_history_count = 0;
}

void ui_history_clear(void) {
  s_history_write_idx = 0;
  s_history_count = 0;
}

void ui_history_clear(void) {
  s_history_write_idx = 0;
  s_history_count = 0;
}
