# BLE / GATT plan (non‑Victron)

**Important:** Victron SmartShunt BLE / Bluetooth advertising uses **AES‑CTR encryption**, so **emulating a SmartShunt over BLE or advertising in a way that VictronConnect will accept is not realistically possible** without Victron’s keys/protocol details. The notes below are kept only as a sketch for *generic* BLE telemetry, **not** for SmartShunt emulation.

## Reference / constraints

- Victron **SmartShunt** BLE / advertising layer is encrypted (AES‑CTR); realistic emulation is off the table.
- We can still use ESP32 BLE for **our own apps / tools** (custom mobile app, Home Assistant bridge, etc.).
- Any BLE we add here should be treated as **generic telemetry**, not as a Victron‑compatible interface.

## Tasks (generic BLE telemetry – planning only)

1. **Advertising**
   - Advertise with a project‑specific name (e.g. `CYD Smart Shunt`).
   - Optionally include simple, non‑encrypted manufacturer data for “instant glance” stats (voltage, current).

2. **GATT service and characteristics**
   - Define our own GATT service UUID (not Victron’s) for shunt telemetry.
   - Expose characteristics for at least: battery voltage, current, power, energy, SOC, temperature.
   - Use straightforward binary formats (e.g. little‑endian integers in mV / mA / 0.1Ah) that are easy to parse.

3. **Keep-alive / connection model**
   - Decide between short, read‑only connections vs. longer sessions with a simple app‑level keep‑alive.

4. **Optional: pairing / bonding**
   - Decide whether we need pairing (e.g. to prevent random phones from connecting) for **our own** app(s).

5. **Integration settings**
   - Add “BLE” / “Bluetooth” toggle and optional device name on the Integration settings page (alongside VE.Direct).
   - Persist BLE on/off (and name) in NVS.

## Dependencies

- ESP32 BLE stack (NimBLE / Bluedroid GATT server).

## Status

- **Planning only.** No BLE code in the repo yet. VE.Direct (serial) remains the primary integration path; BLE is optional and **will not attempt Victron SmartShunt emulation**.
