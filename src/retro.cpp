#if defined(WATCH_FACE_ACTIVE) || defined(BUILD_RETRO_BACKUP)

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "clock_motion.h"

constexpr int STATUS_LED = 15;

// Verified GC9A01 wiring for ESP32-C6 Super Mini.
constexpr int TFT_SCLK = 4;
constexpr int TFT_MOSI = 6;
constexpr int TFT_DC = 2;
constexpr int TFT_CS = 7;
constexpr int TFT_RST = 1;

constexpr uint32_t TFT_SPI_FREQUENCY = 20000000;
constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 240;
constexpr int CX = SCREEN_W / 2;
constexpr int CY = SCREEN_H / 2;

constexpr uint16_t BLACK = 0x0000;
constexpr uint16_t CASE = 0x1082;
constexpr uint16_t CASE_EDGE = 0x39E7;
constexpr uint16_t CASE_SHADOW = 0x0000;
constexpr uint16_t LCD = 0x9D21;
constexpr uint16_t LCD_DARK = 0x4AE0;
constexpr uint16_t LCD_TEXT = 0x1120;
constexpr uint16_t AMBER = 0xFD20;
constexpr uint16_t AMBER_DIM = 0x9B60;
constexpr uint16_t ORANGE = 0xFBE0;
constexpr uint16_t CREAM = 0xFFB0;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t STEEL = 0xBDF7;
constexpr uint16_t RED = 0xC145;

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, TFT_RST, 0, true);
Arduino_Canvas *frame = new Arduino_Canvas(SCREEN_W, SCREEN_H, gfx);

bool timeSynced = false;
uint32_t lastFrameMs = 0;
int lastSecondPrinted = -1;

time_t buildEpoch()
{
  tm t{};
  const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char monthText[4] = {0};
  int day = 1;
  int year = 2026;
  int hour = 0;
  int minute = 0;
  int second = 0;

  sscanf(__DATE__, "%3s %d %d", monthText, &day, &year);
  sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

  const char *monthPtr = strstr(months, monthText);
  t.tm_mon = monthPtr ? (monthPtr - months) / 3 : 0;
  t.tm_mday = day;
  t.tm_year = year - 1900;
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_sec = second;
  t.tm_isdst = -1;
  return mktime(&t);
}

time_t nowEpoch()
{
  time_t now = time(nullptr);
  if (now > 1700000000) {
    return now;
  }

  static const time_t bootEpoch = buildEpoch();
  return bootEpoch + (millis() / 1000);
}

void centeredText(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t size = 1)
{
  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;
  frame->setTextSize(size);
  frame->setTextColor(color);
  frame->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  frame->setCursor(x - (w / 2), y - (h / 2));
  frame->print(text);
}

void radialLine(float angleDeg, int innerRadius, int outerRadius, uint16_t color)
{
  float a = (angleDeg - 90.0f) * DEG_TO_RAD;
  int x1 = CX + cosf(a) * innerRadius;
  int y1 = CY + sinf(a) * innerRadius;
  int x2 = CX + cosf(a) * outerRadius;
  int y2 = CY + sinf(a) * outerRadius;
  frame->drawLine(x1, y1, x2, y2, color);
}

void drawCase()
{
  frame->fillScreen(BLACK);
  frame->fillCircle(CX, CY, 118, CASE_SHADOW);
  frame->fillCircle(CX, CY, 114, CASE);
  frame->drawCircle(CX, CY, 115, CASE_EDGE);
  frame->drawCircle(CX, CY, 109, BLACK);
  frame->drawCircle(CX, CY, 104, AMBER_DIM);

  for (int i = 0; i < 24; ++i) {
    int y = 25 + i * 8;
    int dy = y - CY;
    int span = sqrtf(max(0, 107 * 107 - dy * dy));
    frame->drawFastHLine(CX - span, y, span * 2, (i % 2) ? 0x18C3 : 0x2104);
  }
}

void drawLcdWindow(int x, int y, int w, int h)
{
  frame->fillRoundRect(x + 2, y + 3, w, h, 4, CASE_SHADOW);
  frame->fillRoundRect(x, y, w, h, 4, LCD_DARK);
  frame->fillRoundRect(x + 3, y + 3, w - 6, h - 6, 3, LCD);
  frame->drawRoundRect(x, y, w, h, 4, BLACK);
  frame->drawRoundRect(x + 2, y + 2, w - 4, h - 4, 3, 0x6BC0);
}

