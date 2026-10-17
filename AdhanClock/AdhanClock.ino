/*
 *  ساعة الأذان  -  Adhan Clock   (ESP32 + ST7735S 1.8" + TTP223 + buzzer)
 *  Created by El-Amine (MedMA07) - https://github.com/MedMA07  - MIT License (see LICENSE)
 *  ---------------------------------------------------------------------
 *  4 screens (tap the touch sensor to switch):
 *    1) Home      : time, Gregorian + Hijri date, next prayer + countdown
 *    2) Prayers   : the 6 times of today
 *    3) Adhkar    : adhkar / duas (slideshow)
 *    4) Phone link: QR codes -> join the device Wi-Fi -> open the settings page
 *  Touch: tap = next screen | hold 1s = action (next page / mark Kahf read) |
 *         hold 2s = next category (adhkar) | hold 6s on QR screen = factory reset
 *
 *  Libraries (Library Manager):
 *     Adafruit GFX Library, Adafruit ST7735 and ST7789 Library, Adafruit BusIO,
 *     ArduinoJson (7.x)       (QR generator is bundled: qrcodegen.c/.h)
 *  Board: "ESP32 Dev Module"  | Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
 */
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include "qrcodegen.h"      // QR generator bundled with the project (no external library needed)
#include <time.h>
#include <sys/time.h>

#include "config.h"
#include "bmp.h"
#include "types.h"
#include "calc.h"
#include "assets_labels.h"
#include "assets_duas.h"
#include "assets_splash.h"
#include "web_page.h"

// ============================================================================
//  Colors
// ============================================================================
#if SWAP_RB
#define RGB(r, g, b) ((uint16_t)((((b) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((r) >> 3)))
#else
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#endif
#define C_GOLD   RGB(233, 191, 96)
#define C_WHITE  RGB(255, 255, 255)
#define C_CREAM  RGB(245, 236, 210)
#define C_DIM    RGB(125, 165, 145)
#define C_HI     RGB(18, 92, 66)
#define C_RED    RGB(255, 120, 90)
#define C_GREEN  RGB(90, 220, 140)
#define C_BLACK  RGB(0, 0, 0)

// ============================================================================
//  Globals
// ============================================================================
Adafruit_ST7735 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 cv(160, 128);
uint16_t* fb;
uint16_t bgLine[128];
WebServer server(80);
Preferences prefs;

// ---------------------------------------------------------------------------
//  Settings (stored as one blob in NVS)
// ---------------------------------------------------------------------------
#define SETTINGS_MAGIC 0xAD0C0004UL
struct Settings {
  uint32_t magic;
  char ssid[33], pass[65], apPass[17];
  char city[32], country[32];
  uint8_t method, school;
  int8_t hadj;
  float tz, lat, lng;
  int8_t tune[5];                       // fajr, dhuhr, asr, maghrib, isha (minutes)
  uint8_t beep, adhan, pre;
  uint16_t istg;                        // minutes, 0 = off
  uint8_t qfrom, qto;                   // quiet hours for istighfar
  uint8_t morn, eve, sleep, kahf;
  uint16_t mornOff, eveOff, sleepT, kahfT;
} S;

void setDefaults() {
  memset(&S, 0, sizeof(S));
  S.magic = SETTINGS_MAGIC;
  strcpy(S.apPass, AP_PASS_DEFAULT);
  strcpy(S.city, "Algiers");
  strcpy(S.country, "Algeria");
  S.method = 19; S.school = 0; S.hadj = 0; S.tz = 1.0f;
  S.lat = 36.7538f; S.lng = 3.0588f;
  S.beep = 1; S.adhan = 1; S.pre = 10; S.istg = 60; S.qfrom = 23; S.qto = 6;
  S.morn = 1; S.mornOff = 30; S.eve = 1; S.eveOff = 30;
  S.sleep = 1; S.sleepT = 22 * 60 + 30; S.kahf = 1; S.kahfT = 8 * 60;
}
void loadSettings() {
  setDefaults();
  Settings tmp;
  if (prefs.getBytesLength("cfg") == sizeof(tmp)) {
    prefs.getBytes("cfg", &tmp, sizeof(tmp));
    if (tmp.magic == SETTINGS_MAGIC) S = tmp;
  }
}
void saveSettings() { prefs.putBytes("cfg", &S, sizeof(S)); }

// ---------------------------------------------------------------------------
//  Day data
// ---------------------------------------------------------------------------
DayData day;
int eff[6];              // tuned minutes
bool dayOk = false;

bool needFetch = false;
uint32_t nextFetchMs = 0, fetchBackoff = 30000;
String lastMsg = "-";

uint32_t sigOf() {
  uint32_t h = 2166136261UL;
  auto mix = [&](uint8_t b) { h ^= b; h *= 16777619UL; };
  for (const char* p = S.city; *p; p++) mix(*p);
  mix('|');
  for (const char* p = S.country; *p; p++) mix(*p);
  mix(S.method); mix(S.school); mix((uint8_t)S.hadj);
  return h;
}

// ---------------------------------------------------------------------------
//  Time
// ---------------------------------------------------------------------------
tm tmNow;
bool timeOK = false;
bool ntpStarted = false;

