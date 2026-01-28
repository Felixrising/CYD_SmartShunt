/**
 * @file ui_lvgl.h
 * LVGL UI for CYD Smart Shunt: init, Monitoring/Settings screens, nav, INA228 update timer.
 * Expects: tft inited + setRotation(1), TouchInit + TouchSetCalibration done.
 */
#ifndef UI_LVGL_H
#define UI_LVGL_H

#include <stdbool.h>

/** Call after display + touch init and calibration. Creates display/indev and screens. */
void ui_lvgl_init(void);

/** Call when touch calibration was re-done (e.g. after LVGL calibration flow) to refresh mapping. */
void ui_lvgl_on_touch_calibration_done(void);

/** Call every ~5 ms from loop: advances LVGL tick and runs timer handler. */
void ui_lvgl_poll(void);

#endif /* UI_LVGL_H */
