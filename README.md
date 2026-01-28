# CYD Smart Shunt

An ESP32 Cheap Yellow Display (CYD) based **DC current & voltage meter** with a modern LVGL touch UI and an **INA228 50A/75mV shunt**, plus support for other INA-family devices.

### What it does today

- **Dashboard**
  - Large **Current** and **Voltage** tiles
  - **Power** and **Energy** on the same row
  - **Temperature** readout
  - Long‑press **Energy** to reset accumulated energy/charge (with confirmation)
- **Sensors**
  - I2C auto‑detection for **INA228 / INA226 / INA219**
  - Per‑device backends behind a common `Sensor` API
- **Calibration & UX**
  - Touchscreen calibration stored in NVS and restored on boot
  - Shunt calibration: standard shunt list + known‑load calibration
  - Value entry with digit cursor, coarse/fine steps, and keypad fallback

## Hardware

- **Display**: ESP32‑2432S028R (“Cheap Yellow Display”)
  - 320×240 2.8" LCD (ILI9341)
  - Resistive touch (XPT2046)
  - ESP32 with Wi‑Fi + Bluetooth
- **Shunt / sensor**:
  - INA228‑based 50A/75mV 0.5% accuracy shunt
  - I2C sensor support: **INA228 / INA226 / INA219** (auto‑detected)
  - The linked INA2xx breakout boards ship with small onboard shunts  
    (typically **R002**, **R015** for INA238, and **R100**) that are fine for low/medium currents on the PCB itself;  
    for **higher current work you should use an external shunt** such as the CG FL‑2C block.  
    This requires **removing/desoldering the onboard shunt resistor and wiring the external shunt into its Kelvin pads** on the module.

## Getting started

### Prerequisites

- PlatformIO installed
- Appropriate USB‑UART driver for your CYD variant (e.g. CH340, CP210x, or built‑in OS driver; check your board/OS docs)

### Build and upload

```bash
# Build the project
pio run

# Upload to device
pio run -t upload

# Monitor serial output
pio device monitor
```

## Roadmap

- **UI / logging / alarms**
  - Dashboard polish (icons, formatting, smoothing, clearer error states)
  - Data page: min/max, session stats, export/logging
  - Alarms: over‑current / under‑voltage, configurable thresholds
  - Shunt calibration UX + persistence/versioning
- **Victron SmartShunt–style integration**
  - Serial output compatible with Victron‑style shunt telemetry
  - BLE / GATT broadcasting in a format that *VictronConnect might be able to read*

## Pin reference

### Display (HSPI)
- MISO: GPIO 12
- MOSI: GPIO 13
- SCK: GPIO 14
- CS: GPIO 15
- DC: GPIO 2
- RST: Connected to ESP32 RST
- Backlight: GPIO 21

### Touch screen (VSPI)
- IRQ: GPIO 36
- MOSI: GPIO 32
- MISO: GPIO 39
- CLK: GPIO 25
- CS: GPIO 33

### INA2xx shunt (I2C on CN1)
- SDA: GPIO 22
- SCL: GPIO 27
- 3.3V and GND available on CN1

## Wiring notes

This project is designed around a **high‑side shunt** (in the positive line), which is how the INA228/226/219 devices are intended to be used in the default configuration.  
You can also use the same hardware **low‑side** (in the negative return), but be aware of the different ground‑reference and noise implications in your system.

> Replace the image paths below with your own photos/diagrams placed in an `images/` folder in the repo.

![CYD Smart Shunt wiring – high side](images/cyd-smart-shunt-high-side.png)

![CYD Smart Shunt wiring – module + external shunt](images/cyd-smart-shunt-module-external-shunt.png)

## Hardware links (affiliate)

- **Shunt (50A/75mV selected; other ranges work too)**: [`https://s.click.aliexpress.com/e/_c2xnLz9D`](https://s.click.aliexpress.com/e/_c2xnLz9D)
- **INA226 / INA228 / INA238 module**: [`https://s.click.aliexpress.com/e/_c3se5pX9`](https://s.click.aliexpress.com/e/_c3se5pX9)
- **CYD 2.8" ESP32‑2432S028R (ILI9341 + XPT2046)**: [`https://s.click.aliexpress.com/e/_c3MhOaqP`](https://s.click.aliexpress.com/e/_c3MhOaqP)

## References

- [ESP32‑Cheap‑Yellow‑Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display)
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)
- [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen)
