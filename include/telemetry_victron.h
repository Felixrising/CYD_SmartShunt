/**
 * @file telemetry_victron.h
 * Minimal Victron VE.Direct (Text + Hex) telemetry backend for CYD Smart Shunt.
 *
 * Design:
 * - Pulls values from the main application via a small TelemetryState struct.
 * - Emits:
 *   - VE.Direct Text frames (PID / V / I / P / CE / SOC / TTG / Alarm / Relay / AR / AR / BMV / FW / MON).
 *   - A minimal subset of the Hex protocol (ping, product/app id, basic GET/SET for name/serial),
 *     based on the SmartShuntINA2xx reference implementation.
 *
 * This module is intentionally independent from the sensor abstraction and LVGL UI.
 */

#pragma once

#include <Arduino.h>
#include <stddef.h>

struct TelemetryState {
  float  voltage_V     = 0.0f;
  float  current_A     = 0.0f;
  float  power_W       = 0.0f;
  double energy_Wh     = 0.0;   ///< accumulated energy
  float  temperature_C = 0.0f;
  bool   sensor_connected = false;

  // Optional / roadmap (not yet wired to UI)
  float soc_percent    = NAN;   ///< state-of-charge in %, if known
  float capacity_Ah    = NAN;   ///< nominal capacity in Ah, if configured

  // VE.Direct history block (optional; used for full Text protocol compatibility)
  float  min_voltage_V = NAN;   ///< minimum battery voltage seen (for H10)
  float  max_voltage_V = NAN;   ///< maximum battery voltage seen (for H11)
  double total_Ah_charged   = 0.0;  ///< total Ah charged (H7, 0.1 Ah units)
  double total_Ah_discharged = 0.0; ///< total Ah discharged (H8, 0.1 Ah units)
  int32_t seconds_since_full = -1;  ///< seconds since full charge (H12), -1 = unknown
};

/** Configure the UART and internal state for VE.Direct. Call once from setup(). */
void TelemetryVictronInit();

/**
 * Pump VE.Direct state machine and optionally emit Text/Hex frames.
 * Call frequently (e.g. once per main loop) with latest values.
 */
void TelemetryVictronUpdate(const TelemetryState &state);

/** Enable or disable VE.Direct output (e.g. from Integration settings). */
void TelemetryVictronSetEnabled(bool enabled);

/** Return current VE.Direct enabled state. */
bool TelemetryVictronGetEnabled(void);

/** Fill buf with UART/pin info string (e.g. "Serial1, 19200 8N1, TX:17 RX:16"). */
void TelemetryVictronGetUartInfo(char *buf, size_t len);

