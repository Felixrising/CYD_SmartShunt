#include "telemetry_victron.h"

#include <math.h>

/**
 * NOTE ON UART SELECTION
 *
 * For now we use Serial1 as the VE.Direct UART on the ESP32.
 * You can change the pins here to match your wiring.
 */
static HardwareSerial &VE_UART = Serial1;

// ESP32 Serial1 pins (single source of truth for Integration settings display).
// CYD / many ESP32 boards: UART1 = RX 16, TX 17. Use -1 for core default.
static constexpr int VE_UART_RX_PIN = 16;
static constexpr int VE_UART_TX_PIN = 17;

// This is a SmartShunt 500A (Victron-style product id)
static const uint16_t PID      = 0xA389;
// Firmware/App ID as seen by Victron apps (same as reference)
static const uint16_t AppId    = 0x0419;   // 0b0000010000011001

// Basic text/hex update pacing (ms)
static const unsigned long UPDATE_INTERVAL_MS = 1000;
static const unsigned long UART_TIMEOUT_MS   = 900;

// Runtime enable flag (set from Integration settings / NVS).
static bool s_victronEnabled = true;

// Minimal "device type" (Victron MON field) — 3 = generic DC system in many examples
static const char s_victronDevice[] = "3";

// Optional custom name (can be set via Hex SET command) — keep small, 0-terminated
static char s_customName[64] = "CYD Smart Shunt";

// ──────────────────────────────────────────────────────────────────────────────
//  Hex protocol helpers (adapted from SmartShuntINA2xx, trimmed for CYD)
// ──────────────────────────────────────────────────────────────────────────────

static unsigned long s_lastHexCmdMillis = 0;

enum HexState {
  HEX_IDLE = 0,
  HEX_COMMAND_PING = 1,
  HEX_COMMAND_APP_VERSION = 3,
  HEX_COMMAND_PRODUCT_ID = 4,
  HEX_COMMAND_RESTART = 6,
  HEX_COMMAND_GET = 7,
  HEX_COMMAND_SET = 8,
  HEX_COMMAND_ASYNC = 0xA,
  HEX_COMMAND_UNKNOWN,
  HEX_NUM_COMMANDS,
  HEX_READ_COMMAND,
  HEX_READ_CHECKSUM,
  HEX_READ_REGISTER,
  HEX_READ_FLAGS,
  HEX_READ_VALUE,
  HEX_READ_DATA,
  HEX_READ_STRING,
  HEX_COMPLETE,
  HEX_EXECUTE
};

enum HexAnswer {
  ANSWER_DONE    = 1,
  ANSWER_UNKNOWN = 3,
  ANSWER_PING    = 5,
  ANSWER_GET     = 7,
  ANSWER_SET     = 8
};

enum HexFlags {
  FLAG_OK            = 0x0,
  FLAG_UNKNOWN_ID    = 0x1,
  FLAG_NOT_SUPPORTED = 0x2,
  FLAG_PARAMETER_ERR = 0x4
};

static void sendHexAnswer(uint8_t *bytes, uint8_t count) {
  uint8_t checksum = bytes[0];
  VE_UART.write(':');
  // First nibble is the command
  VE_UART.printf("%hhX", bytes[0]);
  for (int i = 1; i < count; ++i) {
    checksum += bytes[i];
    VE_UART.printf("%02hhX", bytes[i]);
  }
  checksum = 0x55 - checksum;
  VE_UART.printf("%02hhX", checksum);
  VE_UART.write('\n');
}

using HexCommandFunc = void (*)(uint8_t, uint16_t, uint8_t, uint8_t *, uint8_t);

static void cmdPing(uint8_t, uint16_t, uint8_t, uint8_t *, uint8_t) {
  uint8_t answer[] = { ANSWER_PING, (uint8_t)(AppId & 0xFF), (uint8_t)(AppId >> 8) };
  sendHexAnswer(answer, sizeof(answer));
}

