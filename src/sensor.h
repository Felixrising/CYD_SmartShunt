/**
 * @file sensor.h
 * Abstract API for INA* current/power sensors (Rob Tillaart drivers).
 * Auto-detection: INA228, INA226, INA219 are probed on I2C 0x40–0x4F via device ID
 * (INA228/INA226) or begin() (INA219). First match wins. Backends: sensor_ina228.cpp,
 * sensor_ina226.cpp, sensor_ina219.cpp; dispatcher: sensor.cpp.
 */
#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>

/** Call once after Wire.begin(). Returns false if sensor not found. */
bool SensorBegin(void);

/** Readings (latest poll). */
float  SensorGetCurrent(void);
float  SensorGetBusVoltage(void);
float  SensorGetPower(void);
double SensorGetWattHour(void);
float  SensorGetTemperature(void);
bool   SensorIsConnected(void);

/** Shunt config: set max current (A) and shunt resistance (Ω). Returns 0 on success. */
int SensorSetShunt(float maxCurrent_A, float shuntResistance_Ohm);

/** Clear energy/charge accumulation. */
void SensorResetEnergy(void);

/** Averaging: cycle to next profile; string for UI. */
void        SensorCycleAveraging(void);
const char *SensorGetAveragingString(void);

/** Short name for status line, e.g. "INA228". */
const char *SensorGetDriverName(void);

#endif /* SENSOR_H */