void startNtp() {
  configTime((long)(S.tz * 3600.0f), 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}
void updateTime() {
  time_t t = time(nullptr);
  timeOK = t > 1700000000;
  localtime_r(&t, &tmNow);
}
uint32_t todayYmd() { return (uint32_t)(tmNow.tm_year + 1900) * 10000 + (tmNow.tm_mon + 1) * 100 + tmNow.tm_mday; }
int nowSec() { return tmNow.tm_hour * 3600 + tmNow.tm_min * 60 + tmNow.tm_sec; }

// ---------------------------------------------------------------------------
//  UI state
// ---------------------------------------------------------------------------
Screen screen = S_HOME;
Mode mode = M_NORMAL;
uint32_t modeStart = 0, lastActivity = 0;
int alertPrayer = 0;
bool kahfJustDone = false;
uint32_t kahfDoneYmd = 0;

// adhkar browsing
int catLo[CAT_COUNT], catHi[CAT_COUNT];
int duaLo = 0, duaHi = DUA_REF_COUNT - 1, duaCur = 0;
bool popup = false;
uint32_t popupUntil = 0, duaChanged = 0;

// QR
uint8_t qrData[qrcodegen_BUFFER_LEN_FOR_VERSION(3)], qrTmp[qrcodegen_BUFFER_LEN_FOR_VERSION(3)];
int qrStep = 0, qrShown = -1;
uint32_t qrChanged = 0;

// touch
bool tRaw = false, tStable = false, tConsumed = false;
uint32_t tChange = 0, tDown = 0;
uint8_t tTier = 0;

// wifi
bool staWasUp = false;
bool wifiRestart = false;
uint32_t wifiRestartAt = 0;
bool rebootPending = false;
uint32_t rebootAt = 0;

// fired events ring
uint32_t firedKeys[24];
uint8_t firedIdx = 0;
bool wasFired(uint32_t k) { for (auto v : firedKeys) if (v == k) return true; return false; }
void markFired(uint32_t k) { firedKeys[firedIdx++ % 24] = k; }

// ============================================================================
//  Buzzer (non blocking patterns)
// ============================================================================
static const Note PAT_ADHAN[]  = {{523,170},{0,60},{659,170},{0,60},{784,170},{0,60},{1047,340},{0,350},{784,170},{0,60},{659,170},{0,60},{523,340},{0,1300}};
static const Note PAT_SOFT[]   = {{1760,70},{0,90},{1760,70}};
static const Note PAT_REMIND[] = {{880,120},{0,80},{1175,120},{0,80},{1568,240}};
static const Note PAT_PRE[]    = {{1318,150},{0,120},{1318,150},{0,120},{1318,150}};
static const Note PAT_TICK[]   = {{2400,25}};
static const Note PAT_TEST[]   = {{1000,150},{0,100},{2000,150}};
static const Note PAT_OK[]     = {{1568,90},{0,40},{2093,160}};
static const Note PAT_BOOT[]   = {{1047,80},{0,30},{1319,80},{0,30},{1568,140}};
const Note* patP = nullptr;
uint8_t patN = 0, patI = 0, patRep = 0;
uint32_t patNext = 0;

void buzzTone(uint16_t f) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(BUZZ_PIN, f);
#else
  ledcWriteTone(0, f);
#endif
}
void buzzStop() { patP = nullptr; buzzTone(0); }
void buzzStart(const Note* p, uint8_t n, uint8_t rep, bool force = false) {
  if (!S.beep && !force) return;
  patP = p; patN = n; patI = 0; patRep = rep;
  buzzTone(p[0].f); patNext = millis() + p[0].ms;
}
#define BUZZ(p, rep) buzzStart(p, sizeof(p) / sizeof(Note), rep)
void buzzTick() {
  if (!patP || (int32_t)(millis() - patNext) < 0) return;
  patI++;
  if (patI >= patN) {
    if (patRep > 1) { patRep--; patI = 0; }
    else { buzzStop(); return; }
  }
  buzzTone(patP[patI].f);
  patNext = millis() + patP[patI].ms;
}

// ============================================================================
//  Drawing helpers (everything is drawn into the 160x128 canvas, then pushed)
// ============================================================================
static inline uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t a) {   // a = 0..15
  if (!a) return bg;
  if (a >= 15) return fg;
  uint32_t r1 = bg >> 11, g1 = (bg >> 5) & 63, b1 = bg & 31;
  uint32_t r2 = fg >> 11, g2 = (fg >> 5) & 63, b2 = fg & 31;
  uint32_t ia = 15 - a;
  uint32_t r = (r1 * ia + r2 * a) / 15, g = (g1 * ia + g2 * a) / 15, b = (b1 * ia + b2 * a) / 15;
  return (r << 11) | (g << 5) | b;
}
void drawBmp(const AlphaBmp& b, int x, int y, uint16_t color) {
  // streaming decoder of the nibble-RLE data (nonzero = alpha; 0,k = k+1 zeros; 0,15,hi,lo = 16+hilo zeros)
  uint32_t np = 0; int zr = 0;
  auto nib = [&]() -> uint8_t { uint8_t v = pgm_read_byte(&b.data[np >> 1]); uint8_t r = (np & 1) ? (v & 15) : (v >> 4); np++; return r; };
  for (int row = 0; row < b.h; row++) {
    int py = y + row;
    bool rowOk = (py >= 0 && py < 128);
    for (int col = 0; col < b.w; col++) {
      uint8_t a;
      if (zr > 0) { zr--; a = 0; }
      else {
        a = nib();
        if (a == 0) {
          uint8_t k = nib();
          int run = (k < 15) ? k + 1 : 16 + (nib() << 4) + nib();
          zr = run - 1;
        }
      }
      int px = x + col;
      if (a && rowOk && px >= 0 && px < 160) { uint16_t* p = &fb[py * 160 + px]; *p = blend565(*p, color, a); }
    }
  }
}
void bmpR(const AlphaBmp& b, int xr, int y, uint16_t c) { drawBmp(b, xr - b.w, y, c); }
void bmpC(const AlphaBmp& b, int cx, int y, uint16_t c) { drawBmp(b, cx - b.w / 2, y, c); }
inline const AlphaBmp& LB(int id) { return LABELS[id]; }

