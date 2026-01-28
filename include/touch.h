/**
 * @file touch.h
 * XPT2046 touch driver for CYD - init, calibration-based mapping, optional diagnostic.
 * Touch SPI: VSPI remapped CLK=25, MOSI=32, MISO=39, CS=33, IRQ=36.
 */
#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>
#include <stdbool.h>

#define TOUCH_DISPLAY_WIDTH  320
#define TOUCH_DISPLAY_HEIGHT 240

/** Calibration limits from NVS (same keys as main). */
typedef struct {
  int16_t xMin;
  int16_t xMax;
  int16_t yMin;
  int16_t yMax;
  bool    isValid;
} TouchCalibration_t;

/** Use main's XPT2046 instance: call after ts.begin(mySpi) and ts.setRotation(1). */
void TouchInit(void *ts_instance);

/** Set calibration used by TouchRawToScreen. Call after loading from NVS. */
void TouchSetCalibration(const TouchCalibration_t *cal);

/**
 * Map raw XPT2046 coordinates to screen coordinates.
 * 1) Normalize raw to [0,1] using cal min/max and clamp.
 * 2) Apply mapping (swap/invert) to get screen x,y.
 *
 * To fix mirrored or wrong-handed touch, edit the "Mapping variant" block below
 * in touch.cpp:
 *   - Swap X/Y: use ny for x, nx for y.
 *   - Invert X: use (1.f - nx) for x.
 *   - Invert Y: use (1.f - ny) for y.
 */
void TouchRawToScreen(int16_t raw_x, int16_t raw_y, int16_t *screen_x, int16_t *screen_y);

/** If true, log raw and mapped coords on press (optional diagnostic). */
void TouchSetDiagnostic(bool on);

/** Read current touch: screen coords and pressed state. For LVGL indev. */
void TouchGetScreenPoint(int16_t *x, int16_t *y, bool *pressed);

#endif /* TOUCH_H */
