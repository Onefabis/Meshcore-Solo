#pragma once
#include "Content.h"

#include <stddef.h>
#include <string.h>
namespace veylith {
struct Training {
  uint32_t wallet = 0, lifetime = 0;
  uint8_t rank[6] = {}, equipped[4] = {}, flags = 0;
  unsigned total() const {
    unsigned n = 0;
    for (auto r : rank)
      n += r;
    return n;
  }
  unsigned level() const {
    unsigned n = 1 + total() / 3;
    return n > 99 ? 99 : n;
  }
  unsigned stage() const { return level() >= 35 ? 2 : level() >= 15 ? 1 : 0; }
  unsigned cost(unsigned stat) const { return stat < 6 ? 30 + 4 * rank[stat] + 2 * (level() - 1) : 0; }
  unsigned value(unsigned family, unsigned stat) const {
    if (family >= 20 || stat >= 6) return 0;
    static const uint8_t bonus[3][6] = { { 0, 0, 0, 0, 0, 0 }, { 14, 4, 4, 4, 4, 3 }, { 30, 9, 9, 9, 9, 7 } };
    return families[family].base[stat] + rank[stat] * (stat == HP ? 3 : 1) + bonus[stage()][stat];
  }
  bool eligible(unsigned family, unsigned id, bool unlocked = true) const {
    if (family >= 20 || id < 1 || id > 80) return false;
    const auto &m = move(id);
    if (m.element != UNIVERSAL && m.element != families[family].primary &&
        m.element != families[family].secondary)
      return false;
    if (!unlocked) return true; // Affiliated locked previews only; never valid for equipping.
    if (m.level > level()) return false;
    for (unsigned i = 0; i < 2; ++i)
      if (m.requiredValue[i] && value(family, m.requiredStat[i]) < m.requiredValue[i]) return false;
    return true;
  }
  bool equip(unsigned family, unsigned slot, unsigned id) {
    if (slot >= 4 || !eligible(family, id)) return false;
    for (unsigned i = 0; i < 4; ++i)
      if (i != slot && equipped[i] == id) return false;
    equipped[slot] = id;
    return true;
  }
  bool buy(unsigned stat) {
    if (stat >= 6 || rank[stat] >= 80 || total() >= 294 || level() >= 99 || wallet < cost(stat)) return false;
    wallet -= cost(stat);
    ++rank[stat];
    return true;
  }
};
struct Progress {
  static const uint32_t XP_CAP = 1000000000;
  Training family[20];
  uint8_t selected[3] = { 0, 6, 16 };
  uint32_t recent[8] = {}, nextResult = 1;
  uint8_t results[5] = {}, resultCount = 0;
  void reset() {
    *this = Progress();
    for (unsigned f = 0; f < 20; ++f) {
      family[f].equipped[0] = families[f].primary * 8 + 1;
      family[f].equipped[1] = families[f].secondary * 8 + 1;
      family[f].equipped[2] = 42;
      family[f].equipped[3] = 43;
    }
  }
  bool valid() const {
    if (!nextResult || resultCount > 5) return false;
    for (unsigned f = 0; f < 20; ++f) {
      const auto &t = family[f];
      if (t.wallet > XP_CAP || t.lifetime > XP_CAP || t.total() > 294 || t.flags) return false;
      for (auto rank : t.rank)
        if (rank > 80) return false;
      for (unsigned i = 0; i < 4; ++i) {
        if (!t.eligible(f, t.equipped[i])) return false;
        for (unsigned j = 0; j < i; ++j)
          if (t.equipped[i] == t.equipped[j]) return false;
      }
    }
    for (unsigned i = 0; i < 3; ++i) {
      if (selected[i] >= 20) return false;
      for (unsigned j = 0; j < i; ++j)
        if (selected[i] == selected[j]) return false;
    }
    for (unsigned i = 0; i < 8; ++i) {
      if (recent[i] >= nextResult) return false;
      if (recent[i])
        for (unsigned j = 0; j < i; ++j)
          if (recent[i] == recent[j]) return false;
    }
    for (unsigned i = 0; i < 5; ++i)
      if (results[i] > 3 || (i < resultCount && !results[i])) return false;
    return true;
  }
  bool select(unsigned slot, unsigned f) {
    if (slot >= 3 || f >= 20) return false;
    for (unsigned i = 0; i < 3; ++i)
      if (i != slot && selected[i] == f) return false;
    selected[slot] = f;
    return true;
  }
  unsigned averageLevel() const {
    unsigned total = 0;
    for (auto f : selected)
      total += family[f].level();
    return total / 3;
  }
  unsigned consecutiveLosses() const {
    unsigned count = 0;
    while (count < resultCount && results[count] == 2)
      ++count;
    return count;
  }
  uint32_t reserveResult() { return nextResult < 0xFFFFFFFFU ? nextResult++ : 0; }
  bool award(uint32_t id, unsigned result, unsigned opponentAverage, const uint8_t lineup[3],
             unsigned playerAverage) {
    if (!id || id >= nextResult || result < 1 || result > 3 || opponentAverage < 1 || opponentAverage > 99)
      return false;
    for (auto old : recent)
      if (old == id) return false;
    for (unsigned i = 0; i < 3; ++i) {
      if (lineup[i] >= 20) return false;
      for (unsigned j = 0; j < i; ++j)
        if (lineup[i] == lineup[j]) return false;
    }
    unsigned base = 60 + 8 * opponentAverage;
    if (playerAverage > opponentAverage) {
      unsigned gap = playerAverage - opponentAverage;
      unsigned scale = gap >= 10 ? 25 : 100 - 8 * gap;
      if (scale < 25) scale = 25;
      base = base * scale / 100;
    }
    unsigned xp = base * (result == 1 ? 100 : result == 2 ? 45 : 60) / 100;
    if (result == 2 && xp < 20) xp = 20;
    for (unsigned slot = 0; slot < 3; ++slot) {
      unsigned f = lineup[slot];
      auto &t = family[f];
      t.lifetime = t.lifetime > XP_CAP - xp ? XP_CAP : t.lifetime + xp;
      if (t.level() < 99) t.wallet = t.wallet > XP_CAP - xp ? XP_CAP : t.wallet + xp;
    }
    for (unsigned i = 7; i > 0; --i)
      recent[i] = recent[i - 1];
    recent[0] = id;
    for (unsigned i = 4; i > 0; --i)
      results[i] = results[i - 1];
    results[0] = result;
    if (resultCount < 5) ++resultCount;
    return true;
  }
};
class SaveCodec {
  static uint32_t crc(const uint8_t *p, size_t n) {
    uint32_t crc = 0xFFFFFFFFU;
    while (n--) {
      crc ^= *p++;
      for (unsigned b = 0; b < 8; ++b)
        crc = (crc >> 1) ^ (0xEDB88320U & uint32_t(-int32_t(crc & 1)));
    }
    return ~crc;
  }
  static void put(uint8_t *&p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) {
      *p++ = uint8_t(n);
      n >>= 8;
    }
  }
  static uint32_t get(const uint8_t *&p) {
    uint32_t n = 0;
    for (unsigned i = 0; i < 4; ++i)
      n |= uint32_t(*p++) << (8 * i);
    return n;
  }

public:
  static const size_t SIZE = 437;
  static bool encode(const Progress &profile, uint8_t *out, size_t capacity) {
    if (capacity < SIZE || !profile.valid()) return false;
    memcpy(out, "VYL1", 4);
    out[4] = 1;
    out[5] = 20;
    out[6] = uint8_t(SIZE - 12);
    out[7] = uint8_t((SIZE - 12) >> 8);
    uint8_t *p = out + 8;
    for (const auto &f : profile.family) {
      put(p, f.wallet);
      put(p, f.lifetime);
      for (auto r : f.rank)
        *p++ = r;
      for (auto id : f.equipped)
        *p++ = id;
      *p++ = f.flags;
    }
    for (auto f : profile.selected)
      *p++ = f;
    for (auto id : profile.recent)
      put(p, id);
    put(p, profile.nextResult);
    for (auto result : profile.results)
      *p++ = result;
    *p++ = profile.resultCount;
    put(p, crc(out, SIZE - 4));
    return size_t(p - out) == SIZE;
  }
  static bool decode(const uint8_t *input, size_t size, Progress &profile) {
    if (size != SIZE || memcmp(input, "VYL1", 4) || input[4] != 1 || input[5] != 20 ||
        (unsigned(input[6]) | (unsigned(input[7]) << 8)) != SIZE - 12)
      return false;
    const uint8_t *check = input + SIZE - 4;
    if (get(check) != crc(input, SIZE - 4)) return false;
    Progress next;
    const uint8_t *p = input + 8;
    for (auto &f : next.family) {
      f.wallet = get(p);
      f.lifetime = get(p);
      for (auto &r : f.rank)
        r = *p++;
      for (auto &id : f.equipped)
        id = *p++;
      f.flags = *p++;
    }
    for (auto &f : next.selected)
      f = *p++;
    for (auto &id : next.recent)
      id = get(p);
    next.nextResult = get(p);
    for (auto &result : next.results)
      result = *p++;
    next.resultCount = *p++;
    if (!next.valid()) return false;
    profile = next;
    return true;
  }
};
static_assert(sizeof(Progress) <= 640, "Progress must stay bounded and owned by Arena");
} // namespace veylith
