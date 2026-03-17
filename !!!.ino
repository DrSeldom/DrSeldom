#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <math.h>
#include <string.h>

#include <calibri28.h>
#include "zar_banner_480x50_text_main_v2.h"

// ============================================================
// DISPLAY
// ============================================================
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

// ============================================================
// BME280 (I2C)
// SDA = 21
// SCL = 22
// ============================================================
Adafruit_BME280 bme;
bool bmePresent = false;

static const uint8_t I2C_SDA_PIN = 21;
static const uint8_t I2C_SCL_PIN = 22;
static const uint8_t BME280_ADDR_1 = 0x76;
static const uint8_t BME280_ADDR_2 = 0x77;

// ============================================================
// COLORS
// ============================================================
#define BG_COLOR           0x1044
#define PANEL_COLOR        0x18A6
#define PANEL_DARK         0x0823
#define LINE_COLOR         0x6C8F
#define TITLE_COLOR        0xFEC0
#define DATE_COLOR         0xDEFB

#define TEMP_COLOR         0xFB40
#define HUM_COLOR          0x4EFF
#define LABEL_COLOR        0xFDB0

#define GREEN_ON           0x07E0
#define RED_OFF            0xF800
#define YELLOW_TEXT        0xFE60
#define CYAN_TEXT          0x4EFF
#define WHITE_TEXT         0xFFFF
#define PURPLE_TEXT        0xD81F

#define BTN_BLUE           0x1A7F
#define BTN_BLUE_DARK      0x10D8
#define BTN_BORDER         0x6D7F
#define BTN_YELLOW_BORDER  0xFEA0

// ============================================================
// LAYOUT
// ============================================================
static const int W = 480;
static const int H = 320;

// ============================================================
// APP TYPES
// ============================================================
enum UIMode : uint8_t {
  UI_MODE_PROGRAMS = 0,
  UI_MODE_MANUAL   = 1
};

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

  UIMode mode;
};

// ============================================================
// GLOBAL STATE
// ============================================================
UIState ui;
UIState prevUi;

unsigned long lastClockRefresh = 0;
unsigned long lastSensorRefresh = 0;
unsigned long lastUiRefresh = 0;

// ============================================================
// LOW LEVEL HELPERS
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

void fillVerticalGradientTFT(int x, int y, int w, int h, uint16_t c1, uint16_t c2) {
  uint8_t r1 = ((c1 >> 11) & 0x1F) << 3;
  uint8_t g1 = ((c1 >> 5)  & 0x3F) << 2;
  uint8_t b1 = ( c1        & 0x1F) << 3;

  uint8_t r2 = ((c2 >> 11) & 0x1F) << 3;
  uint8_t g2 = ((c2 >> 5)  & 0x3F) << 2;
  uint8_t b2 = ( c2        & 0x1F) << 3;

  for (int i = 0; i < h; i++) {
    float k = (h <= 1) ? 0.0f : (float)i / (float)(h - 1);
    uint8_t r = r1 + (int)((r2 - r1) * k);
    uint8_t g = g1 + (int)((g2 - g1) * k);
    uint8_t b = b1 + (int)((b2 - b1) * k);
    tft.drawFastHLine(x, y + i, w, tft.color565(r, g, b));
  }
}

void fillVerticalGradientSprite(TFT_eSprite& s, int x, int y, int w, int h, uint16_t c1, uint16_t c2) {
  uint8_t r1 = ((c1 >> 11) & 0x1F) << 3;
  uint8_t g1 = ((c1 >> 5)  & 0x3F) << 2;
  uint8_t b1 = ( c1        & 0x1F) << 3;

  uint8_t r2 = ((c2 >> 11) & 0x1F) << 3;
  uint8_t g2 = ((c2 >> 5)  & 0x3F) << 2;
  uint8_t b2 = ( c2        & 0x1F) << 3;

  for (int i = 0; i < h; i++) {
    float k = (h <= 1) ? 0.0f : (float)i / (float)(h - 1);
    uint8_t r = r1 + (int)((r2 - r1) * k);
    uint8_t g = g1 + (int)((g2 - g1) * k);
    uint8_t b = b1 + (int)((b2 - b1) * k);
    s.drawFastHLine(x, y + i, w, tft.color565(r, g, b));
  }
}

