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
#include <INA228.h>
#include "touch.h"
#ifndef USE_LEGACY_UI
#include "ui_lvgl.h"
#endif

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

// Touch areas for screen switching
#define SETTINGS_BUTTON_X 260
#define SETTINGS_BUTTON_Y 10
#define SETTINGS_BUTTON_W 50
#define SETTINGS_BUTTON_H 30

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

Preferences preferences;

// Create SPI instance for touch screen (uses VSPI)
SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// Create TFT display instance
TFT_eSPI tft = TFT_eSPI();

// Create INA228 sensor instance
INA228 ina228(INA228_ADDRESS);

// Screen state
enum ScreenMode {
  SCREEN_MONITORING,
  SCREEN_SETTINGS,
  SCREEN_SHUNT_CALIBRATION
};
ScreenMode currentScreen = SCREEN_MONITORING;

// Update timing
unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 500; // Update every 500ms

// Settings
bool energyAccumulationEnabled = true;
uint8_t averagingSamples = INA228_16_SAMPLES;
uint8_t conversionTime = INA228_1052_us;

// Forward declarations
void displayError(String message);
void drawMonitoringScreen();
void updateMonitoringScreen();
void drawSettingsScreen();
void handleTouch(TS_Point p);
void resetEnergyAccumulation();
void cycleAveraging();
String getAveragingString();
bool loadTouchCalibration();
void saveTouchCalibration();
void performTouchCalibration();
TS_Point calibrateTouchPoint(TS_Point raw);
bool loadShuntCalibration();
void saveShuntCalibration();
void performShuntCalibration();
void drawShuntCalibrationScreen();
float getNumericInput(String prompt, float currentValue, float minVal, float maxVal, String unit);
void performKnownLoadCalibration();
void showStandardValues();
void calculateFromVoltageCurrent();

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
  
  // Initialize INA228
  Serial.println("Initializing INA228...");
  if (!ina228.begin()) {
    Serial.println("ERROR: INA228 not found!");
    displayError("INA228 not found!");
    while(1) delay(1000);
  }
  Serial.println("INA228 connected!");
  
  // Calibrate the sensor with loaded values
  Serial.print("Calibrating INA228 (Max Current: ");
  Serial.print(maxCurrent);
  Serial.print("A, Shunt: ");
  Serial.print(shuntResistance);
  Serial.println("Ω)...");
  
  int result = ina228.setMaxCurrentShunt(maxCurrent, shuntResistance);
  if (result != 0) {
    Serial.print("ERROR: Calibration failed with code: ");
    Serial.println(result);
    displayError("Calibration failed!");
    while(1) delay(1000);
  }
  
  // Configure sensor
  ina228.setMode(INA228_MODE_CONT_TEMP_BUS_SHUNT);
  ina228.setAverage(averagingSamples);
  ina228.setBusVoltageConversionTime(conversionTime);
  ina228.setShuntVoltageConversionTime(conversionTime);
  ina228.setTemperatureConversionTime(conversionTime);
  ina228.setTemperatureCompensation(true);
  
  Serial.println("Setup complete!");

#ifdef USE_LEGACY_UI
  // Draw initial screen (old TFT_eSPI UI)
  drawMonitoringScreen();
#else
  // LVGL UI
  ui_lvgl_init();
#endif
}

void loop() {
#ifdef USE_LEGACY_UI
  // Check for touch input
  if (ts.tirqTouched() && ts.touched()) {
    TS_Point rawPoint = ts.getPoint();
    TS_Point p = calibrateTouchPoint(rawPoint);
    handleTouch(p);
    delay(200); // Debounce delay
  }

  // Update display periodically
  if (millis() - lastUpdate >= UPDATE_INTERVAL) {
    if (currentScreen == SCREEN_MONITORING) {
      updateMonitoringScreen();
    }
    lastUpdate = millis();
  }

  // Handle shunt calibration screen updates
  if (currentScreen == SCREEN_SHUNT_CALIBRATION) {
    // Calibration screen handles its own updates
  }
#else
  // LVGL: tick + timer handler every ~5 ms
  ui_lvgl_poll();
  delay(5);
#endif
}

void handleTouch(TS_Point p) {
  Serial.print("Touch: X=");
  Serial.print(p.x);
  Serial.print(", Y=");
  Serial.println(p.y);
  
  if (currentScreen == SCREEN_MONITORING) {
    // Check if settings button was pressed
    if (p.x >= SETTINGS_BUTTON_X && p.x <= SETTINGS_BUTTON_X + SETTINGS_BUTTON_W &&
        p.y >= SETTINGS_BUTTON_Y && p.y <= SETTINGS_BUTTON_Y + SETTINGS_BUTTON_H) {
      currentScreen = SCREEN_SETTINGS;
      drawSettingsScreen();
    }
  } else if (currentScreen == SCREEN_SETTINGS) {
    // Check for back button (top left)
    if (p.x >= 10 && p.x <= 60 && p.y >= 10 && p.y <= 40) {
      currentScreen = SCREEN_MONITORING;
      drawMonitoringScreen();
    }
    // Check for reset energy button
    else if (p.x >= 10 && p.x <= DISPLAY_WIDTH - 10 && p.y >= 45 && p.y <= 77) {
      resetEnergyAccumulation();
    }
    // Check for recalibrate touch button
    else if (p.x >= 10 && p.x <= DISPLAY_WIDTH - 10 && p.y >= 85 && p.y <= 117) {
      performTouchCalibration();
      drawSettingsScreen(); // Return to settings after calibration
    }
    // Check for averaging selection
    else if (p.x >= 10 && p.x <= DISPLAY_WIDTH - 10 && p.y >= 145 && p.y <= 177) {
      cycleAveraging();
    }
    // Check for shunt calibration button
    else if (p.x >= 10 && p.x <= DISPLAY_WIDTH - 10 && p.y >= 195 && p.y <= 227) {
      performShuntCalibration();
    }
  } else if (currentScreen == SCREEN_SHUNT_CALIBRATION) {
    // Handle calibration screen touches
    // Back button (top left)
    if (p.x >= 10 && p.x <= 60 && p.y >= 6 && p.y <= 26) {
      currentScreen = SCREEN_SETTINGS;
      drawSettingsScreen();
    }
  }
}

