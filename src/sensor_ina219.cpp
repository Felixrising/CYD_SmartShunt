/**
 * @file sensor_ina219.cpp
 * Sensor backend implementation for INA219 (RobTillaart/INA219).
 * INA219 has no device ID; no energy accumulation or temperature.
 */
#include "sensor_backend.h"
#include <INA219.h>
#include <Wire.h>

static INA219 *s_ina219 = nullptr;
static uint8_t s_averaging = 1; /* INA219 uses setShuntADC/setBusADC; we cycle 1/2/4/8 samples */

bool INA219_Begin(uint8_t i2c_addr) {
  if (s_ina219) {
    delete s_ina219;
    s_ina219 = nullptr;
  }
  s_ina219 = new INA219(i2c_addr);
  if (!s_ina219->begin()) {
    delete s_ina219;
    s_ina219 = nullptr;
    return false;
  }
  s_ina219->setMode(7); /* Shunt+Bus continuous */
  s_ina219->setBusVoltageRange(16);
  s_ina219->setGain(8);
  s_ina219->setShuntADC(0x03); /* 12 bit, 1 sample default */
  s_ina219->setBusADC(0x03);
  return true;
}

float INA219_GetCurrent(void)     { return s_ina219 ? s_ina219->getCurrent() : 0.0f; }
float INA219_GetBusVoltage(void)  { return s_ina219 ? s_ina219->getBusVoltage() : 0.0f; }
float INA219_GetPower(void)       { return s_ina219 ? s_ina219->getPower() : 0.0f; }
double INA219_GetWattHour(void)   { (void)0; return 0.0; }
float INA219_GetTemperature(void) { (void)0; return 0.0f; }
bool INA219_IsConnected(void)     { return s_ina219 && s_ina219->isConnected(); }

int INA219_SetShunt(float maxCurrent_A, float shunt_Ohm) {
  if (!s_ina219) return -1;
  return s_ina219->setMaxCurrentShunt(maxCurrent_A, shunt_Ohm) ? 0 : -1;
}

void INA219_ResetEnergy(void) {
  (void)0;
}

void INA219_CycleAveraging(void) {
  if (!s_ina219) return;
  /* INA219: setShuntADC/setBusADC 0=9bit 1s, 1=10bit 1s, 2=11bit 1s, 3=12bit 1s, 4=12bit 2s, 5=12bit 4s, 6=12bit 8s, 7=12bit 16s */
  static const uint8_t adc[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
  s_averaging = (s_averaging + 1) % 8;
  s_ina219->setShuntADC(adc[s_averaging]);
  s_ina219->setBusADC(adc[s_averaging]);
}

const char *INA219_GetAveragingString(void) {
  static const char *str[] = { "9b 1s", "10b 1s", "11b 1s", "12b 1s", "12b 2s", "12b 4s", "12b 8s", "12b 16s" };
  return str[s_averaging % 8];
}

const char *INA219_GetDriverName(void) {
  return "INA219";
}
