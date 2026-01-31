/*******************************************************************
    CYD Smart Shunt - INA228 Monitoring and Settings Interface
    
    Features:
    - Real-time monitoring of current, voltage, power, energy, temperature
    - Touch-based settings panel for calibration and configuration
    - Energy and charge accumulation tracking
    
    Hardware:
    - ESP32-2432S028R (Cheap Yellow Display)
    - INA228-based 50A/75mV shunt (connected via CN1 I2C)
    
    Based on:
    https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display
    https://github.com/RobTillaart/INA228
 *******************************************************************/

#include <SPI.h>
#include <Wire.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "sensor.h"
#include "telemetry_victron.h"
#include "touch.h"
#include "ui_lvgl.h"

// Touch Screen pins (CYD uses non-default SPI pins)
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// I2C pins for INA228 (CN1 connector)
#define I2C_SDA 22
#define I2C_SCL 27
#define INA228_ADDRESS 0x40  // Default address, adjust if A0/A1 pins are configured

// Default shunt specifications (50A/75mV) - can be overridden by calibration
#define DEFAULT_MAX_CURRENT 50.0      // Default maximum current in Amperes
#define DEFAULT_SHUNT_RESISTANCE 0.0015  // Default: 75mV / 50A = 0.0015Ω

// Shunt calibration values (loaded from NVS or defaults)
float maxCurrent = DEFAULT_MAX_CURRENT;
float shuntResistance = DEFAULT_SHUNT_RESISTANCE;

// Display dimensions
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240

// Touch calibration (layout matches TouchCalibration_t in touch.h)
TouchCalibration_t touchCal = {0, 0, 0, 0, false};

// NVS namespace for touch calibration
#define NVS_NAMESPACE "cyd_shunt"
#define NVS_KEY_CALIBRATED "touch_cal"
#define NVS_KEY_XMIN "xmin"
#define NVS_KEY_XMAX "xmax"
#define NVS_KEY_YMIN "ymin"
#define NVS_KEY_YMAX "ymax"

// NVS keys for shunt calibration
#define NVS_KEY_SHUNT_CALIBRATED "shunt_cal"
#define NVS_KEY_MAX_CURRENT "max_current"
#define NVS_KEY_SHUNT_RESISTANCE "shunt_res"

// NVS key for VE.Direct integration (Settings > Integration)
#define NVS_KEY_VEDIRECT_ENABLED "vedirect_enabled"

Preferences preferences;

// Create SPI instance for touch screen (uses VSPI)
SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// Create TFT display instance
TFT_eSPI tft = TFT_eSPI();

// Update interval for Victron telemetry (main loop); display uses LVGL timer (200 ms).
const unsigned long UPDATE_INTERVAL_MS = 500;