void txt(int x, int y, const char* s, const GFXfont* f, uint16_t c) {
  cv.setFont(f); cv.setTextColor(c); cv.setCursor(x, y); cv.print(s);
}
void txtR(int xr, int y, const char* s, const GFXfont* f, uint16_t c) {
  int16_t x1, y1; uint16_t w, h;
  cv.setFont(f); cv.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  txt(xr - w - x1, y, s, f, c);
}
void txtC(int cx, int y, const char* s, const GFXfont* f, uint16_t c) {
  int16_t x1, y1; uint16_t w, h;
  cv.setFont(f); cv.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  txt(cx - w / 2 - x1, y, s, f, c);
}
int txtW(const char* s, const GFXfont* f) {
  int16_t x1, y1; uint16_t w, h;
  cv.setFont(f); cv.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return w;
}
void initBg() {
  for (int y = 0; y < 128; y++) {
    int r = 4 + (5 * y) / 128, g = 38 + (26 * y) / 128, b = 28 + (19 * y) / 128;
    bgLine[y] = RGB(r, g, b);
  }
}
void clearBg() {
  for (int y = 0; y < 128; y++) { uint16_t c = bgLine[y]; uint16_t* p = &fb[y * 160]; for (int x = 0; x < 160; x++) p[x] = c; }
}
void frame(uint16_t c1, uint16_t c2) {
  cv.drawRoundRect(1, 1, 158, 126, 5, c1);
  cv.drawRoundRect(3, 3, 154, 122, 4, c2);
}
void push() { tft.drawRGBBitmap(0, 0, fb, 160, 128); }

void showSplash() {
#if SWAP_RB
  for (int i = 0; i < 160 * 128; i++) {
    uint16_t v = pgm_read_word(&SPLASH_IMG[i]);
    fb[i] = ((v & 31) << 11) | (v & 0x07E0) | (v >> 11);
  }
#else
  memcpy_P(fb, SPLASH_IMG, 160 * 128 * 2);
#endif
  push();
}

// ============================================================================
//  Day data computation
// ============================================================================
void applyTune() {
  static const int8_t map[6] = {0, -1, 1, 2, 3, 4};
  for (int i = 0; i < 6; i++) {
    int v = day.raw[i] + (map[i] >= 0 ? S.tune[map[i]] : 0);
    eff[i] = constrain(v, 0, 1439);
  }
}
bool computeLocal(int y, int m, int d, DayData& out) {
  if (S.lat == 0 && S.lng == 0) return false;
  int r[6];
  if (!calc::prayerTimes(y, m, d, S.lat, S.lng, S.tz, S.method, S.school, r)) return false;
  for (int i = 0; i < 6; i++) out.raw[i] = r[i];
  calc::Hijri h = calc::hijriFromGregorian(y, m, d, S.hadj);
  out.hd = h.d; out.hm = h.m; out.hy = h.y; out.src = 1;
  return true;
}
void ensureDay() {
  if (!timeOK) return;
  uint32_t ymd = todayYmd();
  if (dayOk && day.ymd == ymd) return;
  int y = tmNow.tm_year + 1900, m = tmNow.tm_mon + 1, d = tmNow.tm_mday;
  DayData c;
  dayOk = false;
  if (prefs.getBytesLength("day") == sizeof(c)) {
    prefs.getBytes("day", &c, sizeof(c));
    if (c.ymd == ymd && c.sig == sigOf() && c.src == 2) { day = c; dayOk = true; }
  }
  if (!dayOk) {
    memset(&c, 0, sizeof(c));
    if (computeLocal(y, m, d, c)) { c.ymd = ymd; c.sig = sigOf(); day = c; dayOk = true; }
    needFetch = true; fetchBackoff = 30000; nextFetchMs = millis();
  }
  if (dayOk) applyTune();
}

String urlEnc(const char* s) {
  String o; char b[4];
  for (; *s; s++) {
    if (isalnum((uint8_t)*s) || *s == '-' || *s == '_' || *s == '.' || *s == '~') o += *s;
    else { snprintf(b, sizeof(b), "%%%02X", (uint8_t)*s); o += b; }
  }
  return o;
}
bool parseHM(const char* s, int16_t& out) {
  int h, m;
  if (!s || sscanf(s, "%d:%d", &h, &m) != 2) return false;
  out = h * 60 + m; return true;
}

bool fetchApi() {
  if (WiFi.status() != WL_CONNECTED || !timeOK) return false;
  int y = tmNow.tm_year + 1900, m = tmNow.tm_mon + 1, d = tmNow.tm_mday;
  char date[16]; snprintf(date, sizeof(date), "%02d-%02d-%04d", d, m, y);
  String url = String("https://api.aladhan.com/v1/timingsByCity/") + date + "?city=" + urlEnc(S.city) +
               "&country=" + urlEnc(S.country) + "&method=" + S.method + "&school=" + S.school + "&adjustment=" + (int)S.hadj;
  WiFiClientSecure client;
  client.setInsecure();               // public data only (prayer times); no certificate pinning
  HTTPClient http;
  http.setTimeout(9000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) { lastMsg = "begin failed"; return false; }
  int code = http.GET();
  if (code != 200) { lastMsg = "HTTP " + String(code); http.end(); return false; }
  String body = http.getString();
  http.end();

  JsonDocument filter;
  filter["data"]["timings"] = true;
  filter["data"]["date"]["hijri"]["day"] = true;
  filter["data"]["date"]["hijri"]["year"] = true;
  filter["data"]["date"]["hijri"]["month"]["number"] = true;
  filter["data"]["meta"]["latitude"] = true;
  filter["data"]["meta"]["longitude"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) { lastMsg = String("JSON: ") + err.c_str(); return false; }
  JsonObject t = doc["data"]["timings"];
  static const char* keys[6] = {"Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha"};
  DayData n; memset(&n, 0, sizeof(n));
  for (int i = 0; i < 6; i++) if (!parseHM(t[keys[i]] | "", n.raw[i])) { lastMsg = "bad time field"; return false; }
  JsonObject hj = doc["data"]["date"]["hijri"];
  n.hd = atoi(hj["day"] | "0");
  n.hm = hj["month"]["number"] | 0;
  n.hy = atoi(hj["year"] | "0");
  if (!n.hd || !n.hm || !n.hy) {                       // fall back to the tabular calendar
    calc::Hijri h = calc::hijriFromGregorian(y, m, d, S.hadj);
    n.hd = h.d; n.hm = h.m; n.hy = h.y;
  }
  n.src = 2; n.ymd = todayYmd(); n.sig = sigOf();
  float la = doc["data"]["meta"]["latitude"] | 0.0f, lo = doc["data"]["meta"]["longitude"] | 0.0f;
  if (la != 0 || lo != 0) {
    if (fabsf(la - S.lat) > 0.01f || fabsf(lo - S.lng) > 0.01f) { S.lat = la; S.lng = lo; saveSettings(); }
  }
  day = n; dayOk = true; applyTune();
  prefs.putBytes("day", &day, sizeof(day));
  char b[40]; snprintf(b, sizeof(b), "OK %02d:%02d", tmNow.tm_hour, tmNow.tm_min);
  lastMsg = b;
  return true;
}
void fetchTick() {
  if (!needFetch || !timeOK || WiFi.status() != WL_CONNECTED) return;
  if ((int32_t)(millis() - nextFetchMs) < 0) return;
  // tell the user
  clearBg(); bmpC(LB(L_UPDATING), 80, 50, C_GOLD); push();
  if (fetchApi()) { needFetch = false; fetchBackoff = 30000; }
  else { nextFetchMs = millis() + fetchBackoff; fetchBackoff = min<uint32_t>(fetchBackoff * 2, 900000); }
}

