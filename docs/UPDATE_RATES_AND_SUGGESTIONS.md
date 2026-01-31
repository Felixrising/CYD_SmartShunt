# Update Rates, Victron Standard, and Enhancement Suggestions

## Current behaviour

| Layer | Rate | Notes |
|-------|------|--------|
| **Display (LVGL)** | 200 ms (5 Hz) | `lv_timer_create(update_timer_cb, 200, NULL)`. Reads sensor, updates labels and history. |
| **Victron telemetry** | Main loop: 500 ms (2 Hz) poll; VE.Direct TEXT: 1 s | `UPDATE_INTERVAL_MS = 500` in main; `TelemetryVictronUpdate()` paces TEXT frames at 1 s (`UPDATE_INTERVAL_MS` in telemetry_victron.cpp). |
| **Sensor** | On demand | No background polling. Each `SensorGet*()` does a synchronous I2C read. INA228 is in continuous conversion + hardware averaging. |

## Victron VE.Direct standard

- **TEXT mode**: Victron devices typically send unsolicited runtime data at **1 Hz (1 second)**. Our code already paces TEXT updates at 1 s in `TelemetryVictronUpdate()` (`UPDATE_INTERVAL_MS = 1000`), so we meet the usual expectation.
- **Conclusion**: 2 Hz sensor read in the main loop for Victron is fine; the telemetry layer throttles to 1 Hz for the host. No change required for compliance.

## Suggested updates and enhancements

1. **Unify sensor read rate (optional)**  
   - Right now the display timer reads at 5 Hz and the main loop reads at 2 Hz for Victron. You could feed both from a single source: e.g. one 5 Hz timer that reads the sensor, updates in-memory values, and triggers label updates + (every 2nd or 3rd call) call `TelemetryVictronUpdate()` with the latest snapshot. That would avoid duplicate I2C reads and keep one place that defines “sensor read rate”.

2. **Sensor layer**  
   - No change needed for “high-rate poll + software integration”: the INA228 already does conversion and averaging in hardware. If you ever add a driver that does not (e.g. raw ADC), you could add a small software filter or decimation in the abstraction before exposing values to UI/Victron.

3. **Display update rate**  
   - 5 Hz (200 ms) is a good balance for readability and CPU. Going to 10 Hz can make the history graph smoother but increases I2C and LVGL work; 2–5 Hz is usually enough for a shunt display.

4. **Victron poll in main loop**  
   - You could align the main-loop interval with the 1 s TEXT rate: e.g. `UPDATE_INTERVAL_MS = 1000` for the telemetry branch only (and keep calling `TelemetryVictronUpdate()` every 1 s). That would match Victron’s 1 Hz expectation exactly and slightly reduce CPU; the telemetry module would still only send at 1 s.

5. **Non-blocking**  
   - The only blocking left in the normal path is `delay(5)` in the main loop and `delay(100)` in `INA228_ResetEnergy()`. Touch calibration remains intentionally blocking. Consider replacing the global `delay(5)` with a timer or `lv_timer_handler()`-driven pacing if you need stricter non-blocking behaviour.

6. **Optional: single “sensor tick”**  
   - One timer at 5 Hz (or 10 Hz) that: reads I2C once, updates globals or a small struct, pushes to history, refreshes LVGL labels, and every N-th tick calls `TelemetryVictronUpdate()`. Main loop would only run `ui_lvgl_poll()` and optionally a 1 s timer for Victron if you prefer to keep that separate.

Summary: **Victron expects ~1 Hz TEXT updates; we already send at 1 s.** The rest of the suggestions are optional cleanups (unify read path, align main-loop interval, or add a single sensor-tick timer) for clarity and consistency.
