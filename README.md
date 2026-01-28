# CYD Smart Shunt

ESP32 Cheap Yellow Display (CYD) based **DC current + voltage meter** with a touch UI, designed around an **INA228 50A/75mV shunt** (and compatible INA-family parts).

## Hardware

- **Display**: ESP32-2432S028R (Cheap Yellow Display)
  - 320x240 2.8" LCD Display
  - Resistive Touch Screen (XPT2046)
  - ESP32 with WiFi and Bluetooth
- **Shunt / sensor**:
  - INA228-based 50A/75mV 0.5% accuracy shunt
  - I2C sensor support: **INA228 / INA226 / INA219** (auto-detected)

## Project Setup

### Prerequisites

- PlatformIO installed
- CH340 USB driver (if needed) - [Sparkfun Guide](https://learn.sparkfun.com/tutorials/how-to-install-ch340-drivers/all)

### Libraries

The project uses PlatformIO's library manager with shared library directory:
- `TFT_eSPI` - Display library
- `XPT2046_Touchscreen` - Touch screen library

Libraries are installed to: `${USERPROFILE}/.platformio/libdeps-shared` (Windows)

### Build and Upload

```bash
# Build the project
pio run

# Upload to device
pio run -t upload

# Monitor serial output
pio device monitor
```

## Current Status

✅ **LVGL dashboard + touch + sensor stack** working

The current firmware demonstrates:
- LVGL dashboard (Current / Voltage, Power + Energy on one line, Temperature)
- Long-press **Energy** on the dashboard to reset the accumulated energy/charge (with confirmation)
- Touch calibration stored in NVS and reloaded on boot
- Sensor auto-detection and reading (INA228/INA226/INA219)
- Shunt calibration tools (standard shunts + known-load calibration)

## Next Steps

- [ ] Improve dashboard polish (icons, formatting, smoothing, error states)
- [ ] Data page: min/max, session stats, export/logging
- [ ] Alarms: over-current / under-voltage, configurable thresholds
- [ ] Better shunt calibration workflow + persistence/versioning

## Future enhancements (Victron SmartShunt emulation)

The longer-term goal is to emulate a Victron SmartShunt-style data source so other apps can consume the readings:

- [ ] **Serial output** compatible with Victron-style shunt telemetry
- [ ] **BLE / GATT broadcasting** in a format that *VictronConnect might be able to read*

## Pin Reference

### Display (HSPI)
- MISO: GPIO 12
- MOSI: GPIO 13
- SCK: GPIO 14
- CS: GPIO 15
- DC: GPIO 2
- RST: Connected to ESP32 RST
- Backlight: GPIO 21

### Touch Screen (VSPI)
- IRQ: GPIO 36
- MOSI: GPIO 32
- MISO: GPIO 39
- CLK: GPIO 25
- CS: GPIO 33

### Available GPIO for INA228 (I2C)
- SDA: GPIO 22 (CN1 connector)
- SCL: GPIO 27 (CN1 connector)
- 3.3V and GND available on CN1 connector

## References

- [ESP32-Cheap-Yellow-Display Repository](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display)
- [TFT_eSPI Library](https://github.com/Bodmer/TFT_eSPI)
- [XPT2046 Touchscreen Library](https://github.com/PaulStoffregen/XPT2046_Touchscreen)