void drawPanel(int x, int y, int w, int h, uint16_t topColor, uint16_t bottomColor, uint16_t borderColor) {
  fillVerticalGradientTFT(x, y, w, h, topColor, bottomColor);
  tft.drawRect(x, y, w, h, borderColor);
  tft.drawRect(x + 1, y + 1, w - 2, h - 2, darkenColor(borderColor, 50));
}

void drawPanelSprite(TFT_eSprite& s, int w, int h, uint16_t topColor, uint16_t bottomColor, uint16_t borderColor) {
  fillVerticalGradientSprite(s, 0, 0, w, h, topColor, bottomColor);
  s.drawRect(0, 0, w, h, borderColor);
  s.drawRect(1, 1, w - 2, h - 2, darkenColor(borderColor, 50));
}

void spriteCenteredText(TFT_eSprite& s, const char* text, int x, int y, uint16_t fg, uint16_t bg, int font = 4) {
  s.setTextDatum(MC_DATUM);
  s.setTextColor(fg, bg);
  s.drawString(text, x, y, font);
}

void spriteLeftText(TFT_eSprite& s, const char* text, int x, int y, uint16_t fg, uint16_t bg, int font = 4) {
  s.setTextDatum(TL_DATUM);
  s.setTextColor(fg, bg);
  s.drawString(text, x, y, font);
}

void spriteSmoothCenteredText(TFT_eSprite& s, const char* text, int x, int y, uint16_t fg, uint16_t bg) {
  s.loadFont(calibri28);
  s.setTextDatum(MC_DATUM);
  s.setTextColor(fg, bg);
  s.drawString(text, x, y);
  s.unloadFont();
}

void spriteSmoothLeftText(TFT_eSprite& s, const char* text, int x, int y, uint16_t fg, uint16_t bg) {
  s.loadFont(calibri28);
  s.setTextDatum(TL_DATUM);
  s.setTextColor(fg, bg);
  s.drawString(text, x, y);
  s.unloadFont();
}

bool eqFloat(float a, float b, float eps) {
  return fabs(a - b) <= eps;
}

void invalidatePrevUi() {
  memset(&prevUi, 0xFF, sizeof(prevUi));
}

// ============================================================
// BME280
// ============================================================
bool initBME280() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

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

// ============================================================
// REAL DATA INPUT
// ============================================================

bool readDateTime(char* out, size_t outSize) {
  // Здесь позже можно подключить RTC/NTP.
  // Пока выводим заглушку.
  snprintf(out, outSize, "--.--.----  --:--:--");
  return false;
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
  // Здесь подставишь свою реальную логику программы инкубации.
  incubDay = 12;
  totalDays = 21;
  daysToHatch = 9;

  strncpy(turnStr, "01:34", turnStrSize);
  turnStr[turnStrSize - 1] = '\0';
}

void readActuators(bool& heaterOn, bool& humidifierOn, bool& fanAuto) {
  // Здесь подставишь реальные состояния реле/логики.
  heaterOn = false;
  humidifierOn = false;
  fanAuto = true;
}

UIMode readUiMode() {
  // Здесь потом можно читать touch/кнопки/энкодер.
  return UI_MODE_PROGRAMS;
}

// ============================================================
// STATIC UI
// ============================================================
void drawHeaderStatic() {
  fillVerticalGradientTFT(0, 0, W, 50, 0x1C9F, 0x001F);
  tft.drawFastHLine(0, 50, W, 0x867F);

  tft.pushImage(
    0, 0,
    ZAR_BANNER_480X50_TEXT_MAIN_V2_WIDTH,
    ZAR_BANNER_480X50_TEXT_MAIN_V2_HEIGHT,
    zar_banner_480x50_text_main_v2
  );
}