// ============================================================================
//  Events & alerts
// ============================================================================
int findNext(int ns, bool& tomorrow, int& remainSec) {
  tomorrow = false;
  for (int i = 0; i < 6; i++) if (eff[i] * 60 > ns) { remainSec = eff[i] * 60 - ns; return i; }
  tomorrow = true; remainSec = eff[0] * 60 + 86400 - ns; return 0;
}
void startDuaPopup(int cat, uint32_t ms, bool single = false) {
  duaLo = catLo[cat]; duaHi = catHi[cat];
  if (duaHi < duaLo) return;
  duaCur = single ? duaLo + (int)(esp_random() % (duaHi - duaLo + 1)) : duaLo;
  if (single) duaLo = duaHi = duaCur;
  popup = true; popupUntil = millis() + ms; screen = S_DUAS; duaChanged = millis(); lastActivity = millis();
}
void endPopup() { popup = false; duaLo = 0; duaHi = DUA_REF_COUNT - 1; screen = S_HOME; }
void startMode(Mode m, int pr = 0) {
  mode = m; modeStart = millis(); alertPrayer = pr; lastActivity = millis(); kahfJustDone = false;
  popup = false; duaLo = 0; duaHi = DUA_REF_COUNT - 1;
}
bool kahfBannerActive() {
  if (!S.kahf || !timeOK || tmNow.tm_wday != 5 || kahfDoneYmd == todayYmd() || !dayOk) return false;
  int ns = nowSec();
  return ns >= S.kahfT * 60 && ns < eff[4] * 60;
}
bool inWin(int ns, int t, int w) { return ns >= t && ns < t + w; }
bool isQuiet(int h) {
  if (S.qfrom == S.qto) return false;
  return S.qfrom > S.qto ? (h >= S.qfrom || h < S.qto) : (h >= S.qfrom && h < S.qto);
}
void checkEvents() {
  if (!timeOK || !dayOk) return;
  int ns = nowSec();
  uint32_t base = 1 + (uint32_t)tmNow.tm_yday * 4000UL + (tmNow.tm_year % 4) * 366UL * 4000UL;   // +1: never 0 (firedKeys[] starts zeroed)
  static const uint8_t pr[5] = {0, 2, 3, 4, 5};
  for (int k = 0; k < 5; k++) {
    int i = pr[k], t = eff[i] * 60;
    if (S.adhan && inWin(ns, t, 60) && !wasFired(base + i)) {
      markFired(base + i); startMode(M_ADHAN, i); BUZZ(PAT_ADHAN, 5); return;
    }
    if (S.pre && mode == M_NORMAL && inWin(ns, t - S.pre * 60, 30) && !wasFired(base + 10 + i)) {
      markFired(base + 10 + i); startMode(M_PRE, i); BUZZ(PAT_PRE, 1); return;
    }
  }
  if (mode != M_NORMAL) return;
  bool canPopup = (screen != S_QR);
  if (canPopup && S.morn && inWin(ns, eff[0] * 60 + S.mornOff * 60, 600) && !wasFired(base + 20)) {
    markFired(base + 20); startDuaPopup(0, 120000); BUZZ(PAT_REMIND, 2); return; }
  if (canPopup && S.eve && inWin(ns, eff[3] * 60 + S.eveOff * 60, 600) && !wasFired(base + 21)) {
    markFired(base + 21); startDuaPopup(1, 120000); BUZZ(PAT_REMIND, 2); return; }
  if (canPopup && S.sleep && inWin(ns, S.sleepT * 60, 600) && !wasFired(base + 22)) {
    markFired(base + 22); startDuaPopup(3, 120000); BUZZ(PAT_REMIND, 2); return; }
  if (canPopup && S.kahf && tmNow.tm_wday == 5 && kahfDoneYmd != todayYmd()) {
    if (inWin(ns, S.kahfT * 60, 3000) && !wasFired(base + 23)) { markFired(base + 23); startMode(M_KAHF); BUZZ(PAT_REMIND, 3); return; }
    int t2 = eff[3] * 60 - 3600;
    if (t2 > S.kahfT * 60 && inWin(ns, t2, 3000) && !wasFired(base + 24)) { markFired(base + 24); startMode(M_KAHF); BUZZ(PAT_REMIND, 3); return; }
  }
  if (S.istg && screen == S_HOME && !popup && !isQuiet(tmNow.tm_hour)) {
    int nm = ns / 60;
    if (nm % S.istg == 0 && ns % 60 < 20 && !wasFired(base + 100 + nm)) {
      markFired(base + 100 + nm);
      for (int i = 0; i < 6; i++) if (abs(nm - eff[i]) < 4) return;     // not right next to a prayer
      startDuaPopup(6, 12000, true); BUZZ(PAT_SOFT, 1);
    }
  }
}

