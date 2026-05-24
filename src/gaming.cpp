#if defined(WATCH_FACE_ACTIVE) || defined(BUILD_GAMING_BACKUP)

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
constexpr uint16_t VOID = 0x0005;
constexpr uint16_t PANEL = 0x1087;
constexpr uint16_t PANEL_EDGE = 0x31AF;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t TEXT_DIM = 0x8C71;
constexpr uint16_t CYAN = 0x57FF;
constexpr uint16_t MAGENTA = 0xF81F;
constexpr uint16_t GREEN = 0x07E0;
constexpr uint16_t AMBER = 0xFD20;
constexpr uint16_t RED = 0xF800;

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, TFT_RST, 0, true);
Arduino_Canvas *frame = new Arduino_Canvas(SCREEN_W, SCREEN_H, gfx);

bool timeSynced = false;
uint32_t lastFrameMs = 0;
int lastSecondPrinted = -1;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

uint16_t colorWheel(uint8_t pos)
{
  pos = 255 - pos;
  if (pos < 85) {
    return rgb565(255 - pos * 3, 0, pos * 3);
  }
  if (pos < 170) {
    pos -= 85;
    return rgb565(0, pos * 3, 255 - pos * 3);
  }
  pos -= 170;
  return rgb565(pos * 3, 255 - pos * 3, 0);
}

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

void drawBackground(uint32_t nowMs)
{
  frame->fillScreen(VOID);
  frame->fillCircle(CX, CY, 118, BLACK);
  frame->fillCircle(CX, CY, 114, VOID);

  for (int y = 26; y <= 214; y += 12) {
    uint16_t color = ((y + nowMs / 80) % 36 == 0) ? PANEL_EDGE : 0x0845;
    int dy = y - CY;
    int span = sqrtf(111 * 111 - dy * dy);
    frame->drawFastHLine(CX - span, y, span * 2, color);
  }

  for (int x = 30; x <= 210; x += 18) {
    int offset = (nowMs / 90 + x) % 18;
    for (int y = 24 - offset; y <= 216; y += 18) {
      int dx = x - CX;
      int dy = y - CY;
      if ((dx * dx + dy * dy) < 106 * 106) {
        frame->drawPixel(x, y, colorWheel((x * 3 + y + nowMs / 10) & 0xFF));
      }
    }
  }
}

void drawRgbRing(uint32_t nowMs, int radius, int thickness, float speed, uint8_t hueOffset)
{
  float spin = fmodf(nowMs * speed, 360.0f);
  for (int segment = 0; segment < 36; ++segment) {
    float start = spin + segment * 10.0f;
    uint16_t color = colorWheel(hueOffset + segment * 7 + nowMs / 18);
    for (int t = 0; t < thickness; ++t) {
      drawArc(CX, CY, radius - t, start, start + 6.5f, color, 2);
    }
  }
}

void drawOuterHardwareRing(uint32_t nowMs)
{
  frame->drawCircle(CX, CY, 115, PANEL_EDGE);
  frame->drawCircle(CX, CY, 109, 0x18E7);

  for (int deg = 0; deg < 360; deg += 6) {
    bool major = (deg % 30) == 0;
    uint16_t color = major ? colorWheel(deg / 2 + nowMs / 20) : 0x18E7;
    radialLine(deg, major ? 100 : 105, 109, color);
  }

  drawRgbRing(nowMs, 102, 2, 0.055f, 0);
  drawRgbRing(nowMs, 91, 2, -0.035f, 90);
}

void drawChipCore(uint32_t nowMs, const tm &localTime)
{
  int pulse = 2 + ((nowMs / 70) % 9);
  uint16_t glow = colorWheel(nowMs / 12);

  frame->fillRoundRect(74, 82, 92, 76, 8, 0x0843);
  frame->drawRoundRect(74, 82, 92, 76, 8, glow);
  frame->drawRoundRect(79, 87, 82, 66, 5, PANEL_EDGE);

  for (int i = 0; i < 9; ++i) {
    int pinY = 90 + i * 7;
    frame->drawFastHLine(66, pinY, 8, colorWheel(i * 21 + nowMs / 16));
    frame->drawFastHLine(166, pinY, 8, colorWheel(160 + i * 21 + nowMs / 16));
  }

  frame->fillCircle(CX, CY - 2, 23 + pulse, 0x0805);
  frame->drawCircle(CX, CY - 2, 28 + pulse, glow);
  frame->drawCircle(CX, CY - 2, 20, CYAN);

  char timeText[12];
  snprintf(timeText, sizeof(timeText), "%02d:%02d", localTime.tm_hour, localTime.tm_min);
  centeredText(CX, CY - 10, timeText, WHITE, 2);

  char secText[8];
  snprintf(secText, sizeof(secText), ":%02d", localTime.tm_sec);
  centeredText(CX, CY + 12, secText, colorWheel(180 + nowMs / 8));
  centeredText(CX, CY + 42, timeSynced ? "RGB SYNC" : "LOCAL CLK", TEXT_DIM);
}

