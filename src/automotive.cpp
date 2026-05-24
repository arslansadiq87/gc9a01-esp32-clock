#if defined(WATCH_FACE_ACTIVE) || defined(BUILD_AUTOMOTIVE_BACKUP)

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
constexpr uint16_t DASH_BLACK = 0x0841;
constexpr uint16_t CARBON = 0x1082;
constexpr uint16_t PANEL = 0x2104;
constexpr uint16_t STEEL_DARK = 0x4A49;
constexpr uint16_t STEEL = 0xBDF7;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t TEXT_DIM = 0x8C71;
constexpr uint16_t CYAN = 0x57FF;
constexpr uint16_t CYAN_DIM = 0x1455;
constexpr uint16_t AMBER = 0xFD20;
constexpr uint16_t ORANGE = 0xFBE0;
constexpr uint16_t RED = 0xF800;
constexpr uint16_t RED_DIM = 0x9000;
constexpr uint16_t GREEN = 0x07E0;

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

int gaugeX(float angleDeg, int radius)
{
  return CX + cosf((angleDeg - 90.0f) * DEG_TO_RAD) * radius;
}

int gaugeY(float angleDeg, int radius)
{
  return CY + sinf((angleDeg - 90.0f) * DEG_TO_RAD) * radius;
}

void drawBackground()
{
  frame->fillScreen(BLACK);
  frame->fillCircle(CX, CY, 118, BLACK);
  frame->fillCircle(CX, CY, 114, DASH_BLACK);

  for (int y = 18; y <= 222; y += 8) {
    int dy = y - CY;
    int span = sqrtf(max(0, 112 * 112 - dy * dy));
    uint16_t color = ((y / 8) % 2) ? 0x1082 : 0x0841;
    frame->drawFastHLine(CX - span, y, span * 2, color);
  }

  for (int i = -210; i < 240; i += 16) {
    frame->drawLine(i, 20, i + 90, 220, CARBON);
    frame->drawLine(i + 8, 220, i + 98, 20, 0x0801);
  }

  frame->drawCircle(CX, CY, 115, STEEL_DARK);
  frame->drawCircle(CX, CY, 109, PANEL);
}

void drawTachScale(uint32_t nowMs)
{
  drawArc(CX, CY, 98, 220, 500, STEEL_DARK, 3);
  drawArc(CX, CY, 98, 220, 402, CYAN_DIM, 3);
  drawArc(CX, CY, 98, 402, 500, RED_DIM, 3);

  for (int i = 0; i <= 8; ++i) {
    float angle = 220 + i * 35.0f;
    uint16_t color = (i >= 6) ? RED : WHITE;
    radialLine(angle, i >= 6 ? 78 : 82, 100, color);

    char label[3];
    snprintf(label, sizeof(label), "%d", i);
    centeredText(gaugeX(angle, 68), gaugeY(angle, 68), label, color);
  }

  for (int i = 0; i <= 40; ++i) {
    float angle = 220 + i * 7.0f;
    bool big = (i % 5) == 0;
    uint16_t color = angle > 402 ? RED_DIM : TEXT_DIM;
    radialLine(angle, big ? 88 : 94, 100, color);
  }

  float sweep = fmodf(nowMs * 0.030f, 360.0f);
  drawArc(CX, CY, 105, sweep, sweep + 28, CYAN, 3);
}

void drawNeedle(float angleDeg, int outerRadius, uint16_t fill, uint16_t edge)
{
  float a = (angleDeg - 90.0f) * DEG_TO_RAD;
  float p = a + HALF_PI;
  int tail = -18;
  int half = 5;
  int bx = CX + cosf(a) * tail;
  int by = CY + sinf(a) * tail;
  int tx = CX + cosf(a) * outerRadius;
  int ty = CY + sinf(a) * outerRadius;
  int ox = cosf(p) * half;
  int oy = sinf(p) * half;

  frame->fillTriangle(bx + ox, by + oy, bx - ox, by - oy, tx, ty, fill);
  frame->drawLine(bx + ox, by + oy, tx, ty, edge);
  frame->drawLine(bx - ox, by - oy, tx, ty, edge);
}

void drawTachNeedle(uint32_t nowMs)
{
  float rpmWave = (sinf(nowMs * 0.0026f) + 1.0f) * 0.5f;
  float kick = (sinf(nowMs * 0.010f) > 0.90f) ? 0.12f : 0.0f;
  float rpm = constrain(rpmWave * 0.82f + kick, 0.05f, 0.96f);
  float angle = 220.0f + rpm * 280.0f;

  drawArc(CX, CY, 89, 220, angle, rpm > 0.72f ? RED : ORANGE, 3);
  drawNeedle(angle, 78, RED, ORANGE);

  int rpmValue = (int)(rpm * 8500.0f);
  char text[16];
  snprintf(text, sizeof(text), "%04d", rpmValue);
  centeredText(CX, 72, "RPM x1000", TEXT_DIM);
  centeredText(CX, 88, text, WHITE, 2);
}

