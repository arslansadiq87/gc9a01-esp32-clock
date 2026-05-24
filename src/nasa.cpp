#if defined(WATCH_FACE_ACTIVE) || defined(BUILD_NASA_BACKUP)

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
constexpr uint16_t VOID_BLUE = 0x0008;
constexpr uint16_t PANEL_BLUE = 0x018A;
constexpr uint16_t NASA_BLUE = 0x039F;
constexpr uint16_t EARTH_BLUE = 0x04BF;
constexpr uint16_t EARTH_DEEP = 0x0237;
constexpr uint16_t LAND = 0x3D89;
constexpr uint16_t CLOUD = 0xC638;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t WHITE_DIM = 0x8C71;
constexpr uint16_t CYAN = 0x57FF;
constexpr uint16_t CYAN_DIM = 0x1455;
constexpr uint16_t AMBER = 0xFD20;
constexpr uint16_t AMBER_DIM = 0x7AC0;
constexpr uint16_t MOON = 0xBDF7;
constexpr uint16_t MOON_DARK = 0x39E7;

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

void drawArc(int cx, int cy, int radius, float startDeg, float endDeg, uint16_t color, uint8_t step = 3)
{
  float prevX = cx + cosf(startDeg * DEG_TO_RAD) * radius;
  float prevY = cy + sinf(startDeg * DEG_TO_RAD) * radius;

  for (float a = startDeg + step; a <= endDeg; a += step) {
    float rad = a * DEG_TO_RAD;
    float x = cx + cosf(rad) * radius;
    float y = cy + sinf(rad) * radius;
    frame->drawLine((int)prevX, (int)prevY, (int)x, (int)y, color);
    prevX = x;
    prevY = y;
  }
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

void drawStarfield(uint32_t nowMs)
{
  frame->fillScreen(VOID_BLUE);
  frame->fillCircle(CX, CY, 118, BLACK);
  frame->fillCircle(CX, CY, 114, VOID_BLUE);

  for (int i = 0; i < 72; ++i) {
    int x = (i * 47 + 19) % SCREEN_W;
    int y = (i * 83 + 31) % SCREEN_H;
    int dx = x - CX;
    int dy = y - CY;
    if ((dx * dx + dy * dy) > 112 * 112) {
      continue;
    }

    bool bright = ((nowMs / 350 + i * 3) % 11) == 0;
    uint16_t color = bright ? WHITE : ((i % 3) ? WHITE_DIM : CYAN_DIM);
    frame->drawPixel(x, y, color);
    if (bright && (i % 4 == 0)) {
      frame->drawPixel(x + 1, y, color);
      frame->drawPixel(x - 1, y, color);
    }
  }
}

void drawOuterInstrumentRing(uint32_t nowMs)
{
  frame->drawCircle(CX, CY, 114, NASA_BLUE);
  frame->drawCircle(CX, CY, 108, PANEL_BLUE);

  for (int deg = 0; deg < 360; deg += 6) {
    bool major = (deg % 30) == 0;
    radialLine(deg, major ? 101 : 105, 108, major ? WHITE_DIM : CYAN_DIM);
  }

  drawArc(CX, CY, 101, -85, 18, AMBER, 3);
  drawArc(CX, CY, 101, 35, 142, CYAN, 3);
  drawArc(CX, CY, 101, 163, 258, NASA_BLUE, 3);

  float sweep = fmodf(nowMs * 0.018f, 360.0f);
  drawArc(CX, CY, 96, sweep - 24, sweep + 4, CYAN_DIM, 4);
}

void drawEarth(uint32_t nowMs)
{
  frame->fillCircle(CX, CY + 6, 34, EARTH_DEEP);
  frame->fillCircle(CX - 4, CY + 2, 28, EARTH_BLUE);

  frame->fillCircle(CX - 15, CY - 6, 9, LAND);
  frame->fillCircle(CX + 10, CY - 2, 8, LAND);
  frame->fillCircle(CX - 1, CY + 17, 10, LAND);
  frame->fillRect(CX + 4, CY + 13, 16, 6, LAND);
  frame->drawCircle(CX, CY + 6, 34, CYAN);
  frame->drawCircle(CX, CY + 6, 35, CYAN_DIM);

  for (int i = 0; i < 3; ++i) {
    int drift = ((nowMs / 90) + i * 42) % 56;
    frame->drawFastHLine(CX - 28 + drift - 28, CY - 8 + i * 14, 20, CLOUD);
  }
}

void drawOrbitIndicator(uint32_t nowMs, int radius, uint16_t color, float speed, float offset)
{
  drawArc(CX, CY + 6, radius, 0, 360, CYAN_DIM, 5);

  float angle = fmodf((nowMs * speed) + offset, 360.0f);
  float rad = (angle - 90.0f) * DEG_TO_RAD;
  int x = CX + cosf(rad) * radius;
  int y = CY + 6 + sinf(rad) * radius;

  frame->fillCircle(x, y, 3, color);
  frame->drawCircle(x, y, 5, color);

  int tailX = CX + cosf((angle - 102.0f) * DEG_TO_RAD) * radius;
  int tailY = CY + 6 + sinf((angle - 102.0f) * DEG_TO_RAD) * radius;
  frame->drawLine(tailX, tailY, x, y, color);
}

void drawMoonPhase(const tm &localTime)
{
  constexpr int mx = 188;
  constexpr int my = 64;

  frame->drawRoundRect(mx - 25, my - 23, 50, 46, 4, PANEL_BLUE);
  centeredText(mx, my - 32, "MOON", WHITE_DIM);

  int phaseOffset = ((localTime.tm_yday % 29) * 20) / 28 - 10;
  frame->fillCircle(mx, my, 13, MOON);
  frame->fillCircle(mx + phaseOffset, my, 13, VOID_BLUE);
  frame->drawCircle(mx, my, 13, WHITE_DIM);
  frame->drawPixel(mx - 4, my - 5, MOON_DARK);
  frame->drawPixel(mx + 5, my + 3, MOON_DARK);
}

void drawTelemetryBlock(const tm &localTime, uint32_t nowMs)
{
  char text[18];

  centeredText(CX, 18, "MISSION CLOCK", WHITE);
  centeredText(CX, 30, timeSynced ? "NTP LOCK" : "BUILD TIME", timeSynced ? CYAN : AMBER);

  frame->drawRoundRect(12, 48, 54, 54, 4, PANEL_BLUE);
  frame->setTextColor(WHITE_DIM);
  frame->setTextSize(1);
  frame->setCursor(19, 56);
  frame->print("ORB ALT");
  snprintf(text, sizeof(text), "%03dkm", 410 + (int)((nowMs / 900) % 9));
  centeredText(39, 77, text, CYAN);
  frame->drawFastHLine(20, 90, 36, AMBER_DIM);

  frame->drawRoundRect(174, 151, 54, 54, 4, PANEL_BLUE);
  frame->setCursor(184, 159);
  frame->print("VEL");
  snprintf(text, sizeof(text), "7.%02d", (int)((nowMs / 170) % 100));
  centeredText(201, 179, text, CYAN);
  centeredText(201, 191, "km/s", WHITE_DIM);

  frame->drawRoundRect(12, 151, 54, 54, 4, PANEL_BLUE);
  frame->setCursor(20, 159);
  frame->print("LOS");
  snprintf(text, sizeof(text), "%02d:%02d", 59 - localTime.tm_min, 59 - localTime.tm_sec);
  centeredText(39, 181, text, AMBER);

  frame->drawRoundRect(80, 205, 80, 20, 4, PANEL_BLUE);
  snprintf(text, sizeof(text), "%02d:%02d:%02d", localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
  centeredText(CX, 215, text, WHITE);
}

void drawCompassAndLabels()
{
  centeredText(CX, 45, "N", CYAN);
  centeredText(CX, 196, "S", CYAN_DIM);
  centeredText(43, CY, "W", CYAN_DIM);
  centeredText(197, CY, "E", CYAN_DIM);

  for (int i = 0; i < 4; ++i) {
    int y = 112 + (i * 5);
    frame->drawFastHLine(206, y, 16, (i == 2) ? AMBER : CYAN_DIM);
  }
}

void drawAnalogHands(const tm &localTime)
{
  float second = smoothClockSecond(localTime, millis());
  float minute = localTime.tm_min + (second / 60.0f);
  float hour = (localTime.tm_hour % 12) + (minute / 60.0f);

  radialLine(hour * 30.0f, -7, 38, WHITE);
  radialLine(hour * 30.0f + 0.9f, -4, 35, WHITE_DIM);
  radialLine(minute * 6.0f, -10, 58, CYAN);
  radialLine(minute * 6.0f + 0.7f, -7, 55, CYAN_DIM);
  radialLine(second * 6.0f, -14, 72, AMBER);

  frame->fillCircle(CX, CY, 6, WHITE);
  frame->fillCircle(CX, CY, 3, AMBER);
  frame->drawCircle(CX, CY, 9, CYAN_DIM);
}

void renderFrame(const tm &localTime)
{
  uint32_t nowMs = millis();

  drawStarfield(nowMs);
  drawOuterInstrumentRing(nowMs);
  drawOrbitIndicator(nowMs, 48, AMBER, 0.055f, 0);
  drawOrbitIndicator(nowMs, 72, CYAN, 0.027f, 180);
  drawEarth(nowMs);
  drawCompassAndLabels();
  drawMoonPhase(localTime);
  drawTelemetryBlock(localTime, nowMs);
  drawAnalogHands(localTime);

  frame->flush();
}

void drawBootStatus(const char *status)
{
  frame->fillScreen(VOID_BLUE);
  frame->drawCircle(CX, CY, 108, NASA_BLUE);
  centeredText(CX, 90, "NASA MODE", WHITE, 2);
  centeredText(CX, 118, status, CYAN);
  centeredText(CX, 140, "GC9A01 ONLINE", AMBER);
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
  Serial.println("Space / NASA Mission Control watch face booting");
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
    Serial.printf("Mission clock %02d:%02d:%02d source=%s\n",
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  timeSynced ? "ntp" : "build");
  }
}

#endif
