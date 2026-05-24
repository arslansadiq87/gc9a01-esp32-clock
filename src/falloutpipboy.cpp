#if defined(WATCH_FACE_ACTIVE) || defined(BUILD_FALLOUT_PIPBOY_BACKUP)
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
constexpr uint16_t CRT_BG = 0x0020;
constexpr uint16_t CRT_PANEL = 0x0100;
constexpr uint16_t GREEN = 0x07E0;
constexpr uint16_t GREEN_BRIGHT = 0xAFE5;
constexpr uint16_t GREEN_DIM = 0x03A0;
constexpr uint16_t GREEN_DARK = 0x0140;
constexpr uint16_t GREEN_FAINT = 0x00A0;
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

void drawArc(int radius, float startDeg, float endDeg, uint16_t color, int width)
{
  if (endDeg < startDeg) {
    endDeg += 360.0f;
  }

  for (float a = startDeg; a <= endDeg; a += 3.0f) {
    float next = min(a + 3.0f, endDeg);
    float a0 = (a - 90.0f) * DEG_TO_RAD;
    float a1 = (next - 90.0f) * DEG_TO_RAD;
    for (int w = 0; w < width; w++) {
      int r = radius - w;
      frame->drawLine(CX + cosf(a0) * r,
                      CY + sinf(a0) * r,
                      CX + cosf(a1) * r,
                      CY + sinf(a1) * r,
                      color);
    }
  }
}

void drawScanlines(uint32_t nowMs)
{
  int offset = (nowMs / 90) % 6;
  for (int y = offset; y < SCREEN_H; y += 6) {
    frame->drawFastHLine(15, y, 210, GREEN_FAINT);
  }

  int sweepY = (nowMs / 12) % SCREEN_H;
  frame->drawFastHLine(20, sweepY, 200, GREEN_DIM);
  if (sweepY + 1 < SCREEN_H) {
    frame->drawFastHLine(28, sweepY + 1, 184, GREEN_FAINT);
  }
}

void drawTerminalFrame(uint32_t nowMs)
{
  frame->fillScreen(BLACK);
  frame->fillCircle(CX, CY, 118, CRT_BG);
  frame->fillCircle(CX, CY, 109, CRT_PANEL);
  frame->drawCircle(CX, CY, 118, GREEN_DIM);
  frame->drawCircle(CX, CY, 116, GREEN_DARK);
  frame->drawCircle(CX, CY, 107, GREEN_DIM);
  frame->drawCircle(CX, CY, 85, GREEN_DARK);

  frame->drawRect(31, 35, 178, 168, GREEN_DARK);
  frame->drawFastHLine(40, 57, 160, GREEN_DIM);
  frame->drawFastHLine(40, 179, 160, GREEN_DARK);
  frame->drawFastVLine(68, 66, 104, GREEN_DARK);
  frame->drawFastVLine(172, 66, 104, GREEN_DARK);

  centeredText("ROBCO INDUSTRIES", CX, 24, GREEN_DIM, BLACK, 1);
  centeredText("PIP-BOY 3000", CX, 45, GREEN_BRIGHT, CRT_PANEL, 1);

  for (int i = 0; i < 18; i++) {
    int x = 44 + ((i * 37 + nowMs / 41) % 152);
    int y = 68 + ((i * 29) % 96);
    frame->drawPixel(x, y, (i % 3 == 0) ? GREEN_DIM : GREEN_FAINT);
  }
}

void drawRadiationMeter(uint32_t nowMs, const tm &localTime)
{
  int rad = 17 + (sin(nowMs / 830.0f) + 1.0f) * 17 + (localTime.tm_sec % 5);
  float end = -130 + rad * 2.6f;

  drawArc(96, -130, 130, GREEN_DARK, 2);
  drawArc(91, -130, 130, GREEN_DARK, 1);
  drawArc(96, -130, end, GREEN_BRIGHT, 3);

  for (int i = 0; i <= 10; i++) {
    float angle = -130.0f + i * 26.0f;
    radialLine(angle, 89, 101, (i % 5 == 0) ? GREEN_BRIGHT : GREEN_DIM);
  }

  char radText[16] = {};
  snprintf(radText, sizeof(radText), "RAD %02d", rad);
  centeredText(radText, CX, CY + 70, rad > 42 ? WARNING : GREEN_BRIGHT, CRT_PANEL, 1);
}

