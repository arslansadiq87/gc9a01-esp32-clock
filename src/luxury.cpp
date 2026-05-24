#if defined(WATCH_FACE_ACTIVE) || defined(BUILD_LUXURY_BACKUP)
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
constexpr uint16_t MATTE = 0x0841;
constexpr uint16_t MATTE_DARK = 0x0208;
constexpr uint16_t CHARCOAL = 0x18C3;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t SOFT_WHITE = 0xD69A;
constexpr uint16_t DIM_WHITE = 0x7BEF;
constexpr uint16_t SHADOW = 0x0000;
constexpr uint16_t ORANGE = 0xFD20;
constexpr uint16_t ORANGE_DARK = 0xB300;

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

Arduino_Canvas *faceCanvas = new Arduino_Canvas(SCREEN_W, SCREEN_H, gfx);

struct Rect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

struct HandGeometry {
  float hourAngle;
  float minuteAngle;
  float secondAngle;
  Rect hourRect;
  Rect minuteRect;
  Rect secondRect;
  Rect centerRect;
  Rect digitalRect;
};

bool timeSynced = false;
bool havePreviousHands = false;
HandGeometry previousHands{};
uint16_t restoreRow[SCREEN_W];
int lastSecondPrinted = -1;
uint32_t lastFrameMs = 0;

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

void centeredText(Arduino_GFX *target, const char *text, int centerX, int centerY, uint16_t fg, uint16_t bg, uint8_t size)
{
  target->setTextColor(fg, bg);
  target->setTextSize(size);

  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  target->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  target->setCursor(centerX - (w / 2) - x1, centerY - (h / 2) - y1);
  target->print(text);
}

void radialLine(Arduino_GFX *target, float angleDeg, int inner, int outer, uint16_t color)
{
  float angle = (angleDeg - 90.0f) * DEG_TO_RAD;
  int x1 = CX + cosf(angle) * inner;
  int y1 = CY + sinf(angle) * inner;
  int x2 = CX + cosf(angle) * outer;
  int y2 = CY + sinf(angle) * outer;
  target->drawLine(x1, y1, x2, y2, color);
}

void drawLuxuryFace()
{
  faceCanvas->fillScreen(BLACK);
  faceCanvas->fillCircle(CX, CY, 118, MATTE_DARK);
  faceCanvas->fillCircle(CX, CY, 111, MATTE);

  for (int r = 110; r >= 72; r -= 8) {
    faceCanvas->drawCircle(CX, CY, r, (r == 110) ? CHARCOAL : 0x1082);
  }

  faceCanvas->drawCircle(CX, CY, 117, DIM_WHITE);
  faceCanvas->drawCircle(CX, CY, 116, CHARCOAL);
  faceCanvas->drawCircle(CX, CY, 105, CHARCOAL);

  for (int i = 0; i < 60; i++) {
    bool major = (i % 5) == 0;
    int inner = major ? 91 : 99;
    int outer = major ? 107 : 104;
    uint16_t color = major ? SOFT_WHITE : 0x528A;
    radialLine(faceCanvas, i * 6.0f, inner, outer, color);
    if (major) {
      radialLine(faceCanvas, i * 6.0f + 0.6f, inner, outer, color);
    }
  }

  faceCanvas->setTextColor(SOFT_WHITE, MATTE);
  faceCanvas->setTextSize(1);
  centeredText(faceCanvas, "12", CX, CY - 78, SOFT_WHITE, MATTE, 2);
  centeredText(faceCanvas, "3", CX + 78, CY, SOFT_WHITE, MATTE, 2);
  centeredText(faceCanvas, "6", CX, CY + 78, SOFT_WHITE, MATTE, 2);
  centeredText(faceCanvas, "9", CX - 78, CY, SOFT_WHITE, MATTE, 2);

  centeredText(faceCanvas, "MINIMAL", CX, CY - 34, DIM_WHITE, MATTE, 1);
  centeredText(faceCanvas, timeSynced ? "NTP" : "BUILD", CX, CY + 46, 0x6B4D, MATTE, 1);
}

Rect clippedRect(int16_t x, int16_t y, int16_t w, int16_t h)
{
  int16_t x2 = x + w - 1;
  int16_t y2 = y + h - 1;
  x = constrain(x, 0, SCREEN_W - 1);
  y = constrain(y, 0, SCREEN_H - 1);
  x2 = constrain(x2, 0, SCREEN_W - 1);
  y2 = constrain(y2, 0, SCREEN_H - 1);
  return Rect{x, y, static_cast<int16_t>(x2 - x + 1), static_cast<int16_t>(y2 - y + 1)};
}

Rect handRect(float angleDeg, int length, int width)
{
  float angle = (angleDeg - 90.0f) * DEG_TO_RAD;
  int endX = CX + cosf(angle) * length;
  int endY = CY + sinf(angle) * length;
  int pad = width + 6;
  int16_t x1 = min(CX, endX) - pad;
  int16_t y1 = min(CY, endY) - pad;
  int16_t x2 = max(CX, endX) + pad;
  int16_t y2 = max(CY, endY) + pad;
  return clippedRect(x1, y1, x2 - x1 + 1, y2 - y1 + 1);
}

void restoreRect(const Rect &rect)
{
  if (rect.w <= 0 || rect.h <= 0) {
    return;
  }

  uint16_t *framebuffer = faceCanvas->getFramebuffer();
  for (int16_t row = 0; row < rect.h; row++) {
    uint16_t *source = framebuffer + ((rect.y + row) * SCREEN_W) + rect.x;
    memcpy(restoreRow, source, rect.w * sizeof(uint16_t));
    gfx->draw16bitRGBBitmap(rect.x, rect.y + row, restoreRow, rect.w, 1);
  }
}

