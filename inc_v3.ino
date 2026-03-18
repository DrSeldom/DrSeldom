#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <math.h>
#include <string.h>

#include "zar_banner_480x50_text_main_v2.h"

TFT_eSPI tft = TFT_eSPI();

Adafruit_BME280 bme;
bool bmePresent = false;
bool rtcPresent = false;
bool eepromPresent = false;

static const uint8_t I2C_SDA_PIN = 21;
static const uint8_t I2C_SCL_PIN = 22;
static const uint8_t BME280_ADDR_1 = 0x76;
static const uint8_t BME280_ADDR_2 = 0x77;
static const uint8_t DS3231_ADDR = 0x68;
static const uint8_t EEPROM_24C32_ADDR = 0x57;

// GPIO 22 занят I2C SCL, кнопка старта перенесена на GPIO 15.
static const uint8_t START_BTN_PIN = 15;
static const unsigned long START_BTN_HOLD_MS = 3000;

// Свободный пин управления нагревателем через ШИМ (ESP32 LEDC)
static const uint8_t HEATER_PWM_PIN = 25;
static const uint16_t HEATER_PWM_FREQ_HZ = 5000;
static const uint8_t HEATER_PWM_RESOLUTION = 12;
static const uint16_t HEATER_PWM_MAX = 4095;

// PID коэффициенты для нагревателя (выход 0..4095)
static const float HEATER_PID_KP = 450.0f;
static const float HEATER_PID_KI = 7.0f;
static const float HEATER_PID_KD = 300.0f;

#define BG_COLOR      TFT_BLACK
#define FRAME_COLOR   0x4208
#define DATE_COLOR    0xBDF7
#define TEMP_COLOR    0xFD20
#define HUM_COLOR     0x4EFF
#define LABEL_COLOR   0xC618
#define VALUE_COLOR   TFT_WHITE
#define GREEN_ON      0x07E0
#define RED_OFF       0xF800
#define WARN_COLOR    0xFFE0

static const int W = 480;


struct UIState {
  char dateStr[24];

  float temperature;
  float humidity;

  int incubDay;
  int totalDays;
  int daysToHatch;

  bool heaterOn;
  bool humidifierOn;
  bool fanAuto;

  char turnStr[16];
  char activeSensor[16];

};

struct ProgramData {
  uint32_t magic;
  uint8_t version;
  uint8_t totalDays;
  uint8_t currentDay;
  uint16_t nextTurnMin;
  uint16_t startYear;
  uint8_t startMonth;
  uint8_t startDay;
  uint8_t profileType;
};

// ============================================================
// ПРОГРАММА ИНКУБАЦИИ (куриная, 17+ дней)
// ============================================================
struct DayProgram {
  float    targetTemp;       // °C
  float    targetHum;        // %
  bool     ventilation;      // проветривание
  bool     rotation;         // поворот яиц
  uint16_t ventDurationMin;  // длительность проветривания (мин)
};

enum IncubProfile : uint8_t {
  INCUB_PROFILE_CHICKEN = 0,
  INCUB_PROFILE_PEREPEL = 1
};

//  Индекс 0 → день 1,  индекс 16 → день 17 и далее
static const DayProgram INCUB_TABLE[] = {
  /*  1 */ {37.8f, 75.0f,  true,  true,   10},
  /*  2 */ {37.8f, 70.0f,  true,  true,   10},
  /*  3 */ {37.8f, 65.0f,  true,  true,   60},
  /*  4 */ {37.8f, 60.0f,  true,  true,   60},
  /*  5 */ {37.8f, 70.0f,  true,  true,   60},
  /*  6 */ {37.8f, 70.0f,  true,  true,   60},
  /*  7 */ {37.8f, 70.0f,  true,  true,   60},
  /*  8 */ {37.8f, 70.0f,  true,  true,   60},
  /*  9 */ {37.8f, 70.0f,  true,  true,  160},
  /* 10 */ {37.8f, 45.0f,  true,  true,  160},
  /* 11 */ {37.8f, 45.0f,  true,  true,  160},
  /* 12 */ {37.8f, 45.0f,  true,  true,  160},
  /* 13 */ {37.8f, 45.0f,  true,  true,  160},
  /* 14 */ {37.8f, 45.0f,  true,  false, 160},
  /* 15 */ {37.3f, 70.0f,  true,  false, 160},
  /* 16 */ {37.3f, 75.0f,  false, false, 160},
  /* 17+*/ {37.3f, 75.0f,  true,  false, 160},
};
static const int INCUB_TABLE_SIZE = sizeof(INCUB_TABLE) / sizeof(INCUB_TABLE[0]);

