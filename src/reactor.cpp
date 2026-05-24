#if defined(WATCH_FACE_ACTIVE) || defined(BUILD_REACTOR_BACKUP)
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
constexpr uint16_t CORE_BLACK = 0x0008;
constexpr uint16_t REACTOR_BLUE = 0x07FF;
constexpr uint16_t BLUE_DIM = 0x03EF;
constexpr uint16_t BLUE_DARK = 0x0149;
constexpr uint16_t ICE = 0xBFFF;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t STEEL = 0x7BEF;
constexpr uint16_t STEEL_DARK = 0x3186;
constexpr uint16_t AMBER = 0xFD20;
constexpr uint16_t ORANGE = 0xFCA0;
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

  for (float a = startDeg; a <= endDeg; a += 2.5f) {
    float next = min(a + 2.5f, endDeg);
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

void drawSegmentedRing(int radius, int segmentCount, int segmentDeg, float phase, uint16_t color)
{
  for (int i = 0; i < segmentCount; i++) {
    float start = phase + (360.0f / segmentCount) * i;
    drawArc(radius, start, start + segmentDeg, color, 2);
  }
}

void drawReactorBackground()
{
  frame->fillScreen(BLACK);

  frame->fillCircle(CX, CY, 119, CORE_BLACK);
  frame->drawCircle(CX, CY, 118, STEEL_DARK);
  frame->drawCircle(CX, CY, 116, BLUE_DARK);

  for (int i = 0; i < 72; i++) {
    uint16_t color = (i % 6 == 0) ? BLUE_DIM : STEEL_DARK;
    int inner = (i % 6 == 0) ? 101 : 108;
    int outer = (i % 6 == 0) ? 114 : 112;
    radialLine(i * 5.0f, inner, outer, color);
  }

  frame->drawCircle(CX, CY, 96, STEEL_DARK);
  frame->drawCircle(CX, CY, 82, BLUE_DARK);
  frame->drawCircle(CX, CY, 64, BLUE_DIM);
  frame->drawCircle(CX, CY, 48, STEEL_DARK);

  for (int i = 0; i < 8; i++) {
    float angle = (i * 45.0f - 90.0f) * DEG_TO_RAD;
    int x = CX + cosf(angle) * 73;
    int y = CY + sinf(angle) * 73;
    frame->drawCircle(x, y, 5, STEEL_DARK);
    frame->fillCircle(x, y, 2, BLUE_DARK);
  }
}

void drawGlowingCore(uint32_t nowMs)
{
  int pulse = (sin(nowMs / 260.0f) + 1.0f) * 4.0f;

  frame->fillCircle(CX, CY, 38 + pulse, BLUE_DARK);
  frame->fillCircle(CX, CY, 31 + pulse / 2, BLUE_DIM);
  frame->fillCircle(CX, CY, 23, REACTOR_BLUE);
  frame->fillCircle(CX, CY, 14, ICE);
  frame->fillCircle(CX, CY, 6, WHITE);

  for (int i = 0; i < 6; i++) {
    float angle = i * 60.0f + (nowMs / 25.0f);
    radialLine(angle, 26, 42 + pulse, ICE);
  }
}

void drawOrbiters(uint32_t nowMs)
{
  const int radii[] = {58, 76, 94};
  const uint16_t colors[] = {ICE, REACTOR_BLUE, AMBER};
  const float speeds[] = {0.12f, -0.075f, 0.045f};

  for (int orbit = 0; orbit < 3; orbit++) {
    for (int dot = 0; dot < 3; dot++) {
      float angle = nowMs * speeds[orbit] + dot * 120.0f + orbit * 35.0f;
      float rad = (angle - 90.0f) * DEG_TO_RAD;
      int x = CX + cosf(rad) * radii[orbit];
      int y = CY + sinf(rad) * radii[orbit];
      frame->fillCircle(x, y, orbit == 2 ? 3 : 2, colors[orbit]);
      frame->drawCircle(x, y, orbit == 2 ? 5 : 4, BLUE_DARK);
    }
  }
}

void drawAnimatedRings(uint32_t nowMs, const tm &localTime)
{
  float phaseFast = nowMs / 18.0f;
  float phaseSlow = -(nowMs / 42.0f);

  drawSegmentedRing(112, 12, 13, phaseFast, REACTOR_BLUE);
  drawSegmentedRing(86, 9, 18, phaseSlow, BLUE_DIM);
  drawSegmentedRing(68, 6, 22, phaseFast * 0.65f, STEEL);

  float second = smoothClockSecond(localTime, millis());
  float minute = localTime.tm_min + second / 60.0f;
  drawArc(104, -90, -90 + (second / 60.0f) * 360.0f, AMBER, 2);
  drawArc(100, -90, -90 + (minute / 60.0f) * 360.0f, ICE, 1);
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

void drawAnalogHands(const tm &localTime)
{
  float second = smoothClockSecond(localTime, millis());
  float minute = localTime.tm_min + second / 60.0f;
  float hour = (localTime.tm_hour % 12) + minute / 60.0f;

  drawTaperedHand(hour * 30.0f, 46, 10, 5, WHITE, BLACK);
  drawTaperedHand(minute * 6.0f, 70, 13, 3, ICE, BLACK);
  drawTaperedHand(second * 6.0f, 88, 18, 1, ORANGE, BLACK);

  frame->fillCircle(CX + 2, CY + 2, 9, BLACK);
  frame->fillCircle(CX, CY, 9, CORE_BLACK);
  frame->drawCircle(CX, CY, 9, ICE);
  frame->fillCircle(CX, CY, 4, AMBER);
}

void drawTimeText(const tm &localTime)
{
  char buffer[16] = {};
  snprintf(buffer, sizeof(buffer), "%02d:%02d", localTime.tm_hour, localTime.tm_min);
  centeredText(buffer, CX, 164, ICE, CORE_BLACK, 1);
  centeredText(timeSynced ? "REACTOR SYNC" : "BUILD CORE", CX, 181, timeSynced ? REACTOR_BLUE : WARNING, CORE_BLACK, 1);
}

void renderFrame(const tm &localTime)
{
  uint32_t nowMs = millis();
  drawReactorBackground();
  drawAnimatedRings(nowMs, localTime);
  drawGlowingCore(nowMs);
  drawOrbiters(nowMs);
  drawAnalogHands(localTime);
  drawTimeText(localTime);
  frame->flush(true);
}

void drawBootStatus(const char *message)
{
  frame->fillScreen(BLACK);
  drawReactorBackground();
  drawGlowingCore(millis());
  centeredText(message, CX, 170, REACTOR_BLUE, CORE_BLACK, 1);
  frame->flush(true);
}

void syncTimeIfConfigured()
{
  setenv("TZ", CLOCK_TZ, 1);
  tzset();

  if (strlen(WIFI_SSID) == 0) {
    Serial.println("WiFi not configured; using build-time clock.");
    drawBootStatus("BUILD-TIME CORE");
    delay(900);
    return;
  }

  Serial.println("Connecting WiFi for NTP time sync.");
  drawBootStatus("LINKING WIFI");

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
  drawBootStatus("SYNCING CORE");

  tm localTime{};
  if (getLocalTime(&localTime, 10000)) {
    timeSynced = true;
    Serial.println("NTP time synced.");
    drawBootStatus("CORE SYNCED");
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
  Serial.println("GC9A01 Sci-Fi Reactor clock starting");

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

  drawBootStatus("REACTOR ONLINE");
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
    Serial.printf("Reactor %02d:%02d:%02d source=%s\n",
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  timeSynced ? "ntp" : "build");
  }
}
#endif
