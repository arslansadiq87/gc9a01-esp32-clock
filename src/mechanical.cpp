#if defined(WATCH_FACE_ACTIVE) || defined(BUILD_MECHANICAL_BACKUP)

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
constexpr uint16_t CASE_DARK = 0x1082;
constexpr uint16_t CASE = 0x39E7;
constexpr uint16_t STEEL_DARK = 0x3186;
constexpr uint16_t STEEL = 0x7BEF;
constexpr uint16_t STEEL_LIGHT = 0xD69A;
constexpr uint16_t GOLD_DARK = 0x9B20;
constexpr uint16_t GOLD = 0xFDE0;
constexpr uint16_t COPPER = 0xE3A0;
constexpr uint16_t RUBY = 0xC006;
constexpr uint16_t BLUE_SCREW = 0x041F;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t ORANGE = 0xFD20;
constexpr uint16_t SHADOW = 0x0000;
constexpr uint16_t WARNING = 0xFFE0;

Arduino_DataBus *bus = new Arduino_ESP32SPI(
    TFT_DC,
    TFT_CS,
    TFT_SCLK,
    TFT_MOSI,
    GFX_NOT_DEFINED);

Arduino_GFX *gfx = new Arduino_GC9A01(
    bus,
    TFT_RST,
    0,
    true);

Arduino_Canvas *frame = new Arduino_Canvas(SCREEN_W, SCREEN_H, gfx);

bool timeSynced = false;
uint32_t lastFrameMs = 0;
int lastSecondPrinted = -1;

time_t buildEpoch()
{
  tm t{};
  const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char monthStr[4] = {};
  int day = 1;
  int year = 1970;
  int hour = 0;
  int minute = 0;
  int second = 0;

  sscanf(__DATE__, "%3s %d %d", monthStr, &day, &year);
  sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

  const char *monthPos = strstr(months, monthStr);
  int month = monthPos ? (monthPos - months) / 3 : 0;

  t.tm_year = year - 1900;
  t.tm_mon = month;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_sec = second;
  t.tm_isdst = -1;
  return mktime(&t);
}

time_t nowEpoch()
{
  if (timeSynced) {
    return time(nullptr);
  }

  static const time_t startEpoch = buildEpoch();
  return startEpoch + (millis() / 1000);
}

void centeredText(const char *text, int centerX, int centerY, uint16_t fg, uint16_t bg, uint8_t size)
{
  frame->setTextColor(fg, bg);
  frame->setTextSize(size);

  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  frame->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  frame->setCursor(centerX - (w / 2) - x1, centerY - (h / 2) - y1);
  frame->print(text);
}

void radialLineFrom(int cx, int cy, float angleDeg, int inner, int outer, uint16_t color)
{
  float angle = (angleDeg - 90.0f) * DEG_TO_RAD;
  int x1 = cx + cosf(angle) * inner;
  int y1 = cy + sinf(angle) * inner;
  int x2 = cx + cosf(angle) * outer;
  int y2 = cy + sinf(angle) * outer;
  frame->drawLine(x1, y1, x2, y2, color);
}

void radialLine(float angleDeg, int inner, int outer, uint16_t color)
{
  radialLineFrom(CX, CY, angleDeg, inner, outer, color);
}

void drawGear(int cx, int cy, int radius, int teeth, float phase, uint16_t body, uint16_t edge)
{
  frame->fillCircle(cx + 2, cy + 2, radius + 3, SHADOW);

  for (int i = 0; i < teeth; i++) {
    float angle = (phase + i * (360.0f / teeth) - 90.0f) * DEG_TO_RAD;
    int x = cx + cosf(angle) * (radius + 3);
    int y = cy + sinf(angle) * (radius + 3);
    frame->fillCircle(x, y, max(2, radius / 8), body);
  }

  frame->fillCircle(cx, cy, radius, body);
  frame->drawCircle(cx, cy, radius, edge);
  frame->drawCircle(cx, cy, radius - 5, edge);

  for (int i = 0; i < 6; i++) {
    radialLineFrom(cx, cy, phase + i * 60.0f, 7, radius - 6, edge);
  }

  frame->fillCircle(cx, cy, max(4, radius / 5), BLACK);
  frame->drawCircle(cx, cy, max(4, radius / 5), edge);
  frame->fillCircle(cx, cy, 2, RUBY);
}