static const DayProgram PEREPEL_INCUB_TABLE[] = {
  /*  1 */ {37.8f, 45.0f,  true,  true,   10},
  /*  2 */ {37.8f, 45.0f,  true,  true,   10},
  /*  3 */ {37.8f, 45.0f,  true,  true,   60},
  /*  4 */ {37.8f, 45.0f,  true,  true,   60},
  /*  5 */ {37.8f, 45.0f,  true,  true,   60},
  /*  6 */ {37.8f, 45.0f,  true,  true,   60},
  /*  7 */ {37.8f, 45.0f,  true,  true,   60},
  /*  8 */ {37.8f, 45.0f,  true,  true,   60},
  /*  9 */ {37.8f, 45.0f,  true,  true,  160},
  /* 10 */ {37.8f, 45.0f,  true,  true,  160},
  /* 11 */ {37.8f, 45.0f,  true,  true,  160},
  /* 12 */ {37.8f, 45.0f,  true,  true,  160},
  /* 13 */ {37.8f, 45.0f,  true,  true,  160},
  /* 14 */ {37.8f, 45.0f,  true,  false, 160},
  /* 15 */ {37.3f, 70.0f,  true,  false, 160},
  /* 16 */ {37.3f, 75.0f,  false, false, 160},
  /* 17+*/ {37.3f, 75.0f,  true,  false, 160},
};
static const int PEREPEL_INCUB_TABLE_SIZE = sizeof(PEREPEL_INCUB_TABLE) / sizeof(PEREPEL_INCUB_TABLE[0]);

DayProgram curDayProg;
IncubProfile currentProfile = INCUB_PROFILE_CHICKEN;

DayProgram getDayProgramFromTable(int day, const DayProgram* table, int tableSize) {
  if (day < 1) return table[0];
  int idx = day - 1;
  if (idx >= tableSize) idx = tableSize - 1;
  return table[idx];
}

DayProgram getDayProgram(int day) {
  if (currentProfile == INCUB_PROFILE_PEREPEL) {
    return getDayProgramFromTable(day, PEREPEL_INCUB_TABLE, PEREPEL_INCUB_TABLE_SIZE);
  }
  return getDayProgramFromTable(day, INCUB_TABLE, INCUB_TABLE_SIZE);
}

static const uint32_t PROGRAM_MAGIC = 0x50474D31UL;
static const uint8_t PROGRAM_VERSION = 3;

UIState ui;
UIState prevUi;
ProgramData programData;

bool lastSensorValid = false;
bool prevSensorValid = false;

unsigned long lastClockRefresh = 0;
unsigned long lastSensorRefresh = 0;
unsigned long btnPressStartMs = 0;
bool btnLongPressFired = false;

float heaterPidIntegral = 0.0f;
float heaterPidPrevError = 0.0f;
unsigned long heaterPidPrevMs = 0;
uint16_t heaterPwmValue = 0;

bool eqFloat(float a, float b, float eps) { return fabs(a - b) <= eps; }

void resetHeaterPid() {
  heaterPidIntegral = 0.0f;
  heaterPidPrevError = 0.0f;
  heaterPidPrevMs = millis();
  heaterPwmValue = 0;
  ledcWrite(HEATER_PWM_PIN, 0);
}

