#pragma once

#include <Arduino.h>
#include <time.h>

inline float smoothClockSecond(const tm &localTime, uint32_t nowMs)
{
  static int lastHour = -1;
  static int lastMinute = -1;
  static int lastSecond = -1;
  static uint32_t secondStartMs = 0;

  if (localTime.tm_hour != lastHour || localTime.tm_min != lastMinute || localTime.tm_sec != lastSecond) {
    lastHour = localTime.tm_hour;
    lastMinute = localTime.tm_min;
    lastSecond = localTime.tm_sec;
    secondStartMs = nowMs;
  }

  uint32_t elapsed = nowMs - secondStartMs;
  if (elapsed > 999) {
    elapsed = 999;
  }

  return localTime.tm_sec + (elapsed / 1000.0f);
}
