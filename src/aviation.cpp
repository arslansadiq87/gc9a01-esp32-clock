#if defined(WATCH_FACE_ACTIVE) || defined(BUILD_AVIATION_BACKUP)
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
constexpr uint16_t COCKPIT = 0x0020;
constexpr uint16_t PANEL = 0x0104;
constexpr uint16_t HUD = 0x07E0;
constexpr uint16_t HUD_DIM = 0x03A0;
constexpr uint16_t HUD_DARK = 0x0140;
constexpr uint16_t LIME = 0xAFE5;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t AMBER = 0xFFE0;
constexpr uint16_t RED = 0xF800;

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

void drawTape(int x, int y, int w, int h, const char *label, int value, const char *unit)
{
  frame->drawRoundRect(x, y, w, h, 4, HUD_DIM);
  frame->fillRect(x + 2, y + h / 2 - 9, w - 4, 18, PANEL);
  frame->drawRect(x + 2, y + h / 2 - 9, w - 4, 18, HUD);

  frame->setTextColor(HUD_DIM, BLACK);
  frame->setTextSize(1);
  frame->setCursor(x + 5, y + 5);
  frame->print(label);

  char valueText[16] = {};
  snprintf(valueText, sizeof(valueText), "%d", value);
  centeredText(valueText, x + w / 2, y + h / 2 - 1, LIME, PANEL, 1);

  frame->setCursor(x + 5, y + h - 13);
  frame->setTextColor(HUD_DIM, BLACK);
  frame->print(unit);

  for (int i = -2; i <= 2; i++) {
    int tickY = y + h / 2 + i * 15;
    if (tickY > y + 14 && tickY < y + h - 14) {
      frame->drawFastHLine(x + w - 12, tickY, 9, HUD_DARK);
    }
  }
}

void drawCompassRing(float heading)
{
  frame->drawCircle(CX, CY, 116, HUD_DARK);
  frame->drawCircle(CX, CY, 111, HUD_DIM);
  frame->drawCircle(CX, CY, 92, HUD_DARK);

  for (int deg = 0; deg < 360; deg += 5) {
    float screenDeg = deg - heading;
    bool major = (deg % 30) == 0;
    bool cardinal = (deg % 90) == 0;
    int inner = cardinal ? 93 : (major ? 98 : 105);
    int outer = 112;
    radialLine(screenDeg, inner, outer, cardinal ? LIME : (major ? HUD : HUD_DARK));
  }

  const char *labels[] = {"N", "E", "S", "W"};
  for (int i = 0; i < 4; i++) {
    float angle = (i * 90.0f - heading - 90.0f) * DEG_TO_RAD;
    int x = CX + cosf(angle) * 78;
    int y = CY + sinf(angle) * 78;
    centeredText(labels[i], x, y, LIME, COCKPIT, 1);
  }

  frame->fillTriangle(CX, 10, CX - 7, 25, CX + 7, 25, AMBER);
  centeredText("HDG", CX, 31, HUD_DIM, BLACK, 1);
}

void drawRadarSweep(uint32_t nowMs)
{
  int radarX = CX;
  int radarY = CY + 52;
  int radius = 31;
  frame->drawCircle(radarX, radarY, radius, HUD_DARK);
  frame->drawCircle(radarX, radarY, radius - 10, HUD_DARK);
  frame->drawFastHLine(radarX - radius, radarY, radius * 2, HUD_DARK);
  frame->drawFastVLine(radarX, radarY - radius, radius * 2, HUD_DARK);

  float sweep = (nowMs / 12) % 360;
  for (int trail = 0; trail < 6; trail++) {
    float angle = (sweep - trail * 8.0f - 90.0f) * DEG_TO_RAD;
    uint16_t color = trail < 2 ? HUD : HUD_DIM;
    frame->drawLine(radarX, radarY, radarX + cosf(angle) * radius, radarY + sinf(angle) * radius, color);
  }

  frame->fillCircle(radarX + 12, radarY - 7, 2, LIME);
  frame->fillCircle(radarX - 16, radarY + 9, 2, HUD);
}