uint16_t computeHeaterPidPwm(float currentTemp, float targetTemp, bool sensorValid) {
  if (!sensorValid) {
    resetHeaterPid();
    return 0;
  }

  unsigned long now = millis();
  float dt = (heaterPidPrevMs == 0) ? 0.1f : (float)(now - heaterPidPrevMs) / 1000.0f;
  if (dt < 0.02f) dt = 0.02f;
  if (dt > 2.0f) dt = 2.0f;

  float error = targetTemp - currentTemp;
  heaterPidIntegral += error * dt;
  if (heaterPidIntegral > 100.0f) heaterPidIntegral = 100.0f;
  if (heaterPidIntegral < -100.0f) heaterPidIntegral = -100.0f;

  float derivative = (error - heaterPidPrevError) / dt;
  float out = HEATER_PID_KP * error + HEATER_PID_KI * heaterPidIntegral + HEATER_PID_KD * derivative;

  if (out < 0.0f) out = 0.0f;
  if (out > HEATER_PWM_MAX) out = HEATER_PWM_MAX;

  heaterPidPrevError = error;
  heaterPidPrevMs = now;

  uint16_t pwm = (uint16_t)out;
  ledcWrite(HEATER_PWM_PIN, pwm);
  return pwm;
}

void invalidatePrevUi() {
  memset(&prevUi, 0xFF, sizeof(prevUi));
  prevSensorValid = !lastSensorValid;
}

void drawFrame(int x, int y, int w, int h) { tft.drawRect(x, y, w, h, FRAME_COLOR); }

void drawFieldLabel(const char* text, int x, int y, uint16_t color = LABEL_COLOR, int font = 2) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(color, BG_COLOR);
  tft.drawString(text, x, y, font);
}

uint8_t bcdToDec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }

bool i2cDevicePresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

int32_t daysFromCivil(int32_t y, uint8_t m, uint8_t d) {
  y -= (m <= 2);
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

bool initDS3231() {
  rtcPresent = i2cDevicePresent(DS3231_ADDR);
  Serial.println(rtcPresent ? "DS3231 found" : "DS3231 not found");
  return rtcPresent;
}

bool ds3231ReadDateTime(uint8_t& sec, uint8_t& min, uint8_t& hour, uint8_t& day, uint8_t& month, uint16_t& year) {
  if (!rtcPresent) return false;

  Wire.beginTransmission(DS3231_ADDR);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom((int)DS3231_ADDR, 7) != 7) return false;

  const uint8_t rawSec = Wire.read();
  const uint8_t rawMin = Wire.read();
  const uint8_t rawHour = Wire.read();
  (void)Wire.read();
  const uint8_t rawDate = Wire.read();
  const uint8_t rawMonth = Wire.read();
  const uint8_t rawYear = Wire.read();

  sec = bcdToDec(rawSec & 0x7F);
  min = bcdToDec(rawMin & 0x7F);
  hour = bcdToDec(rawHour & 0x3F);
  day = bcdToDec(rawDate & 0x3F);
  month = bcdToDec(rawMonth & 0x1F);
  year = (uint16_t)(2000 + bcdToDec(rawYear));
  return true;
}

bool init24C32() {
  eepromPresent = i2cDevicePresent(EEPROM_24C32_ADDR);
  Serial.println(eepromPresent ? "24C32 found" : "24C32 not found");
  return eepromPresent;
}

bool eeprom24c32ReadBytes(uint16_t addr, uint8_t* dst, size_t len) {
  if (!eepromPresent) return false;

  Wire.beginTransmission(EEPROM_24C32_ADDR);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)(addr & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;

  size_t readTotal = 0;
  while (readTotal < len) {
    size_t chunk = len - readTotal;
    if (chunk > 28) chunk = 28;

    uint8_t got = Wire.requestFrom((int)EEPROM_24C32_ADDR, (int)chunk);
    if (got != chunk) return false;

    for (size_t i = 0; i < chunk; i++) dst[readTotal + i] = Wire.read();
    readTotal += chunk;
  }
  return true;
}

