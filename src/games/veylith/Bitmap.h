#pragma once
#include <stdint.h>
namespace veylith {
// Mirror MSB-first art directly into the caller's framebuffer, one horizontal
// run at a time. No copied sprite, allocation, or additional framebuffer.
template <class Emit> void mirrorRuns(const uint8_t *bits, unsigned width, unsigned height, Emit emit) {
  unsigned stride = (width + 7) / 8;
  for (unsigned y = 0; y < height; ++y) {
    unsigned start = 0, length = 0;
    for (unsigned x = 0; x < width; ++x) {
      unsigned source = width - 1 - x;
      bool lit = (bits[y * stride + source / 8] & (0x80U >> (source % 8))) != 0;
      if (lit) {
        if (!length) start = x;
        ++length;
      } else if (length) {
        emit(start, y, length);
        length = 0;
      }
    }
    if (length) emit(start, y, length);
  }
}
} // namespace veylith
