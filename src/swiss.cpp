#if defined(WATCH_FACE_ACTIVE) || defined(BUILD_SWISS_BACKUP)

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
constexpr uint16_t DIAL = 0x0841;
constexpr uint16_t DIAL_EDGE = 0x2104;
constexpr uint16_t SHADOW = 0x0000;
constexpr uint16_t STEEL_DARK = 0x5AEB;
constexpr uint16_t STEEL = 0xBDF7;
constexpr uint16_t STEEL_BRIGHT = 0xFFFF;
constexpr uint16_t GOLD_DARK = 0x7B80;
constexpr uint16_t GOLD = 0xDDA0;
constexpr uint16_t GOLD_BRIGHT = 0xFEE0;
constexpr uint16_t IVORY = 0xF7BE;
constexpr uint16_t TEXT_DIM = 0x9CF3;
constexpr uint16_t DATE_BG = 0x18C3;
constexpr uint16_t DATE_EDGE = 0x6B4D;
constexpr uint16_t SECOND_RED = 0xA145;

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

void drawThickHand(float angleDeg, int innerRadius, int outerRadius, int halfWidth, uint16_t fill, uint16_t edge)
{
  float a = (angleDeg - 90.0f) * DEG_TO_RAD;
  float p = a + HALF_PI;
  int bx = CX + cosf(a) * innerRadius;
  int by = CY + sinf(a) * innerRadius;
  int tx = CX + cosf(a) * outerRadius;
  int ty = CY + sinf(a) * outerRadius;
  int ox = cosf(p) * halfWidth;
  int oy = sinf(p) * halfWidth;

  frame->fillTriangle(bx + ox, by + oy, bx - ox, by - oy, tx, ty, fill);
  frame->drawLine(bx + ox, by + oy, tx, ty, edge);
  frame->drawLine(bx - ox, by - oy, tx, ty, edge);
}

void drawBrushedDial()
{
  frame->fillScreen(BLACK);

  for (int r = 116; r >= 0; --r) {
    uint16_t color = (r > 108) ? DIAL_EDGE : DIAL;
    if (r < 80 && (r % 7 == 0)) {
      color = 0x1082;
    }
    frame->drawCircle(CX, CY, r, color);
  }

  for (int y = 22; y <= 218; y += 5) {
    int span = sqrtf(112 * 112 - (y - CY) * (y - CY));
    uint16_t color = ((y / 5) % 2) ? 0x1082 : 0x0841;
    frame->drawFastHLine(CX - span, y, span * 2, color);
  }

  frame->drawCircle(CX, CY, 115, GOLD_DARK);
  frame->drawCircle(CX, CY, 112, GOLD_BRIGHT);
  frame->drawCircle(CX, CY, 107, STEEL_DARK);
  frame->drawCircle(CX, CY, 104, SHADOW);
}

void drawAppliedMarker(float angleDeg, bool quarter, bool fiveMinute)
{
  float a = (angleDeg - 90.0f) * DEG_TO_RAD;
  float p = a + HALF_PI;
  int outer = 98;
  int inner = quarter ? 73 : (fiveMinute ? 82 : 91);
  int half = quarter ? 4 : (fiveMinute ? 3 : 1);
  uint16_t fill = quarter ? GOLD_BRIGHT : STEEL;
  uint16_t edge = quarter ? GOLD_DARK : STEEL_DARK;

  int x1 = CX + cosf(a) * outer;
  int y1 = CY + sinf(a) * outer;
  int x2 = CX + cosf(a) * inner;
  int y2 = CY + sinf(a) * inner;
  int ox = cosf(p) * half;
  int oy = sinf(p) * half;

  frame->fillTriangle(x1 + ox, y1 + oy, x1 - ox, y1 - oy, x2, y2, fill);
  frame->drawLine(x1 + ox, y1 + oy, x2, y2, edge);
  frame->drawLine(x1 - ox, y1 - oy, x2, y2, edge);
}