void drawBridge(int x1, int y1, int x2, int y2, uint16_t color)
{
  frame->drawLine(x1 + 2, y1 + 2, x2 + 2, y2 + 2, SHADOW);
  frame->drawLine(x1, y1, x2, y2, color);
  frame->drawLine(x1 + 1, y1, x2 + 1, y2, color);
  frame->fillCircle(x1, y1, 4, BLUE_SCREW);
  frame->fillCircle(x2, y2, 4, BLUE_SCREW);
  frame->drawCircle(x1, y1, 5, STEEL_LIGHT);
  frame->drawCircle(x2, y2, 5, STEEL_LIGHT);
}

void drawCaseAndMarkers()
{
  frame->fillScreen(BLACK);
  frame->fillCircle(CX, CY, 119, CASE_DARK);
  frame->fillCircle(CX, CY, 113, BLACK);
  frame->drawCircle(CX, CY, 118, STEEL_LIGHT);
  frame->drawCircle(CX, CY, 116, CASE);
  frame->drawCircle(CX, CY, 108, STEEL_DARK);

  for (int i = 0; i < 60; i++) {
    bool major = (i % 5) == 0;
    radialLine(i * 6.0f, major ? 96 : 103, 110, major ? STEEL_LIGHT : STEEL_DARK);
  }

  centeredText("12", CX, CY - 83, STEEL_LIGHT, BLACK, 1);
  centeredText("3", CX + 83, CY, STEEL_LIGHT, BLACK, 1);
  centeredText("6", CX, CY + 83, STEEL_LIGHT, BLACK, 1);
  centeredText("9", CX - 83, CY, STEEL_LIGHT, BLACK, 1);
}

void drawEscapement(uint32_t nowMs)
{
  float tick = (nowMs / 160) % 2 == 0 ? -10.0f : 10.0f;
  int x = CX;
  int y = CY + 58;

  frame->drawCircle(x, y, 23, STEEL_DARK);
  frame->drawCircle(x, y, 18, STEEL);
  frame->drawLine(x - 25, y + 18, x + 25, y + 18, STEEL_DARK);

  float angle = (tick - 90.0f) * DEG_TO_RAD;
  int ax = x + cosf(angle) * 28;
  int ay = y + sinf(angle) * 28;
  int bx = x - cosf(angle) * 28;
  int by = y - sinf(angle) * 28;
  frame->drawLine(ax + 2, ay + 2, bx + 2, by + 2, SHADOW);
  frame->drawLine(ax, ay, bx, by, GOLD);
  frame->fillCircle(x, y, 5, RUBY);
}

void drawMechanism(uint32_t nowMs)
{
  float phase = nowMs / 24.0f;

  drawBridge(47, 83, 193, 78, STEEL);
  drawBridge(55, 161, 183, 151, STEEL_DARK);
  drawBridge(79, 48, 158, 190, STEEL);

  drawGear(69, 85, 27, 18, phase, GOLD_DARK, GOLD);
  drawGear(164, 82, 24, 16, -phase * 1.35f, STEEL_DARK, STEEL_LIGHT);
  drawGear(83, 154, 23, 15, -phase * 1.8f, COPPER, GOLD);
  drawGear(158, 156, 31, 20, phase * 0.75f, STEEL_DARK, STEEL);
  drawGear(CX, CY, 36, 24, -phase * 0.45f, 0x4208, STEEL_LIGHT);

  drawEscapement(nowMs);
}