void drawTerminalAnimation(uint32_t nowMs)
{
  int phase = (nowMs / 250) % 4;
  const char *dots[] = {"", ".", "..", "..."};

  frame->setTextColor(GREEN_DIM, CRT_PANEL);
  frame->setTextSize(1);
  frame->setCursor(44, 188);
  frame->print("> SCAN");
  frame->print(dots[phase]);

  int cursorX = 44 + ((nowMs / 80) % 18) * 6;
  frame->fillRect(cursorX, 199, 5, 2, GREEN_BRIGHT);
}

void drawClockHands(const tm &localTime)
{
  float second = smoothClockSecond(localTime, millis());
  float minute = localTime.tm_min + second / 60.0f;
  float hour = (localTime.tm_hour % 12) + minute / 60.0f;

  for (int i = 0; i < 60; i++) {
    bool major = (i % 5) == 0;
    radialLine(i * 6.0f, major ? 72 : 78, major ? 83 : 82, major ? GREEN_BRIGHT : GREEN_DIM);
  }

  radialLine(hour * 30.0f, -8, 38, GREEN_BRIGHT);
  radialLine(hour * 30.0f + 1.2f, -8, 38, GREEN_BRIGHT);
  radialLine(minute * 6.0f, -10, 58, GREEN);
  radialLine(minute * 6.0f + 0.8f, -10, 58, GREEN);
  radialLine(second * 6.0f, -15, 72, WARNING);

  frame->fillCircle(CX, CY, 7, BLACK);
  frame->drawCircle(CX, CY, 7, GREEN_BRIGHT);
  frame->fillCircle(CX, CY, 3, WARNING);
}

void drawTimeReadout(const tm &localTime)
{
  char timeText[16] = {};
  char dateText[18] = {};
  snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d", localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
  snprintf(dateText, sizeof(dateText), "%02d.%02d.%04d",
           localTime.tm_mday,
           localTime.tm_mon + 1,
           localTime.tm_year + 1900);

  centeredText(timeText, CX, CY - 20, GREEN_BRIGHT, CRT_PANEL, 2);
  centeredText(dateText, CX, CY + 18, GREEN_DIM, CRT_PANEL, 1);
  centeredText(timeSynced ? "VAULT-TIME LINK" : "LOCAL HOLOTAPE", CX, CY + 38, timeSynced ? GREEN_BRIGHT : WARNING, CRT_PANEL, 1);
}

void renderFrame(const tm &localTime)
{
  uint32_t nowMs = millis();
  drawTerminalFrame(nowMs);
  drawRadiationMeter(nowMs, localTime);
  drawClockHands(localTime);
  drawTimeReadout(localTime);
  drawTerminalAnimation(nowMs);
  drawScanlines(nowMs);
  frame->flush(true);
}

void drawBootStatus(const char *message)
{
  frame->fillScreen(BLACK);
  drawTerminalFrame(millis());
  centeredText(message, CX, CY + 55, GREEN_BRIGHT, CRT_PANEL, 1);
  drawScanlines(millis());
  frame->flush(true);
}

void syncTimeIfConfigured()
{
  setenv("TZ", CLOCK_TZ, 1);
  tzset();

  if (strlen(WIFI_SSID) == 0) {
    Serial.println("WiFi not configured; using build-time clock.");
    drawBootStatus("LOCAL CLOCK MODE");
    delay(900);
    return;
  }

  Serial.println("Connecting WiFi for NTP time sync.");
  drawBootStatus("RADIO LINK START");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi unavailable; using build-time clock.");
    drawBootStatus("RADIO LINK FAIL");
    delay(900);
    return;
  }

  configTzTime(CLOCK_TZ, "pool.ntp.org", "time.nist.gov");
  drawBootStatus("VAULT-TIME SYNC");

  tm localTime{};
  if (getLocalTime(&localTime, 10000)) {
    timeSynced = true;
    Serial.println("NTP time synced.");
    drawBootStatus("TIME LINK OK");
  } else {
    Serial.println("NTP unavailable; using build-time clock.");
    drawBootStatus("TIME LINK FAIL");
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
  Serial.println("GC9A01 Pip-Boy clock starting");

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

  drawBootStatus("BOOTING PIP-OS");
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
    Serial.printf("PipBoy %02d:%02d:%02d source=%s\n",
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  timeSynced ? "ntp" : "build");
  }
}
#endif
