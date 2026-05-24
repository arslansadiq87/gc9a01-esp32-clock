#if defined(WATCH_FACE_ACTIVE) || defined(BUILD_ARC_REACTOR_BACKUP)

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
constexpr uint16_t REACTOR_DARK = 0x0128;
constexpr uint16_t REACTOR_PANEL = 0x094B;
constexpr uint16_t CYAN_DARK = 0x02D6;
constexpr uint16_t CYAN = 0x57FF;
constexpr uint16_t CYAN_WHITE = 0xDFFF;
constexpr uint16_t STEEL_DARK = 0x4A49;
constexpr uint16_t STEEL = 0xBDF7;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t AMBER = 0xFD20;
constexpr uint16_t ARC_BLUE = 0x249F;
constexpr uint16_t RED_WARN = 0xF9A6;

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

int orbitX(float angleDeg, int radius)
{
  return CX + cosf((angleDeg - 90.0f) * DEG_TO_RAD) * radius;
}

int orbitY(float angleDeg, int radius)
{
  return CY + sinf((angleDeg - 90.0f) * DEG_TO_RAD) * radius;
}

void drawBackground(uint32_t nowMs)
{
  frame->fillScreen(BLACK);
  frame->fillCircle(CX, CY, 118, VOID_BLUE);
  frame->fillCircle(CX, CY, 114, BLACK);

  for (int i = 0; i < 36; ++i) {
    float a = i * 10.0f + fmodf(nowMs * 0.006f, 10.0f);
    int x1 = orbitX(a, 28);
    int y1 = orbitY(a, 28);
    int x2 = orbitX(a, 108);
    int y2 = orbitY(a, 108);
    frame->drawLine(x1, y1, x2, y2, (i % 3 == 0) ? REACTOR_PANEL : 0x0045);
  }

  frame->drawCircle(CX, CY, 115, CYAN_DARK);
  frame->drawCircle(CX, CY, 109, STEEL_DARK);
}

void drawOuterEnergyRings(uint32_t nowMs)
{
  for (int deg = 0; deg < 360; deg += 6) {
    bool major = (deg % 30) == 0;
    radialLine(deg, major ? 96 : 103, 109, major ? CYAN : REACTOR_PANEL);
  }

  float spin = fmodf(nowMs * 0.040f, 360.0f);
  for (int band = 0; band < 3; ++band) {
    int radius = 98 - band * 10;
    float offset = spin * (band % 2 ? -1.0f : 1.0f) + band * 40;
    for (int i = 0; i < 6; ++i) {
      uint16_t color = (i % 2) ? ARC_BLUE : CYAN;
      drawArc(CX, CY, radius, offset + i * 60, offset + i * 60 + 28, color, 2);
      drawArc(CX, CY, radius - 1, offset + i * 60, offset + i * 60 + 28, CYAN_DARK, 2);
    }
  }
}

void drawReactorFins(uint32_t nowMs)
{
  float spin = fmodf(nowMs * 0.018f, 360.0f);
  for (int i = 0; i < 12; ++i) {
    float a = spin + i * 30.0f;
    float rad = (a - 90.0f) * DEG_TO_RAD;
    float side = rad + HALF_PI;
    int inner = 40;
    int outer = 76;
    int half = 5;
    int ix = CX + cosf(rad) * inner;
    int iy = CY + sinf(rad) * inner;
    int ox = CX + cosf(rad) * outer;
    int oy = CY + sinf(rad) * outer;
    int sx = cosf(side) * half;
    int sy = sinf(side) * half;

    uint16_t fill = (i % 3 == 0) ? CYAN : STEEL_DARK;
    frame->fillTriangle(ix + sx, iy + sy, ix - sx, iy - sy, ox, oy, fill);
    frame->drawLine(ix + sx, iy + sy, ox, oy, CYAN_DARK);
    frame->drawLine(ix - sx, iy - sy, ox, oy, CYAN_DARK);
  }
}

void drawPowerPulses(uint32_t nowMs)
{
  for (int i = 0; i < 10; ++i) {
    float a = i * 36.0f + fmodf(nowMs * 0.065f, 360.0f);
    int radius = 52 + ((nowMs / 80 + i * 9) % 38);
    int x = orbitX(a, radius);
    int y = orbitY(a, radius);
    uint16_t color = (i % 4 == 0) ? CYAN_WHITE : CYAN;
    frame->fillCircle(x, y, (i % 3) + 1, color);
    frame->drawLine(CX, CY, x, y, (i % 2) ? CYAN_DARK : REACTOR_PANEL);
  }
}

