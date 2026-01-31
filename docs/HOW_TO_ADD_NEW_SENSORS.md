# How to add a new sensor (e.g. another INA or compatible chip)

This project uses a **sensor abstraction**: one public API (`sensor.h`) and multiple **backends** (one per chip). The dispatcher in `sensor.cpp` probes I2C addresses, detects which chip is present, and delegates all calls to that backend. To add a new sensor you add a backend and wire it into detection and dispatch.

---

## 1. Backend API contract

Every backend must provide the same logical interface. The UI and main loop only call the public `Sensor*` functions in `sensor.h`; those are implemented in `sensor.cpp` by forwarding to the active backend.

Your new backend (e.g. `sensor_ina229.cpp`) must implement:

| Function | Returns | Notes |
|----------|---------|--------|
| `XXX_Probe(uint8_t i2c_addr)` | `bool` | Optional. Return true if this address looks like your chip (e.g. device ID register). If the chip has no reliable ID, you can skip Probe and use a “looks like” check in the dispatcher (see INA219). |
| `XXX_Begin(uint8_t i2c_addr)` | `bool` | Initialize the device at this I2C address. Return false on failure. |
| `XXX_GetCurrent(void)` | `float` | Current in **amperes** (A). |
| `XXX_GetBusVoltage(void)` | `float` | Bus voltage in **volts** (V). |
| `XXX_GetPower(void)` | `float` | Power in **watts** (W). |
| `XXX_GetWattHour(void)` | `double` | Accumulated energy in **watt‑hours** (Wh). Return 0 if the chip has no energy register. |
| `XXX_GetTemperature(void)` | `float` | Temperature in **°C**. Return 0 if the chip has no temperature sensor. |
| `XXX_IsConnected(void)` | `bool` | True if the device is present and readable. |
| `XXX_SetShunt(float maxCurrent_A, float shuntResistance_Ohm)` | `int` | Set shunt parameters. Return 0 on success. |
| `XXX_ResetEnergy(void)` | `void` | Clear energy accumulation. No‑op if not supported. |
| `XXX_CycleAveraging(void)` | `void` | Cycle to next averaging / ADC setting if supported. |
| `XXX_GetAveragingString(void)` | `const char *` | Short string for UI (e.g. `"16 Samples"`). |
| `XXX_GetDriverName(void)` | `const char *` | Short name for status line and UI (e.g. `"INA229"`). **Must be ASCII.** |

Use the same naming pattern as the existing backends: prefix every symbol with your chip name (e.g. `INA229_`).

---

## 2. Add backend declarations to `include/sensor_backend.h`

Add a block like this (replace `INA229` with your chip name):

```c
/* INA229 (example) */
bool INA229_Probe(uint8_t i2c_addr);
bool INA229_Begin(uint8_t i2c_addr);
float INA229_GetCurrent(void);
float INA229_GetBusVoltage(void);
float INA229_GetPower(void);
double INA229_GetWattHour(void);
float INA229_GetTemperature(void);
bool  INA229_IsConnected(void);
int   INA229_SetShunt(float maxCurrent_A, float shunt_Ohm);
void  INA229_ResetEnergy(void);
void  INA229_CycleAveraging(void);
const char *INA229_GetAveragingString(void);
const char *INA229_GetDriverName(void);
```

If your chip has no device ID, you can omit `Probe` and use only `Begin` (see step 4).

---

## 3. Implement the backend in `src/sensor_ina229.cpp` (or your filename)

- Include `sensor_backend.h` and the library header for your chip.
- Implement every function listed in the contract. For unsupported features (e.g. no energy register), return 0 or false / no‑op as in the table.
- Use the same **units** as the rest of the project: A, V, W, Wh, °C.
- Keep a static pointer to your driver instance (as in `sensor_ina228.cpp`) and initialize it in `XXX_Begin`.

Copy `sensor_ina228.cpp` or `sensor_ina226.cpp` as a template; they show the expected structure and how to call the Rob Tillaart–style APIs.

---

## 4. Wire detection and dispatch in `src/sensor.cpp`

**4.1 Add an enum value**

In the same file, extend the backend enum:

```c
typedef enum {
  SENSOR_NONE = 0,
  SENSOR_INA228,
  SENSOR_INA226,
  SENSOR_INA219,
  SENSOR_INA229   /* new */
} sensor_backend_id_t;
```