void drawLayoutStatic() {
  tft.fillScreen(BG_COLOR);

  drawHeaderStatic();

  drawPanel(18, 86, 218, 92, 0x20C8, 0x1866, LINE_COLOR);
  drawPanel(244, 86, 218, 92, 0x185F, 0x104F, LINE_COLOR);

  drawPanel(24, 145, 206, 27, 0x40A8, 0x20A7, LINE_COLOR);
  drawPanel(250, 145, 206, 27, 0x187F, 0x106F, LINE_COLOR);

  tft.drawFastHLine(18, 205, 444, LINE_COLOR);
  tft.drawFastHLine(18, 235, 444, LINE_COLOR);
  tft.drawFastHLine(18, 265, 444, LINE_COLOR);
  tft.drawFastHLine(18, 295, 444, LINE_COLOR);

  tft.drawFastVLine(238, 205, 90, LINE_COLOR);
}

// ============================================================
// DYNAMIC BLOCKS
// ============================================================
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

void drawTemperatureBlock(float t, bool valid) {
  const int bw = 218;
  const int bh = 92;

  spr.deleteSprite();
  spr.setColorDepth(16);
  spr.createSprite(bw, bh);

  drawPanelSprite(spr, bw, bh, 0x20C8, 0x1866, LINE_COLOR);

  fillVerticalGradientSprite(spr, 6, 59, 206, 27, 0x40A8, 0x20A7);
  spr.drawRect(6, 59, 206, 27, LINE_COLOR);
  spr.drawRect(7, 60, 204, 25, darkenColor(LINE_COLOR, 50));

  char buf[20];
  if (valid) {
    snprintf(buf, sizeof(buf), "%.1f%cC", t, 0xB0);
  } else {
    snprintf(buf, sizeof(buf), "--.-%cC", 0xB0);
  }

  spriteCenteredText(spr, buf, 110, 36, TEMP_COLOR, 0x1866, 8);
  spriteCenteredText(spr, "TEMP", 109, 72, LABEL_COLOR, 0x20A7, 4);

  spr.pushSprite(18, 86);
  spr.deleteSprite();
}