void drawBoostGauge(uint32_t nowMs)
{
  constexpr int gx = 52;
  constexpr int gy = 159;
  float boost = (sinf(nowMs * 0.0038f + 1.3f) + 1.0f) * 0.5f;
  float angle = 220.0f + boost * 280.0f;

  frame->fillCircle(gx, gy, 33, BLACK);
  frame->drawCircle(gx, gy, 34, PANEL);
  drawArc(gx, gy, 29, 220, 500, STEEL_DARK, 5);
  drawArc(gx, gy, 29, 220, angle, CYAN, 5);

  int x = gx + cosf((angle - 90.0f) * DEG_TO_RAD) * 24;
  int y = gy + sinf((angle - 90.0f) * DEG_TO_RAD) * 24;
  frame->drawLine(gx, gy, x, y, CYAN);
  frame->fillCircle(gx, gy, 3, WHITE);
  centeredText(gx, gy - 9, "BOOST", TEXT_DIM);

  char text[8];
  snprintf(text, sizeof(text), "%1.1f", boost * 2.4f);
  centeredText(gx, gy + 14, text, WHITE);
}

void drawTempGauge(uint32_t nowMs)
{
  constexpr int gx = 188;
  constexpr int gy = 159;
  float temp = (sinf(nowMs * 0.0024f + 2.1f) + 1.0f) * 0.5f;
  float angle = 220.0f + temp * 280.0f;

  frame->fillCircle(gx, gy, 33, BLACK);
  frame->drawCircle(gx, gy, 34, PANEL);
  drawArc(gx, gy, 29, 220, 500, STEEL_DARK, 5);
  drawArc(gx, gy, 29, 220, angle, temp > 0.70f ? AMBER : GREEN, 5);

  int x = gx + cosf((angle - 90.0f) * DEG_TO_RAD) * 24;
  int y = gy + sinf((angle - 90.0f) * DEG_TO_RAD) * 24;
  frame->drawLine(gx, gy, x, y, temp > 0.70f ? AMBER : GREEN);
  frame->fillCircle(gx, gy, 3, WHITE);
  centeredText(gx, gy - 9, "OIL", TEXT_DIM);

  char text[8];
  snprintf(text, sizeof(text), "%02dC", 82 + (int)(temp * 24.0f));
  centeredText(gx, gy + 14, text, WHITE);
}

void drawSpeedPanel(const tm &localTime, uint32_t nowMs)
{
  int speed = 62 + (int)((sinf(nowMs * 0.0020f) + 1.0f) * 58.0f);
  char text[18];

  frame->fillRoundRect(72, 138, 96, 45, 5, BLACK);
  frame->drawRoundRect(72, 138, 96, 45, 5, PANEL);
  snprintf(text, sizeof(text), "%03d", speed);
  centeredText(CX, 153, text, WHITE, 2);
  centeredText(CX, 173, "KM/H", TEXT_DIM);

  frame->fillRoundRect(72, 199, 96, 23, 4, BLACK);
  frame->drawRoundRect(72, 199, 96, 23, 4, PANEL);
  snprintf(text, sizeof(text), "%02d:%02d:%02d", localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
  centeredText(CX, 211, text, CYAN);

  centeredText(CX, 23, "TURBO SPORT", ORANGE);
  centeredText(CX, 35, timeSynced ? "GPS TIME LOCK" : "LOCAL CLOCK", TEXT_DIM);
}

void drawShiftLights(uint32_t nowMs)
{
  int active = (nowMs / 180) % 9;
  for (int i = 0; i < 9; ++i) {
    int x = 56 + i * 16;
    uint16_t color = i < 5 ? GREEN : (i < 7 ? AMBER : RED);
    frame->fillCircle(x, 52, 4, i <= active ? color : STEEL_DARK);
    frame->drawCircle(x, 52, 5, BLACK);
  }
}

void drawClockHands(const tm &localTime, uint32_t nowMs)
{
  float second = smoothClockSecond(localTime, nowMs);
  float minute = localTime.tm_min + (second / 60.0f);
  float hour = (localTime.tm_hour % 12) + (minute / 60.0f);

  radialLine(hour * 30.0f, -8, 37, STEEL);
  radialLine(minute * 6.0f, -10, 58, WHITE);
  radialLine(second * 6.0f, -17, 82, CYAN);

  frame->fillCircle(CX + 2, CY + 2, 11, BLACK);
  frame->fillCircle(CX, CY, 10, STEEL);
  frame->fillCircle(CX, CY, 5, RED);
  frame->drawCircle(CX, CY, 13, CYAN_DIM);
}

void renderFrame(const tm &localTime)
{
  uint32_t nowMs = millis();
  drawBackground();
  drawTachScale(nowMs);
  drawShiftLights(nowMs);
  drawTachNeedle(nowMs);
  drawBoostGauge(nowMs);
  drawTempGauge(nowMs);
  drawSpeedPanel(localTime, nowMs);
  drawClockHands(localTime, nowMs);
  frame->flush();
}

void drawBootStatus(const char *status)
{
  frame->fillScreen(BLACK);
  frame->fillCircle(CX, CY, 112, DASH_BLACK);
  frame->drawCircle(CX, CY, 108, STEEL_DARK);
  drawArc(CX, CY, 92, 220, 500, RED, 3);
  centeredText(CX, 88, "SPORT", ORANGE, 2);
  centeredText(CX, 119, status, WHITE);
  centeredText(CX, 143, "DASH ONLINE", CYAN);
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
  Serial.println("Automotive Dashboard watch face booting");
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
    Serial.printf("Automotive dash %02d:%02d:%02d source=%s\n",
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  timeSynced ? "ntp" : "build");
  }
}

#endif
