#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <RTClib.h>
#include <math.h>
#include <string.h>

#include <calibri28.h>
#include "zar_banner_480x50_text_main_v2.h"

// ============================================================
// DISPLAY
// ============================================================
TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

// ============================================================
// I2C BUS — общая для BME280, DS3231, 24C32
// ============================================================
static const uint8_t I2C_SDA_PIN = 21;
static const uint8_t I2C_SCL_PIN = 22;

// ============================================================
// BME280
// ============================================================
Adafruit_BME280 bme;
bool bmePresent = false;
static const uint8_t BME280_ADDR_1 = 0x76;
static const uint8_t BME280_ADDR_2 = 0x77;

// ============================================================
// DS3231 RTC
// ============================================================
RTC_DS3231 rtc;
bool rtcPresent   = false;
bool rtcTimeValid = false;

// ============================================================
// 24C32 EEPROM
// ============================================================
bool    eepromPresent = false;
uint8_t eepromAddr    = 0x57;

static const uint8_t  EEPROM_ADDR_1   = 0x57;
static const uint8_t  EEPROM_ADDR_2   = 0x50;
static const uint32_t EEPROM_MAGIC    = 0x5A415231;   // "ZAR1"
static const uint16_t EEPROM_CFG_ADDR = 0x0000;

struct __attribute__((packed)) IncubConfig {
  uint32_t magic;
  uint32_t startTimestamp;
  uint8_t  totalDays;
  uint8_t  turnIntervalH;
  uint8_t  turnIntervalM;
  uint8_t  flags;              // bit0 = инкубация активна
  uint32_t lastTurnTimestamp;
};

IncubConfig incubCfg;
bool incubCfgValid = false;

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
static const int INCUB_TABLE_SIZE =
  sizeof(INCUB_TABLE) / sizeof(INCUB_TABLE[0]);

DayProgram curDayProg;       // параметры текущего дня

DayProgram getDayProgram(int day) {
  if (day < 1) return INCUB_TABLE[0];
  int idx = day - 1;
  if (idx >= INCUB_TABLE_SIZE) idx = INCUB_TABLE_SIZE - 1;
  return INCUB_TABLE[idx];
}

// ============================================================
// КНОПКА «СТАРТ ИНКУБАЦИИ»
//
// ⚠️  GPIO 22 занят шиной I2C (SCL) !
//     Кнопка назначена на GPIO 27.
//     Подключение: пин — кнопка — GND  (INPUT_PULLUP)
//     Длинное нажатие 3 сек → установка 1-го дня инкубации.
// ============================================================
static const uint8_t       BTN_START_PIN      = 27;
static const unsigned long BTN_LONG_PRESS_MS  = 3000;

bool          btnLastState   = true;     // HIGH = не нажата
unsigned long btnPressStart  = 0;
bool          btnLongHandled = false;

// forward declarations
void startNewIncubation();
void showStartMessage();

// ============================================================
// COLORS
// ============================================================
#define BG_COLOR      0x1044
#define LINE_COLOR    0x6C8F
#define DATE_COLOR    0xDEFB

#define TEMP_COLOR    0xFB40
#define HUM_COLOR     0x4EFF
#define LABEL_COLOR   0xFDB0

#define GREEN_ON      0x07E0
#define RED_OFF       0xF800
#define YELLOW_TEXT   0xFE60
#define CYAN_TEXT     0x4EFF
#define WHITE_TEXT    0xFFFF
#define PURPLE_TEXT   0xD81F

// ============================================================
// LAYOUT
// ============================================================
static const int W = 480;
static const int H = 320;

// ============================================================
// UI STATE
// ============================================================
struct UIState {
  char  dateStr[24];
  float temperature;
  float humidity;
  float targetTemp;
  float targetHum;
  int   incubDay;
  int   totalDays;
  int   daysToHatch;
  bool  heaterOn;
  bool  humidifierOn;
  bool  fanAuto;
  bool  rotEnabled;
  bool  ventEnabled;
  char  turnStr[16];
  char  activeSensor[16];
};

// ============================================================
// GLOBAL STATE
// ============================================================
UIState ui;
UIState prevUi;

bool lastSensorValid = false;
bool prevSensorValid = false;

unsigned long lastClockRefresh  = 0;
unsigned long lastSensorRefresh = 0;
unsigned long lastUiRefresh     = 0;

// Гистерезис нагревателя / увлажнителя
static bool  heaterState = false;
static bool  humidState  = false;
static const float TEMP_HYST = 0.3f;   // ±0.3 °C
static const float HUM_HYST  = 3.0f;   // ±3 %

