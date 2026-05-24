#if defined(WATCH_FACE_ACTIVE) || defined(BUILD_HEALTH_BACKUP)

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
constexpr uint16_t BIO_BLACK = 0x0842;
constexpr uint16_t BIO_PANEL = 0x1128;
constexpr uint16_t BIO_PANEL_EDGE = 0x32B3;
constexpr uint16_t MINT_DARK = 0x03AC;
constexpr uint16_t MINT = 0x4FF7;
constexpr uint16_t MINT_WHITE = 0xDFFF;
constexpr uint16_t CYAN = 0x57FF;
constexpr uint16_t CYAN_DIM = 0x1455;
constexpr uint16_t HEART = 0xF986;
constexpr uint16_t HEART_DARK = 0x7804;
constexpr uint16_t OXYGEN = 0x25FF;
constexpr uint16_t AMBER = 0xFD20;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t TEXT_DIM = 0x8C71;

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

void drawBackground()
{
  frame->fillScreen(BLACK);
  frame->fillCircle(CX, CY, 118, BLACK);
  frame->fillCircle(CX, CY, 114, BIO_BLACK);

  for (int y = 20; y <= 220; y += 10) {
    int dy = y - CY;
    int span = sqrtf(max(0, 111 * 111 - dy * dy));
    frame->drawFastHLine(CX - span, y, span * 2, (y % 20) ? 0x0863 : 0x1085);
  }

  for (int x = 20; x <= 220; x += 18) {
    int dx = x - CX;
    int span = sqrtf(max(0, 109 * 109 - dx * dx));
    frame->drawFastVLine(x, CY - span, span * 2, 0x0863);
  }

  frame->drawCircle(CX, CY, 115, BIO_PANEL_EDGE);
  frame->drawCircle(CX, CY, 108, BIO_PANEL);
}

void drawBioRings(uint32_t nowMs)
{
  float spin = fmodf(nowMs * 0.035f, 360.0f);

  for (int deg = 0; deg < 360; deg += 6) {
    bool major = (deg % 30) == 0;
    radialLine(deg, major ? 100 : 105, 108, major ? MINT : BIO_PANEL);
  }

  drawArc(CX, CY, 101, spin, spin + 86, MINT, 3);
  drawArc(CX, CY, 94, -spin * 0.8f, -spin * 0.8f + 62, CYAN, 3);
  drawArc(CX, CY, 86, spin * 1.2f + 130, spin * 1.2f + 188, HEART, 3);

  int pulse = 2 + (int)((sinf(nowMs * 0.012f) + 1.0f) * 4.0f);
  frame->drawCircle(CX, CY, 68 + pulse, MINT_DARK);
  frame->drawCircle(CX, CY, 55, CYAN_DIM);
}

void drawHeartIcon(int x, int y, int scale, uint16_t color)
{
  frame->fillCircle(x - scale, y - scale, scale, color);
  frame->fillCircle(x + scale, y - scale, scale, color);
  frame->fillTriangle(x - scale * 2, y - scale / 2, x + scale * 2, y - scale / 2, x, y + scale * 3, color);
}

void drawHeartPanel(uint32_t nowMs)
{
  int beat = (nowMs % 1000 < 120 || (nowMs % 1000 > 240 && nowMs % 1000 < 330)) ? 4 : 0;
  int bpm = 72 + (int)(sinf(nowMs * 0.0027f) * 8.0f);
  char text[14];

  frame->fillRoundRect(18, 53, 60, 48, 6, BLACK);
  frame->drawRoundRect(18, 53, 60, 48, 6, BIO_PANEL_EDGE);
  drawHeartIcon(48, 69, 5 + beat / 2, HEART);
  snprintf(text, sizeof(text), "%03d", bpm);
  centeredText(48, 89, text, WHITE, 1);
  centeredText(68, 89, "BPM", TEXT_DIM, 1);
}

void drawOxygenPanel(uint32_t nowMs)
{
  int spo2 = 96 + (int)((sinf(nowMs * 0.0019f + 0.7f) + 1.0f) * 1.5f);
  char text[14];

  frame->fillRoundRect(162, 53, 60, 48, 6, BLACK);
  frame->drawRoundRect(162, 53, 60, 48, 6, BIO_PANEL_EDGE);
  centeredText(192, 65, "SpO2", OXYGEN);
  snprintf(text, sizeof(text), "%02d%%", spo2);
  centeredText(192, 84, text, WHITE, 2);
}

int ecgY(int x, uint32_t nowMs)
{
  int phase = (x + (nowMs / 12)) % 96;
  int baseline = 178;

  if (phase < 18) return baseline;
  if (phase < 24) return baseline - 5;
  if (phase < 30) return baseline + 4;
  if (phase < 36) return baseline;
  if (phase < 42) return baseline - 25;
  if (phase < 48) return baseline + 21;
  if (phase < 54) return baseline - 8;
  if (phase < 72) return baseline;
  if (phase < 82) return baseline - 7;
  return baseline;
}

