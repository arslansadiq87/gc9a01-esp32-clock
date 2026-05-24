#if defined(WATCH_FACE_ACTIVE) || defined(BUILD_MATRIX_BACKUP)

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
constexpr uint16_t VOID = 0x0000;
constexpr uint16_t GREEN_DARK = 0x0200;
constexpr uint16_t GREEN_DIM = 0x03E0;
constexpr uint16_t GREEN = 0x07E0;
constexpr uint16_t GREEN_BRIGHT = 0x87F0;
constexpr uint16_t WHITE_GREEN = 0xDFFB;
constexpr uint16_t PANEL = 0x0120;
constexpr uint16_t SHADOW = 0x0000;

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

void radialLine(float angleDeg, int innerRadius, int outerRadius, uint16_t color)
{
  float a = (angleDeg - 90.0f) * DEG_TO_RAD;
  int x1 = CX + cosf(a) * innerRadius;
  int y1 = CY + sinf(a) * innerRadius;
  int x2 = CX + cosf(a) * outerRadius;
  int y2 = CY + sinf(a) * outerRadius;
  frame->drawLine(x1, y1, x2, y2, color);
}

bool insideDial(int x, int y, int margin = 0)
{
  int dx = x - CX;
  int dy = y - CY;
  int r = 112 - margin;
  return (dx * dx + dy * dy) < (r * r);
}

char matrixChar(int column, int row, uint32_t tick)
{
  static const char chars[] = "01010110ABCDEFGHIJKLMNOPQRSTUVWXYZ#$%&<>[]{}";
  uint8_t index = (column * 13 + row * 7 + tick * 3) % (sizeof(chars) - 1);
  return chars[index];
}

void drawRainColumn(int column, int x, uint32_t nowMs)
{
  int speed = 46 + (column % 5) * 13;
  int head = ((nowMs / speed) * 12 + column * 29) % 292 - 32;
  int trail = 7 + (column % 4);
  uint32_t tick = nowMs / 180;

  frame->setTextSize(1);

  for (int i = 0; i < trail; ++i) {
    int y = head - i * 12;
    if (y < 0 || y > SCREEN_H - 8 || !insideDial(x + 3, y + 4, 4)) {
      continue;
    }

    uint16_t color = GREEN_DARK;
    if (i == 0) {
      color = WHITE_GREEN;
    } else if (i < 3) {
      color = GREEN_BRIGHT;
    } else if (i < 6) {
      color = GREEN;
    }

    frame->setTextColor(color);
    frame->setCursor(x, y);
    frame->print(matrixChar(column, y / 12, tick));
  }
}

void drawMatrixRain(uint32_t nowMs)
{
  frame->fillScreen(BLACK);
  frame->fillCircle(CX, CY, 118, VOID);
  frame->fillCircle(CX, CY, 114, BLACK);

  for (int x = 10, column = 0; x < SCREEN_W; x += 10, ++column) {
    drawRainColumn(column, x, nowMs);
  }
}

void drawGreenGlowRings(uint32_t nowMs)
{
  frame->drawCircle(CX, CY, 115, GREEN_DIM);
  frame->drawCircle(CX, CY, 109, PANEL);
  frame->drawCircle(CX, CY, 74, GREEN_DARK);

  for (int deg = 0; deg < 360; deg += 6) {
    bool major = (deg % 30) == 0;
    radialLine(deg, major ? 100 : 106, 109, major ? GREEN : GREEN_DARK);
  }

  float sweep = fmodf(nowMs * 0.050f, 360.0f);
  for (int i = 0; i < 38; i += 3) {
    radialLine(sweep - i, 81, 88, i < 9 ? GREEN_BRIGHT : GREEN_DIM);
  }
}

