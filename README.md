# CYD Smart Shunt - ESP32 Cheap Yellow Display Project

DC Shunt Meter project using ESP32 CYD (Cheap Yellow Display) with touch screen and INA228-based 50A/75mV shunt.

## Hardware

- **Display**: ESP32-2432S028R (Cheap Yellow Display)
  - 320x240 2.8" LCD Display
  - Resistive Touch Screen (XPT2046)
  - ESP32 with WiFi and Bluetooth
- **Shunt**: INA228-based 50A/75mV 0.5% accuracy shunt

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

✅ **Hello World + Touch Test** - Basic display and touch functionality working

The current firmware demonstrates:
- Display initialization and text rendering
- Touch screen input detection
- Real-time touch coordinate display

## Next Steps

- [ ] Integrate INA228 I2C communication
- [ ] Implement shunt measurement reading
- [ ] Create UI for displaying current, voltage, power
- [ ] Add data logging capabilities
- [ ] Implement calibration features

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