// Forward declarations (used by LVGL or setup)
void resetEnergyAccumulation();
void cycleAveraging();
String getAveragingString();
bool loadTouchCalibration();
void saveTouchCalibration();
void performTouchCalibration();
TS_Point calibrateTouchPoint(TS_Point raw);
bool loadShuntCalibration();
void saveShuntCalibration();
float getDefaultMaxCurrent();
float getDefaultShuntResistance();
bool get_vedirect_enabled(void);
void set_vedirect_enabled(bool on);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nCYD Smart Shunt - INA228 Monitor");
  Serial.println("==================================");

  // Initialize TFT display first (needed for calibration)
  Serial.println("Initializing display...");
  tft.init();
  tft.setRotation(1); // Landscape orientation
  tft.fillScreen(TFT_BLACK);
  
  // Initialize touch screen SPI and library
  Serial.println("Initializing touch screen...");
  mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(mySpi);
  ts.setRotation(1); // Landscape orientation
  TouchInit(&ts);

  // Initialize NVS
  Serial.println("Initializing NVS...");
  preferences.begin(NVS_NAMESPACE, false);
  
  // Load touch calibration or perform calibration if not found
  Serial.println("Loading touch calibration...");
  if (!loadTouchCalibration()) {
    Serial.println("No calibration found. Starting calibration...");
    performTouchCalibration();
  } else {
    Serial.println("Touch calibration loaded successfully!");
    Serial.print("X: "); Serial.print(touchCal.xMin); Serial.print(" - "); Serial.println(touchCal.xMax);
    Serial.print("Y: "); Serial.print(touchCal.yMin); Serial.print(" - "); Serial.println(touchCal.yMax);
  }
  TouchSetCalibration(&touchCal);
  
  // Load shunt calibration from NVS
  Serial.println("Loading shunt calibration...");
  if (!loadShuntCalibration()) {
    Serial.println("No shunt calibration found. Using defaults.");
    Serial.print("Default Max Current: "); Serial.print(maxCurrent); Serial.println("A");
    Serial.print("Default Shunt: "); Serial.print(shuntResistance * 1000); Serial.println("mΩ");
  } else {
    Serial.println("Shunt calibration loaded!");
    Serial.print("Max Current: "); Serial.print(maxCurrent); Serial.println("A");
    Serial.print("Shunt: "); Serial.print(shuntResistance * 1000); Serial.println("mΩ");
  }
  
  // Initialize I2C
  Serial.println("Initializing I2C...");
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(100);
  
  // Initialize current/power sensor (INA228 or other INA* via sensor abstraction)
  Serial.println("Initializing sensor...");
  if (!SensorBegin()) {
    Serial.println("Sensor not found - dashboard will show \"N/C\".");
  } else {
    Serial.print(SensorGetDriverName());
    Serial.println(" connected!");
    int result = SensorSetShunt(maxCurrent, shuntResistance);
    if (result != 0) {
      Serial.print("Warning: shunt config failed (code ");
      Serial.print(result);
      Serial.println("). Using defaults.");
    }
  }
  Serial.println("Setup complete!");

  // Initialize Victron VE.Direct: load enable flag from NVS, then start UART if enabled
  {
    bool vedirectOn = preferences.getBool(NVS_KEY_VEDIRECT_ENABLED, true);
    TelemetryVictronSetEnabled(vedirectOn);
    TelemetryVictronInit();
  }

  ui_lvgl_init();
}

void loop() {
  ui_lvgl_poll();

  // Victron VE.Direct: feed latest readings (Victron TEXT mode expects ~1 Hz; we poll at 500 ms, module paces at 1 s)
  static unsigned long lastTelemetryPoll = 0;
  unsigned long now = millis();
  if (now - lastTelemetryPoll >= UPDATE_INTERVAL_MS) {
    TelemetryState t;
    t.voltage_V        = SensorGetBusVoltage();
    t.current_A        = SensorGetCurrent();
    t.power_W          = SensorGetPower();
    t.energy_Wh        = SensorGetWattHour();
    t.temperature_C    = SensorGetTemperature();
    t.sensor_connected = SensorIsConnected();
    TelemetryVictronUpdate(t);
    lastTelemetryPoll = now;
  }

  delay(5);
}

void resetEnergyAccumulation() {
  Serial.println("Resetting energy and charge accumulation...");
  SensorResetEnergy();
}

void cycleAveraging() {
  SensorCycleAveraging();
  Serial.print("Averaging set to: ");
  Serial.println(SensorGetAveragingString());
}

String getAveragingString() {
  return String(SensorGetAveragingString());
}

float getDefaultMaxCurrent() {
  return DEFAULT_MAX_CURRENT;
}

float getDefaultShuntResistance() {
  return DEFAULT_SHUNT_RESISTANCE;
}

