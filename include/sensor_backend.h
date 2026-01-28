/**
 * @file sensor_backend.h
 * Internal backend API for INA* drivers. Used by sensor.cpp for detection and dispatch.
 * Each driver (sensor_ina228.cpp, sensor_ina226.cpp, sensor_ina219.cpp) implements
 * Begin(addr), GetCurrent(), etc. and is probed by sensor.cpp via device ID or begin().
 */
#ifndef SENSOR_BACKEND_H
#define SENSOR_BACKEND_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* INA228: TI register map - Manufacturer 0x3E, Device ID 0x3F */
bool INA228_Probe(uint8_t i2c_addr);
bool INA228_Begin(uint8_t i2c_addr);
float INA228_GetCurrent(void);
float INA228_GetBusVoltage(void);
float INA228_GetPower(void);
double INA228_GetWattHour(void);
float INA228_GetTemperature(void);
bool  INA228_IsConnected(void);
int   INA228_SetShunt(float maxCurrent_A, float shunt_Ohm);
void  INA228_ResetEnergy(void);
void  INA228_CycleAveraging(void);
const char *INA228_GetAveragingString(void);
const char *INA228_GetDriverName(void);

/* INA226: TI register map - Manufacturer 0xFE, Die ID 0xFF */
bool INA226_Probe(uint8_t i2c_addr);
bool INA226_Begin(uint8_t i2c_addr);
float INA226_GetCurrent(void);
float INA226_GetBusVoltage(void);
float INA226_GetPower(void);
double INA226_GetWattHour(void);
float INA226_GetTemperature(void);
bool  INA226_IsConnected(void);
int   INA226_SetShunt(float maxCurrent_A, float shunt_Ohm);
void  INA226_ResetEnergy(void);
void  INA226_CycleAveraging(void);
const char *INA226_GetAveragingString(void);
const char *INA226_GetDriverName(void);

/* INA219: no device ID; try INA219_Begin(addr) when INA228/INA226 not detected */
bool INA219_Begin(uint8_t i2c_addr);
float INA219_GetCurrent(void);
float INA219_GetBusVoltage(void);
float INA219_GetPower(void);
double INA219_GetWattHour(void);
float INA219_GetTemperature(void);
bool  INA219_IsConnected(void);
int   INA219_SetShunt(float maxCurrent_A, float shunt_Ohm);
void  INA219_ResetEnergy(void);
void  INA219_CycleAveraging(void);
const char *INA219_GetAveragingString(void);
const char *INA219_GetDriverName(void);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_BACKEND_H */
