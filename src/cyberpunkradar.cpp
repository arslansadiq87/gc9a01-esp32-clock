#if defined(WATCH_FACE_ACTIVE) || defined(BUILD_CYBERPUNK_RADAR_BACKUP)
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
constexpr uint16_t PANEL = 0x0008;
constexpr uint16_t CYAN = 0x07FF;
constexpr uint16_t CYAN_DIM = 0x03EF;
constexpr uint16_t CYAN_DARK = 0x0149;
constexpr uint16_t MAGENTA = 0xF81F;
constexpr uint16_t MAGENTA_DIM = 0x7810;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t GRID = 0x0125;
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

void radialLine(float angleDeg, int inner, int outer, uint16_t color)
{
  float angle = (angleDeg - 90.0f) * DEG_TO_RAD;
  int x1 = CX + cosf(angle) * inner;
  int y1 = CY + sinf(angle) * inner;
  int x2 = CX + cosf(angle) * outer;
  int y2 = CY + sinf(angle) * outer;
  frame->drawLine(x1, y1, x2, y2, color);
}

void thickRadialLine(float angleDeg, int inner, int outer, uint16_t color, int width)
{
  for (int offset = -width / 2; offset <= width / 2; offset++) {
    float angle = (angleDeg - 90.0f) * DEG_TO_RAD;
    int dx = cosf(angle + HALF_PI) * offset;
    int dy = sinf(angle + HALF_PI) * offset;
    int x1 = CX + cosf(angle) * inner + dx;
    int y1 = CY + sinf(angle) * inner + dy;
    int x2 = CX + cosf(angle) * outer + dx;
    int y2 = CY + sinf(angle) * outer + dy;
    frame->drawLine(x1, y1, x2, y2, color);
  }
}

void drawArc(int radius, float startDeg, float endDeg, uint16_t color, int width)
{
  if (endDeg < startDeg) {
    endDeg += 360.0f;
  }

  for (float a = startDeg; a <= endDeg; a += 3.0f) {
    float next = min(a + 3.0f, endDeg);
    for (int w = 0; w < width; w++) {
      int r = radius - w;
      float a0 = (a - 90.0f) * DEG_TO_RAD;
      float a1 = (next - 90.0f) * DEG_TO_RAD;
      int x0 = CX + cosf(a0) * r;
      int y0 = CY + sinf(a0) * r;
      int x1 = CX + cosf(a1) * r;
      int y1 = CY + sinf(a1) * r;
      frame->drawLine(x0, y0, x1, y1, color);
    }
  }
}

void drawHudFrame()
{
  frame->fillScreen(BLACK);

  for (int r = 118; r >= 86; r -= 8) {
    frame->drawCircle(CX, CY, r, (r % 16 == 0) ? CYAN_DIM : CYAN_DARK);
  }

  frame->fillCircle(CX, CY, 82, PANEL);
  frame->drawCircle(CX, CY, 82, CYAN);
  frame->drawCircle(CX, CY, 70, CYAN_DARK);
  frame->drawCircle(CX, CY, 46, MAGENTA_DIM);

  for (int i = 0; i < 60; i++) {
    int inner = (i % 5 == 0) ? 94 : 102;
    int outer = (i % 5 == 0) ? 112 : 109;
    radialLine(i * 6.0f, inner, outer, (i % 5 == 0) ? CYAN : GRID);
  }

  for (int x = 36; x <= 204; x += 18) {
    frame->drawFastVLine(x, 42, 156, GRID);
  }
  for (int y = 42; y <= 198; y += 18) {
    frame->drawFastHLine(36, y, 168, GRID);
  }

  frame->drawFastHLine(20, CY, 44, CYAN_DIM);
  frame->drawFastHLine(176, CY, 44, CYAN_DIM);
  frame->drawFastVLine(CX, 18, 44, CYAN_DIM);
  frame->drawFastVLine(CX, 178, 44, CYAN_DIM);
  frame->drawRect(50, 34, 140, 172, MAGENTA_DIM);

  centeredText("CYBER HUD", CX, 31, CYAN, BLACK, 1);
  centeredText("SYNC", 54, 210, MAGENTA, BLACK, 1);
  centeredText("ESP-C6", 183, 210, CYAN, BLACK, 1);
}

void drawRadarSweep(float angleDeg)
{
  for (int trail = 0; trail < 7; trail++) {
    uint16_t color = trail == 0 ? CYAN : (trail < 3 ? CYAN_DIM : CYAN_DARK);
    thickRadialLine(angleDeg - trail * 5.0f, 0, 78, color, trail == 0 ? 2 : 1);
  }
  frame->fillCircle(CX, CY, 3, CYAN);
}

void drawTimeHud(const tm &localTime)
{
  char timeBuffer[16] = {};
  char dateBuffer[20] = {};
  snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d", localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
  snprintf(dateBuffer, sizeof(dateBuffer), "%04d-%02d-%02d",
           localTime.tm_year + 1900,
           localTime.tm_mon + 1,
           localTime.tm_mday);

  centeredText(timeBuffer, CX, 112, WHITE, PANEL, 3);
  centeredText(dateBuffer, CX, 143, CYAN_DIM, PANEL, 1);
  centeredText(timeSynced ? "NTP LOCK" : "BUILD TIME", CX, 158, timeSynced ? CYAN : WARNING, PANEL, 1);
}

void drawProgressRings(const tm &localTime)
{
  float second = smoothClockSecond(localTime, millis());
  float minute = localTime.tm_min + second / 60.0f;
  float hour = (localTime.tm_hour % 12) + minute / 60.0f;

  drawArc(116, -120, -120 + (second / 60.0f) * 300.0f, CYAN, 3);
  drawArc(108, -120, -120 + (minute / 60.0f) * 300.0f, MAGENTA, 3);
  drawArc(100, -120, -120 + (hour / 12.0f) * 300.0f, WHITE, 2);

  radialLine(second * 6.0f, 84, 116, CYAN);
  radialLine(minute * 6.0f, 62, 82, MAGENTA);
  radialLine(hour * 30.0f, 38, 58, WHITE);
}

void renderFrame(const tm &localTime)
{
  drawHudFrame();
  drawProgressRings(localTime);
  drawRadarSweep((millis() / 16) % 360);
  drawTimeHud(localTime);
  frame->flush(true);
}

void drawBootStatus(const char *message)
{
  frame->fillScreen(BLACK);
  drawHudFrame();
  centeredText(message, CX, CY, CYAN, PANEL, 1);
  frame->flush(true);
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
  drawBootStatus("SYNCING NTP");

  tm localTime{};
  if (getLocalTime(&localTime, 10000)) {
    timeSynced = true;
    Serial.println("NTP time synced.");
    drawBootStatus("NTP LOCK");
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
  Serial.println("GC9A01 Cyberpunk HUD clock starting");

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

  drawBootStatus("HUD ONLINE");
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
    Serial.printf("HUD %02d:%02d:%02d source=%s\n",
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  timeSynced ? "ntp" : "build");
  }
}
#endif