static void cmdAppVersion(uint8_t, uint16_t, uint8_t, uint8_t *, uint8_t) {
  uint8_t answer[] = { ANSWER_DONE, (uint8_t)(AppId & 0xFF), (uint8_t)(AppId >> 8) };
  sendHexAnswer(answer, sizeof(answer));
}

static void cmdProductId(uint8_t, uint16_t, uint8_t, uint8_t *, uint8_t) {
  uint8_t answer[] = { ANSWER_DONE, (uint8_t)(PID & 0xFF), (uint8_t)(PID >> 8) };
  sendHexAnswer(answer, sizeof(answer));
}

static void cmdRestart(uint8_t, uint16_t, uint8_t, uint8_t *, uint8_t) {
  // Not implemented; no-op is sufficient for many Victron hosts.
}

static void cmdGet(uint8_t, uint16_t address, uint8_t, uint8_t *, uint8_t) {
  uint8_t answer[128];
  uint8_t aSize = 4;

  answer[0] = HEX_COMMAND_GET;
  answer[1] = (uint8_t)address;
  answer[2] = (uint8_t)(address >> 8);
  answer[3] = FLAG_OK;

  // Minimal set of GET addresses:
  // 0x010A: serial number (we derive from efuse)
  // 0x010C: custom name
  // 0x0104: group id (single byte)

  switch (address) {
    case 0x010A: {  // serial number
      char serialnr[32];
#if ESP32
      sprintf(serialnr, "%08X", (uint32_t)ESP.getEfuseMac());
#else
      sprintf(serialnr, "%08X", ESP.getChipId());
#endif
      size_t len = strlen(serialnr);
      if (len > sizeof(answer) - 4) len = sizeof(answer) - 4;
      memcpy(answer + 4, serialnr, len);
      aSize = 4 + (uint8_t)len;
      break;
    }
    case 0x010C: {  // custom name
      size_t len = strlen(s_customName);
      if (len > sizeof(answer) - 4) len = sizeof(answer) - 4;
      memcpy(answer + 4, s_customName, len);
      aSize = 4 + (uint8_t)len;
      break;
    }
    case 0x0104: {  // group id
      // u8 – used to group similar devices; we just use 0 for now
      answer[4] = 0;
      aSize     = 5;
      break;
    }
    default:
      // Unknown register; report as such (Victron may simply ignore)
      answer[3] = FLAG_UNKNOWN_ID;
      break;
  }

  sendHexAnswer(answer, aSize);
}

static void cmdSet(uint8_t, uint16_t address, uint8_t, uint8_t *valueBuf, uint8_t valueSize) {
  uint8_t answer[128];

  answer[0] = HEX_COMMAND_SET;
  answer[1] = (uint8_t)address;
  answer[2] = (uint8_t)(address >> 8);
  answer[3] = FLAG_OK;
  memcpy(answer + 4, valueBuf, valueSize);

  switch (address) {
    case 0x010C: {  // custom name (string)
      size_t len = valueSize;
      if (len >= sizeof(s_customName)) len = sizeof(s_customName) - 1;
      memcpy(s_customName, valueBuf, len);
      s_customName[len] = '\0';
      break;
    }
    default:
      answer[3] = FLAG_UNKNOWN_ID;
      break;
  }

  sendHexAnswer(answer, (uint8_t)(valueSize + 4));
}

static void cmdUnknown(uint8_t command, uint16_t, uint8_t, uint8_t *, uint8_t) {
  uint8_t answer[2] = { ANSWER_UNKNOWN, command };
  // Debug only; VE_DIRECT line is separate UART
  // Serial.printf("VE.Direct: unknown command %u\r\n", command);
  sendHexAnswer(answer, sizeof(answer));
}

static HexCommandFunc s_commandHandlers[] = {
    cmdUnknown,    cmdPing,       cmdUnknown,    cmdAppVersion,
    cmdProductId,  cmdUnknown,    cmdRestart,    cmdGet,
    cmdSet,        cmdUnknown,    cmdUnknown};

