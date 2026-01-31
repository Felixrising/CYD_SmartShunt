# Metrics: units, precision, and meaningful display

Reference for every displayed metric: unit, current format (decimals / significant figures), and recommended useful precision.

---

## INA sensor hardware: units, resolution, and precision

The project uses Rob Tillaart’s INA228, INA226, and INA219 libraries (Texas Instruments parts). The **API returns SI units** and **float** (or **double** for energy). Precision is set by the chip’s ADC and register LSBs, not by the C type.

### INA228 (20-bit delta-sigma ADC)

| Quantity        | Unit | Register / source      | LSB (resolution) | Effective precision / significant figures |
|-----------------|------|------------------------|----------------------------------|-------------------------------------------|
| Shunt voltage   | V    | 20-bit register       | **2.5 µV** per LSB               | ~5–6 digits (e.g. 0.000 075 V = 75 µV)    |
| Bus voltage      | V    | 20-bit, 0–85 V        | **195.3125 µV** (~0.2 mV) per LSB| ~4–5 digits (e.g. 12.3456 V)              |
| Current         | A    | Derived: V_shunt / R  | **2.5 µV / R_shunt** (e.g. 1.67 mA for 1.5 mΩ) | Depends on R; 3–4 decimals typical      |
| Power           | W    | Computed: V × I       | Product of V and I resolution    | Typically 3–4 significant figures          |
| Energy          | Wh   | Internal accumulation | Depends on current LSB and time | Use **double**; display 1 dec Wh, 2–3 kWh |
| Temperature     | °C   | Internal sensor       | Typically ±1 °C accuracy        | 1 decimal (e.g. 25.3 °C)                   |

- **Units:** A, V, W, Wh, °C (all SI / common practice).
- **Averaging:** 1, 4, 16, 64, 128, 256, 512, or 1024 samples — improves noise, not nominal LSB size.
- **Shunt range:** ±163.84 mV or ±40.96 mV full scale.

### INA226 (16-bit ADC)

| Quantity        | Unit | Register / source      | LSB (resolution) | Effective precision / significant figures |
|-----------------|------|------------------------|----------------------------------|-------------------------------------------|
| Shunt voltage   | V    | 16-bit register       | **1.25 µV** per LSB              | ~5–6 digits (e.g. 0.000 040 V = 40 µV)    |
| Bus voltage     | V    | 16-bit, 0–36 V        | **1.25 mV** per LSB (36 V / 2^16)| ~4 digits (e.g. 12.34 V)                  |
| Current         | A    | Derived: V_shunt / R  | **1.25 µV / R_shunt**             | Depends on R; 3–4 decimals typical        |
| Power           | W    | Computed: V × I       | Product of V and I resolution    | Typically 3–4 significant figures        |
| Energy          | Wh   | **Not available**     | —                                | Library returns 0; no energy register     |
| Temperature     | °C   | **Not available**     | —                                | Library returns 0                          |

- **Units:** A, V, W (SI). No Wh or °C.
- **Averaging:** 1, 4, 16, 64, 128, 256, 512, or 1024 samples.

### INA219 (12-bit ADC)

| Quantity        | Unit | Register / source      | LSB (resolution) | Effective precision / significant figures |
|-----------------|------|------------------------|----------------------------------|-------------------------------------------|
| Shunt voltage   | V    | 12-bit, ±40 mV (or ±320 mV with PGA) | **~10 µV** (40 mV / 4096)   | ~4 digits in V (e.g. 0.0123 V)            |
| Bus voltage     | V    | 13-bit, 0–16 V or 32 V | **4 mV** per LSB (16 V / 4096)  | ~3–4 digits (e.g. 12.34 V)                |
| Current         | A    | From calibration register × shunt  | Depends on cal and R       | Typically 2–3 decimals                      |
| Power           | W    | Computed                               | Product of V and I         | Typically 2–3 significant figures         |
| Energy          | Wh   | **Not available**     | —                                | Library returns 0                          |
| Temperature     | °C   | **Not available**     | —                                | Library returns 0                          |

- **Units:** A, V, W (SI). No Wh or °C.
- **ADC options:** 9–12 bit, 1–16 samples (setShuntADC/setBusADC); more samples improve noise, not nominal LSB.

### Summary: sensor output types and meaningful digits