bool eeprom24c32WriteBytes(uint16_t addr, const uint8_t* src, size_t len) {
  if (!eepromPresent) return false;

  size_t written = 0;
  while (written < len) {
    uint8_t pageOffset = (uint8_t)((addr + written) % 32);
    size_t chunk = len - written;
    size_t spaceInPage = 32 - pageOffset;
    if (chunk > spaceInPage) chunk = spaceInPage;

    Wire.beginTransmission(EEPROM_24C32_ADDR);
    uint16_t cur = addr + written;
    Wire.write((uint8_t)(cur >> 8));
    Wire.write((uint8_t)(cur & 0xFF));
    for (size_t i = 0; i < chunk; i++) Wire.write(src[written + i]);

    if (Wire.endTransmission() != 0) return false;
    delay(6);
    written += chunk;
  }
  return true;
}

void setDefaultProgramData(ProgramData& p) {
  p.magic = PROGRAM_MAGIC;
  p.version = PROGRAM_VERSION;
  p.totalDays = 21;
  p.currentDay = 1;
  p.nextTurnMin = 94;
  p.startYear = 0;
  p.startMonth = 0;
  p.startDay = 0;
  p.profileType = INCUB_PROFILE_CHICKEN;
}

void saveProgramData() {
  if (eepromPresent) eeprom24c32WriteBytes(0, (const uint8_t*)&programData, sizeof(programData));
}

void loadProgramData() {
  setDefaultProgramData(programData);
  if (!eepromPresent) return;

  ProgramData tmp;
  if (!eeprom24c32ReadBytes(0, (uint8_t*)&tmp, sizeof(tmp))) return;

  if (tmp.magic != PROGRAM_MAGIC || tmp.version != PROGRAM_VERSION || tmp.totalDays == 0 || tmp.currentDay == 0 || tmp.currentDay > 99 || tmp.profileType > INCUB_PROFILE_PEREPEL) {
    saveProgramData();
    return;
  }
  programData = tmp;
  currentProfile = (programData.profileType == INCUB_PROFILE_PEREPEL) ? INCUB_PROFILE_PEREPEL : INCUB_PROFILE_CHICKEN;
}


void selectIncubationProfileOnBoot() {
  // При включении с нажатой кнопкой GPIO15 переключаем таблицу инкубации.
  if (digitalRead(START_BTN_PIN) == LOW) {
    if (programData.profileType == INCUB_PROFILE_PEREPEL) {
      programData.profileType = INCUB_PROFILE_CHICKEN;
    } else {
      programData.profileType = INCUB_PROFILE_PEREPEL;
    }
    saveProgramData();
    delay(250);
  }

  currentProfile = (programData.profileType == INCUB_PROFILE_PEREPEL) ? INCUB_PROFILE_PEREPEL : INCUB_PROFILE_CHICKEN;
}

void startIncubationNow() {
  uint8_t sec, min, hour, day, month;
  uint16_t year;

  programData.currentDay = 1;
  if (ds3231ReadDateTime(sec, min, hour, day, month, year)) {
    programData.startYear = year;
    programData.startMonth = month;
    programData.startDay = day;
  } else {
    programData.startYear = 0;
    programData.startMonth = 0;
    programData.startDay = 0;
  }

  saveProgramData();
  invalidatePrevUi();
  Serial.println("Incubation started: day=1");
}

void handleStartButton() {
  int state = digitalRead(START_BTN_PIN); // INPUT_PULLUP: LOW when pressed
  unsigned long now = millis();

  if (state == LOW) {
    if (btnPressStartMs == 0) {
      btnPressStartMs = now;
      btnLongPressFired = false;
    }

    if (!btnLongPressFired && (now - btnPressStartMs >= START_BTN_HOLD_MS)) {
      startIncubationNow();
      btnLongPressFired = true;
    }
  } else {
    btnPressStartMs = 0;
    btnLongPressFired = false;
  }
}

