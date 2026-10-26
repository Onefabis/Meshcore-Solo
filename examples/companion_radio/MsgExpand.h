#pragma once
#include <cstring>
#include <cstdio>
#include <ctime>
#include <cstdint>
#include <helpers/SensorManager.h>
#include "LocalSensors.h"
#include "NodePrefs.h"   // localTm()
#include <helpers/sensors/LPPDataHelpers.h>
#include "ui-core/FixedFormat.h"

// Expands placeholders in tmpl into out (out_len bytes).
//   {loc}   — GPS coordinates (lat/lon) or "no GPS"
//   {time}  — local time HH:MM from RTC
//   {temp}  — temperature in °C        (requires sm)
//   {hum}   — relative humidity %      (requires sm)
//   {pres}  — barometric pressure hPa  (requires sm)
//   {batt}  — battery voltage V        (batt_volts >= 0 or LPP_VOLTAGE from sm)
//   {alt}   — altitude m               (requires sm)
//   {lux}   — luminosity lux           (requires sm)
//   {dist}  — distance m               (requires sm)
//   {co2}   — CO2 concentration ppm    (requires sm)
//   {name}  — sender's name (bot replies only; requires sender_name, else left literal)
//   {hops}  — hop count: "direct" or "N hops" (bot replies only; requires hops>=0, else left literal)
inline void expandMsg(const char* tmpl, char* out, int out_len,
                      double lat, double lon, bool gps_valid,
                      uint32_t utc_ts, int8_t tz_hours,
                      SensorManager* sm = nullptr,
                      float batt_volts  = -1.0f,
                      const char* sender_name = nullptr,
                      int hops = -1,
                      float mcu_temp = NAN) {   // the chip's, for {temp} without a sensor
  // sv indices: 0=temp 1=hum 2=pres 3=batt 4=alt 5=lux 6=dist 7=co2
  float sv[8]    = {};
  bool  sv_ok[8] = {};

  if (sm) {
    CayenneLPP lpp(100);
    queryLocalSensors(*sm, mcu_temp, lpp);
    LPPReader r(lpp.getBuffer(), lpp.getSize());
    uint8_t ch, type;
    while (r.readHeader(ch, type)) {
      float tmp;
      switch (type) {
        case LPP_TEMPERATURE:
          r.readTemperature(tmp);
          if (!sv_ok[0]) { sv[0] = tmp; sv_ok[0] = true; } break;
        case LPP_RELATIVE_HUMIDITY:
          r.readRelativeHumidity(tmp);
          if (!sv_ok[1]) { sv[1] = tmp; sv_ok[1] = true; } break;
        case LPP_BAROMETRIC_PRESSURE:
          r.readPressure(tmp);
          if (!sv_ok[2]) { sv[2] = tmp; sv_ok[2] = true; } break;
        case LPP_VOLTAGE:
          r.readVoltage(tmp);
          if (!sv_ok[3]) { sv[3] = tmp; sv_ok[3] = true; } break;
        case LPP_ALTITUDE:
          r.readAltitude(tmp);
          if (!sv_ok[4]) { sv[4] = tmp; sv_ok[4] = true; } break;
        case LPP_LUMINOSITY:
          r.readLuminosity(tmp);
          if (!sv_ok[5]) { sv[5] = tmp; sv_ok[5] = true; } break;
        case LPP_DISTANCE:
          r.readDistance(tmp);
          if (!sv_ok[6]) { sv[6] = tmp; sv_ok[6] = true; } break;
        case LPP_CONCENTRATION:
          r.readConcentration(tmp);
          if (!sv_ok[7]) { sv[7] = tmp; sv_ok[7] = true; } break;
        default:
          r.skipData(type); break;
      }
    }
  }
  // board battery takes precedence over INA sensor voltage
  if (batt_volts >= 0.0f) { sv[3] = batt_volts; sv_ok[3] = true; }

  int oi = 0;
  const char* p = tmpl;
  while (*p && oi < out_len - 1) {
    // helper macro: append a buffer whose length is already known
    #define APPEND(s, slen) do { \
      int _l = (slen); \
      if (oi + _l < out_len) { memcpy(out + oi, (s), _l); oi += _l; } \
    } while(0)

    if (strncmp(p, "{loc}", 5) == 0) {
      char lb[32];
      if (gps_valid) { fixedfmt::format(lb, sizeof(lb), lat, 5); size_t z = strlen(lb); lb[z++] = ','; lb[z] = '\0'; char c[20]; fixedfmt::format(c, sizeof(c), lon, 5); strncat(lb, c, sizeof(lb) - strlen(lb) - 1); }
      else           strcpy(lb, "no GPS");
      APPEND(lb, strlen(lb)); p += 5;
    } else if (strncmp(p, "{time}", 6) == 0) {
      struct tm ti;
      if (localTm(utc_ts, tz_hours, ti)) {
        char tb[8];
        snprintf(tb, sizeof(tb), "%02d:%02d", ti.tm_hour, ti.tm_min);
        APPEND(tb, strlen(tb));
      }
      p += 6;
    } else if (strncmp(p, "{temp}", 6) == 0) {
      if (sv_ok[0]) { char b[10]; fixedfmt::format(b,sizeof(b),sv[0],1,"C");   APPEND(b,strlen(b)); }
      else           { APPEND("{temp}", 6); }
      p += 6;
    } else if (strncmp(p, "{hum}", 5) == 0) {
      if (sv_ok[1]) { char b[8];  fixedfmt::format(b,sizeof(b),sv[1],0,"%");  APPEND(b,strlen(b)); }
      else           { APPEND("{hum}", 5); }
      p += 5;
    } else if (strncmp(p, "{pres}", 6) == 0) {
      if (sv_ok[2]) { char b[12]; fixedfmt::format(b,sizeof(b),sv[2],0,"hPa"); APPEND(b,strlen(b)); }
      else           { APPEND("{pres}", 6); }
      p += 6;
    } else if (strncmp(p, "{batt}", 6) == 0) {
      if (sv_ok[3]) { char b[8];  fixedfmt::format(b,sizeof(b),sv[3],2,"V");   APPEND(b,strlen(b)); }
      else           { APPEND("{batt}", 6); }
      p += 6;
    } else if (strncmp(p, "{alt}", 5) == 0) {
      if (sv_ok[4]) { char b[10]; fixedfmt::format(b,sizeof(b),sv[4],0,"m");   APPEND(b,strlen(b)); }
      else           { APPEND("{alt}", 5); }
      p += 5;
    } else if (strncmp(p, "{lux}", 5) == 0) {
      if (sv_ok[5]) { char b[10]; fixedfmt::format(b,sizeof(b),sv[5],0,"lux"); APPEND(b,strlen(b)); }
      else           { APPEND("{lux}", 5); }
      p += 5;
    } else if (strncmp(p, "{dist}", 6) == 0) {
      if (sv_ok[6]) { char b[12]; fixedfmt::format(b,sizeof(b),sv[6],2,"m");   APPEND(b,strlen(b)); }
      else           { APPEND("{dist}", 6); }
      p += 6;
    } else if (strncmp(p, "{co2}", 5) == 0) {
      if (sv_ok[7]) { char b[12]; fixedfmt::format(b,sizeof(b),sv[7],0,"ppm"); APPEND(b,strlen(b)); }
      else           { APPEND("{co2}", 5); }
      p += 5;
    } else if (strncmp(p, "{name}", 6) == 0) {
      if (sender_name && sender_name[0]) { APPEND(sender_name, strlen(sender_name)); }
      else                                { APPEND("{name}", 6); }
      p += 6;
    } else if (strncmp(p, "{hops}", 6) == 0) {
      if (hops >= 0) {
        char b[10];
        if (hops == 0) strcpy(b, "direct");
        else           snprintf(b, sizeof(b), "%u hops", (unsigned)hops);
        APPEND(b, strlen(b));
      } else { APPEND("{hops}", 6); }
      p += 6;
    } else {
      out[oi++] = *p++;
    }

    #undef APPEND
  }
  out[oi] = '\0';
}
