/**
 * @file sensor.cpp
 * Sensor abstraction dispatcher: detects INA228/INA226/INA219 on I2C and delegates to the matching backend.
 */
#include "sensor.h"
#include "sensor_backend.h"
#include <Wire.h>

/* INA device ID registers (TI standard) */
#define INA228_REG_MFG_ID  0x3E
#define INA228_REG_DEV_ID  0x3F
#define INA226_REG_MFG_ID  0xFE
#define INA226_REG_DEV_ID  0xFF

#define TI_MANUFACTURER_ID 0x5449
#define INA228_DIE_ID      0x0228
#define INA226_DIE_ID      0x0226

/* I2C address range for INA* (pin-selectable) */
#define INA_ADDR_MIN 0x40
#define INA_ADDR_MAX 0x4F

typedef enum {
  SENSOR_NONE = 0,
  SENSOR_INA228,
  SENSOR_INA226,
  SENSOR_INA219
} sensor_backend_id_t;

static sensor_backend_id_t s_backend = SENSOR_NONE;

static uint16_t readRegister(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFFFF;
  if (Wire.requestFrom((uint8_t)addr, (uint8_t)2) != 2) return 0xFFFF;
  uint16_t v = (uint16_t)Wire.read() << 8;
  v |= (uint16_t)Wire.read();
  return v;
}

static bool probeINA228(uint8_t addr) {
  /* INA228 device id register layout:
   * - manufacturer id @ 0x3E should be 0x5449 (TI)
   * - device id @ 0x3F contains DIE_ID in bits [15:4] (per common TI INA228/238 convention)
   * See "sensor detection flow.md" in this repo for rationale (high-confidence ID first).
   */
  uint16_t mfg = readRegister(addr, INA228_REG_MFG_ID);
  if (mfg != TI_MANUFACTURER_ID) return false;
  uint16_t dev = readRegister(addr, INA228_REG_DEV_ID);
  uint16_t die = (uint16_t)((dev >> 4) & 0x0FFF);
  return (die == INA228_DIE_ID);
}

static bool probeINA226(uint8_t addr) {
  uint16_t mfg = readRegister(addr, INA226_REG_MFG_ID);
  uint16_t dev = readRegister(addr, INA226_REG_DEV_ID);
  /* INA226 exposes manufacturer and die id in 0xFE/0xFF in common Arduino libs. */
  if (mfg != TI_MANUFACTURER_ID) return false;
  return ((dev & 0x0FFF) == INA226_DIE_ID);
}

/* INA219 doesn't have the same high-confidence ID scheme in common use.
 * Avoid false positives by doing a light RW sanity check on well-known registers.
 * If it behaves and it wasn't identified as INA228/INA226, we call it INA219.
 */
static bool looksLikeINA219(uint8_t addr) {
  /* INA219: 0x00 config, 0x05 calibration */
  uint16_t cfg = readRegister(addr, 0x00);
  if (cfg == 0xFFFF) return false;

  /* Write a known-safe config and read back (mask lightly due to reserved bits). */
  const uint16_t testCfg = 0x399F; /* BRNG=32V, PGA=/8, BADC/SADC default-ish, continuous */
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)0x00);
  Wire.write((uint8_t)(testCfg >> 8));
  Wire.write((uint8_t)(testCfg & 0xFF));
  if (Wire.endTransmission(true) != 0) return false;

  uint16_t cfg2 = readRegister(addr, 0x00);
  if (cfg2 == 0xFFFF) return false;
  if ( (cfg2 & 0x3FFF) != (testCfg & 0x3FFF) ) return false;

  /* Calibration register should be RW */
  const uint16_t testCal = 0x1000;
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)0x05);
  Wire.write((uint8_t)(testCal >> 8));
  Wire.write((uint8_t)(testCal & 0xFF));
  if (Wire.endTransmission(true) != 0) return false;

  uint16_t cal2 = readRegister(addr, 0x05);
  if (cal2 != testCal) return false;

  return true;
}