bool initBME280() {
  if (bme.begin(BME280_ADDR_1, &Wire)) {
    bmePresent = true;
    Serial.println("BME280 found at 0x76");
    return true;
  }
  if (bme.begin(BME280_ADDR_2, &Wire)) {
    bmePresent = true;
    Serial.println("BME280 found at 0x77");
    return true;
  }
  bmePresent = false;
  Serial.println("BME280 not found");
  return false;
}

bool readDateTime(char* out, size_t outSize) {
  uint8_t sec = 0, min = 0, hour = 0, day = 0, month = 0;
  uint16_t year = 0;

  bool ok = ds3231ReadDateTime(sec, min, hour, day, month, year);
  if (!ok) {
    snprintf(out, outSize, "RTC ERROR");
    return false;
  }

  snprintf(out, outSize, "%02u.%02u.%04u  %02u:%02u:%02u", day, month, year, hour, min, sec);
  return true;
}

bool readSensors(float& temperature, float& humidity, char* sensorName, size_t sensorNameSize) {
  if (!bmePresent) {
    temperature = 0.0f;
    humidity = 0.0f;
    strncpy(sensorName, "BME280 ERR", sensorNameSize);
    sensorName[sensorNameSize - 1] = '\0';
    return false;
  }

  float t = bme.readTemperature();
  float h = bme.readHumidity();

  if (isnan(t) || isnan(h)) {
    temperature = 0.0f;
    humidity = 0.0f;
    strncpy(sensorName, "BME280 NAN", sensorNameSize);
    sensorName[sensorNameSize - 1] = '\0';
    return false;
  }

  temperature = t;
  humidity = h;
  strncpy(sensorName, "BME280", sensorNameSize);
  sensorName[sensorNameSize - 1] = '\0';
  return true;
}

void readIncubationProgram(int& incubDay, int& totalDays, int& daysToHatch, char* turnStr, size_t turnStrSize) {
  uint8_t sec, min, hour, day, month;
  uint16_t year;

  incubDay = programData.currentDay;
  if (programData.startYear > 0 && ds3231ReadDateTime(sec, min, hour, day, month, year)) {
    int32_t startAbs = daysFromCivil(programData.startYear, programData.startMonth, programData.startDay);
    int32_t nowAbs = daysFromCivil(year, month, day);
    int32_t diff = nowAbs - startAbs;
    if (diff < 0) diff = 0;
    incubDay = (int)(diff + 1);
    programData.currentDay = (uint8_t)incubDay;
  }

  totalDays = (int)programData.totalDays;
  if (totalDays < 1) totalDays = 21;

  daysToHatch = totalDays - incubDay;
  if (daysToHatch < 0) daysToHatch = 0;

  uint16_t m = programData.nextTurnMin;
  snprintf(turnStr, turnStrSize, "%02u:%02u", (unsigned)((m / 60) % 24), (unsigned)(m % 60));
  turnStr[turnStrSize - 1] = '\0';

  curDayProg = getDayProgram(incubDay);
}

void readActuators(bool& heaterOn, bool& humidifierOn, bool& fanAuto) {
  heaterPwmValue = computeHeaterPidPwm(ui.temperature, curDayProg.targetTemp, lastSensorValid);
  heaterOn = (heaterPwmValue > 0);

  humidifierOn = lastSensorValid && (ui.humidity < (curDayProg.targetHum - 1.0f));
  fanAuto = curDayProg.ventilation;
}


void drawHeaderStatic() {
  tft.fillRect(0, 0, W, 50, BG_COLOR);
  tft.pushImage(0, 0, ZAR_BANNER_480X50_TEXT_MAIN_V2_WIDTH, ZAR_BANNER_480X50_TEXT_MAIN_V2_HEIGHT, zar_banner_480x50_text_main_v2);
  tft.drawRect(0, 0, W, 50, FRAME_COLOR);
}