// ============================================================
// LOW-LEVEL HELPERS
// ============================================================
uint16_t darkenColor(uint16_t c, uint8_t amount) {
  uint8_t r = ((c >> 11) & 0x1F) << 3;
  uint8_t g = ((c >> 5)  & 0x3F) << 2;
  uint8_t b = ( c        & 0x1F) << 3;
  r = (r > amount) ? (r - amount) : 0;
  g = (g > amount) ? (g - amount) : 0;
  b = (b > amount) ? (b - amount) : 0;
  return tft.color565(r, g, b);
}

void fillVerticalGradientTFT(int x, int y, int w, int h,
                              uint16_t c1, uint16_t c2) {
  if (h <= 0) return;
  int r1 = ((c1 >> 11) & 0x1F) << 3;
  int g1 = ((c1 >> 5)  & 0x3F) << 2;
  int b1 = ( c1        & 0x1F) << 3;
  int r2 = ((c2 >> 11) & 0x1F) << 3;
  int g2 = ((c2 >> 5)  & 0x3F) << 2;
  int b2 = ( c2        & 0x1F) << 3;
  int d  = (h > 1) ? (h - 1) : 1;
  for (int i = 0; i < h; i++) {
    uint8_t rr = (uint8_t)(r1 + (r2 - r1) * i / d);
    uint8_t gg = (uint8_t)(g1 + (g2 - g1) * i / d);
    uint8_t bb = (uint8_t)(b1 + (b2 - b1) * i / d);
    tft.drawFastHLine(x, y + i, w, tft.color565(rr, gg, bb));
  }
}

void fillVerticalGradientSprite(TFT_eSprite& s,
                                 int x, int y, int w, int h,
                                 uint16_t c1, uint16_t c2) {
  if (h <= 0) return;
  int r1 = ((c1 >> 11) & 0x1F) << 3;
  int g1 = ((c1 >> 5)  & 0x3F) << 2;
  int b1 = ( c1        & 0x1F) << 3;
  int r2 = ((c2 >> 11) & 0x1F) << 3;
  int g2 = ((c2 >> 5)  & 0x3F) << 2;
  int b2 = ( c2        & 0x1F) << 3;
  int d  = (h > 1) ? (h - 1) : 1;
  for (int i = 0; i < h; i++) {
    uint8_t rr = (uint8_t)(r1 + (r2 - r1) * i / d);
    uint8_t gg = (uint8_t)(g1 + (g2 - g1) * i / d);
    uint8_t bb = (uint8_t)(b1 + (b2 - b1) * i / d);
    s.drawFastHLine(x, y + i, w, tft.color565(rr, gg, bb));
  }
}

void drawPanel(int x, int y, int w, int h,
               uint16_t top, uint16_t bot, uint16_t brd) {
  fillVerticalGradientTFT(x, y, w, h, top, bot);
  tft.drawRect(x, y, w, h, brd);
  tft.drawRect(x + 1, y + 1, w - 2, h - 2, darkenColor(brd, 50));
}

void drawPanelSprite(TFT_eSprite& s, int w, int h,
                     uint16_t top, uint16_t bot, uint16_t brd) {
  fillVerticalGradientSprite(s, 0, 0, w, h, top, bot);
  s.drawRect(0, 0, w, h, brd);
  s.drawRect(1, 1, w - 2, h - 2, darkenColor(brd, 50));
}

void spriteCenteredText(TFT_eSprite& s, const char* t,
                        int x, int y, uint16_t fg, uint16_t bg,
                        int font = 4) {
  s.setTextDatum(MC_DATUM);
  s.setTextColor(fg, bg);
  s.drawString(t, x, y, font);
}

void spriteLeftText(TFT_eSprite& s, const char* t,
                    int x, int y, uint16_t fg, uint16_t bg,
                    int font = 4) {
  s.setTextDatum(TL_DATUM);
  s.setTextColor(fg, bg);
  s.drawString(t, x, y, font);
}

bool eqFloat(float a, float b, float eps) {
  if (isnan(a) && isnan(b)) return true;
  if (isnan(a) || isnan(b)) return false;
  return fabsf(a - b) <= eps;
}