void drawEqualizer(uint32_t nowMs)
{
  constexpr int bars = 18;
  constexpr int baseX = 33;
  constexpr int baseY = 203;
  constexpr int barW = 6;

  frame->drawRoundRect(24, 170, 192, 47, 4, PANEL);
  centeredText(CX, 162, "AUDIO REACTIVE", TEXT_DIM);

  for (int i = 0; i < bars; ++i) {
    float wave = sinf((nowMs * 0.010f) + i * 0.85f);
    float beat = sinf((nowMs * 0.024f) + i * 0.33f);
    int h = 8 + (int)((wave * 0.5f + 0.5f) * 21) + (beat > 0.82f ? 8 : 0);
    int x = baseX + i * 10;
    uint16_t color = colorWheel(i * 13 + nowMs / 7);
    frame->fillRoundRect(x, baseY - h, barW, h, 2, color);
    frame->drawFastHLine(x, baseY + 2, barW, 0x39E7);
  }
}

void drawMetricPanel(int x, int y, const char *label, int value, uint16_t color)
{
  char text[12];
  frame->fillRoundRect(x + 2, y + 2, 58, 39, 5, BLACK);
  frame->drawRoundRect(x, y, 58, 39, 5, PANEL_EDGE);
  frame->setTextSize(1);
  frame->setTextColor(TEXT_DIM);
  frame->setCursor(x + 10, y + 7);
  frame->print(label);
  snprintf(text, sizeof(text), "%02d%%", value);
  centeredText(x + 29, y + 27, text, color);

  int filled = map(value, 0, 100, 0, 44);
  frame->drawFastHLine(x + 7, y + 35, 44, 0x2967);
  frame->drawFastHLine(x + 7, y + 35, filled, color);
}

void drawHardwarePanels(uint32_t nowMs)
{
  int cpu = 48 + (int)(sinf(nowMs * 0.0037f) * 22.0f);
  int gpu = 62 + (int)(sinf(nowMs * 0.0029f + 1.8f) * 24.0f);
  int ram = 55 + (int)(sinf(nowMs * 0.0021f + 3.0f) * 18.0f);

  drawMetricPanel(17, 48, "CPU", cpu, colorWheel(nowMs / 10));
  drawMetricPanel(165, 48, "GPU", gpu, colorWheel(85 + nowMs / 10));
  drawMetricPanel(91, 28, "RAM", ram, colorWheel(170 + nowMs / 10));
}

void drawAnalogOverlay(const tm &localTime, uint32_t nowMs)
{
  float second = smoothClockSecond(localTime, nowMs);
  float minute = localTime.tm_min + second / 60.0f;
  float hour = (localTime.tm_hour % 12) + minute / 60.0f;

  radialLine(hour * 30.0f, -8, 36, WHITE);
  radialLine(minute * 6.0f, -10, 55, CYAN);
  radialLine(second * 6.0f, -17, 76, colorWheel(nowMs / 4));

  frame->fillCircle(CX, CY, 7, WHITE);
  frame->fillCircle(CX, CY, 4, BLACK);
  frame->drawCircle(CX, CY, 10, colorWheel(nowMs / 6));
}

void renderFrame(const tm &localTime)
{
  uint32_t nowMs = millis();
  drawBackground(nowMs);
  drawOuterHardwareRing(nowMs);
  drawHardwarePanels(nowMs);
  drawChipCore(nowMs, localTime);
  drawEqualizer(nowMs);
  drawAnalogOverlay(localTime, nowMs);
  frame->flush();
}

void drawBootStatus(const char *status)
{
  frame->fillScreen(BLACK);
  frame->fillCircle(CX, CY, 112, VOID);
  drawRgbRing(millis(), 100, 3, 0.05f, 0);
  centeredText(CX, 88, "RGB", WHITE, 3);
  centeredText(CX, 122, status, CYAN);
  centeredText(CX, 145, "GAMING MODE", MAGENTA);
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
  Serial.println("Gaming RGB watch face booting");
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
  if (millis() - lastFrameMs < 50) {
    delay(3);
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
    Serial.printf("Gaming RGB %02d:%02d:%02d source=%s\n",
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  timeSynced ? "ntp" : "build");
  }
}

#endif