// ============================================================================
//  Screens
// ============================================================================
void drawWifiIcon(int x, int y, bool on) {
  uint16_t c = on ? C_GREEN : C_RED;
  for (int i = 0; i < 3; i++) cv.drawFastVLine(x + i * 4, y + 8 - (i + 1) * 3, (i + 1) * 3, c);
  if (!on) cv.drawLine(x - 1, y + 9, x + 11, y - 1, C_RED);
}

void drawHome() {
  clearBg();
  char b[32];
  if (!timeOK) {
    bmpC(LB(L_NO_TIME), 80, 30, C_GOLD);
    bmpC(LB(L_NO_DATA), 80, 75, C_DIM);
    drawWifiIcon(74, 108, WiFi.status() == WL_CONNECTED);
    return;
  }
  // row A: date + weekday + wifi
  snprintf(b, sizeof(b), "%02d/%02d/%04d", tmNow.tm_mday, tmNow.tm_mon + 1, tmNow.tm_year + 1900);
  txt(4, 15, b, &FreeSans9pt7b, C_CREAM);
  bmpR(LB(L_DAY0 + tmNow.tm_wday), 157, 0, C_GOLD);
  drawWifiIcon(100, 4, WiFi.status() == WL_CONNECTED);
  // row B: time
  snprintf(b, sizeof(b), "%02d:%02d", tmNow.tm_hour, tmNow.tm_min);
  txt(3, 51, b, &FreeSansBold18pt7b, C_WHITE);
  snprintf(b, sizeof(b), "%02d", tmNow.tm_sec);
  txtR(157, 51, b, &FreeSans12pt7b, C_GOLD);
  // row C: hijri date (or Kahf banner on Fridays)
  if (dayOk) {
    if (kahfBannerActive() && ((millis() / 3500) & 1)) {
      bmpC(LB(L_KAHF_BANNER), 80, 55, C_GOLD);
    } else {
      snprintf(b, sizeof(b), "%d", day.hd);
      int w1 = txtW(b, &FreeSans9pt7b);
      txtR(157, 73, b, &FreeSans9pt7b, C_CREAM);
      int hm = constrain((int)day.hm, 1, 12);
      const AlphaBmp& mb = LB(L_HM1 + hm - 1);
      int xl = 157 - w1 - 5 - mb.w;
      drawBmp(mb, xl, 55, C_GOLD);
      snprintf(b, sizeof(b), "%d", day.hy);
      txtR(xl - 5, 73, b, &FreeSans9pt7b, C_CREAM);
    }
  }
  cv.drawFastHLine(6, 83, 148, C_GOLD);
  // row D: next prayer
  if (dayOk) {
    bool tom; int rem; int ns = nowSec();
    int n = findNext(ns, tom, rem);
    int id = (n == 2 && tmNow.tm_wday == 5 && !tom) ? 6 : n;
    bmpR(LB(L_FAJR_S + id), 157, 84, C_WHITE);
    snprintf(b, sizeof(b), "%02d:%02d", eff[n] / 60, eff[n] % 60);
    txt(4, 101, b, &FreeSansBold9pt7b, C_CREAM);
    snprintf(b, sizeof(b), "%02d:%02d:%02d", rem / 3600, (rem / 60) % 60, rem % 60);
    txtC(80, 123, b, &FreeSansBold12pt7b, rem < 600 ? C_RED : C_GOLD);
  } else {
    bmpC(LB(L_NO_DATA), 80, 95, C_DIM);
  }
}

void drawTimes() {
  clearBg();
  char b[32];
  snprintf(b, sizeof(b), "%02d/%02d", tmNow.tm_mday, tmNow.tm_mon + 1);
  txt(5, 15, b, &FreeSans9pt7b, C_CREAM);
  bmpR(LB(L_T_TIMES), 157, 0, C_GOLD);
  cv.drawFastHLine(4, 22, 152, C_GOLD);
  if (!dayOk) { bmpC(LB(L_NO_DATA), 80, 70, C_DIM); return; }
  bool tom; int rem; int n = findNext(nowSec(), tom, rem);
  for (int i = 0; i < 6; i++) {
    int top = 24 + i * 17;
    bool isNext = (i == n && !tom);
    bool past = (!tom && i < n) || false;
    if (tom) past = true;
    if (isNext) {
      cv.fillRoundRect(3, top, 154, 17, 4, C_HI);
      cv.fillTriangle(8, top + 4, 8, top + 12, 14, top + 8, C_GOLD);
    }
    uint16_t c = isNext ? C_GOLD : (past ? C_DIM : C_WHITE);
    snprintf(b, sizeof(b), "%02d:%02d", eff[i] / 60, eff[i] % 60);
    txt(20, top + 14, b, &FreeSansBold9pt7b, c);
    int id = (i == 2 && tmNow.tm_wday == 5) ? 6 : i;
    const AlphaBmp& nb = LB(L_FAJR_S + id);
    drawBmp(nb, 154 - nb.w, top + (17 - nb.h) / 2, c);
  }
}

void drawDuas() {
  clearBg();
  if (DUA_REF_COUNT == 0) return;
  duaCur = constrain(duaCur, duaLo, duaHi);
  const DuaRef& r = DUA_REFS[duaCur];
  bmpR(CAT_TITLES[r.cat], 157, 0, C_GOLD);
  char b[32]; snprintf(b, sizeof(b), "%d/%d", duaCur - duaLo + 1, duaHi - duaLo + 1);
  cv.setFont(NULL); cv.setTextSize(1); cv.setTextColor(C_DIM); cv.setCursor(4, 8); cv.print(b);
  cv.drawFastHLine(4, 24, 152, C_GOLD);
  const AlphaBmp& p = DUA_PAGES[r.page];
  int y = 26 + (102 - p.h) / 2;
  drawBmp(p, (160 - p.w) / 2, max(26, y), C_WHITE);
}

