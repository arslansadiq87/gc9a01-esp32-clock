#if defined(WATCH_FACE_ACTIVE) || defined(BUILD_AI_CORE_BACKUP)

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
constexpr uint16_t VOID = 0x0004;
constexpr uint16_t DEEP_NAVY = 0x010B;
constexpr uint16_t PANEL = 0x1087;
constexpr uint16_t CORE_BLUE = 0x047F;
constexpr uint16_t CORE_CYAN = 0x57FF;
constexpr uint16_t CORE_WHITE = 0xFFFF;
constexpr uint16_t NODE_BLUE = 0x2D7F;
constexpr uint16_t NODE_DIM = 0x0A4D;
constexpr uint16_t NEURAL_PURPLE = 0xA2FF;
constexpr uint16_t MAGENTA = 0xF81F;
constexpr uint16_t AMBER = 0xFD20;
constexpr uint16_t AMBER_DIM = 0x7AC0;
constexpr uint16_t GRID = 0x094B;
constexpr uint16_t TEXT_DIM = 0x7BEF;

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

void drawNeuralBackground(uint32_t nowMs)
{
  frame->fillScreen(VOID);
  frame->fillCircle(CX, CY, 118, BLACK);
  frame->fillCircle(CX, CY, 114, DEEP_NAVY);

  for (int y = 28; y <= 212; y += 23) {
    int offset = ((nowMs / 180) + y) % 23;
    for (int x = 8 - offset; x < SCREEN_W; x += 23) {
      int dx = x - CX;
      int dy = y - CY;
      if ((dx * dx + dy * dy) < 111 * 111) {
        frame->drawPixel(x, y, GRID);
      }
    }
  }

  for (int i = 0; i < 26; ++i) {
    int ax = (i * 67 + 29) % SCREEN_W;
    int ay = (i * 41 + 37) % SCREEN_H;
    int bx = (i * 31 + 173) % SCREEN_W;
    int by = (i * 59 + 61) % SCREEN_H;
    int adx = ax - CX;
    int ady = ay - CY;
    int bdx = bx - CX;
    int bdy = by - CY;
    if ((adx * adx + ady * ady) < 103 * 103 && (bdx * bdx + bdy * bdy) < 103 * 103) {
      frame->drawLine(ax, ay, bx, by, ((i + nowMs / 240) % 5 == 0) ? NODE_DIM : GRID);
    }
  }
}

void drawOuterProcessorRing(uint32_t nowMs)
{
  frame->drawCircle(CX, CY, 114, CORE_BLUE);
  frame->drawCircle(CX, CY, 108, NODE_DIM);

  for (int deg = 0; deg < 360; deg += 6) {
    bool major = (deg % 30) == 0;
    radialLine(deg, major ? 100 : 105, 108, major ? CORE_CYAN : NODE_DIM);
  }

  float spin = fmodf(nowMs * 0.030f, 360.0f);
  drawArc(CX, CY, 102, spin, spin + 78, CORE_CYAN, 3);
  drawArc(CX, CY, 102, spin + 132, spin + 184, NEURAL_PURPLE, 3);
  drawArc(CX, CY, 96, -spin * 0.8f, -spin * 0.8f + 42, MAGENTA, 3);
  drawArc(CX, CY, 90, spin * 1.3f, spin * 1.3f + 22, AMBER, 3);
}

void drawNeuralRing(uint32_t nowMs, int radius, float speed, uint16_t color, int nodes, float phase)
{
  drawArc(CX, CY, radius, 0, 360, NODE_DIM, 5);

  float rotation = fmodf(nowMs * speed + phase, 360.0f);
  for (int i = 0; i < nodes; ++i) {
    float angle = rotation + (360.0f / nodes) * i;
    int x = orbitX(angle, radius);
    int y = orbitY(angle, radius);
    int next = (i + 1) % nodes;
    float nextAngle = rotation + (360.0f / nodes) * next;
    int nx = orbitX(nextAngle, radius);
    int ny = orbitY(nextAngle, radius);

    bool active = ((nowMs / 250 + i) % nodes) == 0;
    frame->drawLine(x, y, nx, ny, active ? color : NODE_DIM);
    frame->fillCircle(x, y, active ? 4 : 2, active ? CORE_WHITE : color);
  }
}

void drawCore(uint32_t nowMs)
{
  int pulse = 4 + ((nowMs / 80) % 11);
  frame->fillCircle(CX, CY, 38 + pulse / 2, NODE_DIM);
  frame->drawCircle(CX, CY, 44 + pulse, CORE_BLUE);
  frame->drawCircle(CX, CY, 37, CORE_CYAN);
  frame->drawCircle(CX, CY, 30, NEURAL_PURPLE);
  frame->fillCircle(CX, CY, 22, CORE_BLUE);
  frame->fillCircle(CX, CY, 14 + pulse / 3, CORE_CYAN);
  frame->fillCircle(CX, CY, 6, CORE_WHITE);

  for (int i = 0; i < 12; ++i) {
    float angle = i * 30.0f + fmodf(nowMs * 0.045f, 30.0f);
    int x1 = orbitX(angle, 24);
    int y1 = orbitY(angle, 24);
    int x2 = orbitX(angle, 34);
    int y2 = orbitY(angle, 34);
    frame->drawLine(x1, y1, x2, y2, (i % 3 == 0) ? CORE_WHITE : CORE_CYAN);
  }

  centeredText(CX, CY + 53, "AI CORE", CORE_WHITE);
}