void restorePreviousDynamicAreas()
{
  if (!havePreviousHands) {
    return;
  }

  restoreRect(previousHands.hourRect);
  restoreRect(previousHands.minuteRect);
  restoreRect(previousHands.secondRect);
  restoreRect(previousHands.centerRect);
  restoreRect(previousHands.digitalRect);
}

HandGeometry computeHands(const tm &localTime)
{
  HandGeometry hands{};
  float second = smoothClockSecond(localTime, millis());
  float minute = localTime.tm_min + second / 60.0f;
  float hour = (localTime.tm_hour % 12) + minute / 60.0f;

  hands.hourAngle = hour * 30.0f;
  hands.minuteAngle = minute * 6.0f;
  hands.secondAngle = second * 6.0f;
  hands.hourRect = handRect(hands.hourAngle, 53, 7);
  hands.minuteRect = handRect(hands.minuteAngle, 76, 5);
  hands.secondRect = handRect(hands.secondAngle, 92, 2);
  hands.centerRect = clippedRect(CX - 10, CY - 10, 21, 21);
  hands.digitalRect = clippedRect(74, 158, 92, 16);
  return hands;
}

void drawHand(float angleDeg, int length, uint16_t color, int width)
{
  float angle = (angleDeg - 90.0f) * DEG_TO_RAD;
  int endX = CX + cosf(angle) * length;
  int endY = CY + sinf(angle) * length;

  int shadowX = 2;
  int shadowY = 2;
  for (int offset = -width / 2; offset <= width / 2; offset++) {
    gfx->drawLine(CX + offset + shadowX, CY + shadowY, endX + offset + shadowX, endY + shadowY, SHADOW);
    gfx->drawLine(CX + shadowX, CY + offset + shadowY, endX + shadowX, endY + offset + shadowY, SHADOW);
  }

  for (int offset = -width / 2; offset <= width / 2; offset++) {
    gfx->drawLine(CX + offset, CY, endX + offset, endY, color);
    if (width > 1) {
      gfx->drawLine(CX, CY + offset, endX, endY + offset, color);
    }
  }
}

void drawHands(const HandGeometry &hands)
{
  drawHand(hands.hourAngle, 53, WHITE, 5);
  drawHand(hands.minuteAngle, 76, SOFT_WHITE, 3);
  drawHand(hands.secondAngle, 92, ORANGE, 1);

  float tailAngle = (hands.secondAngle + 180.0f - 90.0f) * DEG_TO_RAD;
  gfx->drawLine(CX, CY, CX + cosf(tailAngle) * 18, CY + sinf(tailAngle) * 18, ORANGE_DARK);

  gfx->fillCircle(CX + 2, CY + 2, 7, SHADOW);
  gfx->fillCircle(CX, CY, 7, MATTE_DARK);
  gfx->drawCircle(CX, CY, 7, SOFT_WHITE);
  gfx->fillCircle(CX, CY, 3, ORANGE);
}

void drawDigitalTime(const tm &localTime)
{
  char buffer[16] = {};
  snprintf(buffer, sizeof(buffer), "%02d:%02d", localTime.tm_hour, localTime.tm_min);

  restoreRect(Rect{74, 158, 92, 16});
  centeredText(gfx, buffer, CX, 166, DIM_WHITE, MATTE, 1);
}

void drawBootStatus(const char *message)
{
  drawLuxuryFace();
  centeredText(faceCanvas, message, CX, CY + 34, ORANGE, MATTE, 1);
  faceCanvas->flush(true);
}

void syncTimeIfConfigured()
{
  setenv("TZ", CLOCK_TZ, 1);
  tzset();

  if (strlen(WIFI_SSID) == 0) {
    Serial.println("WiFi not configured; using build-time clock.");
    drawBootStatus("BUILD-TIME CLOCK");
    delay(900);
    return;
  }

  Serial.println("Connecting WiFi for NTP time sync.");
  drawBootStatus("CONNECTING WIFI");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi unavailable; using build-time clock.");
    drawBootStatus("WIFI UNAVAILABLE");
    delay(900);
    return;
  }

  configTzTime(CLOCK_TZ, "pool.ntp.org", "time.nist.gov");
  drawBootStatus("SYNCING TIME");

  tm localTime{};
  if (getLocalTime(&localTime, 10000)) {
    timeSynced = true;
    Serial.println("NTP time synced.");
    drawBootStatus("NTP SYNCED");
  } else {
    Serial.println("NTP unavailable; using build-time clock.");
    drawBootStatus("NTP UNAVAILABLE");
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
  Serial.println("GC9A01 Minimal Luxury clock starting");

  if (!gfx->begin(TFT_SPI_FREQUENCY)) {
    Serial.println("Display init failed.");
    while (true) {
      digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
      delay(150);
    }
  }

  if (!faceCanvas->begin()) {
    Serial.println("Clock face framebuffer allocation failed.");
    while (true) {
      digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
      delay(500);
    }
  }

  drawLuxuryFace();
  faceCanvas->flush(true);
  syncTimeIfConfigured();
  drawLuxuryFace();
  faceCanvas->flush(true);
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
  restorePreviousDynamicAreas();
  HandGeometry hands = computeHands(localTime);
  drawHands(hands);
  drawDigitalTime(localTime);
  previousHands = hands;
  havePreviousHands = true;

  if (localTime.tm_sec != lastSecondPrinted) {
    lastSecondPrinted = localTime.tm_sec;
    Serial.printf("Luxury %02d:%02d:%02d source=%s\n",
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  timeSynced ? "ntp" : "build");
  }
}
#endif