bool loadTouchCalibration() {
  // Check if calibration exists
  if (!preferences.getBool(NVS_KEY_CALIBRATED, false)) {
    return false;
  }
  
  // Load calibration values
  touchCal.xMin = preferences.getInt(NVS_KEY_XMIN, 0);
  touchCal.xMax = preferences.getInt(NVS_KEY_XMAX, 0);
  touchCal.yMin = preferences.getInt(NVS_KEY_YMIN, 0);
  touchCal.yMax = preferences.getInt(NVS_KEY_YMAX, 0);
  
  // Validate calibration values
  if (touchCal.xMin >= touchCal.xMax || touchCal.yMin >= touchCal.yMax) {
    Serial.println("Invalid calibration data!");
    return false;
  }
  
  touchCal.isValid = true;
  return true;
}

void saveTouchCalibration() {
  preferences.putBool(NVS_KEY_CALIBRATED, true);
  preferences.putInt(NVS_KEY_XMIN, touchCal.xMin);
  preferences.putInt(NVS_KEY_XMAX, touchCal.xMax);
  preferences.putInt(NVS_KEY_YMIN, touchCal.yMin);
  preferences.putInt(NVS_KEY_YMAX, touchCal.yMax);
  Serial.println("Touch calibration saved to NVS");
}

void performTouchCalibration() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  
  // Calibration points (corners of the display)
  struct CalPoint {
    int16_t x, y;
    const char* label;
  };
  
  CalPoint calPoints[] = {
    {20, 20, "Top-Left"},
    {DISPLAY_WIDTH - 20, 20, "Top-Right"},
    {DISPLAY_WIDTH - 20, DISPLAY_HEIGHT - 20, "Bottom-Right"},
    {20, DISPLAY_HEIGHT - 20, "Bottom-Left"}
  };
  
  int16_t rawX[4] = {0};
  int16_t rawY[4] = {0};
  
  // Collect calibration data for each corner
  for (int i = 0; i < 4; i++) {
    // Draw target
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Touch Calibration", DISPLAY_WIDTH / 2, 30, 2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(calPoints[i].label, DISPLAY_WIDTH / 2, 60, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Touch the cross", DISPLAY_WIDTH / 2, 100, 2);
    tft.drawString(String(i + 1) + " of 4", DISPLAY_WIDTH / 2, 120, 2);
    
    // Draw crosshair
    tft.drawLine(calPoints[i].x - 10, calPoints[i].y, calPoints[i].x + 10, calPoints[i].y, TFT_RED);
    tft.drawLine(calPoints[i].x, calPoints[i].y - 10, calPoints[i].x, calPoints[i].y + 10, TFT_RED);
    tft.fillCircle(calPoints[i].x, calPoints[i].y, 3, TFT_RED);
    
    // Wait for touch
    bool touched = false;
    unsigned long startTime = millis();
    while (!touched && (millis() - startTime < 30000)) { // 30 second timeout
      if (ts.tirqTouched() && ts.touched()) {
        TS_Point p = ts.getPoint();
        rawX[i] = p.x;
        rawY[i] = p.y;
        touched = true;
        
        // Visual feedback
        tft.fillCircle(calPoints[i].x, calPoints[i].y, 5, TFT_GREEN);
        delay(500);
        
        Serial.print("Point "); Serial.print(i + 1);
        Serial.print(": Raw X="); Serial.print(rawX[i]);
        Serial.print(", Y="); Serial.println(rawY[i]);
      }
      delay(50);
    }
    
    if (!touched) {
      Serial.println("Calibration timeout!");
      tft.fillScreen(TFT_RED);
      tft.setTextColor(TFT_WHITE, TFT_RED);
      tft.drawString("Calibration", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 - 20, 2);
      tft.drawString("Timeout!", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 + 20, 2);
      delay(3000);
      return;
    }
  }
  
  // Calculate min/max values
  touchCal.xMin = min(min(rawX[0], rawX[1]), min(rawX[2], rawX[3]));
  touchCal.xMax = max(max(rawX[0], rawX[1]), max(rawX[2], rawX[3]));
  touchCal.yMin = min(min(rawY[0], rawY[1]), min(rawY[2], rawY[3]));
  touchCal.yMax = max(max(rawY[0], rawY[1]), max(rawY[2], rawY[3]));
  
  // Add small margin (5%)
  int16_t xMargin = (touchCal.xMax - touchCal.xMin) * 0.05;
  int16_t yMargin = (touchCal.yMax - touchCal.yMin) * 0.05;
  touchCal.xMin -= xMargin;
  touchCal.xMax += xMargin;
  touchCal.yMin -= yMargin;
  touchCal.yMax += yMargin;
  
  touchCal.isValid = true;

  // Save to NVS and notify touch layer (for LVGL indev)
  saveTouchCalibration();
  TouchSetCalibration(&touchCal);

  // Show success message
  tft.fillScreen(TFT_GREEN);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.drawString("Calibration", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 - 20, 2);
  tft.drawString("Complete!", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 + 20, 2);
  delay(2000);
  /* Clear to black before handing back to LVGL so it doesn't see leftover green */
  tft.fillScreen(TFT_BLACK);

  Serial.println("Calibration complete!");
  Serial.print("X: "); Serial.print(touchCal.xMin); Serial.print(" - "); Serial.println(touchCal.xMax);
  Serial.print("Y: "); Serial.print(touchCal.yMin); Serial.print(" - "); Serial.println(touchCal.yMax);
}