void drawCockpitBase(uint32_t nowMs)
{
  frame->fillScreen(BLACK);
  frame->fillCircle(CX, CY, 118, COCKPIT);
  frame->fillCircle(CX, CY, 86, BLACK);

  for (int x = 35; x <= 205; x += 17) {
    frame->drawFastVLine(x, 36, 168, 0x00A0);
  }
  for (int y = 36; y <= 204; y += 17) {
    frame->drawFastHLine(35, y, 170, 0x00A0);
  }

  float heading = fmodf(nowMs / 80.0f, 360.0f);
  drawCompassRing(heading);

  frame->drawFastHLine(34, CY, 52, HUD_DIM);
  frame->drawFastHLine(154, CY, 52, HUD_DIM);
  frame->drawFastVLine(CX, 34, 40, HUD_DIM);
  frame->drawFastVLine(CX, 166, 40, HUD_DIM);
  frame->drawCircle(CX, CY, 65, HUD_DARK);

  centeredText("AVIATION HUD", CX, 46, HUD, BLACK, 1);
}

void drawAnalogHands(const tm &localTime)
{
  float second = smoothClockSecond(localTime, millis());
  float minute = localTime.tm_min + second / 60.0f;
  float hour = (localTime.tm_hour % 12) + minute / 60.0f;

  radialLine(hour * 30.0f, -10, 45, WHITE);
  radialLine(hour * 30.0f + 1.0f, -10, 45, WHITE);
  radialLine(minute * 6.0f, -12, 65, LIME);
  radialLine(minute * 6.0f + 0.6f, -12, 65, LIME);
  radialLine(second * 6.0f, -18, 79, AMBER);

  frame->fillCircle(CX, CY, 7, BLACK);
  frame->drawCircle(CX, CY, 7, LIME);
  frame->fillCircle(CX, CY, 3, AMBER);
}

void drawInstrumentData(const tm &localTime, uint32_t nowMs)
{
  int speed = 420 + sin(nowMs / 900.0f) * 34;
  int altitude = 28000 + sin(nowMs / 1300.0f) * 850;
  int vsi = sin(nowMs / 700.0f) * 1200;

  drawTape(12, 74, 45, 92, "SPD", speed, "KT");
  drawTape(183, 74, 45, 92, "ALT", altitude / 100, "x100");

  char timeText[16] = {};
  snprintf(timeText, sizeof(timeText), "%02d:%02d", localTime.tm_hour, localTime.tm_min);
  centeredText(timeText, CX, CY - 20, WHITE, BLACK, 2);

  char vsiText[18] = {};
  snprintf(vsiText, sizeof(vsiText), "VSI %+d", vsi);
  centeredText(vsiText, CX, 178, HUD_DIM, BLACK, 1);
  centeredText(timeSynced ? "NAV SYNC" : "BUILD NAV", CX, 194, timeSynced ? LIME : AMBER, BLACK, 1);
}

void renderFrame(const tm &localTime)
{
  uint32_t nowMs = millis();
  drawCockpitBase(nowMs);
  drawRadarSweep(nowMs);
  drawInstrumentData(localTime, nowMs);
  drawAnalogHands(localTime);
  frame->flush(true);
}

void drawBootStatus(const char *message)
{
  frame->fillScreen(BLACK);
  drawCockpitBase(millis());
  centeredText(message, CX, CY + 22, LIME, BLACK, 1);
  frame->flush(true);
}

void syncTimeIfConfigured()
{
  setenv("TZ", CLOCK_TZ, 1);
  tzset();

  if (strlen(WIFI_SSID) == 0) {
    Serial.println("WiFi not configured; using build-time clock.");
    drawBootStatus("BUILD-TIME NAV");
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
  drawBootStatus("SYNCING NAV");

  tm localTime{};
  if (getLocalTime(&localTime, 10000)) {
    timeSynced = true;
    Serial.println("NTP time synced.");
    drawBootStatus("NAV SYNCED");
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
  Serial.println("GC9A01 Aviation Cockpit clock starting");

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
    Serial.printf("Aviation %02d:%02d:%02d source=%s\n",
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  timeSynced ? "ntp" : "build");
  }
}
#endif