void drawEcgPanel(uint32_t nowMs)
{
  frame->fillRoundRect(24, 153, 192, 48, 6, BLACK);
  frame->drawRoundRect(24, 153, 192, 48, 6, BIO_PANEL_EDGE);

  for (int x = 32; x < 208; x += 12) {
    frame->drawFastVLine(x, 158, 36, 0x0844);
  }
  for (int y = 160; y < 194; y += 10) {
    frame->drawFastHLine(30, y, 180, 0x0844);
  }

  int prevX = 31;
  int prevY = ecgY(prevX, nowMs);
  for (int x = 32; x <= 209; ++x) {
    int y = ecgY(x, nowMs);
    frame->drawLine(prevX, prevY, x, y, (x + nowMs / 8) % 70 < 10 ? MINT_WHITE : MINT);
    prevX = x;
    prevY = y;
  }

  int scanner = 30 + ((nowMs / 9) % 180);
  frame->drawFastVLine(scanner, 157, 39, MINT_WHITE);
  centeredText(CX, 145, "ECG LIVE", TEXT_DIM);
}

void drawCoreReadout(const tm &localTime, uint32_t nowMs)
{
  char timeText[12];
  char dateText[16];
  int hrv = 54 + (int)(sinf(nowMs * 0.0022f + 2.3f) * 9.0f);

  snprintf(timeText, sizeof(timeText), "%02d:%02d", localTime.tm_hour, localTime.tm_min);
  snprintf(dateText, sizeof(dateText), "%02d/%02d", localTime.tm_mday, localTime.tm_mon + 1);

  frame->fillCircle(CX + 2, CY + 3, 47, BLACK);
  frame->fillCircle(CX, CY, 45, 0x08A5);
  frame->drawCircle(CX, CY, 46, MINT);
  frame->drawCircle(CX, CY, 39, CYAN_DIM);
  centeredText(CX, CY - 15, timeText, WHITE, 2);
  centeredText(CX, CY + 8, timeSynced ? "SYNC" : "LOCAL", MINT);

  char text[16];
  snprintf(text, sizeof(text), "HRV %02d", hrv);
  centeredText(CX, CY + 24, text, TEXT_DIM);
  centeredText(CX, CY + 38, dateText, CYAN);
}

void drawWellnessBars(uint32_t nowMs)
{
  int values[3] = {
      68 + (int)(sinf(nowMs * 0.0019f) * 10.0f),
      82 + (int)(sinf(nowMs * 0.0014f + 1.8f) * 7.0f),
      61 + (int)(sinf(nowMs * 0.0017f + 2.7f) * 12.0f)};
  const char *labels[3] = {"MOVE", "REST", "STRESS"};
  uint16_t colors[3] = {MINT, CYAN, AMBER};

  for (int i = 0; i < 3; ++i) {
    int x = 48 + i * 48;
    int y = 215;
    frame->drawRoundRect(x - 18, y - 11, 36, 16, 3, BIO_PANEL);
    frame->drawFastHLine(x - 13, y - 2, 26, 0x18A6);
    frame->drawFastHLine(x - 13, y - 2, map(values[i], 0, 100, 0, 26), colors[i]);
    centeredText(x, y - 15, labels[i], TEXT_DIM);
  }
}

void drawAnalogHands(const tm &localTime, uint32_t nowMs)
{
  float second = smoothClockSecond(localTime, nowMs);
  float minute = localTime.tm_min + (second / 60.0f);
  float hour = (localTime.tm_hour % 12) + (minute / 60.0f);

  radialLine(hour * 30.0f, -8, 34, WHITE);
  radialLine(hour * 30.0f + 1.0f, -4, 32, MINT_DARK);
  radialLine(minute * 6.0f, -11, 56, MINT_WHITE);
  radialLine(minute * 6.0f + 0.7f, -6, 53, CYAN);
  radialLine(second * 6.0f, -16, 82, HEART);

  frame->fillCircle(CX, CY, 8, MINT_WHITE);
  frame->fillCircle(CX, CY, 4, BLACK);
  frame->drawCircle(CX, CY, 11, HEART);
}

void renderFrame(const tm &localTime)
{
  uint32_t nowMs = millis();
  drawBackground();
  drawBioRings(nowMs);
  drawHeartPanel(nowMs);
  drawOxygenPanel(nowMs);
  drawCoreReadout(localTime, nowMs);
  drawEcgPanel(nowMs);
  drawWellnessBars(nowMs);
  drawAnalogHands(localTime, nowMs);
  frame->flush();
}

void drawBootStatus(const char *status)
{
  frame->fillScreen(BLACK);
  frame->fillCircle(CX, CY, 112, BIO_BLACK);
  frame->drawCircle(CX, CY, 108, MINT);
  frame->drawCircle(CX, CY, 76, CYAN_DIM);
  drawHeartIcon(CX, 89, 8, HEART);
  centeredText(CX, 121, status, MINT_WHITE, 2);
  centeredText(CX, 148, "BIO MONITOR ONLINE", TEXT_DIM);
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
  Serial.println("Health / Bio Monitor watch face booting");
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
  if (millis() - lastFrameMs < 70) {
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
    Serial.printf("Bio monitor %02d:%02d:%02d source=%s\n",
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  timeSynced ? "ntp" : "build");
  }
}

#endif