void drawHumidityBlock(float h, bool valid) {
  const int bw = 218;
  const int bh = 92;

  spr.deleteSprite();
  spr.setColorDepth(16);
  spr.createSprite(bw, bh);

  drawPanelSprite(spr, bw, bh, 0x185F, 0x104F, LINE_COLOR);

  fillVerticalGradientSprite(spr, 6, 59, 206, 27, 0x187F, 0x106F);
  spr.drawRect(6, 59, 206, 27, LINE_COLOR);
  spr.drawRect(7, 60, 204, 25, darkenColor(LINE_COLOR, 50));

  char buf[20];
  if (valid) {
    snprintf(buf, sizeof(buf), "%.0f%%", h);
  } else {
    strncpy(buf, "--%", sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
  }

  spriteCenteredText(spr, buf, 110, 36, HUM_COLOR, 0x104F, 8);
  spriteCenteredText(spr, "HUM", 109, 72, CYAN_TEXT, 0x106F, 4);

  spr.pushSprite(244, 86);
  spr.deleteSprite();
}

void drawDayInfoBlock(int day, int totalDays, int toHatch) {
  spr.deleteSprite();
  spr.setColorDepth(16);
  spr.createSprite(444, 24);
  spr.fillSprite(BG_COLOR);

  char leftBuf[32];
  char rightBuf[16];

  if (day > 0 && totalDays > 0) {
    snprintf(leftBuf, sizeof(leftBuf), "День %d из %d", day, totalDays);
  } else {
    snprintf(leftBuf, sizeof(leftBuf), "День -- из --");
  }

  if (toHatch >= 0) {
    snprintf(rightBuf, sizeof(rightBuf), "%d дн.", toHatch);
  } else {
    snprintf(rightBuf, sizeof(rightBuf), "-- дн.");
  }

  spriteSmoothLeftText(spr, leftBuf, 10, 2, YELLOW_TEXT, BG_COLOR);
  spriteSmoothLeftText(spr, "До вывода:", 237, 2, WHITE_TEXT, BG_COLOR);
  spriteSmoothLeftText(spr, rightBuf, 382, 2, YELLOW_TEXT, BG_COLOR);

  spr.pushSprite(18, 208);
  spr.deleteSprite();
}

void drawStatusesBlock(bool heaterOn, bool humidifierOn, bool fanAuto, const char* turnStr, const char* sensor) {
  spr.deleteSprite();
  spr.setColorDepth(16);
  spr.createSprite(444, 84);
  spr.fillSprite(BG_COLOR);

  spriteLeftText(spr, "Heater:", 10, 2, LABEL_COLOR, BG_COLOR, 4);
  spriteLeftText(spr, heaterOn ? "ON" : "OFF", 127, 2, heaterOn ? GREEN_ON : RED_OFF, BG_COLOR, 4);

  spriteLeftText(spr, "Fan:", 237, 2, CYAN_TEXT, BG_COLOR, 4);
  spriteLeftText(spr, fanAuto ? "AUTO" : "MAN", 302, 2, GREEN_ON, BG_COLOR, 4);

  spriteLeftText(spr, "Humid:", 10, 32, LABEL_COLOR, BG_COLOR, 4);
  spriteLeftText(spr, humidifierOn ? "ON" : "OFF", 132, 32, humidifierOn ? GREEN_ON : RED_OFF, BG_COLOR, 4);

  spriteLeftText(spr, "Turn:", 237, 32, CYAN_TEXT, BG_COLOR, 4);
  spriteLeftText(spr, turnStr, 322, 32, WHITE_TEXT, BG_COLOR, 4);

  spriteLeftText(spr, "Sensor:", 10, 62, PURPLE_TEXT, BG_COLOR, 4);
  spriteLeftText(spr, sensor, 142, 62, WHITE_TEXT, BG_COLOR, 4);

  spr.pushSprite(18, 238);
  spr.deleteSprite();
}

void drawButtonToSprite(TFT_eSprite& s, int x, int y, int w, int h, const char* text, bool selected) {
  uint16_t border = selected ? BTN_YELLOW_BORDER : BTN_BORDER;
  uint16_t top    = selected ? BTN_BLUE         : BTN_BLUE_DARK;
  uint16_t bottom = selected ? BTN_BLUE_DARK    : darkenColor(BTN_BLUE_DARK, 15);

  fillVerticalGradientSprite(s, x, y, w, h, top, bottom);
  s.drawRect(x, y, w, h, border);
  s.drawRect(x + 1, y + 1, w - 2, h - 2, darkenColor(border, 50));

  s.loadFont(calibri28);
  s.setTextDatum(MC_DATUM);
  s.setTextColor(WHITE_TEXT, bottom);
  s.drawString(text, x + w / 2, y + h / 2 + 1);
  s.unloadFont();
}

void drawButtonsBlock(UIMode mode) {
  spr.deleteSprite();
  spr.setColorDepth(16);
  spr.createSprite(444, 42);
  spr.fillSprite(BG_COLOR);

  drawButtonToSprite(spr, 0,   0, 200, 42, "Программы", mode == UI_MODE_PROGRAMS);
  drawButtonToSprite(spr, 244, 0, 200, 42, "Ручное",    mode == UI_MODE_MANUAL);

  spr.pushSprite(18, 268);
  spr.deleteSprite();
}

// ============================================================
// UI UPDATE
// ============================================================
void updateDynamicUI(bool force, bool sensorValid) {
  if (force || strcmp(ui.dateStr, prevUi.dateStr) != 0) {
    drawDateTimeBlock(ui.dateStr);
  }

  if (force || !eqFloat(ui.temperature, prevUi.temperature, 0.05f)) {
    drawTemperatureBlock(ui.temperature, sensorValid);
  }

  if (force || !eqFloat(ui.humidity, prevUi.humidity, 0.4f)) {
    drawHumidityBlock(ui.humidity, sensorValid);
  }

  if (force ||
      ui.incubDay != prevUi.incubDay ||
      ui.totalDays != prevUi.totalDays ||
      ui.daysToHatch != prevUi.daysToHatch) {
    drawDayInfoBlock(ui.incubDay, ui.totalDays, ui.daysToHatch);
  }

  if (force ||
      ui.heaterOn != prevUi.heaterOn ||
      ui.humidifierOn != prevUi.humidifierOn ||
      ui.fanAuto != prevUi.fanAuto ||
      strcmp(ui.turnStr, prevUi.turnStr) != 0 ||
      strcmp(ui.activeSensor, prevUi.activeSensor) != 0) {
    drawStatusesBlock(ui.heaterOn, ui.humidifierOn, ui.fanAuto, ui.turnStr, ui.activeSensor);
  }

  if (force || ui.mode != prevUi.mode) {
    drawButtonsBlock(ui.mode);
  }

  prevUi = ui;
}

bool refreshUiState() {
  readDateTime(ui.dateStr, sizeof(ui.dateStr));

  bool sensorValid = readSensors(
    ui.temperature,
    ui.humidity,
    ui.activeSensor,
    sizeof(ui.activeSensor)
  );

  readIncubationProgram(
    ui.incubDay,
    ui.totalDays,
    ui.daysToHatch,
    ui.turnStr,
    sizeof(ui.turnStr)
  );

  readActuators(ui.heaterOn, ui.humidifierOn, ui.fanAuto);
  ui.mode = readUiMode();

  return sensorValid;
}

// ============================================================
// SETUP / LOOP
// ============================================================
void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  tft.fillScreen(BG_COLOR);

  initBME280();

  memset(&ui, 0, sizeof(ui));
  invalidatePrevUi();

  strncpy(ui.dateStr, "--.--.----  --:--:--", sizeof(ui.dateStr));
  ui.dateStr[sizeof(ui.dateStr) - 1] = '\0';

  ui.temperature = 0.0f;
  ui.humidity = 0.0f;
  ui.incubDay = 0;
  ui.totalDays = 0;
  ui.daysToHatch = -1;

  ui.heaterOn = false;
  ui.humidifierOn = false;
  ui.fanAuto = true;

  strncpy(ui.turnStr, "--:--", sizeof(ui.turnStr));
  ui.turnStr[sizeof(ui.turnStr) - 1] = '\0';

  if (bmePresent) {
    strncpy(ui.activeSensor, "BME280", sizeof(ui.activeSensor));
  } else {
    strncpy(ui.activeSensor, "BME280 ERR", sizeof(ui.activeSensor));
  }
  ui.activeSensor[sizeof(ui.activeSensor) - 1] = '\0';

  ui.mode = UI_MODE_PROGRAMS;

  drawLayoutStatic();

  bool sensorValid = refreshUiState();
  updateDynamicUI(true, sensorValid);
}

void loop() {
  const unsigned long now = millis();
  bool sensorValid = bmePresent;

  if (now - lastClockRefresh >= 1000) {
    lastClockRefresh = now;
    readDateTime(ui.dateStr, sizeof(ui.dateStr));
  }

  if (now - lastSensorRefresh >= 1000) {
    lastSensorRefresh = now;

    sensorValid = readSensors(
      ui.temperature,
      ui.humidity,
      ui.activeSensor,
      sizeof(ui.activeSensor)
    );

    readIncubationProgram(
      ui.incubDay,
      ui.totalDays,
      ui.daysToHatch,
      ui.turnStr,
      sizeof(ui.turnStr)
    );

    readActuators(ui.heaterOn, ui.humidifierOn, ui.fanAuto);
  }

  if (now - lastUiRefresh >= 100) {
    lastUiRefresh = now;
    ui.mode = readUiMode();
  }

  updateDynamicUI(false, sensorValid);
}