void invalidatePrevUi() {
  memset(&prevUi, 0, sizeof(prevUi));
  prevUi.dateStr[0]      = '\x01';  prevUi.dateStr[1]      = '\0';
  prevUi.turnStr[0]      = '\x01';  prevUi.turnStr[1]      = '\0';
  prevUi.activeSensor[0] = '\x01';  prevUi.activeSensor[1] = '\0';
  prevUi.temperature = NAN;
  prevUi.humidity    = NAN;
  prevUi.targetTemp  = NAN;
  prevUi.targetHum   = NAN;
  prevUi.incubDay    = -1;
  prevUi.totalDays   = -1;
  prevUi.daysToHatch = -999;
}

// ============================================================
// 24C32  EEPROM — низкий уровень
// ============================================================
bool eepromDetect(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

bool eepromReadBlock(uint16_t memAddr, uint8_t* buf, size_t len) {
  if (!eepromPresent || len == 0 || len > 32) return false;
  Wire.beginTransmission(eepromAddr);
  Wire.write((uint8_t)(memAddr >> 8));
  Wire.write((uint8_t)(memAddr & 0xFF));
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(eepromAddr, (uint8_t)len) != len) return false;
  for (size_t i = 0; i < len; i++) {
    if (!Wire.available()) return false;
    buf[i] = Wire.read();
  }
  return true;
}

bool eepromWriteBlock(uint16_t memAddr, const uint8_t* buf, size_t len) {
  if (!eepromPresent || len == 0 || len > 32) return false;
  if ((uint16_t)(memAddr + len) > (uint16_t)((memAddr & ~0x1F) + 32))
    return false;

  Wire.beginTransmission(eepromAddr);
  Wire.write((uint8_t)(memAddr >> 8));
  Wire.write((uint8_t)(memAddr & 0xFF));
  for (size_t i = 0; i < len; i++) Wire.write(buf[i]);
  if (Wire.endTransmission() != 0) return false;
  delay(10);
  return true;
}

// ============================================================
// 24C32  EEPROM — конфигурация
// ============================================================
bool isConfigReasonable(const IncubConfig& c) {
  if (c.magic != EEPROM_MAGIC)                return false;
  if (c.totalDays == 0 || c.totalDays > 60)   return false;
  if (c.startTimestamp < 1700000000UL)         return false;
  return true;
}

bool loadIncubConfig() {
  if (!eepromPresent) return false;
  if (!eepromReadBlock(EEPROM_CFG_ADDR,
                       (uint8_t*)&incubCfg, sizeof(incubCfg)))
    return false;
  return isConfigReasonable(incubCfg);
}

bool saveIncubConfig() {
  if (!eepromPresent) return false;
  incubCfg.magic = EEPROM_MAGIC;
  return eepromWriteBlock(EEPROM_CFG_ADDR,
                          (const uint8_t*)&incubCfg, sizeof(incubCfg));
}

bool saveLastTurn(uint32_t ts) {
  if (!eepromPresent) return false;
  uint16_t a = EEPROM_CFG_ADDR + offsetof(IncubConfig, lastTurnTimestamp);
  return eepromWriteBlock(a, (const uint8_t*)&ts, sizeof(ts));
}

// ============================================================
// INIT — I2C шина, DS3231, BME280, 24C32
// ============================================================
void initI2CBus() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);
  Serial.printf("[I2C] SDA=%d  SCL=%d  100 kHz\n",
                I2C_SDA_PIN, I2C_SCL_PIN);
}

