#pragma once
#include "Progress.h"
namespace veylith {
struct Build {
  uint8_t family = 0;
  Training training;
};
struct Modifier {
  uint8_t stat = 0;
  int8_t percent = 0;
  uint8_t turns = 0, since = 0;
};
struct Monster {
  uint16_t stats[6] = {}, hp = 0, shield = 0;
  uint8_t family = 0, level = 1, stage = 0, energy = 6, moves[4] = {}, cooldown[4] = {};
  Modifier modifiers[4];
  uint8_t status = 0, statusTurns = 0, statusSince = 0, shieldTurns = 0, stance = 0, heat = 0, wet = 0,
          silence = 0, silenceSince = 0;
  uint8_t priority = 0, prioritySince = 0, regen = 0, anthem = 0, anthemSince = 0, flags = 0;
};
enum Status : uint8_t { HEALTHY, BURN, POISON, SHOCK, ROOT };
struct Action {
  uint8_t move = 41, target = 0;
  bool switching = false;
};
struct Event {
  enum Kind : uint8_t { STRIKE, HEALING, SHIELDING, STATUS, SWITCHED, REVIVED, FAINTED, PRESSURE, TRAINED };
  uint8_t kind = 0, side = 0, member = 0, move = 0;
  uint16_t amount = 0;
};
class Battle {
public:
  void begin(const Build player[3], const Build cpu[3], uint32_t seed);
  const Monster &member(unsigned side, unsigned index) const { return _team[side][index]; }
  uint8_t active(unsigned side) const { return _active[side]; }
  unsigned roundNumber() const { return _round; }
  unsigned result() const { return _result; }
  bool needsSwitch(unsigned side) const { return !_result && !_team[side][_active[side]].hp; }
  bool forceSwitch(unsigned side, unsigned target);
  bool legal(unsigned side, const Action &action) const;
  unsigned cost(unsigned side, unsigned id) const;
  bool resolve(const Action &player, const Action &cpu);
  unsigned effective(unsigned side, unsigned index, unsigned stat) const;
  unsigned expectedDamage(unsigned side, unsigned id) const;
  bool token(unsigned side, unsigned bit) const { return (_tokens[side] >> bit) & 1; }
  unsigned eventCount() const { return _eventCount; }
  const Event &event(unsigned i) const { return _events[i]; }
  uint32_t stateHash() const;
  static void generateCPU(const Progress &progress, uint32_t seed, Build result[3]);
  Action chooseCPU(unsigned side, uint32_t seed, unsigned difficulty = 1) const;

private:
  friend struct BattleFixture;
  Monster _team[2][3];
  uint8_t _active[2] = {}, _tokens[2] = {}, _round = 0, _result = 0, _eventCount = 0;
  uint8_t _cancelled[2] = {};
  bool _awaiting = false;
  uint32_t _rng = 1;
  Event _events[24];
  static uint32_t next(uint32_t &state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
  void prepareRound();
  void checkResult();
  void execute(unsigned side, const Action &action);
  void endRound();
  void fatal(unsigned side, unsigned index);
  void addEvent(unsigned kind, unsigned side, unsigned index, unsigned id, unsigned amount = 0);
  unsigned damage(unsigned side, const Move &move, bool critical) const;
  unsigned healAmount(unsigned casterSide, unsigned targetSide, unsigned target, unsigned percent) const;
  void heal(unsigned side, unsigned index, unsigned amount, unsigned id);
  void shield(unsigned side, unsigned index, unsigned amount, unsigned id);
  void modifier(Monster &monster, unsigned stat, int percent, unsigned turns = 2);
  void status(Monster &monster, unsigned kind, unsigned chance = 100);
  bool hasMove(const Monster &m, unsigned id) const;
  void restore(unsigned side, unsigned index, unsigned percent, unsigned id);
  int score(unsigned side, const Action &action) const;
};
static_assert(sizeof(Battle) <= 2048, "Battle and bounded visual events must fit 2 KiB");
} // namespace veylith