void drawMonitoringScreen() {
  tft.fillScreen(TFT_BLACK);
  
  // Header bar with gradient effect
  tft.fillRect(0, 0, DISPLAY_WIDTH, 28, TFT_NAVY);
  tft.drawLine(0, 28, DISPLAY_WIDTH, 28, TFT_CYAN);
  
  // Title - modern, compact
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("CYD Smart Shunt", 8, 6, 2);
  
  // Settings button - modern rounded style
  tft.fillRoundRect(SETTINGS_BUTTON_X, SETTINGS_BUTTON_Y, SETTINGS_BUTTON_W, SETTINGS_BUTTON_H, 4, TFT_DARKGREY);
  tft.drawRoundRect(SETTINGS_BUTTON_X, SETTINGS_BUTTON_Y, SETTINGS_BUTTON_W, SETTINGS_BUTTON_H, 4, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("SET", SETTINGS_BUTTON_X + SETTINGS_BUTTON_W/2, SETTINGS_BUTTON_Y + SETTINGS_BUTTON_H/2, 2);
  
  // Draw static UI elements (dividers, labels)
  // Current section divider
  tft.drawLine(0, 35, DISPLAY_WIDTH, 35, TFT_DARKGREY);
  
  // Labels and values will be drawn in updateMonitoringScreen()
}

void updateMonitoringScreen() {
  // Read sensor values
  float current = ina228.getCurrent();
  float voltage = ina228.getBusVoltage();
  float power = ina228.getPower();
  float temperature = ina228.getTemperature();
  double energy = ina228.getWattHour();
  
  // Clear previous values area (from y=36 to y=DISPLAY_HEIGHT-1)
  tft.fillRect(0, 36, DISPLAY_WIDTH, DISPLAY_HEIGHT - 36, TFT_BLACK);
  
  // === PRIMARY METRICS (Large, prominent) ===
  
  // Current - Primary metric (left side, large)
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("CURRENT", 8, 42, 1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String currentStr = String(abs(current), 3);
  if (current < 0) currentStr = "-" + currentStr;
  tft.drawString(currentStr, 8, 58, 6); // Large numeric font
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("A", 8 + currentStr.length() * 24, 75, 2);
  
  // Voltage - Primary metric (right side, large)
  tft.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  tft.drawString("VOLTAGE", DISPLAY_WIDTH - 8, 42, 1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String voltageStr = String(voltage, 2);
  tft.drawString(voltageStr, DISPLAY_WIDTH - 8, 58, 6); // Large numeric font
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("V", DISPLAY_WIDTH - 8 - voltageStr.length() * 24, 75, 2);
  
  // Divider line
  tft.drawLine(0, 100, DISPLAY_WIDTH, 100, TFT_DARKGREY);
  
  // === SECONDARY METRICS (Compact, organized) ===
  
  // Power - Secondary metric (left column)
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("POWER", 8, 108, 1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String powerStr = String(power, 1);
  tft.drawString(powerStr, 8, 124, 4);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("W", 8 + powerStr.length() * 13, 140, 1);
  
  // Energy - Secondary metric (right column)
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  tft.drawString("ENERGY", DISPLAY_WIDTH - 8, 108, 1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String energyStr;
  String energyUnit;
  if (energy >= 1000.0) {
    energyStr = String(energy / 1000.0, 3);
    energyUnit = "kWh";
  } else {
    energyStr = String(energy, 1);
    energyUnit = "Wh";
  }
  tft.drawString(energyStr, DISPLAY_WIDTH - 8, 124, 4);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(energyUnit, DISPLAY_WIDTH - 8 - energyStr.length() * 13, 140, 1);
  
  // Divider line
  tft.drawLine(0, 160, DISPLAY_WIDTH, 160, TFT_DARKGREY);
  
  // === TERTIARY INFO (Bottom bar) ===
  
  // Temperature - Bottom left
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  String tempStr = String(temperature, 1) + " C";
  tft.drawString(tempStr, 8, DISPLAY_HEIGHT - 20, 1);
  
  // Status indicator - Bottom right (small dot)
  uint16_t statusColor = (ina228.isConnected()) ? TFT_GREEN : TFT_RED;
  tft.fillCircle(DISPLAY_WIDTH - 12, DISPLAY_HEIGHT - 12, 3, statusColor);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  tft.drawString("INA228", DISPLAY_WIDTH - 20, DISPLAY_HEIGHT - 20, 1);
  
  // Serial output for debugging
  Serial.print("I=");
  Serial.print(current, 3);
  Serial.print("A, V=");
  Serial.print(voltage, 2);
  Serial.print("V, P=");
  Serial.print(power, 2);
  Serial.print("W, E=");
  Serial.print(energy, 2);
  Serial.print("Wh, T=");
  Serial.print(temperature, 1);
  Serial.println("C");
}

void drawSettingsScreen() {
  tft.fillScreen(TFT_BLACK);
  
  // Header bar
  tft.fillRect(0, 0, DISPLAY_WIDTH, 28, TFT_NAVY);
  tft.drawLine(0, 28, DISPLAY_WIDTH, 28, TFT_CYAN);
  
  // Title
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("SETTINGS", DISPLAY_WIDTH / 2, 6, 2);
  
  // Back button - modern rounded
  tft.fillRoundRect(10, 6, 50, 20, 3, TFT_DARKGREY);
  tft.drawRoundRect(10, 6, 50, 20, 3, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("BACK", 35, 16, 1);
  
  // Section divider
  tft.drawLine(0, 35, DISPLAY_WIDTH, 35, TFT_DARKGREY);
  
  // Reset Energy button - prominent, warning style
  tft.fillRoundRect(10, 45, DISPLAY_WIDTH - 20, 32, 4, TFT_RED);
  tft.drawRoundRect(10, 45, DISPLAY_WIDTH - 20, 32, 4, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("RESET ENERGY/CHARGE", DISPLAY_WIDTH / 2, 61, 2);
  
  // Recalibrate Touch button
  tft.fillRoundRect(10, 85, DISPLAY_WIDTH - 20, 32, 4, TFT_ORANGE);
  tft.drawRoundRect(10, 85, DISPLAY_WIDTH - 20, 32, 4, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_ORANGE);
  tft.drawString("RECALIBRATE TOUCH", DISPLAY_WIDTH / 2, 101, 2);
  
  // Averaging setting - labeled section
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("ADC AVERAGING", 10, 130, 1);
  tft.fillRoundRect(10, 145, DISPLAY_WIDTH - 20, 32, 4, TFT_DARKGREY);
  tft.drawRoundRect(10, 145, DISPLAY_WIDTH - 20, 32, 4, TFT_CYAN);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  String avgStr = getAveragingString();
  tft.drawString(avgStr, DISPLAY_WIDTH / 2, 161, 2);
  
  // Divider
  tft.drawLine(0, 185, DISPLAY_WIDTH, 185, TFT_DARKGREY);
  
  // Shunt Calibration button
  tft.fillRoundRect(10, 185, DISPLAY_WIDTH - 20, 32, 4, TFT_BLUE);
  tft.drawRoundRect(10, 185, DISPLAY_WIDTH - 20, 32, 4, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.drawString("CALIBRATE SHUNT", DISPLAY_WIDTH / 2, 201, 2);
  
  // Sensor info section
  tft.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("SENSOR INFO", 10, 225, 1);
  
  if (ina228.isConnected()) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    String info = "INA228 @ 0x" + String(INA228_ADDRESS, HEX);
    tft.drawString(info, 10, 240, 1);
    
    // Show calibration status
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    String calibInfo = "Max: " + String(maxCurrent, 1) + "A, Shunt: " + String(shuntResistance * 1000, 2) + "mΩ";
    tft.drawString(calibInfo, 10, 255, 1);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("NOT CONNECTED", 10, 240, 1);
  }
}

void resetEnergyAccumulation() {
  Serial.println("Resetting energy and charge accumulation...");
  ina228.setAccumulation(1); // Clear accumulation registers
  delay(100);
  ina228.setAccumulation(0); // Return to normal operation
  
  // Visual feedback
  tft.fillRoundRect(10, 45, DISPLAY_WIDTH - 20, 32, 4, TFT_GREEN);
  tft.drawRoundRect(10, 45, DISPLAY_WIDTH - 20, 32, 4, TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("RESET COMPLETE", DISPLAY_WIDTH / 2, 61, 2);
  delay(1000);
  
  // Redraw settings screen
  drawSettingsScreen();
}

void cycleAveraging() {
  switch(averagingSamples) {
    case INA228_1_SAMPLE:
      averagingSamples = INA228_4_SAMPLES;
      break;
    case INA228_4_SAMPLES:
      averagingSamples = INA228_16_SAMPLES;
      break;
    case INA228_16_SAMPLES:
      averagingSamples = INA228_64_SAMPLES;
      break;
    case INA228_64_SAMPLES:
      averagingSamples = INA228_128_SAMPLES;
      break;
    case INA228_128_SAMPLES:
      averagingSamples = INA228_256_SAMPLES;
      break;
    case INA228_256_SAMPLES:
      averagingSamples = INA228_512_SAMPLES;
      break;
    case INA228_512_SAMPLES:
      averagingSamples = INA228_1024_SAMPLES;
      break;
    default:
      averagingSamples = INA228_1_SAMPLE;
      break;
  }
  
  ina228.setAverage(averagingSamples);
  Serial.print("Averaging set to: ");
  Serial.println(getAveragingString());
  
  // Redraw settings screen
  drawSettingsScreen();
}

String getAveragingString() {
  switch(averagingSamples) {
    case INA228_1_SAMPLE: return "1 Sample";
    case INA228_4_SAMPLES: return "4 Samples";
    case INA228_16_SAMPLES: return "16 Samples";
    case INA228_64_SAMPLES: return "64 Samples";
    case INA228_128_SAMPLES: return "128 Samples";
    case INA228_256_SAMPLES: return "256 Samples";
    case INA228_512_SAMPLES: return "512 Samples";
    case INA228_1024_SAMPLES: return "1024 Samples";
    default: return "Unknown";
  }
}

void displayError(String message) {
  tft.fillScreen(TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("ERROR", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 - 20, 4);
  tft.drawString(message, DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 + 20, 2);
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

void drawShuntCalibrationScreen() {
  tft.fillScreen(TFT_BLACK);
  
  // Header bar
  tft.fillRect(0, 0, DISPLAY_WIDTH, 26, TFT_NAVY);
  tft.drawLine(0, 26, DISPLAY_WIDTH, 26, TFT_CYAN);
  
  // Title
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("SHUNT CALIBRATION", DISPLAY_WIDTH / 2, 5, 2);
  
  // Back button
  tft.fillRoundRect(10, 5, 50, 18, 3, TFT_DARKGREY);
  tft.drawRoundRect(10, 5, 50, 18, 3, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("BACK", 35, 14, 1);
  
  // Current values display (very compact)
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(String(maxCurrent, 2) + "A / " + String(shuntResistance * 1000, 3) + "mΩ", 10, 32, 1);
  
  // Divider
  tft.drawLine(0, 46, DISPLAY_WIDTH, 46, TFT_DARKGREY);
  
  // Menu buttons (compact, fits on screen)
  int yPos = 50;
  int btnHeight = 24;
  int spacing = 1;
  
  // Calibrate Max Current
  tft.fillRoundRect(10, yPos, DISPLAY_WIDTH - 20, btnHeight, 3, TFT_DARKGREY);
  tft.drawRoundRect(10, yPos, DISPLAY_WIDTH - 20, btnHeight, 3, TFT_CYAN);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("SET MAX CURRENT", DISPLAY_WIDTH / 2, yPos + btnHeight/2, 1);
  yPos += btnHeight + spacing;
  
  // Calibrate Shunt Resistance
  tft.fillRoundRect(10, yPos, DISPLAY_WIDTH - 20, btnHeight, 3, TFT_DARKGREY);
  tft.drawRoundRect(10, yPos, DISPLAY_WIDTH - 20, btnHeight, 3, TFT_CYAN);
  tft.drawString("SET SHUNT RESISTANCE", DISPLAY_WIDTH / 2, yPos + btnHeight/2, 1);
  yPos += btnHeight + spacing;
  
  // Calculate from mV/Current
  tft.fillRoundRect(10, yPos, DISPLAY_WIDTH - 20, btnHeight, 3, TFT_DARKGREY);
  tft.drawRoundRect(10, yPos, DISPLAY_WIDTH - 20, btnHeight, 3, TFT_MAGENTA);
  tft.drawString("CALC FROM mV/CURRENT", DISPLAY_WIDTH / 2, yPos + btnHeight/2, 1);
  yPos += btnHeight + spacing;
  
  // Known Load Calibration
  tft.fillRoundRect(10, yPos, DISPLAY_WIDTH - 20, btnHeight, 3, TFT_DARKGREY);
  tft.drawRoundRect(10, yPos, DISPLAY_WIDTH - 20, btnHeight, 3, TFT_YELLOW);
  tft.drawString("KNOWN LOAD CAL", DISPLAY_WIDTH / 2, yPos + btnHeight/2, 1);
  yPos += btnHeight + spacing;
  
  // Standard Values
  tft.fillRoundRect(10, yPos, DISPLAY_WIDTH - 20, btnHeight, 3, TFT_DARKGREY);
  tft.drawRoundRect(10, yPos, DISPLAY_WIDTH - 20, btnHeight, 3, TFT_ORANGE);
  tft.drawString("STANDARD VALUES", DISPLAY_WIDTH / 2, yPos + btnHeight/2, 1);
  yPos += btnHeight + spacing;
  
  // Reset to Defaults
  tft.fillRoundRect(10, yPos, DISPLAY_WIDTH - 20, btnHeight, 3, TFT_DARKGREY);
  tft.drawRoundRect(10, yPos, DISPLAY_WIDTH - 20, btnHeight, 3, TFT_RED);
  tft.drawString("RESET TO DEFAULTS", DISPLAY_WIDTH / 2, yPos + btnHeight/2, 1);
  yPos += btnHeight + spacing;
  
  // Save & Apply button
  tft.fillRoundRect(10, yPos, DISPLAY_WIDTH - 20, btnHeight, 3, TFT_GREEN);
  tft.drawRoundRect(10, yPos, DISPLAY_WIDTH - 20, btnHeight, 3, TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.drawString("SAVE & APPLY", DISPLAY_WIDTH / 2, yPos + btnHeight/2, 1);
}

float getNumericInput(String prompt, float currentValue, float minVal, float maxVal, String unit) {
  float value = currentValue;
  float step = 0.1; // Default step size
  if (maxVal - minVal > 100) step = 1.0;
  else if (maxVal - minVal > 10) step = 0.5;
  else if (maxVal - minVal > 1) step = 0.1;
  else step = 0.01;
  
  bool editing = true;
  unsigned long lastTouch = 0;
  
  while (editing) {
    // Display current value
    tft.fillScreen(TFT_BLACK);
    
    // Header
    tft.fillRect(0, 0, DISPLAY_WIDTH, 28, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(prompt, DISPLAY_WIDTH / 2, 6, 2);
    
    // Value display (large)
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    String valueStr = String(value, 3) + " " + unit;
    tft.drawString(valueStr, DISPLAY_WIDTH / 2, 60, 6);
    
    // Step size display
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    String stepStr = "Step: " + String(step, 3) + " " + unit;
    tft.drawString(stepStr, DISPLAY_WIDTH / 2, 100, 1);
    
    // Min/Max display
    String rangeStr = String(minVal, 2) + " - " + String(maxVal, 2) + " " + unit;
    tft.drawString(rangeStr, DISPLAY_WIDTH / 2, 115, 1);
    
    // Control buttons
    int btnY = 140;
    int btnH = 35;
    
    // Decrease button (left)
    tft.fillRoundRect(10, btnY, 90, btnH, 4, TFT_RED);
    tft.drawRoundRect(10, btnY, 90, btnH, 4, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("-", 55, btnY + btnH/2, 4);
    
    // Confirm button (center)
    tft.fillRoundRect(110, btnY, 100, btnH, 4, TFT_GREEN);
    tft.drawRoundRect(110, btnY, 100, btnH, 4, TFT_WHITE);
    tft.setTextColor(TFT_BLACK, TFT_GREEN);
    tft.drawString("OK", 160, btnY + btnH/2, 2);
    
    // Increase button (right)
    tft.fillRoundRect(220, btnY, 90, btnH, 4, TFT_BLUE);
    tft.drawRoundRect(220, btnY, 90, btnH, 4, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_BLUE);
    tft.drawString("+", 265, btnY + btnH/2, 4);
    
    // Step size buttons (bottom)
    int stepY = 185;
    int stepH = 25;
    tft.fillRoundRect(10, stepY, 60, stepH, 3, TFT_DARKGREY);
    tft.drawRoundRect(10, stepY, 60, stepH, 3, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("x10", 40, stepY + stepH/2, 1);
    
    tft.fillRoundRect(80, stepY, 60, stepH, 3, TFT_DARKGREY);
    tft.drawRoundRect(80, stepY, 60, stepH, 3, TFT_WHITE);
    tft.drawString("x0.1", 110, stepY + stepH/2, 1);
    
    tft.fillRoundRect(150, stepY, 60, stepH, 3, TFT_DARKGREY);
    tft.drawRoundRect(150, stepY, 60, stepH, 3, TFT_WHITE);
    tft.drawString("RESET", 180, stepY + stepH/2, 1);
    
    // Wait for touch
    if (ts.tirqTouched() && ts.touched() && (millis() - lastTouch > 200)) {
      TS_Point rawPoint = ts.getPoint();
      TS_Point p = calibrateTouchPoint(rawPoint);
      lastTouch = millis();
      
      // Decrease button
      if (p.x >= 10 && p.x <= 100 && p.y >= btnY && p.y <= btnY + btnH) {
        value -= step;
        if (value < minVal) value = minVal;
      }
      // Increase button
      else if (p.x >= 220 && p.x <= 310 && p.y >= btnY && p.y <= btnY + btnH) {
        value += step;
        if (value > maxVal) value = maxVal;
      }
      // Confirm button
      else if (p.x >= 110 && p.x <= 210 && p.y >= btnY && p.y <= btnY + btnH) {
        editing = false;
      }
      // Step x10
      else if (p.x >= 10 && p.x <= 70 && p.y >= stepY && p.y <= stepY + stepH) {
        step *= 10.0;
        if (step > (maxVal - minVal) / 10) step = (maxVal - minVal) / 10;
      }
      // Step x0.1
      else if (p.x >= 80 && p.x <= 140 && p.y >= stepY && p.y <= stepY + stepH) {
        step *= 0.1;
        if (step < 0.001) step = 0.001;
      }
      // Reset to current value
      else if (p.x >= 150 && p.x <= 210 && p.y >= stepY && p.y <= stepY + stepH) {
        value = currentValue;
      }
      
      delay(150);
    }
    
    delay(50);
  }
  
  return value;
}

void showStandardValues() {
  // Standard shunt configurations
  struct StandardShunt {
    float maxCurrent;
    float shuntResistance; // in mΩ
    const char* name;
  };
  
  StandardShunt standards[] = {
    {10.0, 15.0, "10A/150mV"},
    {20.0, 7.5, "20A/150mV"},
    {30.0, 5.0, "30A/150mV"},
    {50.0, 1.5, "50A/75mV"},
    {75.0, 1.0, "75A/75mV"},
    {100.0, 0.75, "100A/75mV"},
    {150.0, 0.5, "150A/75mV"},
    {200.0, 0.375, "200A/75mV"}
  };
  
  int numStandards = sizeof(standards) / sizeof(standards[0]);
  int selected = 0;
  bool selecting = true;
  unsigned long lastTouch = 0;
  
  // Wait for touch to be released before accepting new touches
  delay(300);
  while (ts.touched()) {
    delay(50); // Wait until touch is released
  }
  delay(200); // Additional debounce delay
  
  while (selecting) {
    tft.fillScreen(TFT_BLACK);
    
    // Header
    tft.fillRect(0, 0, DISPLAY_WIDTH, 28, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("STANDARD VALUES", DISPLAY_WIDTH / 2, 6, 2);
    
    // Back button
    tft.fillRoundRect(10, 6, 50, 20, 3, TFT_DARKGREY);
    tft.drawRoundRect(10, 6, 50, 20, 3, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("BACK", 35, 16, 1);
    
    // Display options (scrollable)
    int yStart = 35;
    int itemHeight = 25;
    int visibleItems = 7;
    
    for (int i = 0; i < numStandards && i < visibleItems; i++) {
      int idx = (selected - 3 + i);
      if (idx < 0) idx = 0;
      if (idx >= numStandards) idx = numStandards - 1;
      
      int yPos = yStart + i * itemHeight;
      uint16_t bgColor = (idx == selected) ? TFT_CYAN : TFT_DARKGREY;
      uint16_t textColor = (idx == selected) ? TFT_BLACK : TFT_WHITE;
      
      tft.fillRoundRect(10, yPos, DISPLAY_WIDTH - 20, itemHeight - 2, 2, bgColor);
      tft.setTextColor(textColor, bgColor);
      tft.setTextDatum(TL_DATUM);
      String item = standards[idx].name + String(" (") + String(standards[idx].maxCurrent, 0) + 
                    String("A, ") + String(standards[idx].shuntResistance, 3) + String("mΩ)");
      tft.drawString(item, 15, yPos + (itemHeight - 2)/2 - 6, 1);
    }
    
    // Select button
    tft.fillRoundRect(10, DISPLAY_HEIGHT - 30, DISPLAY_WIDTH - 20, 25, 3, TFT_GREEN);
    tft.drawRoundRect(10, DISPLAY_HEIGHT - 30, DISPLAY_WIDTH - 20, 25, 3, TFT_WHITE);
    tft.setTextColor(TFT_BLACK, TFT_GREEN);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("SELECT", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT - 17, 2);
    
    if (ts.tirqTouched() && ts.touched() && (millis() - lastTouch > 300)) {
      TS_Point rawPoint = ts.getPoint();
      TS_Point p = calibrateTouchPoint(rawPoint);
      lastTouch = millis();
      
      // Back button
      if (p.x >= 10 && p.x <= 60 && p.y >= 6 && p.y <= 26) {
        selecting = false;
        return;
      }
      // Scroll up
      else if (p.y < DISPLAY_HEIGHT - 30 && p.y > 35 && p.x < DISPLAY_WIDTH / 2) {
        selected--;
        if (selected < 0) selected = 0;
      }
      // Scroll down
      else if (p.y < DISPLAY_HEIGHT - 30 && p.y > 35 && p.x >= DISPLAY_WIDTH / 2) {
        selected++;
        if (selected >= numStandards) selected = numStandards - 1;
      }
      // Select
      else if (p.y >= DISPLAY_HEIGHT - 30) {
        maxCurrent = standards[selected].maxCurrent;
        shuntResistance = standards[selected].shuntResistance / 1000.0; // Convert to Ω
        selecting = false;
        return;
      }
      
      delay(150);
    }
    
    delay(50);
  }
}

void performKnownLoadCalibration() {
  float knownCurrent = 10.0; // Known actual current from reference meter
  float knownVoltage = 12.0;  // Known actual voltage from reference meter
  
  bool calibrating = true;
  unsigned long lastTouch = 0;
  unsigned long lastUpdate = 0;
  bool screenDrawn = false;
  float lastMeasuredCurrent = 0;
  float lastMeasuredVoltage = 0;
  
  // Wait for touch to be released before accepting new touches
  delay(300);
  while (ts.touched()) {
    delay(50); // Wait until touch is released
  }
  delay(200); // Additional debounce delay
  
  while (calibrating) {
    // Draw screen once, then only update dynamic values
    if (!screenDrawn) {
      tft.fillScreen(TFT_BLACK);
      
      // Header
      tft.fillRect(0, 0, DISPLAY_WIDTH, 26, TFT_NAVY);
      tft.setTextColor(TFT_WHITE, TFT_NAVY);
      tft.setTextDatum(TC_DATUM);
      tft.drawString("KNOWN LOAD CALIBRATION", DISPLAY_WIDTH / 2, 5, 2);
      
      // Back button
      tft.fillRoundRect(10, 5, 50, 18, 3, TFT_DARKGREY);
      tft.drawRoundRect(10, 5, 50, 18, 3, TFT_WHITE);
      tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("BACK", 35, 14, 1);
      
      // Instructions
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.setTextDatum(TC_DATUM);
      tft.drawString("Apply known load, enter reference values", DISPLAY_WIDTH / 2, 35, 1);
      
      // Section divider
      tft.drawLine(0, 50, DISPLAY_WIDTH, 50, TFT_DARKGREY);
      
      // Known values section header
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.setTextDatum(TL_DATUM);
      tft.drawString("REFERENCE VALUES", 10, 58, 1);
      
      // Known Current - label and value on same line
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.drawString("Current:", 10, 75, 1);
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.setTextDatum(TL_DATUM);
      // (removed - replaced above)
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      String knownCurrStr = String(knownCurrent, 2) + " A";
      tft.drawString(knownCurrStr, 120, 70, 1);
      tft.fillRoundRect(10, 85, DISPLAY_WIDTH - 20, 22, 2, TFT_DARKGREY);
      tft.drawRoundRect(10, 85, DISPLAY_WIDTH - 20, 22, 2, TFT_CYAN);
      tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("SET KNOWN CURRENT", DISPLAY_WIDTH / 2, 96, 1);
      
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.drawString("Known Voltage:", 10, 115, 1);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      String knownVoltStr = String(knownVoltage, 2) + " V";
      tft.drawString(knownVoltStr, 120, 115, 1);
      tft.fillRoundRect(10, 130, DISPLAY_WIDTH - 20, 22, 2, TFT_DARKGREY);
      tft.drawRoundRect(10, 130, DISPLAY_WIDTH - 20, 22, 2, TFT_CYAN);
      tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
      tft.drawString("SET KNOWN VOLTAGE", DISPLAY_WIDTH / 2, 141, 1);
      
      // Measured values (from INA228) - will be updated dynamically
      // Labels only drawn here, values updated in loop
      
      
      // Apply button
      tft.fillRoundRect(10, DISPLAY_HEIGHT - 22, DISPLAY_WIDTH - 20, 20, 2, TFT_GREEN);
      tft.drawRoundRect(10, DISPLAY_HEIGHT - 22, DISPLAY_WIDTH - 20, 20, 2, TFT_WHITE);
      tft.setTextColor(TFT_BLACK, TFT_GREEN);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("APPLY CORRECTIONS", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT - 12, 1);
      
      screenDrawn = true;
    }
    
    // Update dynamic values periodically (only if changed significantly)
    if (millis() - lastUpdate >= 500) {
      lastUpdate = millis();
      
      float measuredCurrent = ina228.getCurrent();
      float measuredVoltage = ina228.getBusVoltage();
      
      // Only update if values changed significantly (to reduce flicker)
      if (abs(measuredCurrent - lastMeasuredCurrent) > 0.01 || abs(measuredVoltage - lastMeasuredVoltage) > 0.01) {
        lastMeasuredCurrent = measuredCurrent;
        lastMeasuredVoltage = measuredVoltage;
        
        // Update known values display
        tft.fillRect(70, 75, 80, 10, TFT_BLACK);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextDatum(TL_DATUM);
        String knownCurrStr = String(knownCurrent, 2) + " A";
        tft.drawString(knownCurrStr, 70, 75, 1);
        
        tft.fillRect(225, 75, 80, 10, TFT_BLACK);
        String knownVoltStr = String(knownVoltage, 2) + " V";
        tft.drawString(knownVoltStr, 225, 75, 1);
        
        // Update measured values
        tft.fillRect(70, 145, 80, 10, TFT_BLACK);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        String measCurrStr = String(measuredCurrent, 3) + " A";
        tft.drawString(measCurrStr, 70, 145, 1);
        
        tft.fillRect(225, 145, 80, 10, TFT_BLACK);
        String measVoltStr = String(measuredVoltage, 2) + " V";
        tft.drawString(measVoltStr, 225, 145, 1);
        
        // Calculate and display correction factors
        if (measuredCurrent != 0 && measuredVoltage != 0) {
          float currentCorrection = knownCurrent / measuredCurrent;
          float voltageCorrection = knownVoltage / measuredVoltage;
          
          // Calculate corrected shunt resistance
          float correctedShunt = shuntResistance * (measuredCurrent / knownCurrent);
          
          // Calculate corrected max current
          float correctedMaxCurrent = maxCurrent * (knownCurrent / measuredCurrent);
          
          // Display correction factors
          tft.fillRect(10, 185, 300, 10, TFT_BLACK);
          tft.setTextColor(TFT_WHITE, TFT_BLACK);
          String corrStr = "I: " + String(currentCorrection, 4) + "  V: " + String(voltageCorrection, 4);
          tft.drawString(corrStr, 10, 185, 1);
          
          // Display calculated values
          tft.fillRect(10, 212, 300, 10, TFT_BLACK);
          tft.setTextColor(TFT_YELLOW, TFT_BLACK);
          String calcStr = String(correctedMaxCurrent, 2) + "A  /  " + String(correctedShunt * 1000, 3) + "mΩ";
          tft.drawString(calcStr, 10, 212, 1);
        } else {
          // Clear correction area if no valid measurement
          tft.fillRect(10, 185, 300, 37, TFT_BLACK);
        }
      }
    }
    
    // Handle touch input
    if (ts.tirqTouched() && ts.touched() && (millis() - lastTouch > 300)) {
      TS_Point rawPoint = ts.getPoint();
      TS_Point p = calibrateTouchPoint(rawPoint);
      lastTouch = millis();
      
      // Back button
      if (p.x >= 10 && p.x <= 60 && p.y >= 5 && p.y <= 23) {
        calibrating = false;
        return;
      }
      // Set Known Current
      else if (p.x >= 10 && p.x <= 155 && p.y >= 90 && p.y <= 112) {
        float newKnown = getNumericInput("KNOWN CURRENT", knownCurrent, 0.1, 200.0, "A");
        knownCurrent = newKnown;
        screenDrawn = false; // Force redraw to show new value
      }
      // Set Known Voltage
      else if (p.x >= 165 && p.x <= 310 && p.y >= 90 && p.y <= 112) {
        float newKnown = getNumericInput("KNOWN VOLTAGE", knownVoltage, 1.0, 100.0, "V");
        knownVoltage = newKnown;
        screenDrawn = false; // Force redraw to show new value
      }
      // Apply Corrections
      else if (p.x >= 10 && p.x <= DISPLAY_WIDTH - 10 && p.y >= DISPLAY_HEIGHT - 22 && p.y <= DISPLAY_HEIGHT - 2) {
        float measuredCurrent = ina228.getCurrent();
        float measuredVoltage = ina228.getBusVoltage();
        
        if (measuredCurrent != 0 && measuredVoltage != 0) {
          // Calculate corrected values
          float correctedShunt = shuntResistance * (measuredCurrent / knownCurrent);
          float correctedMaxCurrent = maxCurrent * (knownCurrent / measuredCurrent);
          
          // Apply corrections
          maxCurrent = correctedMaxCurrent;
          shuntResistance = correctedShunt;
          
          // Show success
          tft.fillScreen(TFT_GREEN);
          tft.setTextColor(TFT_BLACK, TFT_GREEN);
          tft.setTextDatum(MC_DATUM);
          tft.drawString("CORRECTIONS", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 - 20, 2);
          tft.drawString("APPLIED!", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 + 20, 2);
          delay(2000);
          
          calibrating = false;
          return;
        } else {
          // Error - no measurement
          tft.fillScreen(TFT_RED);
          tft.setTextColor(TFT_WHITE, TFT_RED);
          tft.setTextDatum(MC_DATUM);
          tft.drawString("ERROR", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 - 20, 2);
          tft.drawString("No measurement!", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 + 20, 2);
          delay(2000);
          screenDrawn = false; // Force redraw after error
        }
      }
      
      delay(150);
    }
    
    delay(50);
  }
}

void calculateFromVoltageCurrent() {
  float shuntVoltage = 0.075; // Default 75mV
  float current = 50.0; // Default 50A
  
  bool calculating = true;
  unsigned long lastTouch = 0;
  bool screenDrawn = false;
  float lastShuntVoltage = -1;
  float lastCurrent = -1;
  
  // Wait for touch to be released before accepting new touches
  delay(300);
  while (ts.touched()) {
    delay(50); // Wait until touch is released
  }
  delay(200); // Additional debounce delay
  
  while (calculating) {
    // Draw screen once, then only update when values change
    if (!screenDrawn || shuntVoltage != lastShuntVoltage || current != lastCurrent) {
      if (!screenDrawn) {
        tft.fillScreen(TFT_BLACK);
      }
    
    // Header
    tft.fillRect(0, 0, DISPLAY_WIDTH, 28, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("CALCULATE FROM mV/A", DISPLAY_WIDTH / 2, 6, 2);
    
    // Back button
    tft.fillRoundRect(10, 6, 50, 20, 3, TFT_DARKGREY);
    tft.drawRoundRect(10, 6, 50, 20, 3, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("BACK", 35, 16, 1);
    
    // Input fields
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Shunt Voltage (mV):", 10, 40, 1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    String voltStr = String(shuntVoltage * 1000, 1) + " mV";
    tft.drawString(voltStr, 10, 55, 2);
    tft.fillRoundRect(10, 75, DISPLAY_WIDTH - 20, 28, 3, TFT_DARKGREY);
    tft.drawRoundRect(10, 75, DISPLAY_WIDTH - 20, 28, 3, TFT_CYAN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("SET VOLTAGE", DISPLAY_WIDTH / 2, 89, 1);
    
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Max Current (A):", 10, 115, 1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    String currStr = String(current, 1) + " A";
    tft.drawString(currStr, 10, 130, 2);
    tft.fillRoundRect(10, 150, DISPLAY_WIDTH - 20, 28, 3, TFT_DARKGREY);
    tft.drawRoundRect(10, 150, DISPLAY_WIDTH - 20, 28, 3, TFT_CYAN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.drawString("SET CURRENT", DISPLAY_WIDTH / 2, 164, 1);
    
    // Calculated result
    float calculatedResistance = (shuntVoltage / current) * 1000.0; // in mΩ
    tft.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
    tft.drawString("Calculated Shunt:", 10, 190, 1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    String calcStr = String(calculatedResistance, 3) + " mΩ";
    tft.drawString(calcStr, 10, 205, 2);
    
      // Apply button
      tft.fillRoundRect(10, DISPLAY_HEIGHT - 30, DISPLAY_WIDTH - 20, 25, 3, TFT_GREEN);
      tft.drawRoundRect(10, DISPLAY_HEIGHT - 30, DISPLAY_WIDTH - 20, 25, 3, TFT_WHITE);
      tft.setTextColor(TFT_BLACK, TFT_GREEN);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("APPLY VALUES", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT - 17, 2);
      
      screenDrawn = true;
      lastShuntVoltage = shuntVoltage;
      lastCurrent = current;
    }
    
    if (ts.tirqTouched() && ts.touched() && (millis() - lastTouch > 300)) {
      TS_Point rawPoint = ts.getPoint();
      TS_Point p = calibrateTouchPoint(rawPoint);
      lastTouch = millis();
      
      // Back button
      if (p.x >= 10 && p.x <= 60 && p.y >= 6 && p.y <= 26) {
        calculating = false;
        return;
      }
      // Set Voltage
      else if (p.x >= 10 && p.x <= DISPLAY_WIDTH - 10 && p.y >= 75 && p.y <= 103) {
        float newVolt = getNumericInput("SHUNT VOLTAGE", shuntVoltage * 1000, 1.0, 200.0, "mV");
        shuntVoltage = newVolt / 1000.0; // Convert to V
        screenDrawn = false; // Force redraw to show new value
      }
      // Set Current
      else if (p.x >= 10 && p.x <= DISPLAY_WIDTH - 10 && p.y >= 150 && p.y <= 178) {
        float newCurr = getNumericInput("MAX CURRENT", current, 1.0, 200.0, "A");
        current = newCurr;
        screenDrawn = false; // Force redraw to show new value
      }
      // Apply
      else if (p.y >= DISPLAY_HEIGHT - 30) {
        float calculatedResistance = (shuntVoltage / current) * 1000.0; // in mΩ
        maxCurrent = current;
        shuntResistance = calculatedResistance / 1000.0; // Convert to Ω
        calculating = false;
        return;
      }
      
      delay(150);
    }
    
    delay(50);
  }
}

void performShuntCalibration() {
  currentScreen = SCREEN_SHUNT_CALIBRATION;
  drawShuntCalibrationScreen();
  
  // Wait for touch to be released before accepting new touches
  // This prevents the touch that opened this menu from immediately triggering a button
  delay(300);
  while (ts.touched()) {
    delay(50); // Wait until touch is released
  }
  delay(200); // Additional debounce delay
  
  bool calibrating = true;
  unsigned long lastTouch = 0;
  
  while (calibrating) {
    if (ts.tirqTouched() && ts.touched() && (millis() - lastTouch > 300)) {
      TS_Point rawPoint = ts.getPoint();
      TS_Point p = calibrateTouchPoint(rawPoint);
      lastTouch = millis();
      
      // Back button
      if (p.x >= 10 && p.x <= 60 && p.y >= 5 && p.y <= 23) {
        calibrating = false;
        currentScreen = SCREEN_SETTINGS;
        drawSettingsScreen();
        return;
      }
      // Set Max Current (y=50, height=24)
      else if (p.x >= 10 && p.x <= DISPLAY_WIDTH - 10 && p.y >= 50 && p.y <= 74) {
        float newMaxCurrent = getNumericInput("MAX CURRENT", maxCurrent, 1.0, 200.0, "A");
        maxCurrent = newMaxCurrent;
        drawShuntCalibrationScreen();
      }
      // Set Shunt Resistance (y=75, height=24)
      else if (p.x >= 10 && p.x <= DISPLAY_WIDTH - 10 && p.y >= 75 && p.y <= 99) {
        float newShunt = getNumericInput("SHUNT RESISTANCE", shuntResistance * 1000, 0.1, 100.0, "mΩ");
        shuntResistance = newShunt / 1000.0; // Convert mΩ to Ω
        drawShuntCalibrationScreen();
      }
      // Calculate from mV/Current (y=100, height=24)
      else if (p.x >= 10 && p.x <= DISPLAY_WIDTH - 10 && p.y >= 100 && p.y <= 124) {
        calculateFromVoltageCurrent();
        drawShuntCalibrationScreen();
      }
      // Known Load Calibration (y=125, height=24)
      else if (p.x >= 10 && p.x <= DISPLAY_WIDTH - 10 && p.y >= 125 && p.y <= 149) {
        performKnownLoadCalibration();
        drawShuntCalibrationScreen();
      }
      // Standard Values (y=150, height=24)
      else if (p.x >= 10 && p.x <= DISPLAY_WIDTH - 10 && p.y >= 150 && p.y <= 174) {
        showStandardValues();
        drawShuntCalibrationScreen();
      }
      // Reset to Defaults (y=175, height=24)
      else if (p.x >= 10 && p.x <= DISPLAY_WIDTH - 10 && p.y >= 175 && p.y <= 199) {
        maxCurrent = DEFAULT_MAX_CURRENT;
        shuntResistance = DEFAULT_SHUNT_RESISTANCE;
        drawShuntCalibrationScreen();
      }
      // Save & Apply (y=200, height=24)
      else if (p.x >= 10 && p.x <= DISPLAY_WIDTH - 10 && p.y >= 200 && p.y <= 224) {
        // Save to NVS
        saveShuntCalibration();
        
        // Recalibrate INA228 with new values
        int result = ina228.setMaxCurrentShunt(maxCurrent, shuntResistance);
        if (result == 0) {
          // Success
          tft.fillScreen(TFT_GREEN);
          tft.setTextColor(TFT_BLACK, TFT_GREEN);
          tft.setTextDatum(MC_DATUM);
          tft.drawString("CALIBRATION", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 - 20, 2);
          tft.drawString("SAVED!", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 + 20, 2);
          delay(2000);
        } else {
          // Error
          tft.fillScreen(TFT_RED);
          tft.setTextColor(TFT_WHITE, TFT_RED);
          tft.setTextDatum(MC_DATUM);
          tft.drawString("ERROR", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 - 20, 2);
          tft.drawString("Code: " + String(result), DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 + 20, 2);
          delay(2000);
        }
        
        calibrating = false;
        currentScreen = SCREEN_SETTINGS;
        drawSettingsScreen();
        return;
      }
      
      delay(100);
    }
    
    delay(50);
  }
}