bool SensorBegin(void) {
  s_backend = SENSOR_NONE;
  for (uint8_t addr = INA_ADDR_MIN; addr <= INA_ADDR_MAX; addr++) {
    if (probeINA228(addr)) {
      if (INA228_Begin(addr)) {
        s_backend = SENSOR_INA228;
        return true;
      }
    }
    if (probeINA226(addr)) {
      if (INA226_Begin(addr)) {
        s_backend = SENSOR_INA226;
        return true;
      }
    }
    if (looksLikeINA219(addr)) {
      if (INA219_Begin(addr)) {
        s_backend = SENSOR_INA219;
        return true;
      }
    }
  }
  return false;
}

float SensorGetCurrent(void) {
  switch (s_backend) {
    case SENSOR_INA228: return INA228_GetCurrent();
    case SENSOR_INA226: return INA226_GetCurrent();
    case SENSOR_INA219: return INA219_GetCurrent();
    default: return 0.0f;
  }
}

float SensorGetBusVoltage(void) {
  switch (s_backend) {
    case SENSOR_INA228: return INA228_GetBusVoltage();
    case SENSOR_INA226: return INA226_GetBusVoltage();
    case SENSOR_INA219: return INA219_GetBusVoltage();
    default: return 0.0f;
  }
}

float SensorGetPower(void) {
  switch (s_backend) {
    case SENSOR_INA228: return INA228_GetPower();
    case SENSOR_INA226: return INA226_GetPower();
    case SENSOR_INA219: return INA219_GetPower();
    default: return 0.0f;
  }
}

double SensorGetWattHour(void) {
  switch (s_backend) {
    case SENSOR_INA228: return INA228_GetWattHour();
    case SENSOR_INA226: return INA226_GetWattHour();
    case SENSOR_INA219: return INA219_GetWattHour();
    default: return 0.0;
  }
}

float SensorGetTemperature(void) {
  switch (s_backend) {
    case SENSOR_INA228: return INA228_GetTemperature();
    case SENSOR_INA226: return INA226_GetTemperature();
    case SENSOR_INA219: return INA219_GetTemperature();
    default: return 0.0f;
  }
}

bool SensorIsConnected(void) {
  switch (s_backend) {
    case SENSOR_INA228: return INA228_IsConnected();
    case SENSOR_INA226: return INA226_IsConnected();
    case SENSOR_INA219: return INA219_IsConnected();
    default: return false;
  }
}

int SensorSetShunt(float maxCurrent_A, float shuntResistance_Ohm) {
  switch (s_backend) {
    case SENSOR_INA228: return INA228_SetShunt(maxCurrent_A, shuntResistance_Ohm);
    case SENSOR_INA226: return INA226_SetShunt(maxCurrent_A, shuntResistance_Ohm);
    case SENSOR_INA219: return INA219_SetShunt(maxCurrent_A, shuntResistance_Ohm);
    default: return -1;
  }
}

void SensorResetEnergy(void) {
  switch (s_backend) {
    case SENSOR_INA228: INA228_ResetEnergy(); break;
    case SENSOR_INA226: INA226_ResetEnergy(); break;
    case SENSOR_INA219: INA219_ResetEnergy(); break;
    default: break;
  }
}

void SensorCycleAveraging(void) {
  switch (s_backend) {
    case SENSOR_INA228: INA228_CycleAveraging(); break;
    case SENSOR_INA226: INA226_CycleAveraging(); break;
    case SENSOR_INA219: INA219_CycleAveraging(); break;
    default: break;
  }
}

const char *SensorGetAveragingString(void) {
  switch (s_backend) {
    case SENSOR_INA228: return INA228_GetAveragingString();
    case SENSOR_INA226: return INA226_GetAveragingString();
    case SENSOR_INA219: return INA219_GetAveragingString();
    default: return "N/A";
  }
}

const char *SensorGetDriverName(void) {
  switch (s_backend) {
    case SENSOR_INA228: return INA228_GetDriverName();
    case SENSOR_INA226: return INA226_GetDriverName();
    case SENSOR_INA219: return INA219_GetDriverName();
    default: return "INA?";
  }
}