String wifiQrText() {
  String p;
  for (const char* c = S.apPass; *c; c++) { if (strchr("\\;,:\"", *c)) p += '\\'; p += *c; }
  return String("WIFI:T:WPA;S:") + AP_SSID + ";P:" + p + ";;";
}
void drawQr() {
  clearBg();
  bool sta = WiFi.status() == WL_CONNECTED;
  int steps = sta ? 3 : 2;
  if ((millis() - qrChanged) > 7000) { qrStep++; qrChanged = millis(); }
  qrStep %= steps;
  String txtq, cap;
  if (qrStep == 0) { txtq = wifiQrText(); cap = String(AP_SSID) + " / " + S.apPass; }
  else if (qrStep == 1) { txtq = "http://192.168.4.1/"; cap = "http://192.168.4.1"; }
  else { txtq = "http://" + WiFi.localIP().toString() + "/"; cap = txtq; }
  bool qrOk = qrcodegen_encodeText(txtq.c_str(), qrTmp, qrData, qrcodegen_Ecc_LOW, 3, 3, qrcodegen_Mask_AUTO, false);
  bmpC(LB(qrStep == 0 ? L_QR1 : L_QR2), 80, 0, C_GOLD);
  const int px = 3, n = qrOk ? qrcodegen_getSize(qrData) : 29, box = n * px + 2 * px;
  int x0 = (160 - box) / 2, y0 = 24;
  cv.fillRect(x0, y0, box, box, C_WHITE);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (qrOk && qrcodegen_getModule(qrData, x, y)) cv.fillRect(x0 + px + x * px, y0 + px + y * px, px, px, C_BLACK);
  if (cap.length() > 26) cap = cap.substring(0, 26);
  cv.setFont(NULL); cv.setTextSize(1); cv.setTextColor(C_CREAM);
  cv.setCursor((160 - cap.length() * 6) / 2, 119); cv.print(cap);
}

void drawAlert() {
  clearBg();
  bool blink = (millis() / 500) & 1;
  frame(blink ? C_GOLD : C_RED, C_DIM);
  char b[16];
  if (mode == M_ADHAN || mode == M_PRE) {
    int id = (alertPrayer == 2 && tmNow.tm_wday == 5) ? 6 : alertPrayer;
    bmpC(LB(mode == M_ADHAN ? L_NOW_TIME : L_SOON), 80, 8, C_GOLD);
    bmpC(LB(L_FAJR_L + id), 80, 38, C_WHITE);
    if (mode == M_ADHAN) {
      snprintf(b, sizeof(b), "%02d:%02d", tmNow.tm_hour, tmNow.tm_min);
      txtC(80, 112, b, &FreeSansBold18pt7b, C_CREAM);
    } else {
      int rem = eff[alertPrayer] * 60 - nowSec(); if (rem < 0) rem = 0;
      snprintf(b, sizeof(b), "%02d:%02d", rem / 60, rem % 60);
      txtC(80, 112, b, &FreeSansBold18pt7b, C_CREAM);
    }
  } else {   // Kahf
    if (kahfJustDone) { bmpC(LB(L_DONE), 80, 50, C_GOLD); return; }
    bmpC(LB(L_KAHF_T1), 80, 10, C_GOLD);
    bmpC(LB(L_KAHF_T2), 80, 42, C_WHITE);
    bmpC(LB(L_KAHF_T3), 80, 72, C_CREAM);
    bmpC(LB(L_LONG_HINT), 80, 102, C_DIM);
  }
}

uint32_t lastDraw = 0;
void render() {
  if (mode != M_NORMAL) drawAlert();
  else switch (screen) {
    case S_HOME: drawHome(); break;
    case S_TIMES: drawTimes(); break;
    case S_DUAS: drawDuas(); break;
    case S_QR: drawQr(); break;
    default: break;
  }
  push();
}

// ============================================================================
//  Touch handling
// ============================================================================
void gotoNextCategory() {
  int cur = DUA_REFS[constrain(duaCur, 0, DUA_REF_COUNT - 1)].cat;
  for (int i = duaCur + 1; i < DUA_REF_COUNT; i++) if (DUA_REFS[i].cat != cur) { duaCur = i; return; }
  duaCur = 0;
}
void endMode() { buzzStop(); mode = M_NORMAL; screen = S_HOME; }

void onPress() {
  tDown = millis(); tTier = 0; tConsumed = false; lastActivity = millis();
  if (mode == M_ADHAN) {                 // silence the adhan immediately, then show the after-adhan dua
    buzzStop(); mode = M_NORMAL; startDuaPopup(7, 45000); tConsumed = true;
  }
}
void onRelease(uint32_t held) {
  lastActivity = millis();
  if (tConsumed) return;
  if (mode == M_KAHF) {
    if (held >= LONG_PRESS_MS) { kahfDoneYmd = todayYmd(); prefs.putUInt("kahf", kahfDoneYmd); kahfJustDone = true; modeStart = millis() - 22000; BUZZ(PAT_OK, 1); }
    else endMode();
    return;
  }
  if (mode != M_NORMAL) { endMode(); return; }
  if (held >= XLONG_PRESS_MS && screen == S_QR) {
    buzzStart(PAT_TEST, 3, 1, true); prefs.clear(); rebootPending = true; rebootAt = millis() + 700; return;
  }
  if (held >= VLONG_PRESS_MS) {
    if (screen == S_DUAS) { gotoNextCategory(); duaChanged = millis(); }
    return;
  }
  if (held >= LONG_PRESS_MS) {
    if (screen == S_DUAS) { duaCur = (duaCur >= duaHi) ? duaLo : duaCur + 1; duaChanged = millis(); }
    else if (screen == S_QR) { qrStep++; qrChanged = millis(); }
    else if (screen == S_HOME && kahfBannerActive()) { kahfDoneYmd = todayYmd(); prefs.putUInt("kahf", kahfDoneYmd); BUZZ(PAT_OK, 1); }
    return;
  }
  // short tap
  if (popup) { endPopup(); return; }
  screen = (Screen)((screen + 1) % S_COUNT);
  if (screen == S_QR) { qrStep = 0; qrChanged = millis(); }
  if (screen == S_DUAS) duaChanged = millis();
  BUZZ(PAT_TICK, 1);
}
void handleTouch() {
  bool raw = digitalRead(TOUCH_PIN) == (TOUCH_ACTIVE_HIGH ? HIGH : LOW);
  uint32_t now = millis();
  if (raw != tRaw) { tRaw = raw; tChange = now; }
  if (now - tChange > 30 && raw != tStable) {
    tStable = raw;
    if (raw) onPress(); else onRelease(now - tDown);
  }
  if (tStable && !tConsumed) {              // audible feedback while holding
    uint32_t held = now - tDown;
    if (tTier == 0 && held >= LONG_PRESS_MS)  { tTier = 1; BUZZ(PAT_TICK, 1); }
    if (tTier == 1 && held >= VLONG_PRESS_MS) { tTier = 2; BUZZ(PAT_SOFT, 1); }
    if (tTier == 2 && held >= XLONG_PRESS_MS && screen == S_QR) { tTier = 3; BUZZ(PAT_TEST, 1); }
  }
}

