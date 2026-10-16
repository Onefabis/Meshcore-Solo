#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Avoid printf's floating-point conversion path. On the nRF52840 that path
// allocates from newlib's heap and can abort with "Balloc succeeded" when the
// heap is fragmented. Values are rounded to an integer scale first.
namespace fixedfmt {

inline void formatScaled(char* out, int n, long scaled, int decimals,
                         const char* suffix = nullptr) {
  if (n <= 0) return;
  if (decimals < 0) decimals = 0;
  long divisor = 1;
  for (int i = 0; i < decimals; ++i) divisor *= 10;
  const bool negative = scaled < 0;
  const unsigned long magnitude = (unsigned long)(negative ? -scaled : scaled);
  const unsigned long whole = magnitude / (unsigned long)divisor;
  const unsigned long frac = magnitude % (unsigned long)divisor;
  int used = 0;
  if (negative && used < n - 1) out[used++] = '-';
  used += snprintf(out + used, n - used, "%lu", whole);
  if (decimals && used < n - 1) {
    used += snprintf(out + used, n - used, ".%0*lu", decimals, frac);
  }
  if (suffix && used < n - 1) snprintf(out + used, n - used, "%s", suffix);
  out[n - 1] = '\0';
}

inline void format(char* out, int n, float value, int decimals,
                   const char* suffix = nullptr) {
  long scale = 1;
  for (int i = 0; i < decimals; ++i) scale *= 10;
  formatScaled(out, n, lroundf(value * (float)scale), decimals, suffix);
}

inline void format(char* out, int n, double value, int decimals,
                   const char* suffix = nullptr) {
  long scale = 1;
  for (int i = 0; i < decimals; ++i) scale *= 10;
  formatScaled(out, n, lround(value * (double)scale), decimals, suffix);
}

inline void formatE6(char* out, int n, int32_t value, int decimals = 5,
                     const char* suffix = nullptr) {
  if (decimals > 6) decimals = 6;
  const long divisor = decimals == 6 ? 1L : decimals == 5 ? 10L : 100L;
  formatScaled(out, n, (long)value / divisor, decimals, suffix);
}

} // namespace fixedfmt
