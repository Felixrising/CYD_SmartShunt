# Release readiness

Short checklist for cutting a GitHub release. The codebase is in good shape for a **v1.0**-style release: feature-complete for the stated scope, documented, and buildable.

---

## Codebase health

- **Sensor layer:** Clean abstraction (sensor.h API, sensor_backend.h, sensor.cpp dispatcher). INA228, INA226, INA219 backends; auto-detect on I2C 0x40–0x4F. Documented how to add new sensors in `docs/HOW_TO_ADD_NEW_SENSORS.md`.
- **UI:** LVGL 9 touch UI; legacy UI removed. Monitor, settings (measurement, calibration, data, integration, system, about), history popups, shunt calibration and known-load flow, magnitude-based decimals (A/V/W/Wh), sensor-dependent precision (INA228 vs others).
- **Telemetry:** Victron-style VE.Direct (1 Hz) in place; BLE/GATT planned (see `docs/BLE_GATT_plan.md`).
- **Build:** PlatformIO; `pio run` / `pio run -t upload`; single env `cyd` with TFT_eSPI, XPT2046, INA228/226/219, LVGL 9.
- **Docs:** README (hardware, wiring, pins), `docs/` (metrics/precision, update rates, legacy removal, BLE plan, how-to add sensors, this file).

---

## Pre-release checklist

- [ ] **Version:** Set a Git tag (e.g. `v1.0.0`). Update the About screen string in `src/ui_lvgl.cpp`: `#define CYD_SMARTSHUNT_VERSION "1.0.0"` (or match your tag).
- [ ] **README:** Up to date (features, supported sensors, build/upload, wiring). Add links to “How to add new sensors” and “Release readiness” if you want them visible from the repo root.
- [ ] **LICENSE:** Present and correct (already in repo).
- [ ] **Build:** `pio run` succeeds; optionally run on a clean clone to confirm lib_deps and env.
- [ ] **Hardware test:** Quick smoke test on CYD + INA228 (or INA226/219): boot, dashboard, touch, settings, calibration, history, telemetry if used.
- [ ] **Changelog (optional):** Add a `CHANGELOG.md` or “Releases” notes on GitHub for v1.0 (features, known limitations, hardware notes).

---

## Suggested release notes (v1.0)

You can paste or adapt this for the GitHub release:

**CYD Smart Shunt v1.0**

- ESP32 Cheap Yellow Display (CYD) DC current/voltage meter with LVGL touch UI.
- **Sensors:** INA228, INA226, INA219 (I2C auto-detect).
- **Dashboard:** Current, voltage, power, energy (magnitude-based decimals); temperature (INA228); history popups; energy reset (long-press).
- **Settings:** Measurement (averaging), calibration (touch, shunt), data, integration (VE.Direct), system, about.
- **Shunt calibration:** Standard shunt list, known-load calibration, custom mV/A.
- **Telemetry:** Victron-style VE.Direct serial output (1 Hz).
- **Docs:** README (wiring, pins), how-to add new sensors, metrics/precision, update rates.

**Hardware:** ESP32-2432S028R (CYD), INA228/226/219 on I2C (e.g. 50A/75mV shunt). See README for wiring and pinout.

---

## Known limitations / roadmap

- BLE/GATT telemetry is planned, not included in this release.
- Data page (min/max, session stats, export) and alarms are on the roadmap (see README).

These can be listed in release notes as “future work” so users know the scope of v1.0.
