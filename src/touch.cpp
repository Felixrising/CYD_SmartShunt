/**
 * @file touch.cpp
 * XPT2046 touch mapping using NVS calibration.
 * Main owns SPI/ts; call TouchInit(&ts) after ts.begin() and ts.setRotation(1).
 */
#include "touch.h"
#include <XPT2046_Touchscreen.h>
#include <Arduino.h>

static XPT2046_Touchscreen *s_ts = NULL;
static TouchCalibration_t s_cal = {0, 0, 0, 0, false};
static bool s_diagnostic = false;

void TouchInit(void *ts_instance) {
  s_ts = (XPT2046_Touchscreen *)ts_instance;
}

void TouchSetCalibration(const TouchCalibration_t *cal) {
  if (cal) {
    s_cal = *cal;
  }
}

static inline float clampf(float v, float a, float b) {
  if (v < a) return a;
  if (v > b) return b;
  return v;
}

void TouchRawToScreen(int16_t raw_x, int16_t raw_y, int16_t *screen_x, int16_t *screen_y) {
  if (!screen_x || !screen_y) return;

  float nx = 0.5f, ny = 0.5f;
  if (s_cal.isValid && (s_cal.xMax > s_cal.xMin) && (s_cal.yMax > s_cal.yMin)) {
    nx = (float)(raw_x - s_cal.xMin) / (float)(s_cal.xMax - s_cal.xMin);
    ny = (float)(raw_y - s_cal.yMin) / (float)(s_cal.yMax - s_cal.yMin);
    nx = clampf(nx, 0.f, 1.f);
    ny = clampf(ny, 0.f, 1.f);
  }

  /* Mapping variant: adjust swap/invert here if touch is mirrored or wrong-handed.
   * Default: x = nx * W, y = ny * H.
   * Swap:   use ny for x, nx for y.
   * Invert X: use (1 - nx) for x.
   * Invert Y: use (1 - ny) for y.
   */
  float sx = nx;
  float sy = ny;
  /* Example: if X is reversed, use: sx = 1.f - nx; */
  /* Example: if Y is reversed, use: sy = 1.f - ny; */
  /* Example: if axes are swapped, use: sx=ny; sy=nx; */

  *screen_x = (int16_t)(sx * (TOUCH_DISPLAY_WIDTH - 1) + 0.5f);
  *screen_y = (int16_t)(sy * (TOUCH_DISPLAY_HEIGHT - 1) + 0.5f);
  if (*screen_x < 0) *screen_x = 0;
  if (*screen_x >= TOUCH_DISPLAY_WIDTH)  *screen_x = TOUCH_DISPLAY_WIDTH - 1;
  if (*screen_y < 0) *screen_y = 0;
  if (*screen_y >= TOUCH_DISPLAY_HEIGHT) *screen_y = TOUCH_DISPLAY_HEIGHT - 1;
}

void TouchSetDiagnostic(bool on) {
  s_diagnostic = on;
}

void TouchGetScreenPoint(int16_t *x, int16_t *y, bool *pressed) {
  if (!x || !y || !pressed) return;
  *pressed = false;
  *x = 0;
  *y = 0;
  if (!s_ts) return;

  if (!s_ts->tirqTouched() || !s_ts->touched()) {
    return;
  }

  TS_Point p = s_ts->getPoint();
  int16_t sx = 0, sy = 0;
  TouchRawToScreen(p.x, p.y, &sx, &sy);
  *x = sx;
  *y = sy;
  *pressed = true;

  if (s_diagnostic) {
    Serial.print(F("[touch] raw=("));
    Serial.print(p.x);
    Serial.print(F(","));
    Serial.print(p.y);
    Serial.print(F(") mapped=("));
    Serial.print(sx);
    Serial.print(F(","));
    Serial.println(sy);
    Serial.flush();
  }
}