bool initDS3231() {
  if (!rtc.begin()) {
    rtcPresent = false;
    Serial.println("[DS3231] not found");
    return false;
  }
  rtcPresent = true;
  Serial.println("[DS3231] found at 0x68");

  if (rtc.lostPower()) {
    Serial.println("[DS3231] lost power -> set compile time");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  DateTime n = rtc.now();
  rtcTimeValid = (n.year() >= 2024 && n.year() <= 2099);
  Serial.printf("[DS3231] %04d-%02d-%02d %02d:%02d:%02d  valid=%d\n",
    n.year(), n.month(), n.day(),
    n.hour(), n.minute(), n.second(), rtcTimeValid);
  return true;
}

bool initBME280() {
  if (bme.begin(BME280_ADDR_1, &Wire)) {
    bmePresent = true;
    Serial.println("[BME280] found at 0x76");
    return true;
  }
  if (bme.begin(BME280_ADDR_2, &Wire)) {
    bmePresent = true;
    Serial.println("[BME280] found at 0x77");
    return true;
  }
  bmePresent = false;
  Serial.println("[BME280] not found");
  return false;
}

bool initEEPROM() {
  if (eepromDetect(EEPROM_ADDR_1)) {
    eepromAddr = EEPROM_ADDR_1;  eepromPresent = true;
  } else if (eepromDetect(EEPROM_ADDR_2)) {
    eepromAddr = EEPROM_ADDR_2;  eepromPresent = true;
  } else {
    eepromPresent = false;
    Serial.println("[24C32] not found");
    return false;
  }
  Serial.printf("[24C32] found at 0x%02X\n", eepromAddr);

  if (loadIncubConfig()) {
    incubCfgValid = true;
    Serial.printf("[24C32] Config loaded: days=%d start=%lu flags=0x%02X\n",
      incubCfg.totalDays, incubCfg.startTimestamp, incubCfg.flags);
  } else {
    incubCfgValid = false;
    Serial.println("[24C32] No valid config");
  }
  return true;
}

// ============================================================
// DATA INPUT
// ============================================================
bool readDateTime(char* out, size_t outSize) {
  if (!rtcPresent) {
    snprintf(out, outSize, "--.--.----  --:--:--");
    return false;
  }
  DateTime n = rtc.now();
  rtcTimeValid = (n.year() >= 2024 && n.year() <= 2099);
  if (!rtcTimeValid) {
    snprintf(out, outSize, "--.--.----  --:--:--");
    return false;
  }
  snprintf(out, outSize, "%02d.%02d.%04d  %02d:%02d:%02d",
    n.day(), n.month(), n.year(),
    n.hour(), n.minute(), n.second());
  return true;
}

bool readSensors(float& temperature, float& humidity,
                 char* sensorName, size_t sensorNameSize) {
  if (!bmePresent) {
    temperature = NAN;  humidity = NAN;
    strncpy(sensorName, "BME280 ERR", sensorNameSize);
    sensorName[sensorNameSize - 1] = '\0';
    return false;
  }
  float t = bme.readTemperature();
  float h = bme.readHumidity();
  if (isnan(t) || isnan(h)) {
    temperature = NAN;  humidity = NAN;
    strncpy(sensorName, "BME280 NAN", sensorNameSize);
    sensorName[sensorNameSize - 1] = '\0';
    return false;
  }
  temperature = t;  humidity = h;
  strncpy(sensorName, "BME280", sensorNameSize);
  sensorName[sensorNameSize - 1] = '\0';
  return true;
}

void readIncubationProgram(int& incubDay, int& totalDays,
                           int& daysToHatch,
                           float& targetTemp, float& targetHum,
                           bool& rotEnabled, bool& ventEnabled,
                           char* turnStr, size_t turnStrSize) {
  // Значения по умолчанию
  incubDay    = 0;
  totalDays   = 0;
  daysToHatch = -1;
  targetTemp  = NAN;
  targetHum   = NAN;
  rotEnabled  = false;
  ventEnabled = false;
  strncpy(turnStr, "--:--", turnStrSize);
  turnStr[turnStrSize - 1] = '\0';

  if (!incubCfgValid || !(incubCfg.flags & 0x01)) return;

  totalDays = incubCfg.totalDays;

  if (!rtcPresent || !rtcTimeValid) return;

  uint32_t nowTs = rtc.now().unixtime();

  // День инкубации
  if (nowTs >= incubCfg.startTimestamp) {
    uint32_t elapsed = nowTs - incubCfg.startTimestamp;
    incubDay    = (int)(elapsed / 86400UL) + 1;
    daysToHatch = totalDays - incubDay + 1;
    if (daysToHatch < 0) daysToHatch = 0;
    if (incubDay > totalDays) incubDay = totalDays;
  }

  // Параметры текущего дня из таблицы
  curDayProg  = getDayProgram(incubDay);
  targetTemp  = curDayProg.targetTemp;
  targetHum   = curDayProg.targetHum;
  rotEnabled  = curDayProg.rotation;
  ventEnabled = curDayProg.ventilation;

  // Время с последнего поворота (HH:MM)
  if (incubCfg.lastTurnTimestamp > 0 &&
      nowTs >= incubCfg.lastTurnTimestamp) {
    uint32_t te = nowTs - incubCfg.lastTurnTimestamp;
    int tH = (int)(te / 3600UL);
    int tM = (int)((te % 3600UL) / 60UL);
    if (tH > 99) tH = 99;
    snprintf(turnStr, turnStrSize, "%02d:%02d", tH, tM);
  }
}

// Базовое управление нагревателем / увлажнителем с гистерезисом
void computeActuators(bool& heaterOn, bool& humidifierOn, bool& fanAuto) {
  fanAuto = true;

  if (!incubCfgValid || !lastSensorValid ||
      isnan(ui.temperature) || isnan(ui.targetTemp)) {
    heaterOn     = false;
    humidifierOn = false;
    return;
  }

  // Нагреватель
  if (ui.temperature < ui.targetTemp - TEMP_HYST / 2.0f)
    heaterState = true;
  else if (ui.temperature > ui.targetTemp + TEMP_HYST / 2.0f)
    heaterState = false;
  heaterOn = heaterState;

  // Увлажнитель
  if (!isnan(ui.humidity) && !isnan(ui.targetHum)) {
    if (ui.humidity < ui.targetHum - HUM_HYST / 2.0f)
      humidState = true;
    else if (ui.humidity > ui.targetHum + HUM_HYST / 2.0f)
      humidState = false;
  } else {
    humidState = false;
  }
  humidifierOn = humidState;
}

// ============================================================
// КНОПКА — обработка
// ============================================================
void startNewIncubation() {
  if (!rtcPresent || !rtcTimeValid) {
    Serial.println("[BTN] RTC unavailable — cannot start");
    return;
  }

  uint32_t now = rtc.now().unixtime();

  memset(&incubCfg, 0, sizeof(incubCfg));
  incubCfg.magic              = EEPROM_MAGIC;
  incubCfg.startTimestamp     = now;
  incubCfg.totalDays          = 21;       // куриная программа
  incubCfg.turnIntervalH      = 2;
  incubCfg.turnIntervalM      = 0;
  incubCfg.flags              = 0x01;     // активна
  incubCfg.lastTurnTimestamp  = now;

  if (eepromPresent) saveIncubConfig();

  incubCfgValid = true;

  Serial.println("[BTN] *** INCUBATION STARTED — DAY 1 ***");

  showStartMessage();
}

// Визуальное подтверждение на дисплее
void showStartMessage() {
  tft.fillRect(40, 120, 400, 80, 0x0010);
  tft.drawRect(40, 120, 400, 80, YELLOW_TEXT);
  tft.drawRect(41, 121, 398, 78, darkenColor(YELLOW_TEXT, 80));

  tft.loadFont(calibri28);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(YELLOW_TEXT, 0x0010);
  tft.drawString("Инкубация запущена!", W / 2, 145);
  tft.setTextColor(WHITE_TEXT, 0x0010);
  tft.drawString("День 1 из 21", W / 2, 175);
  tft.unloadFont();

  delay(2500);

  // Перерисовать всё
  drawLayoutStatic();
  invalidatePrevUi();
  prevSensorValid = !lastSensorValid;    // форсировать перерисовку

  // Обновить данные
  lastSensorValid = readSensors(
    ui.temperature, ui.humidity,
    ui.activeSensor, sizeof(ui.activeSensor));
  readDateTime(ui.dateStr, sizeof(ui.dateStr));
  readIncubationProgram(
    ui.incubDay, ui.totalDays, ui.daysToHatch,
    ui.targetTemp, ui.targetHum,
    ui.rotEnabled, ui.ventEnabled,
    ui.turnStr, sizeof(ui.turnStr));
  computeActuators(ui.heaterOn, ui.humidifierOn, ui.fanAuto);

  updateDynamicUI(true);
}

void handleButton() {
  bool state = digitalRead(BTN_START_PIN);

  // Нажатие (HIGH → LOW)
  if (state == LOW && btnLastState == HIGH) {
    btnPressStart  = millis();
    btnLongHandled = false;
    Serial.println("[BTN] pressed — hold 3 sec to start incubation");
  }

  // Удержание
  if (state == LOW && !btnLongHandled) {
    if (millis() - btnPressStart >= BTN_LONG_PRESS_MS) {
      btnLongHandled = true;
      startNewIncubation();
    }
  }

  btnLastState = state;
}

// ============================================================
// STATIC UI
// ============================================================
void drawHeaderStatic() {
  fillVerticalGradientTFT(0, 0, W, 50, 0x1C9F, 0x001F);
  tft.drawFastHLine(0, 50, W, 0x867F);
  tft.pushImage(0, 0,
    ZAR_BANNER_480X50_TEXT_MAIN_V2_WIDTH,
    ZAR_BANNER_480X50_TEXT_MAIN_V2_HEIGHT,
    zar_banner_480x50_text_main_v2);
}

void drawLayoutStatic() {
  tft.fillScreen(BG_COLOR);
  drawHeaderStatic();

  // Панели температуры / влажности
  drawPanel(18,  86, 218, 92, 0x20C8, 0x1866, LINE_COLOR);
  drawPanel(244, 86, 218, 92, 0x185F, 0x104F, LINE_COLOR);
  drawPanel(24,  145, 206, 27, 0x40A8, 0x20A7, LINE_COLOR);
  drawPanel(250, 145, 206, 27, 0x187F, 0x106F, LINE_COLOR);

  // Разделители информационной секции
  tft.drawFastHLine(18, 205, 444, LINE_COLOR);
  tft.drawFastHLine(18, 233, 444, LINE_COLOR);
  tft.drawFastVLine(238, 205, 31, LINE_COLOR);
}

// ============================================================
// DYNAMIC BLOCKS
// ============================================================

// ---- Дата / время ----
void drawDateTimeBlock(const char* s) {
  spr.deleteSprite();
  spr.setColorDepth(16);
  spr.createSprite(W, 24);
  spr.fillSprite(BG_COLOR);

  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(DATE_COLOR, BG_COLOR);
  spr.drawString(s, W / 2, 12, 4);

  spr.pushSprite(0, 56);
  spr.deleteSprite();
}

// ---- Температура ----
void drawTemperatureBlock(float t, bool valid) {
  const int bw = 218, bh = 92;
  spr.deleteSprite();
  spr.setColorDepth(16);
  spr.createSprite(bw, bh);

  drawPanelSprite(spr, bw, bh, 0x20C8, 0x1866, LINE_COLOR);
  fillVerticalGradientSprite(spr, 6, 59, 206, 27, 0x40A8, 0x20A7);
  spr.drawRect(6, 59, 206, 27, LINE_COLOR);
  spr.drawRect(7, 60, 204, 25, darkenColor(LINE_COLOR, 50));

  char buf[20];
  if (valid && !isnan(t))
    snprintf(buf, sizeof(buf), "%.1f%cC", t, (char)0xB0);
  else
    snprintf(buf, sizeof(buf), "--.-%cC", (char)0xB0);

  spriteCenteredText(spr, buf, 110, 36, TEMP_COLOR, 0x1866, 8);
  spriteCenteredText(spr, "TEMP", 109, 72, LABEL_COLOR, 0x20A7, 4);

  spr.pushSprite(18, 86);
  spr.deleteSprite();
}

// ---- Влажность ----
void drawHumidityBlock(float h, bool valid) {
  const int bw = 218, bh = 92;
  spr.deleteSprite();
  spr.setColorDepth(16);
  spr.createSprite(bw, bh);

  drawPanelSprite(spr, bw, bh, 0x185F, 0x104F, LINE_COLOR);
  fillVerticalGradientSprite(spr, 6, 59, 206, 27, 0x187F, 0x106F);
  spr.drawRect(6, 59, 206, 27, LINE_COLOR);
  spr.drawRect(7, 60, 204, 25, darkenColor(LINE_COLOR, 50));

  char buf[20];
  if (valid && !isnan(h))
    snprintf(buf, sizeof(buf), "%.0f%%", h);
  else {
    strncpy(buf, "--%", sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
  }

  spriteCenteredText(spr, buf, 110, 36, HUM_COLOR, 0x104F, 8);
  spriteCenteredText(spr, "HUM", 109, 72, CYAN_TEXT, 0x106F, 4);

  spr.pushSprite(244, 86);
  spr.deleteSprite();
}

// ---- День инкубации ----
void drawDayInfoBlock(int day, int totalDays, int toHatch) {
  spr.deleteSprite();
  spr.setColorDepth(16);
  spr.createSprite(444, 24);
  spr.fillSprite(BG_COLOR);
  spr.drawFastVLine(220, 0, 24, LINE_COLOR);

  char leftBuf[32], rightBuf[16];
  if (day > 0 && totalDays > 0)
    snprintf(leftBuf, sizeof(leftBuf), "День %d из %d", day, totalDays);
  else
    snprintf(leftBuf, sizeof(leftBuf), "День -- из --");

  if (toHatch >= 0)
    snprintf(rightBuf, sizeof(rightBuf), "%d дн.", toHatch);
  else
    snprintf(rightBuf, sizeof(rightBuf), "-- дн.");

  spr.loadFont(calibri28);
  spr.setTextDatum(TL_DATUM);

  spr.setTextColor(YELLOW_TEXT, BG_COLOR);
  spr.drawString(leftBuf, 10, 2);

  spr.setTextColor(WHITE_TEXT, BG_COLOR);
  spr.drawString("До вывода:", 237, 2);

  spr.setTextColor(YELLOW_TEXT, BG_COLOR);
  spr.drawString(rightBuf, 382, 2);

  spr.unloadFont();

  spr.pushSprite(18, 208);
  spr.deleteSprite();
}

// ---- Статусы (3 строки × 28 px = 84 px, y:236..319) ----
//
//  Row 1: Heat: OFF     Fan: AUTO      →37.8°
//  Row 2: Hum:  OFF     Turn: 01:34    →70%
//  Row 3: BME280        Rot: Y         Vent: Y
//
void drawStatusesBlock() {
  const int sw = 444, sh = 84;

  spr.deleteSprite();
  spr.setColorDepth(16);
  spr.createSprite(sw, sh);
  spr.fillSprite(BG_COLOR);

  // Разделители
  spr.drawFastHLine(0, 28, sw, LINE_COLOR);
  spr.drawFastHLine(0, 56, sw, LINE_COLOR);
  spr.drawFastVLine(220, 0, 56, LINE_COLOR);

  // ----- Строка 1: Нагреватель + Вентилятор + уставка T -----
  spriteLeftText(spr, "Heat:", 10, 4, LABEL_COLOR, BG_COLOR, 4);
  spriteLeftText(spr, ui.heaterOn ? "ON" : "OFF", 90, 4,
                 ui.heaterOn ? GREEN_ON : RED_OFF, BG_COLOR, 4);

  spriteLeftText(spr, "Fan:", 232, 4, CYAN_TEXT, BG_COLOR, 4);
  spriteLeftText(spr, ui.fanAuto ? "AUTO" : "MAN", 290, 4,
                 GREEN_ON, BG_COLOR, 4);

  // Уставка температуры (правый край)
  if (!isnan(ui.targetTemp)) {
    char tb[12];
    snprintf(tb, sizeof(tb), "%.1f%c", ui.targetTemp, (char)0xB0);
    spriteLeftText(spr, tb, 380, 4, YELLOW_TEXT, BG_COLOR, 4);
  }

  // ----- Строка 2: Увлажнитель + Поворот + уставка H -----
  spriteLeftText(spr, "Hum:", 10, 32, LABEL_COLOR, BG_COLOR, 4);
  spriteLeftText(spr, ui.humidifierOn ? "ON" : "OFF", 90, 32,
                 ui.humidifierOn ? GREEN_ON : RED_OFF, BG_COLOR, 4);

  spriteLeftText(spr, "Turn:", 232, 32, CYAN_TEXT, BG_COLOR, 4);
  spriteLeftText(spr, ui.turnStr, 310, 32, WHITE_TEXT, BG_COLOR, 4);

  // Уставка влажности (правый край)
  if (!isnan(ui.targetHum)) {
    char hb[8];
    snprintf(hb, sizeof(hb), "%.0f%%", ui.targetHum);
    spriteLeftText(spr, hb, 400, 32, YELLOW_TEXT, BG_COLOR, 4);
  }

  // ----- Строка 3: Датчик + Rot + Vent -----
  spriteLeftText(spr, ui.activeSensor, 10, 60,
                 WHITE_TEXT, BG_COLOR, 4);

  spriteLeftText(spr, "Rot:", 180, 60, CYAN_TEXT, BG_COLOR, 4);
  spriteLeftText(spr, ui.rotEnabled ? "Y" : "N", 235, 60,
                 ui.rotEnabled ? GREEN_ON : RED_OFF, BG_COLOR, 4);

  spriteLeftText(spr, "Vent:", 290, 60, CYAN_TEXT, BG_COLOR, 4);
  spriteLeftText(spr, ui.ventEnabled ? "Y" : "N", 360, 60,
                 ui.ventEnabled ? GREEN_ON : RED_OFF, BG_COLOR, 4);

  spr.pushSprite(18, 236);
  spr.deleteSprite();
}

// ============================================================
// UI UPDATE
// ============================================================
void updateDynamicUI(bool force) {
  bool svChanged = (lastSensorValid != prevSensorValid);

  if (force || strcmp(ui.dateStr, prevUi.dateStr) != 0)
    drawDateTimeBlock(ui.dateStr);

  if (force || svChanged ||
      !eqFloat(ui.temperature, prevUi.temperature, 0.05f))
    drawTemperatureBlock(ui.temperature, lastSensorValid);

  if (force || svChanged ||
      !eqFloat(ui.humidity, prevUi.humidity, 0.4f))
    drawHumidityBlock(ui.humidity, lastSensorValid);

  if (force ||
      ui.incubDay    != prevUi.incubDay  ||
      ui.totalDays   != prevUi.totalDays ||
      ui.daysToHatch != prevUi.daysToHatch)
    drawDayInfoBlock(ui.incubDay, ui.totalDays, ui.daysToHatch);

  // Полный блок статусов — перерисовка при любом изменении
  if (force ||
      ui.heaterOn     != prevUi.heaterOn     ||
      ui.humidifierOn != prevUi.humidifierOn ||
      ui.fanAuto      != prevUi.fanAuto      ||
      ui.rotEnabled   != prevUi.rotEnabled   ||
      ui.ventEnabled  != prevUi.ventEnabled  ||
      !eqFloat(ui.targetTemp, prevUi.targetTemp, 0.01f) ||
      !eqFloat(ui.targetHum,  prevUi.targetHum,  0.1f)  ||
      strcmp(ui.turnStr,      prevUi.turnStr)     != 0 ||
      strcmp(ui.activeSensor, prevUi.activeSensor) != 0)
    drawStatusesBlock();

  prevSensorValid = lastSensorValid;
  prevUi = ui;
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n========  ZAR INCUBATOR  ========");

  // Кнопка
  pinMode(BTN_START_PIN, INPUT_PULLUP);

  // Дисплей
  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  tft.fillScreen(BG_COLOR);

  // I2C + периферия
  initI2CBus();
  initDS3231();
  initBME280();
  initEEPROM();

  // Начальное состояние UI
  memset(&ui, 0, sizeof(ui));
  invalidatePrevUi();

  strncpy(ui.dateStr, "--.--.----  --:--:--", sizeof(ui.dateStr));
  ui.dateStr[sizeof(ui.dateStr) - 1] = '\0';

  ui.temperature  = NAN;
  ui.humidity     = NAN;
  ui.targetTemp   = NAN;
  ui.targetHum    = NAN;
  ui.incubDay     = 0;
  ui.totalDays    = 0;
  ui.daysToHatch  = -1;
  ui.heaterOn     = false;
  ui.humidifierOn = false;
  ui.fanAuto      = true;
  ui.rotEnabled   = false;
  ui.ventEnabled  = false;

  strncpy(ui.turnStr, "--:--", sizeof(ui.turnStr));
  ui.turnStr[sizeof(ui.turnStr) - 1] = '\0';

  strncpy(ui.activeSensor,
          bmePresent ? "BME280" : "BME280 ERR",
          sizeof(ui.activeSensor));
  ui.activeSensor[sizeof(ui.activeSensor) - 1] = '\0';

  lastSensorValid = false;
  prevSensorValid = false;

  // Статический каркас
  drawLayoutStatic();

  // Первое чтение данных
  lastSensorValid = readSensors(
    ui.temperature, ui.humidity,
    ui.activeSensor, sizeof(ui.activeSensor));

  readDateTime(ui.dateStr, sizeof(ui.dateStr));

  readIncubationProgram(
    ui.incubDay, ui.totalDays, ui.daysToHatch,
    ui.targetTemp, ui.targetHum,
    ui.rotEnabled, ui.ventEnabled,
    ui.turnStr, sizeof(ui.turnStr));

  computeActuators(ui.heaterOn, ui.humidifierOn, ui.fanAuto);

  // Полная отрисовка
  updateDynamicUI(true);

  Serial.println("========  READY  ========\n");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  const unsigned long now = millis();

  // Кнопка — проверяется каждую итерацию
  handleButton();

  // Сенсоры + программа + актуаторы — 1 раз в секунду
  if (now - lastSensorRefresh >= 1000) {
    lastSensorRefresh = now;

    lastSensorValid = readSensors(
      ui.temperature, ui.humidity,
      ui.activeSensor, sizeof(ui.activeSensor));

    readIncubationProgram(
      ui.incubDay, ui.totalDays, ui.daysToHatch,
      ui.targetTemp, ui.targetHum,
      ui.rotEnabled, ui.ventEnabled,
      ui.turnStr, sizeof(ui.turnStr));

    computeActuators(ui.heaterOn, ui.humidifierOn, ui.fanAuto);
  }

  // Часы — 1 раз в секунду
  if (now - lastClockRefresh >= 1000) {
    lastClockRefresh = now;
    readDateTime(ui.dateStr, sizeof(ui.dateStr));
  }

  // Обновление экрана — каждые 100 мс
  if (now - lastUiRefresh >= 100) {
    lastUiRefresh = now;
    updateDynamicUI(false);
  }

  yield();
}