void drawColorFrame(int x, int y, int w, int h, uint16_t color) {
  tft.drawRect(x, y, w, h, color);
}

void drawLayoutStatic() {
  tft.fillScreen(BG_COLOR);
  drawHeaderStatic();

  // Блок даты/времени
  drawColorFrame(18, 54, 444, 26, DATE_COLOR);

  // Верхние 4 прямоугольных блока: текущие и целевые значения
  drawColorFrame(18, 86, 106, 92, TEMP_COLOR);   // TEMP ACT
  drawColorFrame(130, 86, 106, 92, WARN_COLOR);  // TEMP SET
  drawColorFrame(244, 86, 106, 92, HUM_COLOR);   // HUM ACT
  drawColorFrame(356, 86, 106, 92, GREEN_ON);    // HUM SET

  // Информационный блок дня
  drawColorFrame(18, 184, 444, 42, FRAME_COLOR);

  // Блоки состояний
  drawColorFrame(18, 232, 214, 50, FRAME_COLOR);    // Heater/Humid
  drawColorFrame(248, 232, 214, 50, FRAME_COLOR);   // Fan/ROT/V:R

  // Нижний блок сенсора по центру
  drawColorFrame(18, 286, 444, 33, FRAME_COLOR);

}

void drawDateTimeBlock(const char* s) {
  tft.fillRect(19, 55, 442, 24, BG_COLOR);

  char dtBuf[64];
  snprintf(dtBuf, sizeof(dtBuf), "%s  |  PROFILE:%s", s, currentProfile == INCUB_PROFILE_PEREPEL ? "PEREPEL" : "CHICK");

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(VALUE_COLOR, BG_COLOR);
  tft.drawString(dtBuf, 240, 67, 2);
}

void drawTemperatureBlock(float t, bool valid) {
  tft.fillRect(19, 87, 104, 90, BG_COLOR);
  char buf[20];
  if (valid) snprintf(buf, sizeof(buf), "%.1f C", t);
  else snprintf(buf, sizeof(buf), "--.- C");

  drawFieldLabel("TEMP", 26, 95, TEMP_COLOR);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TEMP_COLOR, BG_COLOR);
  tft.drawString(buf, 24, 128, 4);
}

void drawTargetTemperatureBlock(float tSet) {
  tft.fillRect(131, 87, 104, 90, BG_COLOR);
  char buf[20];
  snprintf(buf, sizeof(buf), "%.1f C", tSet);

  drawFieldLabel("T SET", 138, 95, WARN_COLOR);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(WARN_COLOR, BG_COLOR);
  tft.drawString(buf, 136, 128, 4);
}

void drawHumidityBlock(float h, bool valid) {
  tft.fillRect(245, 87, 104, 90, BG_COLOR);
  char buf[20];
  if (valid) snprintf(buf, sizeof(buf), "%.0f%%", h);
  else snprintf(buf, sizeof(buf), "--%%");

  drawFieldLabel("HUM", 252, 95, HUM_COLOR);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(HUM_COLOR, BG_COLOR);
  tft.drawString(buf, 250, 128, 4);
}

void drawTargetHumidityBlock(float hSet) {
  tft.fillRect(357, 87, 104, 90, BG_COLOR);
  char buf[20];
  snprintf(buf, sizeof(buf), "%.0f%%", hSet);

  drawFieldLabel("H SET", 364, 95, GREEN_ON);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(GREEN_ON, BG_COLOR);
  tft.drawString(buf, 362, 128, 4);
}

