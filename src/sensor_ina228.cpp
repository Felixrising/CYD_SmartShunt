/**
 * @file sensor_ina228.cpp
 * Sensor backend implementation for INA228 (RobTillaart/INA228).
 */
#include "sensor_backend.h"
#include <INA228.h>
#include <Wire.h>

static INA228 *s_ina228 = nullptr;
static uint8_t s_averaging = INA228_16_SAMPLES;

bool INA228_Probe(uint8_t i2c_addr) {
  (void)i2c_addr;
  return false; /* Detection done in sensor.cpp via device ID */
}

bool INA228_Begin(uint8_t i2c_addr) {
  if (s_ina228) {
    delete s_ina228;
    s_ina228 = nullptr;
  }
  s_ina228 = new INA228(i2c_addr);
  if (!s_ina228->begin()) {
    delete s_ina228;
    s_ina228 = nullptr;
    return false;
  }
  s_ina228->setMode(INA228_MODE_CONT_TEMP_BUS_SHUNT);
  s_ina228->setAverage(s_averaging);
  s_ina228->setBusVoltageConversionTime(INA228_1052_us);
  s_ina228->setShuntVoltageConversionTime(INA228_1052_us);
  s_ina228->setTemperatureConversionTime(INA228_1052_us);
  s_ina228->setTemperatureCompensation(true);
  return true;
}

float INA228_GetCurrent(void)           { return s_ina228 ? s_ina228->getCurrent() : 0.0f; }
float INA228_GetBusVoltage(void)        { return s_ina228 ? s_ina228->getBusVoltage() : 0.0f; }
float INA228_GetPower(void)             { return s_ina228 ? s_ina228->getPower() : 0.0f; }
double INA228_GetWattHour(void)         { return s_ina228 ? s_ina228->getWattHour() : 0.0; }
float INA228_GetTemperature(void)       { return s_ina228 ? s_ina228->getTemperature() : 0.0f; }
bool INA228_IsConnected(void)           { return s_ina228 && s_ina228->isConnected(); }

int INA228_SetShunt(float maxCurrent_A, float shunt_Ohm) {
  return s_ina228 ? s_ina228->setMaxCurrentShunt(maxCurrent_A, shunt_Ohm) : -1;
}

void INA228_ResetEnergy(void) {
  if (s_ina228) {
    s_ina228->setAccumulation(1);
    delay(100);
    s_ina228->setAccumulation(0);
  }
}

void INA228_CycleAveraging(void) {
  if (!s_ina228) return;
  switch (s_averaging) {
    case INA228_1_SAMPLE:     s_averaging = INA228_4_SAMPLES;   break;
    case INA228_4_SAMPLES:    s_averaging = INA228_16_SAMPLES;  break;
    case INA228_16_SAMPLES:   s_averaging = INA228_64_SAMPLES;  break;
    case INA228_64_SAMPLES:   s_averaging = INA228_128_SAMPLES; break;
    case INA228_128_SAMPLES:  s_averaging = INA228_256_SAMPLES; break;
    case INA228_256_SAMPLES:  s_averaging = INA228_512_SAMPLES; break;
    case INA228_512_SAMPLES:  s_averaging = INA228_1024_SAMPLES; break;
    default:                  s_averaging = INA228_1_SAMPLE;    break;
  }
  s_ina228->setAverage(s_averaging);
}

const char *INA228_GetAveragingString(void) {
  switch (s_averaging) {
    case INA228_1_SAMPLE:     return "1 Sample";
    case INA228_4_SAMPLES:    return "4 Samples";
    case INA228_16_SAMPLES:   return "16 Samples";
    case INA228_64_SAMPLES:   return "64 Samples";
    case INA228_128_SAMPLES:  return "128 Samples";
    case INA228_256_SAMPLES:  return "256 Samples";
    case INA228_512_SAMPLES:  return "512 Samples";
    case INA228_1024_SAMPLES: return "1024 Samples";
    default:                  return "Unknown";
  }
}

const char *INA228_GetDriverName(void) {
  return "INA228";
}