void drawDialDetails()
{
  for (int deg = 0; deg < 360; deg += 6) {
    bool fiveMinute = (deg % 30) == 0;
    bool quarter = (deg % 90) == 0;
    drawAppliedMarker(deg, quarter, fiveMinute);
  }

  centeredText(CX, 52, "GENEVA", GOLD_BRIGHT);
  centeredText(CX, 66, "AUTOMATIC", TEXT_DIM);
  centeredText(CX, 172, timeSynced ? "CHRONOMETER" : "MECHANICAL", TEXT_DIM);

  frame->drawCircle(CX, CY, 70, 0x18C3);
  frame->drawCircle(CX, CY, 69, SHADOW);
}

void drawDateWindow(const tm &localTime)
{
  char dateText[4];
  snprintf(dateText, sizeof(dateText), "%02d", localTime.tm_mday);

  int x = 166;
  int y = 112;
  frame->fillRoundRect(x - 2, y + 2, 36, 22, 3, SHADOW);
  frame->fillRoundRect(x, y, 36, 22, 3, DATE_BG);
  frame->drawRoundRect(x, y, 36, 22, 3, DATE_EDGE);
  frame->drawFastVLine(x + 26, y + 4, 14, GOLD_DARK);
  centeredText(x + 13, y + 11, dateText, IVORY);
  centeredText(x + 31, y + 11, "D", GOLD);
}

void drawSubtleSecondsTrack(uint32_t nowMs)
{
  int glow = (nowMs / 180) % 60;
  for (int i = 0; i < 60; i += 5) {
    float angle = i * 6.0f;
    uint16_t color = (abs(i - glow) <= 2) ? GOLD : 0x18C3;
    radialLine(angle, 61, 64, color);
  }
}

void drawHands(const tm &localTime, uint32_t nowMs)
{
  float second = smoothClockSecond(localTime, nowMs);
  float minute = localTime.tm_min + (second / 60.0f);
  float hour = (localTime.tm_hour % 12) + (minute / 60.0f);

  drawThickHand(hour * 30.0f, -10, 46, 5, GOLD_BRIGHT, GOLD_DARK);
  drawThickHand(minute * 6.0f, -12, 72, 4, IVORY, STEEL_DARK);
  radialLine(second * 6.0f, -18, 84, SECOND_RED);
  radialLine(second * 6.0f + 0.35f, -12, 78, GOLD);

  frame->fillCircle(CX + 2, CY + 3, 11, SHADOW);
  frame->fillCircle(CX, CY, 10, GOLD_BRIGHT);
  frame->drawCircle(CX, CY, 11, GOLD_DARK);
  frame->fillCircle(CX, CY, 4, DIAL);
}

void renderFrame(const tm &localTime)
{
  uint32_t nowMs = millis();
  drawBrushedDial();
  drawDialDetails();
  drawDateWindow(localTime);
  drawSubtleSecondsTrack(nowMs);
  drawHands(localTime, nowMs);
  frame->flush();
}

void drawBootStatus(const char *status)
{
  frame->fillScreen(BLACK);
  frame->fillCircle(CX, CY, 112, DIAL);
  frame->drawCircle(CX, CY, 112, GOLD_BRIGHT);
  frame->drawCircle(CX, CY, 103, STEEL_DARK);
  centeredText(CX, 92, "SWISS", GOLD_BRIGHT, 2);
  centeredText(CX, 120, status, IVORY);
  centeredText(CX, 142, "AUTOMATIC ONLINE", TEXT_DIM);
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
  Serial.println("Minimal Swiss Luxury watch face booting");
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
  if (millis() - lastFrameMs < 50) {
    delay(3);
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
    Serial.printf("Swiss luxury %02d:%02d:%02d date=%02d source=%s\n",
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  localTime.tm_mday,
                  timeSynced ? "ntp" : "build");
  }
}

#endif