// ============================================================================
//  Housekeeping per loop
// ============================================================================
void uiTimers() {
  uint32_t now = millis();
  if (mode == M_ADHAN && now - modeStart > 90000) endMode();
  if (mode == M_PRE && now - modeStart > 12000) endMode();
  if (mode == M_KAHF && now - modeStart > 25000) endMode();
  if (mode == M_NORMAL) {
    if (popup && (int32_t)(now - popupUntil) > 0) endPopup();
    if (!popup && screen == S_DUAS && now - duaChanged > DUA_SLIDE_MS) { duaCur = (duaCur >= duaHi) ? duaLo : duaCur + 1; duaChanged = now; }
    uint32_t idle = now - lastActivity;
    if (!popup && ((screen == S_TIMES && idle > 25000) || (screen == S_DUAS && idle > 90000) || (screen == S_QR && idle > 300000))) screen = S_HOME;
  }
}

void wifiTick() {
  bool up = WiFi.status() == WL_CONNECTED;
  if (up && !staWasUp) {
    startNtp();
    if (!dayOk || day.src != 2) { needFetch = true; nextFetchMs = millis(); }
  }
  staWasUp = up;
  if (wifiRestart && (int32_t)(millis() - wifiRestartAt) > 0) {
    wifiRestart = false;
    WiFi.disconnect(false);
    if (S.ssid[0]) WiFi.begin(S.ssid, S.pass);
  }
  if (rebootPending && (int32_t)(millis() - rebootAt) > 0) ESP.restart();
}

// ============================================================================
//  Web server
// ============================================================================
void fmtHM(char* o, int m) { sprintf(o, "%02d:%02d", m / 60, m % 60); }
void apiGet() {
  JsonDocument d;
  d["ssid"] = S.ssid; d["apPass"] = S.apPass; d["city"] = S.city; d["country"] = S.country;
  d["method"] = S.method; d["school"] = S.school; d["tz"] = S.tz; d["hadj"] = S.hadj;
  d["lat"] = S.lat; d["lng"] = S.lng;
  for (int i = 0; i < 5; i++) d[String("t") + i] = S.tune[i];
  d["beep"] = (bool)S.beep; d["adhan"] = (bool)S.adhan; d["pre"] = S.pre; d["istg"] = S.istg;
  d["qfrom"] = S.qfrom; d["qto"] = S.qto;
  d["morn"] = (bool)S.morn; d["mornOff"] = S.mornOff; d["eve"] = (bool)S.eve; d["eveOff"] = S.eveOff;
  d["sleep"] = (bool)S.sleep; d["kahf"] = (bool)S.kahf;
  char b[8]; fmtHM(b, S.sleepT); d["sleepT"] = b; fmtHM(b, S.kahfT); d["kahfT"] = b;
  String o; serializeJson(d, o);
  server.send(200, "application/json", o);
}
void apiSet() {
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) { server.send(400, "text/plain", "bad json"); return; }
  String oldSsid = S.ssid, oldAp = S.apPass;
  auto cp = [&](char* dst, size_t n, const char* key) { const char* v = d[key] | (const char*)nullptr; if (v) { strlcpy(dst, v, n); } };
  cp(S.ssid, sizeof(S.ssid), "ssid");
  const char* pw = d["pass"] | ""; if (*pw) strlcpy(S.pass, pw, sizeof(S.pass));
  const char* ap = d["apPass"] | ""; if (strlen(ap) >= 8) strlcpy(S.apPass, ap, sizeof(S.apPass));
  cp(S.city, sizeof(S.city), "city"); cp(S.country, sizeof(S.country), "country");
  S.method = d["method"] | S.method; S.school = d["school"] | S.school; S.hadj = d["hadj"] | S.hadj;
  S.tz = d["tz"] | S.tz; S.lat = d["lat"] | S.lat; S.lng = d["lng"] | S.lng;
  for (int i = 0; i < 5; i++) S.tune[i] = constrain((int)(d[String("t") + i] | (int)S.tune[i]), -30, 30);
  S.beep = d["beep"] | (bool)S.beep; S.adhan = d["adhan"] | (bool)S.adhan;
  S.pre = constrain((int)(d["pre"] | (int)S.pre), 0, 60);
  S.istg = constrain((int)(d["istg"] | (int)S.istg), 0, 720);
  S.qfrom = constrain((int)(d["qfrom"] | (int)S.qfrom), 0, 23); S.qto = constrain((int)(d["qto"] | (int)S.qto), 0, 23);
  S.morn = d["morn"] | (bool)S.morn; S.eve = d["eve"] | (bool)S.eve; S.sleep = d["sleep"] | (bool)S.sleep; S.kahf = d["kahf"] | (bool)S.kahf;
  S.mornOff = constrain((int)(d["mornOff"] | (int)S.mornOff), 0, 240); S.eveOff = constrain((int)(d["eveOff"] | (int)S.eveOff), 0, 240);
  int16_t t;
  if (parseHM(d["sleepT"] | "", t)) S.sleepT = t;
  if (parseHM(d["kahfT"] | "", t)) S.kahfT = t;
  saveSettings();
  server.send(200, "text/plain", "ok");
  // apply
  startNtp();
  dayOk = false; day.ymd = 0; ensureDay(); needFetch = true; nextFetchMs = millis(); fetchBackoff = 30000;
  if (oldSsid != S.ssid || *pw) { wifiRestart = true; wifiRestartAt = millis() + 800; }
  if (oldAp != S.apPass) { rebootPending = true; rebootAt = millis() + 1200; }
}
void apiStatus() {
  JsonDocument d;
  char b[16];
  if (timeOK) { sprintf(b, "%02d:%02d:%02d", tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec); d["time"] = b; } else d["time"] = "--:--";
  bool up = WiFi.status() == WL_CONNECTED;
  d["wifi"] = up; d["ip"] = up ? WiFi.localIP().toString() : String("");
  d["src"] = !dayOk ? "لا يوجد" : (day.src == 2 ? "الانترنت (Aladhan)" : "حساب محلي");
  d["last"] = String("آخر محاولة: ") + lastMsg;
  String o; serializeJson(d, o);
  server.send(200, "application/json", o);
}
void apiScan() {
  int n = WiFi.scanNetworks();
  JsonDocument d; JsonArray a = d.to<JsonArray>();
  for (int i = 0; i < n; i++) { String s = WiFi.SSID(i); if (s.length()) a.add(s); }
  WiFi.scanDelete();
  String o; serializeJson(d, o);
  server.send(200, "application/json", o);
}
void setupWeb() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html; charset=utf-8", PAGE_HTML); });
  server.on("/api/get", HTTP_GET, apiGet);
  server.on("/api/set", HTTP_POST, apiSet);
  server.on("/api/status", HTTP_GET, apiStatus);
  server.on("/api/scan", HTTP_GET, apiScan);
  server.on("/api/time", HTTP_POST, []() {
    long e = server.arg("e").toInt();
    if (e > 1700000000L) { struct timeval tv = {(time_t)e, 0}; settimeofday(&tv, nullptr); }
    server.send(200, "text/plain", "ok");
  });
  server.on("/api/test", HTTP_POST, []() { buzzStart(PAT_TEST, sizeof(PAT_TEST) / sizeof(Note), 2, true); server.send(200, "text/plain", "ok"); });
  server.on("/api/fetch", HTTP_POST, []() { needFetch = true; nextFetchMs = millis(); fetchBackoff = 30000; server.send(200, "text/plain", "ok"); });
  server.on("/api/reboot", HTTP_POST, []() { server.send(200, "text/plain", "ok"); rebootPending = true; rebootAt = millis() + 600; });
  server.on("/api/reset", HTTP_POST, []() { prefs.clear(); server.send(200, "text/plain", "ok"); rebootPending = true; rebootAt = millis() + 600; });
  server.onNotFound([]() { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });
  server.begin();
}