- **Current (A):** float. INA228/226 can justify **3–4 decimals** (e.g. 1.234 A) depending on R and averaging; INA219 typically **2–3 decimals**.
- **Voltage (V):** float. INA228 **2–3 decimals** (0.2 mV LSB); INA226 **2 decimals** (1.25 mV LSB); INA219 **2 decimals** (4 mV LSB).
- **Power (W):** float. **2–3 significant figures** typical for all; display 1 decimal is usually sufficient.
- **Energy (Wh):** double only on **INA228**; INA226/219 return 0. Display 1 decimal Wh, 2–3 for kWh.
- **Temperature (°C):** float only on **INA228**; 1 decimal is meaningful (±1 °C typical).

The **display** formatting in the rest of this doc is chosen to match these sensor limits and keep the UI readable.

---

## Monitor screen (main dashboard)

**Units:** Always **A**, **V**, **W**, **Wh** (and kWh when ≥1000 Wh). No mA/mW/mWh on the display.

**Decimals:** **Magnitude-based** so small values get more decimals, large values get fewer (no fake precision). Current and voltage are capped at **3 decimals** (milliamps, millivolts). INA228 uses 4 significant figures, INA226/219 use 3.

| Metric     | Behaviour | Where set |
|-----------|-----------|-----------|
| **Current**   | A; magnitude-based, max 3 decimals (mA) | `update_timer_cb` → label_current |
| **Voltage**   | V; magnitude-based, max 3 decimals (mV) | `update_timer_cb` → label_voltage |
| **Power**     | W; decimals from magnitude (max 3) | `update_timer_cb` → label_power |
| **Energy**    | Wh; decimals from magnitude; ≥1000 Wh → `%.2f kWh` | `update_timer_cb` → label_energy |
| **Temperature** | °C, 1 decimal (INA228 only) | `update_timer_cb` → label_status |

---

## History popup (graph Y-scale label)

**Units:** Always **V**, **A**, **W**, **Wh**. **Decimals:** From the range magnitude; current and voltage capped at 3 (mA/mV), power/energy up to 4.

---

## Settings / calibration screens

| Context | Metric | Unit | Current format | Where | Meaningful |
|---------|--------|------|----------------|-------|------------|
| Shunt calibration | Max current | A | `%.1f A` | `update_shunt_labels` | **1 decimal** (e.g. 50.0 A) is enough for config. |
| Shunt calibration | Shunt resistance | mΩ | `%.3f mOhm` | `update_shunt_labels` | **3 decimals** (e.g. 1.500 mΩ) matches typical 75 mV/50 A = 1.5 mΩ and calibration. |
| Known-load flow | Known current | A | `%.2f A` | known load UI | **2 decimals** for reference current. |
| Known-load flow | Known voltage | V | `%.2f V` | known load UI | **2 decimals** for reference voltage. |
| Known-load flow | Measured I / V | A, V | `%.*f A / %.*f V` from magnitude (sig 4/3 by sensor) | `update_timer_cb` (known_load) | Same adaptive decimals as monitor. |
| Known-load flow | Corrected result | A, mΩ | `%.2f A / %.3f mOhm` | `update_timer_cb` | 2 decimals A, 3 mΩ. |
| Custom shunt (mV/A) | Shunt voltage | mV | `%.1f mV` | calc_mv UI | **1 decimal** (e.g. 75.0 mV) is enough. |
| Custom shunt (mV/A) | Current | A | `%.1f A` | calc_mv UI | **1 decimal** for nominal current. |
| Custom shunt (mV/A) | Result R = V/I | mΩ | `%.3f mOhm` | calc_mv UI | **3 decimals** for derived shunt resistance. |

---

## Summary: magnitude-based decimals, units A/V/W/Wh

- **Units:** Always **A**, **V**, **W**, **Wh** (and kWh when ≥1000 Wh). Small values are readable because we show **enough decimals**, not by switching to mA/mW/mWh.

- **Decimals:** Magnitude-based; **current and voltage** capped at **3 decimals** (milliamps, millivolts). Power and energy use the same logic with higher max where needed. INA228 uses 4 sig figs, INA226/219 use 3.

- **Temperature (°C):** 1 decimal (INA228 only).

- **Resistance (mΩ):** 3 decimals for shunt resistance.

- **Shunt voltage (mV):** 1 decimal.