void drawDayInfoBlock(int day, int totalDays, int toHatch) {
  tft.fillRect(19, 185, 442, 40, BG_COLOR);

  char leftBuf[32];
  char rightBuf[16];
  if (day > 0 && totalDays > 0) snprintf(leftBuf, sizeof(leftBuf), "Day %d / %d", day, totalDays);
  else snprintf(leftBuf, sizeof(leftBuf), "Day -- / --");

  if (toHatch >= 0) snprintf(rightBuf, sizeof(rightBuf), "%d d", toHatch);
  else snprintf(rightBuf, sizeof(rightBuf), "-- d");

  drawFieldLabel(leftBuf, 24, 192, WARN_COLOR, 2);
  drawFieldLabel("To hatch:", 256, 192, VALUE_COLOR, 2);
  drawFieldLabel(rightBuf, 360, 192, WARN_COLOR, 2);
  drawFieldLabel("Turn:", 24, 208, HUM_COLOR, 2);
  drawFieldLabel(ui.turnStr, 80, 208, VALUE_COLOR, 2);
  drawFieldLabel(curDayProg.rotation ? "ROT ON" : "ROT OFF", 170, 208, curDayProg.rotation ? GREEN_ON : RED_OFF, 2);
}

void drawStatusesBlock(bool heaterOn, bool humidifierOn, bool fanAuto, const char* turnStr, const char* sensor) {
  tft.fillRect(19, 233, 212, 48, BG_COLOR);
  tft.fillRect(249, 233, 212, 48, BG_COLOR);
  tft.fillRect(19, 287, 442, 31, BG_COLOR);

  // Левый прямоугольник: Heater / Humid
  drawFieldLabel("Heater:", 24, 238);
  drawFieldLabel(heaterOn ? "ON" : "OFF", 94, 238, heaterOn ? GREEN_ON : RED_OFF);

  drawFieldLabel("Humid:", 24, 254);
  drawFieldLabel(humidifierOn ? "ON" : "OFF", 94, 254, humidifierOn ? GREEN_ON : RED_OFF);

  char pwmBuf[24];
  snprintf(pwmBuf, sizeof(pwmBuf), "PWM:%u", (unsigned)heaterPwmValue);
  drawFieldLabel(pwmBuf, 150, 246, WARN_COLOR);

  // Правый прямоугольник: Fan / ROT / V:R
  drawFieldLabel("Fan:", 254, 238, HUM_COLOR);
  drawFieldLabel(fanAuto ? "AUTO" : "MAN", 292, 238, fanAuto ? GREEN_ON : WARN_COLOR);

  drawFieldLabel("ROT:", 254, 254, HUM_COLOR);
  drawFieldLabel(curDayProg.rotation ? "ON" : "OFF", 292, 254, curDayProg.rotation ? GREEN_ON : RED_OFF);

  char progBuf[64];
  snprintf(progBuf, sizeof(progBuf), "V:%s R:%s", curDayProg.ventilation ? "Y" : "N", curDayProg.rotation ? "Y" : "N");
  drawFieldLabel(progBuf, 352, 254, WARN_COLOR);

  // Sensor: нижний блок, по центру
  char sensorBuf[64];
  snprintf(sensorBuf, sizeof(sensorBuf), "Sensor: %s", sensor);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(VALUE_COLOR, BG_COLOR);
  tft.drawString(sensorBuf, 240, 302, 2);

  (void)turnStr;
}