void drawSevenSegmentDigit(int x, int y, int digit, uint16_t color)
{
  static const uint8_t segs[10] = {
      0b1111110, 0b0110000, 0b1101101, 0b1111001, 0b0110011,
      0b1011011, 0b1011111, 0b1110000, 0b1111111, 0b1111011};

  const int len = 12;
  const int thick = 3;
  uint8_t mask = segs[digit % 10];

  auto hseg = [&](int sx, int sy) {
    frame->fillRoundRect(x + sx, y + sy, len, thick, 1, color);
  };
  auto vseg = [&](int sx, int sy) {
    frame->fillRoundRect(x + sx, y + sy, thick, len, 1, color);
  };

  if (mask & 0b1000000) hseg(3, 0);
  if (mask & 0b0100000) vseg(15, 3);
  if (mask & 0b0010000) vseg(15, 17);
  if (mask & 0b0001000) hseg(3, 29);
  if (mask & 0b0000100) vseg(0, 17);
  if (mask & 0b0000010) vseg(0, 3);
  if (mask & 0b0000001) hseg(3, 14);
}

void drawSegmentTime(const tm &localTime)
{
  int h1 = localTime.tm_hour / 10;
  int h2 = localTime.tm_hour % 10;
  int m1 = localTime.tm_min / 10;
  int m2 = localTime.tm_min % 10;

  drawLcdWindow(42, 82, 156, 54);
  drawSevenSegmentDigit(55, 94, h1, LCD_TEXT);
  drawSevenSegmentDigit(78, 94, h2, LCD_TEXT);
  frame->fillRect(102, 103, 4, 4, LCD_TEXT);
  frame->fillRect(102, 119, 4, 4, LCD_TEXT);
  drawSevenSegmentDigit(113, 94, m1, LCD_TEXT);
  drawSevenSegmentDigit(136, 94, m2, LCD_TEXT);

  char seconds[6];
  snprintf(seconds, sizeof(seconds), "%02d", localTime.tm_sec);
  centeredText(178, 112, seconds, LCD_TEXT);
}

void drawMiniLcdWidgets(const tm &localTime, uint32_t nowMs)
{
  char text[16];
  static const char *days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

  drawLcdWindow(40, 43, 72, 25);
  snprintf(text, sizeof(text), "%s %02d", days[localTime.tm_wday], localTime.tm_mday);
  centeredText(76, 56, text, LCD_TEXT);

  drawLcdWindow(128, 43, 72, 25);
  snprintf(text, sizeof(text), "%02d-%02d", localTime.tm_mon + 1, (localTime.tm_year + 1900) % 100);
  centeredText(164, 56, text, LCD_TEXT);

  drawLcdWindow(38, 153, 61, 27);
  snprintf(text, sizeof(text), "%s", timeSynced ? "WAVE" : "SET");
  centeredText(68, 166, text, LCD_TEXT);

  drawLcdWindow(141, 153, 61, 27);
  int battery = 72 + (int)(sinf(nowMs * 0.0014f) * 10.0f);
  snprintf(text, sizeof(text), "BAT %02d", battery);
  centeredText(171, 166, text, LCD_TEXT);
}

void drawRetroLabels()
{
  centeredText(CX, 22, "CASIO HYBRID", AMBER, 1);
  centeredText(CX, 34, "MULTI ALARM  CHRONO", CREAM, 1);
  centeredText(CX, 199, "RETRO ANALOG", ORANGE, 1);
  centeredText(CX, 212, "WATER RESIST", CREAM, 1);

  frame->drawRoundRect(30, 75, 180, 70, 6, AMBER_DIM);
  frame->drawRoundRect(34, 78, 172, 64, 4, BLACK);
}

void drawAnalogMarkers()
{
  for (int deg = 0; deg < 360; deg += 6) {
    bool major = (deg % 30) == 0;
    radialLine(deg, major ? 96 : 101, 105, major ? CREAM : AMBER_DIM);
  }

  for (int deg = 0; deg < 360; deg += 90) {
    radialLine(deg, 88, 105, ORANGE);
  }
}

void drawModeIndicators(uint32_t nowMs)
{
  const char *modes[] = {"ALM", "SIG", "LAP", "TMR"};
  for (int i = 0; i < 4; ++i) {
    int angle = i * 90 + 45;
    float a = (angle - 90.0f) * DEG_TO_RAD;
    int x = CX + cosf(a) * 90;
    int y = CY + sinf(a) * 90;
    bool active = ((nowMs / 700 + i) % 4) == 0;
    frame->fillRoundRect(x - 13, y - 6, 26, 12, 2, active ? ORANGE : CASE);
    centeredText(x, y, modes[i], active ? BLACK : CREAM);
  }
}

