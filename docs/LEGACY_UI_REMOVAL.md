# Legacy UI Removal

All legacy TFT_eSPI UI code has been removed from `main.cpp`. The project now uses **LVGL only** for the display.

## Removed (LVGL equivalents exist)

| Legacy component | LVGL equivalent |
|------------------|------------------|
| `drawMonitoringScreen()` / `updateMonitoringScreen()` | Monitor screen in `ui_lvgl.cpp` (build_monitor, update_timer_cb) |
| `drawSettingsScreen()` | Settings home + sub-screens (build_settings_home, etc.) |
| `handleTouch()` | LVGL indev + button/label click callbacks |
| `drawShuntCalibrationScreen()` | Shunt calibration screen (build_shunt_calibration) |
| `performShuntCalibration()` | LVGL shunt calibration flow (buttons → value editor / known load / calc / standard) |
| `getNumericInput()` | LVGL value editor modal (spangroup + keypad) |
| `performKnownLoadCalibration()` | Known load calibration screen (build_known_load) |
| `showStandardValues()` | Standard values screen (build_shunt_standard) |
| `calculateFromVoltageCurrent()` | Calc from mV screen (build_calc_mv) |
| `displayError()` | LVGL msgbox where needed |
| Legacy state: `currentScreen`, `lastUpdate`, `SETTINGS_BUTTON_*`, `ScreenMode`, `energyAccumulationEnabled` | Not used; LVGL manages screens |

## Retained (no LVGL equivalent – gap)

- **Touch calibration** (`performTouchCalibration()`, `loadTouchCalibration()`, `saveTouchCalibration()`, `calibrateTouchPoint()`)
  - The 4-point touch calibration flow still uses **TFT_eSPI full-screen drawing** and blocking `delay()`/`while` loops. LVGL does not provide a built-in 4-point calibration UI; the calibration confirm in Settings calls `performTouchCalibration()`, which takes over the TFT, draws targets, and collects raw touch points. This is intentional (see comment in `ui_lvgl.cpp`: "TFT take-over is intentional"). A future improvement could be an LVGL-based calibration screen that draws targets with LVGL and reads raw touch from the driver.

## Still in main.cpp (used by LVGL or setup)

- Display/touch init: `tft`, `ts`, `mySpi`, `TouchInit()`, `TouchSetCalibration()`
- NVS/preferences, shunt globals: `preferences`, `maxCurrent`, `shuntResistance`
- Helpers used by LVGL: `resetEnergyAccumulation()`, `cycleAveraging()`, `getAveragingString()`, `performTouchCalibration()`, `loadTouchCalibration()`, `saveTouchCalibration()`, `loadShuntCalibration()`, `saveShuntCalibration()`, `getDefaultMaxCurrent()`, `getDefaultShuntResistance()`, `get_vedirect_enabled()`, `set_vedirect_enabled()`
- `calibrateTouchPoint()` is used inside `performTouchCalibration()`
