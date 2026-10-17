#pragma once
#include <Arduino.h>
// Types used in function signatures live here (Arduino IDE auto-prototypes need them declared first)
struct DayData {
  uint32_t ymd;          // 20261016
  uint32_t sig;
  int16_t raw[6];        // minutes: fajr, sunrise, dhuhr, asr, maghrib, isha
  uint8_t hd, hm;        // hijri day, month
  uint16_t hy;
  uint8_t src;           // 0 none, 1 local calc, 2 API
};
struct Note { uint16_t f, ms; };
enum Screen : uint8_t { S_HOME, S_TIMES, S_DUAS, S_QR, S_COUNT };
enum Mode : uint8_t { M_NORMAL, M_ADHAN, M_PRE, M_KAHF };
