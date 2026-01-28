# BLE / GATT plan: Victron SmartShunt emulation

**Goal:** Broadcast BLE/GATT in a format compatible with VictronConnect so the CYD Smart Shunt can be discovered and read as a Victron SmartShunt (e.g. for monitoring from a phone).

## Reference

- Victron BLE protocol is documented for SmartShunt (firmware v2.31+); VictronConnect v5.42+ can enable and use it.
- **Service UUID:** `65970000-4bda-4c1e-af4b-551c4cf74769`
- Data is exposed as GATT characteristics (voltage, current, power, energy, SOC, etc.).
- **Keep-alive:** Devices disconnect after ~1 minute without a keep-alive; the app must send periodic keep-alive messages.
- Some devices also use BLE advertising (manufacturer data) for “instant readout”; protocol details may require Victron docs or reverse‑engineering (e.g. manufacturer data PDF, VictronConnect decompilation).

## Tasks (planning only; no implementation yet)

1. **Advertising**
   - Advertise with Victron-like service UUID(s) and local name so VictronConnect can discover the device.
   - Optionally include manufacturer-specific advertising data if required for SmartShunt compatibility.

2. **GATT service and characteristics**
   - Implement the Victron BLE service (`65970000-4bda-4c1e-af4b-551c4cf74769`).
   - Expose characteristics for at least: battery voltage, current, power, (optional) energy, SOC, temperature.
   - Use data formats and units expected by VictronConnect (little-endian, mV/mA/0.1 Ah etc. as per protocol).

3. **Keep-alive**
   - Document or implement the keep-alive mechanism so connected clients (e.g. VictronConnect) do not get disconnected after one minute.

4. **Optional: pairing / bonding**
   - Decide whether to support pairing; some Victron devices use an encryption key obtainable from VictronConnect.

5. **Integration settings**
   - Add “BLE” / “Bluetooth” toggle and optional device name on the Integration settings page (alongside VE.Direct).
   - Persist BLE on/off (and name) in NVS.

## Dependencies

- ESP32 BLE stack (BluetoothSerial and/or NimBLE/Bluedroid GATT server).
- Reference: [Victron BLE protocol announcement](https://community.victronenergy.com/questions/93919/victron-bluetooth-ble-protocol-publication.html), [keshavdv/victron-ble](https://github.com/keshavdv/victron-ble), and any official Victron manufacturer data / GATT specification.

## Status

- **Planning only.** No BLE code in the repo yet; VE.Direct (serial) is implemented first. This document is the placeholder for BLE/GATT work when we start it.