// ============================================================================
//  Setup / loop
// ============================================================================
void buildCatRanges() {
  for (int c = 0; c < CAT_COUNT; c++) { catLo[c] = 1 << 20; catHi[c] = -1; }
  for (int i = 0; i < DUA_REF_COUNT; i++) {
    int c = DUA_REFS[i].cat;
    if (i < catLo[c]) catLo[c] = i;
    if (i > catHi[c]) catHi[c] = i;
  }
  duaLo = 0; duaHi = DUA_REF_COUNT - 1;
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("Adhan Clock v1 - created by El-Amine (MedMA07) - github.com/MedMA07"));
  pinMode(TOUCH_PIN, TOUCH_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
  pinMode(BUZZ_PIN, OUTPUT); digitalWrite(BUZZ_PIN, LOW);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BUZZ_PIN, 2000, 8);
#else
  ledcSetup(0, 2000, 8); ledcAttachPin(BUZZ_PIN, 0);
#endif
  buzzTone(0);
#if BL_PIN >= 0
  pinMode(BL_PIN, OUTPUT); digitalWrite(BL_PIN, BL_INVERT ? LOW : HIGH);
#endif
  fb = cv.getBuffer();
  initBg();
  tft.initR(TAB_TYPE);
  tft.setSPISpeed(SPI_HZ);
  tft.setRotation(ROTATION);
  tft.invertDisplay(TFT_INVERT);
  tft.fillScreen(ST77XX_BLACK);
  showSplash();
  uint32_t t0 = millis();

  prefs.begin("adhan", false);
  loadSettings();
  kahfDoneYmd = prefs.getUInt("kahf", 0);
  buildCatRanges();
  BUZZ(PAT_BOOT, 1);

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname(MDNS_NAME);
  WiFi.softAP(AP_SSID, S.apPass);
  if (S.ssid[0]) WiFi.begin(S.ssid, S.pass);
  WiFi.setAutoReconnect(true);
  startNtp();
  MDNS.begin(MDNS_NAME);
  setupWeb();

  while (millis() - t0 < SPLASH_MS) {            // keep serving while the logo is displayed
    server.handleClient(); buzzTick();
    if (digitalRead(TOUCH_PIN) == (TOUCH_ACTIVE_HIGH ? HIGH : LOW) && millis() - t0 > 800) break;
    delay(5);
  }
  lastActivity = millis(); qrChanged = millis();
}

void loop() {
  server.handleClient();
  buzzTick();
  handleTouch();
  wifiTick();
  static uint32_t lastSec = 0;
  uint32_t now = millis();
  if (now - lastSec >= 250) {
    lastSec = now;
    updateTime();
    ensureDay();
    checkEvents();
    fetchTick();
    uiTimers();
  }
  if (now - lastDraw >= 100) { lastDraw = now; render(); }
}
