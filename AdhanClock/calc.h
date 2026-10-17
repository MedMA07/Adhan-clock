#pragma once
// Pure C++ date / Hijri / prayer-time maths (no Arduino dependency -> testable on a PC).
#include <math.h>
#include <stdint.h>

namespace calc {

// ---- Gregorian <-> day number ------------------------------------------------
inline long daysFromCivil(int y, int m, int d) {           // days since 1970-01-01
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

// ---- Tabular Hijri calendar (fallback only; API value is preferred) -----------
struct Hijri { int d, m, y; };
inline Hijri hijriFromGregorian(int y, int m, int d, int adjustDays = 0) {
  long jdn = daysFromCivil(y, m, d) + 2440588L + adjustDays;
  long l = jdn - 1948440L + 10632L;
  long n = (l - 1) / 10631L;
  l = l - 10631L * n + 354L;
  long j = ((10985L - l) / 5316L) * ((50L * l) / 17719L) + (l / 5670L) * ((43L * l) / 15238L);
  l = l - ((30L - j) / 15L) * ((17719L * j) / 50L) - (j / 16L) * ((15238L * j) / 43L) + 29L;
  long mm = (24L * l) / 709L;
  long dd = l - (709L * mm) / 24L;
  long yy = 30L * n + j - 30L;
  return {(int)dd, (int)mm, (int)yy};
}

// ---- Prayer times (praytimes.org algorithm) ------------------------------------
struct Method { int id; float fajr; float isha; int ishaMin; };   // ishaMin>0 => minutes after maghrib
static const Method METHODS[] = {
  {19, 18.0f, 17.0f, 0},   // Algeria
  {21, 19.0f, 17.0f, 0},   // Morocco
  {18, 18.0f, 18.0f, 0},   // Tunisia
  {5,  19.5f, 17.5f, 0},   // Egypt
  {4,  18.5f, 0,    90},   // Umm al-Qura
  {3,  18.0f, 17.0f, 0},   // MWL
  {2,  15.0f, 15.0f, 0},   // ISNA
  {12, 12.0f, 12.0f, 0},   // France UOIF
  {1,  18.0f, 18.0f, 0},   // Karachi
  {13, 18.0f, 17.0f, 0},   // Diyanet
  {8,  19.5f, 0,    90},   // Gulf
  {9,  18.0f, 17.5f, 0},   // Kuwait
  {10, 18.0f, 0,    90},   // Qatar
  {16, 18.2f, 18.2f, 0},   // Dubai
  {17, 20.0f, 18.0f, 0},   // JAKIM
  {11, 20.0f, 18.0f, 0},   // Singapore
};
inline const Method& methodById(int id) {
  for (auto& m : METHODS) if (m.id == id) return m;
  return METHODS[0];
}

namespace detail {
const double PI_ = 3.14159265358979323846;
inline double dr(double d) { return d * PI_ / 180.0; }
inline double rd(double r) { return r * 180.0 / PI_; }
inline double fixAngle(double a) { a = a - 360.0 * floor(a / 360.0); return a; }
inline double fixHour(double a) { return a - 24.0 * floor(a / 24.0); }
inline double julian(int y, int m, int d) {
  if (m <= 2) { y -= 1; m += 12; }
  double A = floor(y / 100.0), B = 2 - A + floor(A / 4.0);
  return floor(365.25 * (y + 4716)) + floor(30.6001 * (m + 1)) + d + B - 1524.5;
}
inline void sunPos(double jd, double& decl, double& eqt) {
  double D = jd - 2451545.0;
  double g = fixAngle(357.529 + 0.98560028 * D);
  double q = fixAngle(280.459 + 0.98564736 * D);
  double L = fixAngle(q + 1.915 * sin(dr(g)) + 0.020 * sin(dr(2 * g)));
  double e = 23.439 - 0.00000036 * D;
  double RA = rd(atan2(cos(dr(e)) * sin(dr(L)), cos(dr(L)))) / 15.0;
  eqt = q / 15.0 - fixHour(RA);
  decl = rd(asin(sin(dr(e)) * sin(dr(L))));
}
inline double midDay(double jd, double t) { double d, e; sunPos(jd + t, d, e); return fixHour(12 - e); }
inline double angleTime(double jd, double lat, double angle, double t, bool ccw) {
  double d, e; sunPos(jd + t, d, e);
  double v = rd(acos((-sin(dr(angle)) - sin(dr(d)) * sin(dr(lat))) / (cos(dr(d)) * cos(dr(lat))))) / 15.0;
  return midDay(jd, t) + (ccw ? -v : v);
}
inline double asrTime(double jd, double lat, double factor, double t) {
  double d, e; sunPos(jd + t, d, e);
  double ang = -rd(atan(1.0 / (factor + tan(dr(fabs(lat - d))))));
  return angleTime(jd, lat, ang, t, false);
}
}  // namespace detail

// out[6] = minutes since local midnight: Fajr, Sunrise, Dhuhr, Asr, Maghrib, Isha. Returns false on failure (polar).
inline bool prayerTimes(int y, int m, int d, double lat, double lng, double tz, int methodId, int asrSchool, int out[6]) {
  using namespace detail;
  const Method& M = methodById(methodId);
  double jd = julian(y, m, d) - lng / (15.0 * 24.0);
  double fajr = angleTime(jd, lat, M.fajr, 5.0 / 24, true);
  double sunrise = angleTime(jd, lat, 0.833, 5.0 / 24, true);
  double dhuhr = midDay(jd, 12.0 / 24);
  double asr = asrTime(jd, lat, asrSchool ? 2 : 1, 13.0 / 24);
  double maghrib = angleTime(jd, lat, 0.833, 18.0 / 24, false);
  double isha = M.ishaMin ? 0 : angleTime(jd, lat, M.isha, 18.0 / 24, false);
  double adj = tz - lng / 15.0;
  double t[6] = {fajr + adj, sunrise + adj, dhuhr + adj + 1.0 / 60, asr + adj, maghrib + adj + 1.0 / 60,
                 (M.ishaMin ? maghrib + adj + 1.0 / 60 + M.ishaMin / 60.0 : isha + adj)};
  for (int i = 0; i < 6; i++) {
    if (isnan(t[i])) return false;
    int mins = (int)floor(fixHour(t[i]) * 60.0 + 0.5);
    out[i] = mins % 1440;
  }
  return true;
}

}  // namespace calc