static inline uint8_t hexCharToInt(char v) {
  return (v > '@') ? ((v & 0xDF) - 'A' + 10) : (v - '0');
}

static bool readHexByte(uint8_t &value) {
  char buf[2];
  int  read = VE_UART.readBytes(buf, 1);
  if (read != 1) return false;

  if (buf[0] < '0') {
    value = (uint8_t)buf[0];
    return true;
  }

  read = VE_UART.readBytes(buf + 1, 1);
  if (read != 1) return false;

  value = (uint8_t)((hexCharToInt(buf[0]) << 4) | hexCharToInt(buf[1]));
  return true;
}

static void victronHexRx(unsigned long now) {
  static HexState status = HEX_IDLE;
  static uint8_t  command;
  static uint16_t address;
  static uint8_t  flags;
  static uint8_t  checksum;
  static uint8_t  currIndex;
  static uint8_t  valueBuffer[64];

  uint8_t inbyte;
  bool    ok;

  while (true) {
    if (status != HEX_IDLE && (now - s_lastHexCmdMillis > UART_TIMEOUT_MS)) {
      status            = HEX_IDLE;
      s_lastHexCmdMillis = 0;
    }

    switch (status) {
      case HEX_IDLE:
        inbyte = VE_UART.read();
        if (inbyte == ':') {
          s_lastHexCmdMillis = now;
          checksum           = 0;
          command            = HEX_COMMAND_UNKNOWN;
          flags              = 0;
          address            = 0;
          status             = HEX_READ_COMMAND;
        }
        return;

      case HEX_READ_COMMAND:
        inbyte   = VE_UART.read();
        command  = hexCharToInt(inbyte);
        checksum += command;
        if (command < HEX_NUM_COMMANDS) {
          status = (HexState)command;
        } else {
          status = HEX_EXECUTE;  // unknown
        }
        break;

      case HEX_COMMAND_PING:
      case HEX_COMMAND_APP_VERSION:
      case HEX_COMMAND_PRODUCT_ID:
      case HEX_COMMAND_RESTART:
        status = HEX_READ_CHECKSUM;
        return;

      case HEX_COMMAND_GET:
      case HEX_COMMAND_SET:
        status = HEX_READ_REGISTER;
        return;

      case HEX_READ_REGISTER:
        ok = readHexByte(valueBuffer[1]);
        if (ok) {
          checksum += valueBuffer[1];
          ok       = readHexByte(valueBuffer[0]);
        }
        if (ok) {
          checksum = (uint8_t)(checksum + valueBuffer[0]);
          address  = (uint16_t)((valueBuffer[0] << 8) | valueBuffer[1]);
          status   = HEX_READ_FLAGS;
        } else {
          status = HEX_IDLE;
        }
        return;

      case HEX_READ_FLAGS:
        ok = readHexByte(flags);
        checksum += flags;
        if (ok) {
          status = (command == HEX_COMMAND_GET ? HEX_READ_CHECKSUM : HEX_READ_VALUE);
        } else {
          status = HEX_IDLE;
        }
        return;

      case HEX_READ_VALUE:
        currIndex = 0;
        status    = HEX_READ_DATA;
        // fall-through

      case HEX_READ_DATA:
        ok = readHexByte(valueBuffer[currIndex]);
        if (ok) {
          if (valueBuffer[currIndex] == '\r') return;
          if (valueBuffer[currIndex] == '\n') {
            // last byte was checksum; drop it
            valueBuffer[--currIndex] = 0;
            if (checksum != 0x55) {
              status = HEX_IDLE;
            } else {
              status = HEX_EXECUTE;
            }
            break;
          }
          checksum += valueBuffer[currIndex++];
          if (currIndex >= sizeof(valueBuffer)) {
            status = HEX_IDLE;
          }
        } else {
          status = HEX_IDLE;
        }
        return;

      case HEX_READ_CHECKSUM:
        ok = readHexByte(inbyte);
        if (!ok || (uint8_t)(checksum + inbyte) != 0x55) {
          status = HEX_IDLE;
        } else {
          status = HEX_COMPLETE;
        }
        return;

      case HEX_COMPLETE:
        inbyte = VE_UART.read();
        if (inbyte == '\r') return;
        if (inbyte == '\n') {
          status = HEX_EXECUTE;
        } else {
          status = HEX_IDLE;
          return;
        }
        break;

      case HEX_EXECUTE:
        if (command < HEX_NUM_COMMANDS) {
          s_commandHandlers[command](command, address, flags, valueBuffer, currIndex);
        } else {
          cmdUnknown(command, address, flags, valueBuffer, currIndex);
        }
        status = HEX_IDLE;
        return;

      case HEX_COMMAND_ASYNC:
      default:
        status = HEX_IDLE;
        return;
    }
  }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Text protocol (VE.Direct "small block")
// ──────────────────────────────────────────────────────────────────────────────

static uint8_t calcTextChecksum(const String &s) {
  uint8_t result = 0;
  for (size_t i = 0; i < s.length(); ++i) {
    result += (uint8_t)s[i];
  }
  return (uint8_t)(256u - result);
}

static void sendTextSmallBlock(const TelemetryState &st) {
  String S = "\r\nPID\t0x" + String(PID, 16);

  int32_t intVal;

  // V in mV
  intVal = (int32_t)lroundf(st.voltage_V * 1000.0f);
  S += "\r\nV\t" + String(intVal);

  // I in mA
  intVal = (int32_t)lroundf(st.current_A * 1000.0f);
  S += "\r\nI\t" + String(intVal);

  // P in W
  intVal = (int32_t)lroundf(st.power_W);
  S += "\r\nP\t" + String(intVal);

  // CE in mAh — approximate from Wh and voltage if possible, else 0
  if (st.voltage_V > 0.1f) {
    float Ah = (float)(st.energy_Wh / st.voltage_V);
    intVal   = (int32_t)lroundf(Ah * 1000.0f);
  } else {
    intVal = 0;
  }
  S += "\r\nCE\t" + String(intVal);

  // SOC in ‰ (per-mille). If NaN, fall back to 0 or 1000 when "full".
  float soc = st.soc_percent;
  if (isnan(soc)) {
    soc = st.sensor_connected ? 100.0f : 0.0f;
  }
  intVal = (int32_t)lroundf(soc * 10.0f);
  S += "\r\nSOC\t" + String(intVal);

  // TTG in minutes; unknown = -1
  S += "\r\nTTG\t-1";

  S += "\r\nAlarm\tOFF";
  S += "\r\nRelay\tOFF";
  S += "\r\nAR\t0";
  S += "\r\nAR\t0";
  S += "\r\nBMV\tCYDSHNT";
  S += "\r\nFW\t" + String(AppId, 16);
  S += "\r\nMON\t" + String(s_victronDevice);
  S += "\r\nChecksum\t";

  uint8_t cs = calcTextChecksum(S);
  VE_UART.write(S.c_str());
  VE_UART.write(cs);
}

// Full VE.Direct history block (H1–H18) for SmartShunt/BMV compatibility.
// Labels per Victron VE.Direct text protocol: H1 deepest discharge, H2 last discharge,
// H3 average discharge, H4 charge cycles, H5 full discharges, H6 last charge,
// H7 total Ah charged, H8 total Ah discharged, H9 total Wh, H10 min V, H11 max V,
// H12 seconds since full, H13–H18 min/max SOC and misc.
static void sendTextHistoryBlock(const TelemetryState &st) {
  int32_t h1 = 0, h2 = 0, h3 = 0, h4 = 0, h5 = 0, h6 = 0;
  int32_t h7 = (int32_t)lround(st.total_Ah_charged * 10.0);   // 0.1 Ah units
  int32_t h8 = (int32_t)lround(st.total_Ah_discharged * 10.0);
  int32_t h9 = (int32_t)lround(st.energy_Wh);
  int32_t h10 = isnan(st.min_voltage_V) ? 0 : (int32_t)lroundf(st.min_voltage_V * 1000.0f);  // mV
  int32_t h11 = isnan(st.max_voltage_V) ? 0 : (int32_t)lroundf(st.max_voltage_V * 1000.0f);
  int32_t h12 = (st.seconds_since_full < 0) ? -1 : st.seconds_since_full;
  int32_t h13 = 0, h14 = 0, h15 = 0, h16 = 0, h17 = 0, h18 = 0;

  String S = "\r\nH1\t" + String(h1) + "\r\nH2\t" + String(h2) + "\r\nH3\t" + String(h3) +
             "\r\nH4\t" + String(h4) + "\r\nH5\t" + String(h5) + "\r\nH6\t" + String(h6) +
             "\r\nH7\t" + String(h7) + "\r\nH8\t" + String(h8) + "\r\nH9\t" + String(h9) +
             "\r\nH10\t" + String(h10) + "\r\nH11\t" + String(h11) + "\r\nH12\t" + String(h12) +
             "\r\nH13\t" + String(h13) + "\r\nH14\t" + String(h14) + "\r\nH15\t" + String(h15) +
             "\r\nH16\t" + String(h16) + "\r\nH17\t" + String(h17) + "\r\nH18\t" + String(h18);
  S += "\r\nChecksum\t";
  uint8_t cs = calcTextChecksum(S);
  VE_UART.write(S.c_str());
  VE_UART.write(cs);
}

// ──────────────────────────────────────────────────────────────────────────────
//  Public API
// ──────────────────────────────────────────────────────────────────────────────

void TelemetryVictronInit() {
  if (!s_victronEnabled) return;

  if (VE_UART_RX_PIN >= 0 && VE_UART_TX_PIN >= 0) {
    VE_UART.begin(19200, SERIAL_8N1, VE_UART_RX_PIN, VE_UART_TX_PIN);
  } else {
    VE_UART.begin(19200, SERIAL_8N1);
  }
}

void TelemetryVictronSetEnabled(bool enabled) {
  s_victronEnabled = enabled;
}

bool TelemetryVictronGetEnabled(void) {
  return s_victronEnabled;
}

void TelemetryVictronGetUartInfo(char *buf, size_t len) {
  if (!buf || len == 0) return;
  if (VE_UART_RX_PIN >= 0 && VE_UART_TX_PIN >= 0) {
    snprintf(buf, len, "Serial1, 19200 8N1, TX:%d RX:%d", VE_UART_TX_PIN, VE_UART_RX_PIN);
  } else {
    snprintf(buf, len, "Serial1, 19200 8N1 (default pins)");
  }
}

void TelemetryVictronUpdate(const TelemetryState &state) {
  if (!s_victronEnabled) return;

  static unsigned long lastSentSmall   = 0;
  static unsigned long lastSentHistory = millis();

  unsigned long now = millis();

  // Pump hex state machine if there is incoming data
  while (VE_UART.available() > 0) {
    victronHexRx(now);
  }

  // If host is actively talking Hex, pause Text frames briefly
  bool stopText = ((s_lastHexCmdMillis > 0) && (now - s_lastHexCmdMillis < UPDATE_INTERVAL_MS));

  if (!stopText && (now - lastSentSmall >= UPDATE_INTERVAL_MS)) {
    sendTextSmallBlock(state);
    lastSentSmall    = now;
    s_lastHexCmdMillis = 0;

    if (now - lastSentHistory >= UPDATE_INTERVAL_MS * 10UL) {
      sendTextHistoryBlock(state);
      lastSentHistory = now;
    }
  }
}