void drawThinkingBursts(uint32_t nowMs)
{
  for (int i = 0; i < 10; ++i) {
    float angle = i * 36.0f + fmodf(nowMs * 0.020f, 360.0f);
    int radius = 58 + ((i * 9 + nowMs / 120) % 30);
    int x = orbitX(angle, radius);
    int y = orbitY(angle, radius);
    uint16_t color = (i % 3 == 0) ? MAGENTA : ((i % 3 == 1) ? CORE_CYAN : AMBER);
    frame->fillCircle(x, y, 2, color);
    frame->drawLine(CX, CY, x, y, ((nowMs / 180 + i) % 4 == 0) ? color : NODE_DIM);
  }
}

void drawStatusPanels(const tm &localTime, uint32_t nowMs)
{
  char text[18];

  centeredText(CX, 17, "NEURAL INTERFACE", CORE_WHITE);
  centeredText(CX, 29, timeSynced ? "SYNCED MODEL" : "LOCAL MODEL", timeSynced ? CORE_CYAN : AMBER);

  frame->drawRoundRect(13, 50, 54, 39, 4, PANEL);
  frame->setTextSize(1);
  frame->setTextColor(TEXT_DIM);
  frame->setCursor(22, 58);
  frame->print("TOKENS");
  snprintf(text, sizeof(text), "%04d", 2048 + (int)((nowMs / 70) % 896));
  centeredText(40, 76, text, CORE_CYAN);

  frame->drawRoundRect(173, 50, 54, 39, 4, PANEL);
  frame->setCursor(187, 58);
  frame->print("LOAD");
  snprintf(text, sizeof(text), "%02d%%", 43 + (int)((nowMs / 130) % 37));
  centeredText(200, 76, text, AMBER);

  frame->drawRoundRect(13, 154, 54, 39, 4, PANEL);
  frame->setCursor(22, 162);
  frame->print("LAT");
  snprintf(text, sizeof(text), "%02dms", 12 + (int)((nowMs / 220) % 19));
  centeredText(40, 180, text, NEURAL_PURPLE);

  frame->drawRoundRect(173, 154, 54, 39, 4, PANEL);
  frame->setCursor(184, 162);
  frame->print("TEMP");
  snprintf(text, sizeof(text), "%02dC", 38 + (int)((nowMs / 500) % 5));
  centeredText(200, 180, text, CORE_CYAN);

  frame->drawRoundRect(79, 207, 82, 19, 4, PANEL);
  snprintf(text, sizeof(text), "%02d:%02d:%02d", localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
  centeredText(CX, 216, text, CORE_WHITE);
}

void drawAnalogHands(const tm &localTime)
{
  float second = smoothClockSecond(localTime, millis());
  float minute = localTime.tm_min + (second / 60.0f);
  float hour = (localTime.tm_hour % 12) + (minute / 60.0f);

  radialLine(hour * 30.0f, -7, 34, CORE_WHITE);
  radialLine(minute * 6.0f, -10, 54, CORE_CYAN);
  radialLine(second * 6.0f, -16, 75, AMBER);

  frame->fillCircle(CX, CY, 7, CORE_WHITE);
  frame->fillCircle(CX, CY, 4, CORE_BLUE);
  frame->drawCircle(CX, CY, 10, NEURAL_PURPLE);
}

void renderFrame(const tm &localTime)
{
  uint32_t nowMs = millis();

  drawNeuralBackground(nowMs);
  drawOuterProcessorRing(nowMs);
  drawNeuralRing(nowMs, 82, 0.018f, CORE_CYAN, 9, 0);
  drawNeuralRing(nowMs, 67, -0.026f, NEURAL_PURPLE, 7, 30);
  drawNeuralRing(nowMs, 51, 0.040f, AMBER, 6, 70);
  drawThinkingBursts(nowMs);
  drawCore(nowMs);
  drawStatusPanels(localTime, nowMs);
  drawAnalogHands(localTime);

  frame->flush();
}

void drawBootStatus(const char *status)
{
  frame->fillScreen(DEEP_NAVY);
  frame->drawCircle(CX, CY, 108, CORE_BLUE);
  frame->drawCircle(CX, CY, 88, NODE_DIM);
  centeredText(CX, 88, "AI CORE", CORE_WHITE, 2);
  centeredText(CX, 118, status, CORE_CYAN);
  centeredText(CX, 140, "NEURAL BUS ONLINE", AMBER);
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
  Serial.println("AI Core / Neural Interface watch face booting");
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
    Serial.printf("AI core %02d:%02d:%02d source=%s\n",
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  timeSynced ? "ntp" : "build");
  }
}

#endif