**4.2 Add a probe function (if the chip has a device ID)**

If your chip has manufacturer/device ID registers (e.g. TI style), add a function like `probeINA229(uint8_t addr)` that reads those registers and returns true only when they match. Use `readRegister(addr, reg)` (already in `sensor.cpp`) for I2C reads.

**4.3 Run detection in `SensorBegin()`**

Detection order is **first match wins**. Add your probe (and optional “looks like” check) in the same loop, in the order you want (e.g. INA229 before INA228 if they share an address range). Example:

```c
for (uint8_t addr = INA_ADDR_MIN; addr <= INA_ADDR_MAX; addr++) {
  /* existing INA228, INA226, INA219 ... */
  if (probeINA229(addr)) {
    if (INA229_Begin(addr)) {
      s_backend = SENSOR_INA229;
      return true;
    }
  }
}
```

If there is no device ID, use a “looks like” check (e.g. read/write a safe config register) and call `INA229_Begin(addr)` only when that passes, so you don’t mis‑detect other I2C devices.

**4.4 Add dispatch in every `Sensor*` function**

In each of the big `switch (s_backend)` blocks, add a case and call your backend:

```c
case SENSOR_INA229: return INA229_GetCurrent();  /* etc. */
```

Do this for: `SensorGetCurrent`, `SensorGetBusVoltage`, `SensorGetPower`, `SensorGetWattHour`, `SensorGetTemperature`, `SensorIsConnected`, `SensorSetShunt`, `SensorResetEnergy`, `SensorCycleAveraging`, `SensorGetAveragingString`, `SensorGetDriverName`.

---

## 5. Add the library in `platformio.ini`

If your chip uses a third‑party library (e.g. from GitHub), add it under `lib_deps`:

```ini
lib_deps =
  ...
  https://github.com/SomeUser/INA229.git
```

Then in your backend `.cpp` you can `#include <INA229.h>` (or whatever the library provides).

---

## 6. Optional: display precision (significant figures)

The UI uses `SensorGetDriverName()` to choose how many significant figures to show (so we don’t imply more precision than the sensor has). Right now only **INA228** gets 4 significant figures; INA226 and INA219 get 3.

If your new sensor has **higher resolution** (e.g. 20‑bit like INA228), add it to the “high precision” branch in `src/ui_lvgl.cpp`:

- Find `sensor_is_ina228()`. Either extend it to return true for your driver name as well (e.g. `strcmp(drv, "INA229") == 0`), or introduce a small helper (e.g. `sensor_high_precision()`) that returns true for INA228 and INA229, and use that where `sensor_is_ina228()` is used for formatting (monitor labels, history scale, known‑load).

If your sensor has **lower resolution**, you don’t need to change the UI; the existing “non‑INA228” path already uses 3 significant figures and capped decimals (e.g. current/voltage max 3 decimals).

---

## 7. Checklist

- [ ] Backend `.cpp` implements all functions in the contract (A, V, W, Wh, °C).
- [ ] Declarations added to `include/sensor_backend.h`.
- [ ] Enum and `s_backend` assignment in `sensor.cpp` (detection order correct).
- [ ] Every `Sensor*` switch in `sensor.cpp` has a case for your backend.
- [ ] Library (if any) added to `platformio.ini`.
- [ ] Optional: `SensorGetDriverName()` and UI precision (e.g. `sensor_is_ina228()` or equivalent) updated if the new chip has different resolution.
- [ ] Build with `pio run` and test on hardware (or with a stub) to confirm detection and readings.

---

## File reference

| File | Role |
|------|------|
| `src/sensor.h` | Public API; do not change function names/signatures when adding a backend. |
| `include/sensor_backend.h` | Internal backend declarations; add your `XXX_*` declarations here. |
| `src/sensor.cpp` | Detection loop and dispatch; add probe, enum, and switch cases here. |
| `src/sensor_ina228.cpp` (etc.) | Backend implementations; add a new `sensor_<name>.cpp` for your chip. |
| `src/ui_lvgl.cpp` | Uses `SensorGetDriverName()` and `sensor_is_ina228()` for formatting; extend if you need different precision. |
| `platformio.ini` | Add your driver library under `lib_deps` if needed. |