void drawTaperedHand(float angleDeg, int length, int tail, int halfWidth, uint16_t color, uint16_t shadow)
{
  float angle = (angleDeg - 90.0f) * DEG_TO_RAD;
  float side = angle + 1.5707963f;

  int tipX = CX + cosf(angle) * length;
  int tipY = CY + sinf(angle) * length;
  int tailX = CX - cosf(angle) * tail;
  int tailY = CY - sinf(angle) * tail;
  int leftX = CX + cosf(side) * halfWidth;
  int leftY = CY + sinf(side) * halfWidth;
  int rightX = CX - cosf(side) * halfWidth;
  int rightY = CY - sinf(side) * halfWidth;

  frame->fillTriangle(leftX + 2, leftY + 2, rightX + 2, rightY + 2, tipX + 2, tipY + 2, shadow);
  frame->drawLine(tailX + 2, tailY + 2, CX + 2, CY + 2, shadow);
  frame->fillTriangle(leftX, leftY, rightX, rightY, tipX, tipY, color);
  frame->drawLine(tailX, tailY, CX, CY, color);
}

void drawHands(const tm &localTime)
{
  float second = smoothClockSecond(localTime, millis());
  float minute = localTime.tm_min + second / 60.0f;
  float hour = (localTime.tm_hour % 12) + minute / 60.0f;

  drawTaperedHand(hour * 30.0f, 48, 12, 5, WHITE, SHADOW);
  drawTaperedHand(minute * 6.0f, 74, 15, 3, STEEL_LIGHT, SHADOW);
  radialLine(second * 6.0f, -20, 92, ORANGE);

  frame->fillCircle(CX + 2, CY + 2, 9, SHADOW);
  frame->fillCircle(CX, CY, 9, BLACK);
  frame->drawCircle(CX, CY, 9, GOLD);
  frame->fillCircle(CX, CY, 4, RUBY);
}

void drawStatusText(const tm &localTime)
{
  char buffer[16] = {};
  snprintf(buffer, sizeof(buffer), "%02d:%02d", localTime.tm_hour, localTime.tm_min);
  centeredText(buffer, CX, 178, STEEL_LIGHT, BLACK, 1);
  centeredText(timeSynced ? "CHRONO SYNC" : "BUILD WIND", CX, 194, timeSynced ? GOLD : WARNING, BLACK, 1);
}

void renderFrame(const tm &localTime)
{
  uint32_t nowMs = millis();
  drawCaseAndMarkers();
  drawMechanism(nowMs);
  drawHands(localTime);
  drawStatusText(localTime);
  frame->flush(true);
}

void drawBootStatus(const char *message)
{
  frame->fillScreen(BLACK);
  drawCaseAndMarkers();
  drawMechanism(millis());
  centeredText(message, CX, 194, GOLD, BLACK, 1);
  frame->flush(true);
}

void syncTimeIfConfigured()
{
  setenv("TZ", CLOCK_TZ, 1);
  tzset();

  if (strlen(WIFI_SSID) == 0) {
    Serial.println("WiFi not configured; using build-time clock.");
    drawBootStatus("BUILD-TIME WIND");
    delay(900);
    return;
  }

  Serial.println("Connecting WiFi for NTP time sync.");
  drawBootStatus("CONNECTING");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi unavailable; using build-time clock.");
    drawBootStatus("WIFI OFFLINE");
    delay(900);
    return;
  }

  configTzTime(CLOCK_TZ, "pool.ntp.org", "time.nist.gov");
  drawBootStatus("SYNCING");

  tm localTime{};
  if (getLocalTime(&localTime, 10000)) {
    timeSynced = true;
    Serial.println("NTP time synced.");
    drawBootStatus("SYNCED");
  } else {
    Serial.println("NTP unavailable; using build-time clock.");
    drawBootStatus("NTP OFFLINE");
  }
  delay(900);
}

void setup()
{
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("GC9A01 Mechanical Skeleton clock starting");

  if (!gfx->begin(TFT_SPI_FREQUENCY)) {
    Serial.println("Display init failed.");
    while (true) {
      digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
      delay(150);
    }
  }

  if (!frame->begin()) {
    Serial.println("Frame buffer allocation failed.");
    while (true) {
      digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
      delay(500);
    }
  }

  drawBootStatus("WINDING");
  delay(700);
  syncTimeIfConfigured();
}

void loop()
{
  if (millis() - lastFrameMs < 100) {
    delay(5);
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
    Serial.printf("Skeleton %02d:%02d:%02d source=%s\n",
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  timeSynced ? "ntp" : "build");
  }
}

#endif
