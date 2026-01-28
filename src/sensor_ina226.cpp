/**
 * @file sensor_ina226.cpp
 * Sensor backend implementation for INA226 (RobTillaart/INA226).
 * INA226 has no energy accumulation or temperature; those return 0.
 */
#include "sensor_backend.h"
#include <INA226.h>
#include <Wire.h>

static INA226 *s_ina226 = nullptr;
static uint8_t s_averaging = INA226_16_SAMPLES;

bool INA226_Probe(uint8_t i2c_addr) {
  (void)i2c_addr;
  return false; /* Detection done in sensor.cpp via device ID */
}

bool INA226_Begin(uint8_t i2c_addr) {
  if (s_ina226) {
    delete s_ina226;
    s_ina226 = nullptr;
  }
  s_ina226 = new INA226(i2c_addr);
  if (!s_ina226->begin()) {
    delete s_ina226;
    s_ina226 = nullptr;
    return false;
  }
  s_ina226->setMode(7); /* Shunt+Bus continuous */
  s_ina226->setAverage(s_averaging);
  s_ina226->setBusVoltageConversionTime(INA226_1100_us);
  s_ina226->setShuntVoltageConversionTime(INA226_1100_us);
  return true;
}

float INA226_GetCurrent(void)        { return s_ina226 ? s_ina226->getCurrent() : 0.0f; }
float INA226_GetBusVoltage(void)     { return s_ina226 ? s_ina226->getBusVoltage() : 0.0f; }
float INA226_GetPower(void)          { return s_ina226 ? s_ina226->getPower() : 0.0f; }
double INA226_GetWattHour(void)      { (void)0; return 0.0; } /* INA226 has no energy register */
float INA226_GetTemperature(void)    { (void)0; return 0.0f; } /* INA226 has no temperature */
bool INA226_IsConnected(void)        { return s_ina226 && s_ina226->isConnected(); }

int INA226_SetShunt(float maxCurrent_A, float shunt_Ohm) {
  if (!s_ina226) return -1;
  return s_ina226->setMaxCurrentShunt(maxCurrent_A, shunt_Ohm);
}

void INA226_ResetEnergy(void) {
  (void)0; /* INA226 has no energy accumulation */
}

void INA226_CycleAveraging(void) {
  if (!s_ina226) return;
  switch (s_averaging) {
    case INA226_1_SAMPLE:     s_averaging = INA226_4_SAMPLES;   break;
    case INA226_4_SAMPLES:    s_averaging = INA226_16_SAMPLES;  break;
    case INA226_16_SAMPLES:   s_averaging = INA226_64_SAMPLES;  break;
    case INA226_64_SAMPLES:   s_averaging = INA226_128_SAMPLES; break;
    case INA226_128_SAMPLES:  s_averaging = INA226_256_SAMPLES; break;
    case INA226_256_SAMPLES:  s_averaging = INA226_512_SAMPLES; break;
    case INA226_512_SAMPLES:  s_averaging = INA226_1024_SAMPLES; break;
    default:                  s_averaging = INA226_1_SAMPLE;    break;
  }
  s_ina226->setAverage(s_averaging);
}

const char *INA226_GetAveragingString(void) {
  switch (s_averaging) {
    case INA226_1_SAMPLE:     return "1 Sample";
    case INA226_4_SAMPLES:    return "4 Samples";
    case INA226_16_SAMPLES:   return "16 Samples";
    case INA226_64_SAMPLES:   return "64 Samples";
    case INA226_128_SAMPLES:  return "128 Samples";
    case INA226_256_SAMPLES:  return "256 Samples";
    case INA226_512_SAMPLES:  return "512 Samples";
    case INA226_1024_SAMPLES: return "1024 Samples";
    default:                  return "Unknown";
  }
}

const char *INA226_GetDriverName(void) {
  return "INA226";
}