TS_Point calibrateTouchPoint(TS_Point raw) {
  TS_Point calibrated;
  
  if (!touchCal.isValid) {
    // Return raw point if calibration not available
    return raw;
  }
  
  // Map raw coordinates to display coordinates
  calibrated.x = map(raw.x, touchCal.xMin, touchCal.xMax, 0, DISPLAY_WIDTH);
  calibrated.y = map(raw.y, touchCal.yMin, touchCal.yMax, 0, DISPLAY_HEIGHT);
  
  // Clamp to display bounds
  calibrated.x = constrain(calibrated.x, 0, DISPLAY_WIDTH - 1);
  calibrated.y = constrain(calibrated.y, 0, DISPLAY_HEIGHT - 1);
  
  // Keep original Z (pressure) value
  calibrated.z = raw.z;
  
  return calibrated;
}

bool loadShuntCalibration() {
  // Check if calibration exists
  if (!preferences.getBool(NVS_KEY_SHUNT_CALIBRATED, false)) {
    return false;
  }
  
  // Load calibration values
  maxCurrent = preferences.getFloat(NVS_KEY_MAX_CURRENT, DEFAULT_MAX_CURRENT);
  shuntResistance = preferences.getFloat(NVS_KEY_SHUNT_RESISTANCE, DEFAULT_SHUNT_RESISTANCE);
  
  // Validate calibration values
  if (maxCurrent <= 0 || maxCurrent > 200 || shuntResistance <= 0 || shuntResistance > 0.1) {
    Serial.println("Invalid shunt calibration data!");
    maxCurrent = DEFAULT_MAX_CURRENT;
    shuntResistance = DEFAULT_SHUNT_RESISTANCE;
    return false;
  }
  
  return true;
}

void saveShuntCalibration() {
  preferences.putBool(NVS_KEY_SHUNT_CALIBRATED, true);
  preferences.putFloat(NVS_KEY_MAX_CURRENT, maxCurrent);
  preferences.putFloat(NVS_KEY_SHUNT_RESISTANCE, shuntResistance);
  Serial.println("Shunt calibration saved to NVS");
  Serial.print("Max Current: "); Serial.print(maxCurrent); Serial.println("A");
  Serial.print("Shunt: "); Serial.print(shuntResistance * 1000); Serial.println("mΩ");
}

bool get_vedirect_enabled(void) {
  return preferences.getBool(NVS_KEY_VEDIRECT_ENABLED, true);
}

void set_vedirect_enabled(bool on) {
  preferences.putBool(NVS_KEY_VEDIRECT_ENABLED, on);
  TelemetryVictronSetEnabled(on);
  if (on)
    TelemetryVictronInit();  /* start UART when enabling at runtime */
}