void updateDynamicUI(bool force) {
  if (force || strcmp(ui.dateStr, prevUi.dateStr) != 0) drawDateTimeBlock(ui.dateStr);

  if (force || !eqFloat(ui.temperature, prevUi.temperature, 0.05f) || lastSensorValid != prevSensorValid)
    drawTemperatureBlock(ui.temperature, lastSensorValid);

  if (force || !eqFloat(ui.humidity, prevUi.humidity, 0.4f) || lastSensorValid != prevSensorValid)
    drawHumidityBlock(ui.humidity, lastSensorValid);

  if (force || !eqFloat(curDayProg.targetTemp, getDayProgram(prevUi.incubDay).targetTemp, 0.01f))
    drawTargetTemperatureBlock(curDayProg.targetTemp);

  if (force || !eqFloat(curDayProg.targetHum, getDayProgram(prevUi.incubDay).targetHum, 0.01f))
    drawTargetHumidityBlock(curDayProg.targetHum);

  if (force || ui.incubDay != prevUi.incubDay || ui.totalDays != prevUi.totalDays || ui.daysToHatch != prevUi.daysToHatch ||
      strcmp(ui.turnStr, prevUi.turnStr) != 0)
    drawDayInfoBlock(ui.incubDay, ui.totalDays, ui.daysToHatch);

  if (force || ui.heaterOn != prevUi.heaterOn || ui.humidifierOn != prevUi.humidifierOn || ui.fanAuto != prevUi.fanAuto ||
      strcmp(ui.turnStr, prevUi.turnStr) != 0 || strcmp(ui.activeSensor, prevUi.activeSensor) != 0 ||
      !eqFloat(curDayProg.targetTemp, getDayProgram(prevUi.incubDay).targetTemp, 0.01f) ||
      !eqFloat(curDayProg.targetHum, getDayProgram(prevUi.incubDay).targetHum, 0.01f)) {
    drawStatusesBlock(ui.heaterOn, ui.humidifierOn, ui.fanAuto, ui.turnStr, ui.activeSensor);
  }


  prevUi = ui;
  prevSensorValid = lastSensorValid;
}

bool refreshUiState() {
  readDateTime(ui.dateStr, sizeof(ui.dateStr));

  lastSensorValid = readSensors(ui.temperature, ui.humidity, ui.activeSensor, sizeof(ui.activeSensor));

  readIncubationProgram(ui.incubDay, ui.totalDays, ui.daysToHatch, ui.turnStr, sizeof(ui.turnStr));
  readActuators(ui.heaterOn, ui.humidifierOn, ui.fanAuto);
  return lastSensorValid;
}

void setup() {
  Serial.begin(115200);

  pinMode(START_BTN_PIN, INPUT_PULLUP);

  ledcAttach(HEATER_PWM_PIN, HEATER_PWM_FREQ_HZ, HEATER_PWM_RESOLUTION);
  resetHeaterPid();

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  initDS3231();
  init24C32();
  loadProgramData();
  selectIncubationProfileOnBoot();
  initBME280();

  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  tft.fillScreen(BG_COLOR);

  memset(&ui, 0, sizeof(ui));
  lastSensorValid = false;
  invalidatePrevUi();

  strncpy(ui.dateStr, "--.--.----  --:--:--", sizeof(ui.dateStr));
  ui.dateStr[sizeof(ui.dateStr) - 1] = '\0';
  strncpy(ui.turnStr, "--:--", sizeof(ui.turnStr));
  ui.turnStr[sizeof(ui.turnStr) - 1] = '\0';

  if (bmePresent) strncpy(ui.activeSensor, "BME280", sizeof(ui.activeSensor));
  else strncpy(ui.activeSensor, "BME280 ERR", sizeof(ui.activeSensor));
  ui.activeSensor[sizeof(ui.activeSensor) - 1] = '\0';


  curDayProg = getDayProgram(1);

  drawLayoutStatic();
  refreshUiState();
  updateDynamicUI(true);
}

void loop() {
  const unsigned long now = millis();

  handleStartButton();

  if (now - lastClockRefresh >= 1000) {
    lastClockRefresh = now;
    readDateTime(ui.dateStr, sizeof(ui.dateStr));
  }

  if (now - lastSensorRefresh >= 1000) {
    lastSensorRefresh = now;

    lastSensorValid = readSensors(ui.temperature, ui.humidity, ui.activeSensor, sizeof(ui.activeSensor));
    readIncubationProgram(ui.incubDay, ui.totalDays, ui.daysToHatch, ui.turnStr, sizeof(ui.turnStr));
    readActuators(ui.heaterOn, ui.humidifierOn, ui.fanAuto);
  }


  updateDynamicUI(false);
}