void drawCodeTicker(uint32_t nowMs)
{
  static const char *lines[] = {
      "WAKE_UP_NEO//CLOCK_LOCK",
      "SYS.TIME.DECRYPT",
      "MATRIX::RTC_STREAM"};

  frame->setTextSize(1);
  for (int i = 0; i < 3; ++i) {
    int y = 46 + i * 74;
    int start = -((nowMs / (90 + i * 35)) % 90);
    frame->drawFastHLine(35, y - 5, 170, GREEN_DARK);
    frame->setTextColor(i == 1 ? GREEN_BRIGHT : GREEN_DIM);
    for (int x = start; x < 230; x += 126) {
      if (insideDial(x + 45, y, 10)) {
        frame->setCursor(x, y);
        frame->print(lines[i]);
      }
    }
  }
}

void drawDigitalCore(const tm &localTime, uint32_t nowMs)
{
  char timeText[12];
  char dateText[14];
  snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d", localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
  snprintf(dateText, sizeof(dateText), "%02d.%02d.%04d", localTime.tm_mday, localTime.tm_mon + 1, localTime.tm_year + 1900);

  int pulse = 2 + ((nowMs / 120) % 4);
  frame->fillCircle(CX + 2, CY + 3, 54, SHADOW);
  frame->fillCircle(CX, CY, 52, BLACK);
  frame->drawCircle(CX, CY, 53 + pulse, GREEN_DARK);
  frame->drawCircle(CX, CY, 50, GREEN_DIM);
  frame->drawRoundRect(65, 96, 110, 48, 4, PANEL);
  frame->drawRoundRect(69, 100, 102, 40, 3, GREEN_DARK);

  centeredText(CX, 88, "THE MATRIX", GREEN_BRIGHT);
  centeredText(CX, 116, timeText, WHITE_GREEN, 2);
  centeredText(CX, 137, dateText, GREEN_DIM);
  centeredText(CX, 154, timeSynced ? "NTP LINK" : "LOCAL TIME", GREEN_DARK);
}

void drawAnalogHands(const tm &localTime, uint32_t nowMs)
{
  float second = smoothClockSecond(localTime, nowMs);
  float minute = localTime.tm_min + (second / 60.0f);
  float hour = (localTime.tm_hour % 12) + (minute / 60.0f);

  radialLine(hour * 30.0f, -8, 38, GREEN_BRIGHT);
  radialLine(hour * 30.0f + 0.9f, -4, 36, GREEN_DARK);
  radialLine(minute * 6.0f, -10, 60, WHITE_GREEN);
  radialLine(minute * 6.0f + 0.7f, -6, 57, GREEN);
  radialLine(second * 6.0f, -17, 82, GREEN_BRIGHT);

  frame->fillCircle(CX, CY, 8, WHITE_GREEN);
  frame->fillCircle(CX, CY, 4, BLACK);
  frame->drawCircle(CX, CY, 11, GREEN);
}

void renderFrame(const tm &localTime)
{
  uint32_t nowMs = millis();
  drawMatrixRain(nowMs);
  drawCodeTicker(nowMs);
  drawGreenGlowRings(nowMs);
  drawDigitalCore(localTime, nowMs);
  drawAnalogHands(localTime, nowMs);
  frame->flush();
}

void drawBootStatus(const char *status)
{
  frame->fillScreen(BLACK);
  frame->fillCircle(CX, CY, 112, VOID);
  frame->drawCircle(CX, CY, 108, GREEN_DIM);
  frame->drawCircle(CX, CY, 82, GREEN_DARK);
  centeredText(CX, 90, "MATRIX", GREEN_BRIGHT, 2);
  centeredText(CX, 120, status, WHITE_GREEN);
  centeredText(CX, 143, "CODE STREAM ONLINE", GREEN_DIM);
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
  Serial.println("Matrix watch face booting");
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
  if (millis() - lastFrameMs < 80) {
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
    Serial.printf("Matrix %02d:%02d:%02d source=%s\n",
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  timeSynced ? "ntp" : "build");
  }
}

#endif