void drawCore(uint32_t nowMs)
{
  int pulse = 3 + (int)((sinf(nowMs * 0.012f) + 1.0f) * 5.0f);

  frame->fillCircle(CX, CY, 42 + pulse, 0x016D);
  frame->drawCircle(CX, CY, 48 + pulse, CYAN_DARK);
  frame->drawCircle(CX, CY, 43, CYAN);
  frame->drawCircle(CX, CY, 34, CYAN_WHITE);
  frame->fillCircle(CX, CY, 27, ARC_BLUE);
  frame->fillCircle(CX, CY, 18 + pulse / 3, CYAN);
  frame->fillCircle(CX, CY, 8, CYAN_WHITE);

  for (int i = 0; i < 8; ++i) {
    float a = i * 45.0f - fmodf(nowMs * 0.090f, 45.0f);
    radialLine(a, 21, 35, CYAN_WHITE);
  }
}

void drawStatusReadouts(const tm &localTime, uint32_t nowMs)
{
  char text[18];
  int power = 84 + (int)(sinf(nowMs * 0.0025f) * 10.0f);
  int temp = 38 + (int)(sinf(nowMs * 0.0019f + 1.2f) * 4.0f);

  centeredText(CX, 18, "ARC REACTOR", CYAN_WHITE);
  centeredText(CX, 30, timeSynced ? "GRID LOCK" : "LOCAL TIME", CYAN);

  frame->drawRoundRect(17, 54, 54, 35, 4, REACTOR_PANEL);
  centeredText(44, 63, "POWER", STEEL);
  snprintf(text, sizeof(text), "%02d%%", power);
  centeredText(44, 78, text, CYAN_WHITE);

  frame->drawRoundRect(169, 54, 54, 35, 4, REACTOR_PANEL);
  centeredText(196, 63, "CORE", STEEL);
  snprintf(text, sizeof(text), "%02dC", temp);
  centeredText(196, 78, text, AMBER);

  frame->drawRoundRect(72, 201, 96, 22, 4, REACTOR_PANEL);
  snprintf(text, sizeof(text), "%02d:%02d:%02d", localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
  centeredText(CX, 212, text, CYAN_WHITE);
}

void drawHands(const tm &localTime, uint32_t nowMs)
{
  float second = smoothClockSecond(localTime, nowMs);
  float minute = localTime.tm_min + (second / 60.0f);
  float hour = (localTime.tm_hour % 12) + (minute / 60.0f);

  radialLine(hour * 30.0f, -9, 37, WHITE);
  radialLine(hour * 30.0f + 1.0f, -5, 35, STEEL_DARK);
  radialLine(minute * 6.0f, -11, 59, CYAN_WHITE);
  radialLine(minute * 6.0f + 0.8f, -6, 56, CYAN);
  radialLine(second * 6.0f, -17, 84, AMBER);
  radialLine(second * 6.0f + 0.4f, -10, 78, RED_WARN);

  frame->fillCircle(CX + 2, CY + 3, 10, BLACK);
  frame->fillCircle(CX, CY, 9, CYAN_WHITE);
  frame->fillCircle(CX, CY, 5, ARC_BLUE);
  frame->drawCircle(CX, CY, 12, CYAN);
}

void renderFrame(const tm &localTime)
{
  uint32_t nowMs = millis();
  drawBackground(nowMs);
  drawOuterEnergyRings(nowMs);
  drawReactorFins(nowMs);
  drawPowerPulses(nowMs);
  drawCore(nowMs);
  drawStatusReadouts(localTime, nowMs);
  drawHands(localTime, nowMs);
  frame->flush();
}

void drawBootStatus(const char *status)
{
  frame->fillScreen(BLACK);
  frame->fillCircle(CX, CY, 112, VOID_BLUE);
  frame->drawCircle(CX, CY, 108, CYAN_DARK);
  frame->drawCircle(CX, CY, 76, CYAN);
  frame->fillCircle(CX, CY, 36, ARC_BLUE);
  centeredText(CX, 86, "ARC", CYAN_WHITE, 3);
  centeredText(CX, 124, status, CYAN);
  centeredText(CX, 148, "REACTOR ONLINE", AMBER);
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
  Serial.println("Arc Reactor Core watch face booting");
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
  if (millis() - lastFrameMs < 60) {
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
    Serial.printf("Arc reactor %02d:%02d:%02d source=%s\n",
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  timeSynced ? "ntp" : "build");
  }
}

#endif