void drawHands(const tm &localTime, uint32_t nowMs)
{
  float second = smoothClockSecond(localTime, nowMs);
  float minute = localTime.tm_min + (second / 60.0f);
  float hour = (localTime.tm_hour % 12) + (minute / 60.0f);

  radialLine(hour * 30.0f, -8, 41, CREAM);
  radialLine(hour * 30.0f + 1.1f, -5, 38, AMBER_DIM);
  radialLine(minute * 6.0f, -10, 64, WHITE);
  radialLine(minute * 6.0f + 0.8f, -6, 60, STEEL);
  radialLine(second * 6.0f, -18, 83, RED);

  frame->fillCircle(CX + 2, CY + 2, 10, CASE_SHADOW);
  frame->fillCircle(CX, CY, 9, ORANGE);
  frame->drawCircle(CX, CY, 10, BLACK);
  frame->fillCircle(CX, CY, 4, LCD_TEXT);
}

void renderFrame(const tm &localTime)
{
  uint32_t nowMs = millis();
  drawCase();
  drawAnalogMarkers();
  drawRetroLabels();
  drawMiniLcdWidgets(localTime, nowMs);
  drawSegmentTime(localTime);
  drawModeIndicators(nowMs);
  drawHands(localTime, nowMs);
  frame->flush();
}

void drawBootStatus(const char *status)
{
  frame->fillScreen(BLACK);
  frame->fillCircle(CX, CY, 112, CASE);
  frame->drawCircle(CX, CY, 108, AMBER);
  drawLcdWindow(52, 88, 136, 44);
  centeredText(CX, 99, "CASIO HYBRID", LCD_TEXT);
  centeredText(CX, 119, status, LCD_TEXT, 2);
  centeredText(CX, 148, "RETRO MODULE", CREAM);
  frame->flush();
}

void syncTimeIfConfigured()
{
  if (strlen(WIFI_SSID) == 0) {
    Serial.println("WiFi SSID empty. Clock using compile time fallback.");
    drawBootStatus("NO WIFI CFG");
    delay(800);
    return;
  }

  drawBootStatus("WIFI LINK");
  Serial.printf("Connecting WiFi SSID: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection failed. Clock using compile time fallback.");
    WiFi.disconnect(true);
    drawBootStatus("WIFI FAILED");
    delay(800);
    return;
  }

  configTzTime(CLOCK_TZ, "pool.ntp.org", "time.nist.gov");
  for (int i = 0; i < 30; ++i) {
    time_t now = time(nullptr);
    if (now > 1700000000) {
      timeSynced = true;
      Serial.println("NTP time synced.");
      drawBootStatus("NTP LOCK");
      delay(800);
      return;
    }
    delay(300);
  }

  Serial.println("NTP sync timed out. Clock using compile time fallback.");
  drawBootStatus("NTP TIMEOUT");
  delay(800);
}

void setup()
{
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  Serial.begin(115200);
  uint32_t serialStart = millis();
  while (!Serial && millis() - serialStart < 2500) {
    delay(10);
  }

  Serial.println();
  Serial.println("Retro Casio Digital + Analog Hybrid watch face booting");
  Serial.printf("Pins SCLK=%d MOSI=%d DC=%d CS=%d RST=%d\n", TFT_SCLK, TFT_MOSI, TFT_DC, TFT_CS, TFT_RST);

  if (!gfx->begin(TFT_SPI_FREQUENCY)) {
    Serial.println("gfx->begin() FAILED");
    while (true) {
      digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
      delay(250);
    }
  }

  if (!frame->begin()) {
    Serial.println("canvas begin FAILED");
    while (true) {
      digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
      delay(500);
    }
  }

  frame->setRotation(0);
  frame->fillScreen(BLACK);
  frame->flush();

  drawBootStatus("BOOT");
  delay(600);
  syncTimeIfConfigured();
}

void loop()
{
  if (millis() - lastFrameMs < 80) {
    delay(4);
    return;
  }
  lastFrameMs = millis();

  time_t now = nowEpoch();
  tm localTime{};
  localtime_r(&now, &localTime);

  digitalWrite(STATUS_LED, (localTime.tm_sec % 2) ? HIGH : LOW);
  renderFrame(localTime);

  if (localTime.tm_sec != lastSecondPrinted) {
    lastSecondPrinted = localTime.tm_sec;
    Serial.printf("Retro Casio %02d:%02d:%02d date=%02d source=%s\n",
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  localTime.tm_mday,
                  timeSynced ? "ntp" : "build");
  }
}

#endif
