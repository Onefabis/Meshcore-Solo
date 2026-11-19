#include "Battle.h"
namespace veylith {
static unsigned minimum(unsigned a, unsigned b) {
  return a < b ? a : b;
}
static unsigned roundedPercent(unsigned value, unsigned percent) {
  return (value * percent + 99) / 100;
}
bool Battle::hasMove(const Monster &m, unsigned id) const {
  for (auto equipped : m.moves)
    if (equipped == id) return true;
  return false;
}
void Battle::begin(const Build player[3], const Build cpu[3], uint32_t seed) {
  *this = Battle();
  _rng = seed ? seed : 1;
  for (unsigned side = 0; side < 2; ++side)
    for (unsigned i = 0; i < 3; ++i) {
      const auto &build = side ? cpu[i] : player[i];
      auto &m = _team[side][i];
      m.family = build.family;
      m.level = build.training.level();
      m.stage = build.training.stage();
      for (unsigned stat = 0; stat < 6; ++stat)
        m.stats[stat] = build.training.value(m.family, stat);
      m.hp = m.stats[HP];
      for (unsigned slot = 0; slot < 4; ++slot)
        m.moves[slot] = build.training.equipped[slot];
    }
  prepareRound();
}
unsigned Battle::effective(unsigned side, unsigned index, unsigned stat) const {
  const auto &m = _team[side][index];
  unsigned result = m.stats[stat];
  for (auto &mod : m.modifiers)
    if (mod.turns && mod.stat == stat) result = result * (100 + mod.percent) / 100;
  if (stat == SPD && m.status == SHOCK) result = result * 75 / 100;
  return result ? result : 1;
}
unsigned Battle::cost(unsigned side, unsigned id) const {
  if (id < 1 || id > 80) return 255;
  auto &m = move(id);
  return m.energy + (_team[side][_active[side]].silence && m.role != PHY && m.role != ARC ? 2 : 0);
}
bool Battle::legal(unsigned side, const Action &a) const {
  if (side > 1 || _result || needsSwitch(0) || needsSwitch(1)) return false;
  auto &m = _team[side][_active[side]];
  if (a.switching)
    return a.target < 3 && a.target != _active[side] && _team[side][a.target].hp && m.status != ROOT;
  if (a.move < 1 || a.move > 80 || (a.move != 41 && !hasMove(m, a.move)) || a.move == 56 ||
      m.energy < cost(side, a.move))
    return false;
  for (unsigned i = 0; i < 4; ++i)
    if (m.moves[i] == a.move && m.cooldown[i]) return false;
  auto &skill = move(a.move);
  if (skill.target == ALLY && (a.target >= 3 || a.target == _active[side] || !_team[side][a.target].hp))
    return false;
  if (skill.target == FAINT &&
      (a.target >= 3 || a.target == _active[side] || _team[side][a.target].hp || token(side, 0)))
    return false;
  if (skill.effect == MASS_REVIVE) {
    bool faint = false;
    for (auto &ally : _team[side])
      faint |= !ally.hp;
    if (!faint || token(side, 1)) return false;
  }
  if (skill.effect == ANTHEM && token(side, 2)) return false;
  if (skill.effect == FLASH_RELAY &&
      (m.status == ROOT || a.target >= 3 || a.target == _active[side] || !_team[side][a.target].hp))
    return false;
  return true;
}
void Battle::prepareRound() {
  ++_round;
  _awaiting = false;
  for (unsigned side = 0; side < 2; ++side) {
    _cancelled[side] = 0;
    for (unsigned i = 0; i < 3; ++i) {
      auto &m = _team[side][i];
      m.stance = 0;
      if (m.hp) m.energy = minimum(12, m.energy + (i == _active[side] ? 2 : 1));
      if (i == _active[side])
        for (auto &cooldown : m.cooldown)
          if (cooldown) --cooldown;
    }
  }
}
bool Battle::forceSwitch(unsigned side, unsigned target) {
  if (side > 1 || !needsSwitch(side) || target >= 3 || !_team[side][target].hp) return false;
  _active[side] = target;
  addEvent(Event::SWITCHED, side, target, 0);
  if (_awaiting && !needsSwitch(0) && !needsSwitch(1)) prepareRound();
  return true;
}
void Battle::addEvent(unsigned kind, unsigned side, unsigned index, unsigned id, unsigned amount) {
  if (_eventCount < 24) {
    auto &e = _events[_eventCount++];
    e.kind = kind;
    e.side = side;
    e.member = index;
    e.move = id;
    e.amount = amount;
  }
}
void Battle::modifier(Monster &m, unsigned stat, int percent, unsigned turns) {
  unsigned first = percent > 0 ? 0 : 2, slot = first;
  for (unsigned i = first; i < first + 2; ++i) {
    if (m.modifiers[i].turns && m.modifiers[i].stat == stat) {
      slot = i;
      break;
    }
    if (!m.modifiers[i].turns || m.modifiers[i].turns < m.modifiers[slot].turns) slot = i;
  }
  auto &mod = m.modifiers[slot];
  if (mod.turns && mod.stat == stat) {
    if (percent > 0 ? percent < mod.percent : percent > mod.percent) percent = mod.percent;
  }
  mod.stat = stat;
  mod.percent = percent;
  mod.turns = turns;
  mod.since = _round;
}
void Battle::status(Monster &m, unsigned kind, unsigned chance) {
  if (!m.hp || m.status || !chance) return;
  if (chance < 100 && next(_rng) % 1000 >= chance * 10) return;
  m.status = kind;
  m.statusTurns = (kind == BURN || kind == POISON) ? 3 : kind == SHOCK ? 2 : 1;
  m.statusSince = _round;
}
unsigned Battle::damage(unsigned side, const Move &skill, bool critical) const {
  unsigned other = 1 - side;
  auto &attacker = _team[side][_active[side]];
  auto &defender = _team[other][_active[other]];
  unsigned offense = effective(side, _active[side], skill.role == PHY ? ATK : FOC),
           defense = effective(other, _active[other], skill.role == PHY ? DEF : WARD);
  if (skill.effect == PIERCE_DEF && skill.role == PHY) defense -= defense * skill.amount / 100;
  if (skill.effect == PIERCE_WARD && skill.role == ARC) defense -= defense * skill.amount / 100;
  unsigned value = 2 + skill.power * offense / (defense + 20);
  auto &family = families[attacker.family];
  unsigned affinity = skill.element == family.primary     ? 11500
                      : skill.element == family.secondary ? 10800
                                                          : 10000;
  value = value * affinity / 10000;
  auto &target = families[defender.family];
  value = value * effectiveness(skill.element, target.primary, target.secondary) / 10000;
  if (critical) value = value * 125 / 100;
  if (attacker.status == BURN && skill.role == PHY) value = value * 90 / 100;
  if (attacker.anthem) value = value * 110 / 100;
  if (skill.effect == CHARGE_MARK && (defender.status == SHOCK || [&]() {
        for (auto &mod : defender.modifiers)
          if (mod.turns && mod.stat == SPD && mod.percent < 0) return true;
        return false;
      }()))
    value = value * 110 / 100;
  if (defender.wet && skill.element == TIDE) value = value * 115 / 100;
  return value ? value : 1;
}
unsigned Battle::expectedDamage(unsigned side, unsigned id) const {
  return id >= 1 && id <= 80 ? damage(side, move(id), false) : 0;
}
unsigned Battle::healAmount(unsigned casterSide, unsigned targetSide, unsigned target,
                            unsigned percent) const {
  unsigned focus = effective(casterSide, _active[casterSide], FOC), scale = focus * 100 / 30;
  if (scale < 75) scale = 75;
  if (scale > 130) scale = 130;
  unsigned denominator = 10000;
  return (_team[targetSide][target].stats[HP] * percent * scale + denominator - 1) / denominator;
}
void Battle::heal(unsigned side, unsigned index, unsigned amount, unsigned id) {
  auto &m = _team[side][index];
  if (!m.hp) return;
  if (_round >= 41) amount = (amount + 1) / 2;
  unsigned value = minimum(amount, m.stats[HP] - m.hp);
  m.hp += value;
  if (value) addEvent(Event::HEALING, side, index, id, value);
}
void Battle::shield(unsigned side, unsigned index, unsigned amount, unsigned id) {
  auto &m = _team[side][index];
  if (!m.hp) return;
  unsigned cap = m.stats[HP] * 35 / 100;
  unsigned before = m.shield;
  m.shield = minimum(cap, m.shield + amount);
  m.shieldTurns = 2;
  if (m.shield > before) addEvent(Event::SHIELDING, side, index, id, m.shield - before);
}
void Battle::restore(unsigned side, unsigned index, unsigned percent, unsigned id) {
  auto &m = _team[side][index];
  m.hp = roundedPercent(m.stats[HP], percent);
  addEvent(Event::REVIVED, side, index, id, m.hp);
}
void Battle::fatal(unsigned side, unsigned index) {
  auto &m = _team[side][index];
  if (m.hp) return;
  _cancelled[side] |= 1U << index;
  m.stance = 0;
  m.shield = m.shieldTurns = 0;
  if (!(m.flags & 1) && hasMove(m, 56) && m.energy >= 8) {
    m.flags |= 1;
    m.energy = 0;
    restore(side, index, 20, 56);
  } else
    addEvent(Event::FAINTED, side, index, 0);
}
void Battle::checkResult() {
  unsigned living[2] = {};
  for (unsigned side = 0; side < 2; ++side)
    for (auto &m : _team[side])
      living[side] += m.hp != 0;
  if (!living[0] || !living[1]) _result = !living[0] && !living[1] ? 3 : !living[1] ? 1 : 2;
}
void Battle::execute(unsigned side, const Action &action) {
  unsigned own = _active[side], other = 1 - side, enemy = _active[other];
  auto &user = _team[side][own];
  auto &foe = _team[other][enemy];
  if (!user.hp || (_cancelled[side] & (1U << own)) || !legal(side, action)) return;
  if (action.switching) {
    user.stance = 0;
    user.priority = 0;
    _active[side] = action.target;
    addEvent(Event::SWITCHED, side, action.target, 0);
    return;
  }
  const auto &skill = move(action.move);
  user.energy -= cost(side, action.move);
  user.priority = 0;
  for (unsigned i = 0; i < 4; ++i)
    if (user.moves[i] == action.move) user.cooldown[i] = skill.cooldown + 1;
  if (skill.role == PHY || skill.role == ARC) {
    if (!foe.hp) return;
    bool hadShield = foe.shield != 0;
    unsigned full = damage(side, skill, next(_rng) % 16 == 0), value = full, blocked = 0;
    uint8_t stance = foe.stance;
    if (stance && (stance != 2 || skill.role == PHY)) {
      unsigned reduction = stance == 1 ? 50 : stance == 2 ? 35 : 45;
      value = value * (100 - reduction) / 100;
      blocked = full - value;
      foe.stance = 0;
    }
    if (!value) value = 1;
    unsigned hpBefore = foe.hp, shieldBefore = foe.shield;
    unsigned bypass = skill.effect == SHIELD_BYPASS ? value * skill.amount / 100 : 0;
    unsigned direct = minimum(foe.hp, bypass);
    foe.hp -= direct;
    unsigned rest = value - bypass;
    unsigned absorbed = minimum(foe.shield, rest);
    foe.shield -= absorbed;
    rest -= absorbed;
    foe.hp -= minimum(foe.hp, rest);
    if (skill.effect == SHIELD_EXTRA && foe.shield) {
      unsigned bonus = minimum(foe.shield, value * skill.amount / 100);
      foe.shield -= bonus;
    }
    unsigned dealt = hpBefore - foe.hp;
    addEvent(Event::STRIKE, side, own, action.move, dealt);
    if (skill.element == TIDE && foe.wet) foe.wet = 0;
    switch (skill.effect) {
    case BURN_PROC:
      status(foe, BURN, skill.chance);
      break;
    case BURN_BARE:
      if (!hadShield) status(foe, BURN);
      break;
    case POISON_PROC:
      status(foe, POISON, skill.chance);
      break;
    case POISON_BARE:
      if (!hadShield) status(foe, POISON);
      break;
    case SHOCK_PROC:
    case CHARGE_MARK:
      status(foe, SHOCK, skill.chance);
      break;
    case ROOT_PROC:
      status(foe, ROOT, skill.chance);
      break;
    case ROOT_HIT:
      status(foe, ROOT);
      break;
    case ATK_DOWN:
      if (foe.hp) modifier(foe, ATK, -int(skill.amount));
      break;
    case SLOW:
      if (foe.hp) modifier(foe, SPD, -int(skill.amount));
      break;
    case DEF_DOWN:
      if (foe.hp) modifier(foe, DEF, -int(skill.amount));
      break;
    case SLOW_PROC:
      if (foe.hp && next(_rng) % 1000 < skill.chance * 10) modifier(foe, SPD, -int(skill.amount));
      break;
    case DEF_DOWN_PROC:
      if (foe.hp && next(_rng) % 1000 < skill.chance * 10) modifier(foe, DEF, -int(skill.amount));
      break;
    case WET_MARK:
      if (foe.hp) foe.wet = 3;
      break;
    case DRAIN_SELF:
      heal(side, own, dealt * skill.amount / 100, action.move);
      break;
    case DRAIN_ALLY: {
      unsigned target = 3;
      for (unsigned i = 0; i < 3; ++i)
        if (i != own && _team[side][i].hp &&
            (target == 3 || _team[side][i].hp * _team[side][target].stats[HP] <
                                _team[side][target].hp * _team[side][i].stats[HP]))
          target = i;
      if (target < 3) heal(side, target, dealt * skill.amount / 100, action.move);
      break;
    }
    case SHIELD_ON_HIT:
      shield(side, own, dealt * skill.amount / 100, action.move);
      break;
    case ENERGY_HIT:
      user.energy = minimum(12, user.energy + skill.amount);
      break;
    case NEXT_PRIORITY:
      user.priority = 2;
      user.prioritySince = _round;
      break;
    default:
      break;
    }
    // Counter is reactive and non-recursive, and requires surviving a PHY hit.
    if (foe.hp && skill.role == PHY && (stance == 2 || stance == 3)) {
      unsigned basis = stance == 2 ? dealt + shieldBefore - foe.shield : blocked,
               percent = stance == 2 ? 30 : 35;
      unsigned reflected = minimum(user.hp, basis * percent / 100);
      user.hp -= reflected;
      if (reflected) addEvent(Event::STRIKE, other, enemy, 0, reflected);
    }
    if (foe.hp && foe.heat) status(user, BURN, 20);
    if (!foe.hp) fatal(other, enemy);
    if (!user.hp) fatal(side, own);
  } else {
    unsigned target = skill.target == ALLY || skill.target == FAINT ? action.target : own;
    unsigned begin = skill.target == TEAM || skill.target == TEAM_FAINT ? 0 : target,
             end = skill.target == TEAM || skill.target == TEAM_FAINT ? 3 : target + 1;
    switch (skill.effect) {
    case GUARD_HIT:
      user.stance = 1;
      break;
    case COUNTER_DEALT:
      user.stance = 2;
      break;
    case COUNTER_BLOCKED:
      user.stance = 3;
      break;
    case ENERGY_RESTORE:
      user.energy = minimum(12, user.energy + skill.amount);
      break;
    case HEAL_PERCENT:
    case HEAL_BURN:
    case HEAL_TEAM_BURN:
      for (unsigned i = begin; i < end; ++i) {
        heal(side, i, healAmount(side, side, i, skill.amount), action.move);
        if (skill.effect != HEAL_PERCENT && _team[side][i].hp && _team[side][i].status == BURN)
          _team[side][i].status = _team[side][i].statusTurns = 0;
      }
      break;
    case SHIELD_PERCENT:
      for (unsigned i = begin; i < end; ++i)
        shield(side, i, roundedPercent(_team[side][i].stats[HP], skill.amount), action.move);
      break;
    case HEAT_MANTLE:
      shield(side, own, roundedPercent(user.stats[HP], skill.amount), action.move);
      user.heat = 2;
      break;
    case ATK_TEAM:
    case SPD_TEAM:
    case FOC_TEAM:
      for (auto &m : _team[side])
        if (m.hp)
          modifier(m, skill.effect == ATK_TEAM ? ATK : skill.effect == SPD_TEAM ? SPD : FOC, skill.amount);
      break;
    case SPD_SELF:
      modifier(user, SPD, skill.amount);
      break;
    case ATK_WEAKEN:
      modifier(foe, ATK, -int(skill.amount));
      break;
    case DEF_WARD_WEAKEN:
      modifier(foe, DEF, -int(skill.amount));
      modifier(foe, WARD, -int(skill.amount));
      break;
    case CLEAN_ONE:
    case CLEAN_TEAM:
      for (unsigned i = begin; i < end; ++i)
        if (_team[side][i].hp) _team[side][i].status = _team[side][i].statusTurns = 0;
      break;
    case TRANSFER_HP: {
      unsigned amount = minimum(user.hp > 1 ? user.hp - 1 : 0,
                                minimum(roundedPercent(user.stats[HP], skill.amount),
                                        unsigned(_team[side][target].stats[HP] - _team[side][target].hp)));
      user.hp -= amount;
      heal(side, target, amount, action.move);
      break;
    }
    case REVIVE_ONE:
      _tokens[side] |= 1;
      restore(side, target, skill.amount, action.move);
      break;
    case MASS_REVIVE:
      _tokens[side] |= 2;
      for (unsigned i = 0; i < 3; ++i)
        if (!_team[side][i].hp) restore(side, i, skill.amount, action.move);
      break;
    case PRIORITY_SHIELD:
      user.priority = 2;
      user.prioritySince = _round;
      shield(side, own, roundedPercent(user.stats[HP], skill.amount), action.move);
      break;
    case SILENCE:
      foe.silence = 2;
      foe.silenceSince = _round;
      break;
    case ANTHEM:
      _tokens[side] |= 4;
      for (unsigned i = 0; i < 3; ++i)
        if (_team[side][i].hp) {
          shield(side, i, roundedPercent(_team[side][i].stats[HP], skill.amount), action.move);
          _team[side][i].anthem = 2;
          _team[side][i].anthemSince = _round;
        }
      break;
    case REGEN_TEAM:
      for (unsigned i = 0; i < 3; ++i)
        if (_team[side][i].hp) {
          heal(side, i, healAmount(side, side, i, skill.amount), action.move);
          _team[side][i].regen = 4;
        }
      break;
    case FLASH_RELAY:
      user.stance = 0;
      _active[side] = action.target;
      _team[side][action.target].energy = minimum(12, _team[side][action.target].energy + skill.amount);
      addEvent(Event::SWITCHED, side, action.target, action.move);
      break;
    default:
      break;
    }
    addEvent(Event::STATUS, side, own, action.move);
  }
  checkResult();
}
void Battle::endRound() {
  // DOT for every living member, then one delayed regeneration, then timers.
  for (unsigned side = 0; side < 2; ++side)
    for (unsigned i = 0; i < 3; ++i) {
      auto &m = _team[side][i];
      if (m.hp && (m.status == BURN || m.status == POISON)) {
        unsigned amount = minimum(m.hp, roundedPercent(m.stats[HP], m.status == BURN ? 3 : 4));
        m.hp -= amount;
        addEvent(Event::STRIKE, 1 - side, i, 0, amount);
      }
    }
  for (unsigned side = 0; side < 2; ++side)
    for (unsigned i = 0; i < 3; ++i) {
      auto &m = _team[side][i];
      if (m.regen) {
        heal(side, i, healAmount(side, side, i, m.regen), 73);
        m.regen = 0;
      }
      if (m.statusTurns && ((m.status == BURN || m.status == POISON) || m.statusSince != _round)) {
        if (!--m.statusTurns) m.status = 0;
      }
      for (auto &mod : m.modifiers)
        if (mod.turns && mod.since != _round) --mod.turns;
      if (m.shieldTurns && !--m.shieldTurns) m.shield = 0;
      if (m.heat) --m.heat;
      if (m.wet) --m.wet;
      if (m.silence && m.silenceSince != _round) --m.silence;
      if (m.anthem && m.anthemSince != _round) --m.anthem;
      if (i == _active[side] && m.priority && m.prioritySince != _round) --m.priority;
      m.stance = 0;
      if (!m.hp && !(_cancelled[side] & (1U << i))) fatal(side, i);
    }
  if (_round >= 41)
    for (unsigned side = 0; side < 2; ++side) {
      auto i = _active[side];
      auto &m = _team[side][i];
      unsigned amount = minimum(m.hp, 2 + (_round - 41) / 3);
      m.hp -= amount;
      addEvent(Event::PRESSURE, side, i, 0, amount);
      if (!m.hp) fatal(side, i);
    }
  checkResult();
  if (!_result && _round >= 60) {
    uint32_t current[2] = {}, maximum[2] = {};
    unsigned alive[2] = {};
    for (unsigned side = 0; side < 2; ++side)
      for (auto &m : _team[side]) {
        current[side] += m.hp;
        maximum[side] += m.stats[HP];
        alive[side] += m.hp != 0;
      }
    uint32_t a = current[0] * maximum[1], b = current[1] * maximum[0];
    _result = a != b ? (a > b ? 1 : 2) : alive[0] != alive[1] ? (alive[0] > alive[1] ? 1 : 2) : 3;
  }
  if (!_result) {
    _awaiting = true;
    if (!needsSwitch(0) && !needsSwitch(1)) prepareRound();
  }
}
bool Battle::resolve(const Action &player, const Action &cpu) {
  if (!legal(0, player) || !legal(1, cpu)) return false;
  _eventCount = 0;
  Action actions[] = { player, cpu };
  unsigned actors[] = { _active[0], _active[1] };
  auto priority = [&](unsigned side) {
    auto a = actions[side];
    return (a.switching                          ? 2
            : move(a.move).effect == FLASH_RELAY ? 1
            : move(a.move).role == GUARD         ? 1
                                                 : 0) +
           (_team[side][_active[side]].priority ? 1 : 0);
  };
  unsigned first = 0;
  int p0 = priority(0), p1 = priority(1);
  if (p1 > p0)
    first = 1;
  else if (p0 == p1) {
    auto a = effective(0, _active[0], SPD), b = effective(1, _active[1], SPD);
    if (b > a || (a == b && (next(_rng) & 1))) first = 1;
  }
  execute(first, actions[first]);
  unsigned second = 1 - first;
  if (!_result && _active[second] == actors[second] && !(_cancelled[second] & (1U << actors[second])))
    execute(second, actions[second]);
  if (!_result) endRound();
  return true;
}
uint32_t Battle::stateHash() const {
  uint32_t hash = 2166136261U;
  auto add = [&](uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) {
      hash ^= uint8_t(n >> (8 * i));
      hash *= 16777619U;
    }
  };
  add(_rng);
  add(_round);
  add(_result);
  add(_awaiting);
  for (unsigned side = 0; side < 2; ++side) {
    add(_active[side]);
    add(_tokens[side]);
    add(_cancelled[side]);
    for (auto &m : _team[side]) {
      for (auto stat : m.stats)
        add(stat);
      add(m.family);
      add(m.level);
      add(m.stage);
      add(m.hp);
      add(m.shield);
      add(m.energy);
      for (auto id : m.moves)
        add(id);
      for (auto cd : m.cooldown)
        add(cd);
      for (auto &mod : m.modifiers) {
        add(mod.stat);
        add(uint8_t(mod.percent));
        add(mod.turns);
        add(mod.since);
      }
      add(m.status);
      add(m.statusTurns);
      add(m.statusSince);
      add(m.shieldTurns);
      add(m.stance);
      add(m.heat);
      add(m.wet);
      add(m.silence);
      add(m.silenceSince);
      add(m.priority);
      add(m.prioritySince);
      add(m.regen);
      add(m.anthem);
      add(m.anthemSince);
      add(m.flags);
    }
  }
  return hash;
}
void Battle::generateCPU(const Progress &profile, uint32_t seed, Build output[3]) {
  uint32_t rng = seed ? seed : 1;
  unsigned levels[3];
  for (unsigned i = 0; i < 3; ++i)
    levels[i] = profile.family[profile.selected[i]].level();
  if (levels[0] > levels[1]) {
    auto t = levels[0];
    levels[0] = levels[1];
    levels[1] = t;
  }
  if (levels[1] > levels[2]) {
    auto t = levels[1];
    levels[1] = levels[2];
    levels[2] = t;
  }
  if (levels[0] > levels[1]) {
    auto t = levels[0];
    levels[0] = levels[1];
    levels[1] = t;
  }
  // One optional counter pick; the remaining families are sampled independently.
  for (unsigned i = 0; i < 3; ++i) {
    unsigned f;
    do {
      f = next(rng) % 20;
    } while ((i > 0 && f == output[0].family) || (i > 1 && f == output[1].family));
    if (i == 0 && (next(rng) & 1)) {
      unsigned target = profile.selected[next(rng) % 3], best = 0;
      for (unsigned candidate = 0; candidate < 20; ++candidate) {
        auto &pair = families[candidate];
        auto &enemy = families[target];
        unsigned rating = effectiveness(pair.primary, enemy.primary, enemy.secondary);
        if (rating > best) {
          best = rating;
          f = candidate;
        }
      }
    }
    output[i] = Build();
    output[i].family = f;
    auto &t = output[i].training;
    int level = int(levels[1]) + int(next(rng) % 5) - 2;
    if (level < 1) level = 1;
    if (level > 99) level = 99;
    unsigned role = next(rng) % 3, points = 3 * (level - 1);
    const unsigned priorities[3][6] = { { HP, ATK, SPD, DEF, WARD, FOC },
                                        { HP, FOC, SPD, WARD, DEF, ATK },
                                        { HP, DEF, WARD, FOC, ATK, SPD } };
    for (unsigned n = 0; n < points; ++n) {
      unsigned stat = priorities[role][next(rng) % 6];
      if (next(rng) & 1) stat = priorities[role][next(rng) % 3];
      while (t.rank[stat] >= 80)
        stat = (stat + 1) % 6;
      ++t.rank[stat];
    }
    t.equipped[0] = families[f].primary * 8 + 1;
    t.equipped[1] = families[f].secondary * 8 + 1;
    t.equipped[2] = 42;
    t.equipped[3] = 43;
    for (unsigned slot = 0; slot < 2; ++slot) {
      unsigned element = slot ? families[f].secondary : families[f].primary, best = element * 8 + 1;
      for (unsigned id = element * 8 + 1; id <= element * 8 + 8; ++id)
        if (t.eligible(f, id) && move(id).level > move(best).level) best = id;
      t.equipped[slot] = best;
    }
    // Support and revival remain visible in the disclosed loadout; no hidden stats.
    const uint8_t support[] = { 42, 44, 47, 49, 51, 54, 56, 57, 58, 59, 60, 62, 64, 65 };
    unsigned start = next(rng) % (sizeof(support));
    for (unsigned n = 0; n < sizeof(support); ++n) {
      unsigned id = support[(start + n) % sizeof(support)];
      if (t.eligible(f, id) && id != t.equipped[0] && id != t.equipped[1] && id != t.equipped[2]) {
        t.equipped[3] = id;
        break;
      }
    }
  }
}
int Battle::score(unsigned side, const Action &a) const {
  unsigned own = _active[side], enemy = _active[1 - side];
  const auto &user = _team[side][own];
  const auto &foe = _team[1 - side][enemy];
  if (a.switching) {
    const auto &fresh = _team[side][a.target];
    auto &pair = families[fresh.family];
    auto &target = families[foe.family];
    int advantage = int(effectiveness(pair.primary, target.primary, target.secondary)) -
                    int(effectiveness(families[user.family].primary, target.primary, target.secondary));
    return advantage / 500 + (user.hp * 100 / user.stats[HP] < 25 ? 14 : -12) +
           (fresh.hp * 100 / fresh.stats[HP] > 50 ? 2 : 0);
  }
  const auto &skill = move(a.move);
  unsigned target = skill.target == ALLY || skill.target == FAINT ? a.target : own;
  if (skill.role == PHY || skill.role == ARC) {
    unsigned amount = expectedDamage(side, a.move), blocked = minimum(amount, foe.shield);
    unsigned dealt = minimum(foe.hp, amount - blocked);
    int rating = dealt * 3 + (dealt >= foe.hp ? 35 : 0) - int(cost(side, a.move));
    if (!foe.status && (skill.effect == BURN_BARE || skill.effect == POISON_BARE || skill.effect == ROOT_HIT))
      rating += 8;
    if (skill.effect == DRAIN_SELF)
      rating += minimum(user.stats[HP] - user.hp, dealt * skill.amount / 100) * 2;
    if (skill.effect == SHIELD_ON_HIT) rating += dealt * skill.amount / 100;
    return rating;
  }
  unsigned statuses = 0, fainted = 0;
  for (unsigned i = 0; i < 3; ++i) {
    auto &m = _team[side][i];
    statuses += m.hp && m.status;
    fainted += !m.hp;
  }
  switch (skill.effect) {
  case HEAL_PERCENT:
  case HEAL_BURN:
  case HEAL_TEAM_BURN:
  case REGEN_TEAM: {
    unsigned healing = 0;
    for (unsigned i = 0; i < 3; ++i)
      if (_team[side][i].hp && (skill.target == TEAM || i == target))
        healing +=
            minimum(_team[side][i].stats[HP] - _team[side][i].hp, healAmount(side, side, i, skill.amount));
    return int(healing * 3) - int(cost(side, a.move));
  }
  case REVIVE_ONE:
    return 45 + _team[side][target].stats[HP] * skill.amount / 100;
  case MASS_REVIVE:
    return int(fainted) * 65;
  case ENERGY_RESTORE:
    return user.energy <= 4 ? 15 : -12;
  case CLEAN_ONE:
    return _team[side][target].status ? 20 : -15;
  case CLEAN_TEAM:
    return statuses ? int(statuses) * 20 : -15;
  case ATK_TEAM:
  case FOC_TEAM:
  case SPD_TEAM:
  case SPD_SELF:
    return user.modifiers[0].turns ? -8 : 12;
  case ATK_WEAKEN:
  case DEF_WARD_WEAKEN:
    return foe.modifiers[2].turns ? -8 : 14;
  case GUARD_HIT:
  case COUNTER_DEALT:
  case COUNTER_BLOCKED:
    return user.hp * 100 / user.stats[HP] < 25 ? 3 : -4;
  case SHIELD_PERCENT:
  case HEAT_MANTLE:
  case PRIORITY_SHIELD:
    return user.shield ? 0 : 10;
  case ANTHEM:
    return 26;
  case TRANSFER_HP:
    return user.hp > user.stats[HP] / 2 && _team[side][target].hp < _team[side][target].stats[HP] / 3 ? 20
                                                                                                      : -12;
  case FLASH_RELAY: {
    Action switching;
    switching.switching = true;
    switching.target = a.target;
    return score(side, switching) + 5;
  }
  case SILENCE:
    return foe.silence ? -10 : 8;
  default:
    return -10;
  }
}
Action Battle::chooseCPU(unsigned side, uint32_t seed, unsigned difficulty) const {
  // Public board only. No pending human command and no access to the battle RNG.
  uint32_t rng = seed ? seed : 1;
  Action best;
  int bestScore = -100000;
  const auto &user = _team[side][_active[side]];
  for (unsigned slot = 0; slot < 8; ++slot) {
    Action action;
    if (slot < 5)
      action.move = slot == 0 ? 41 : user.moves[slot - 1];
    else {
      action.switching = true;
      action.target = slot - 5;
    }
    unsigned targets = action.switching ? 1 : 3;
    for (unsigned t = 0; t < targets; ++t) {
      if (!action.switching) action.target = t;
      if (!legal(side, action)) continue;
      int rating = score(side, action);
      unsigned noise = difficulty == 0 ? 21 : difficulty == 1 ? 7 : 1;
      rating += int(next(rng) % noise) - int(noise / 2);
      if (rating > bestScore) {
        bestScore = rating;
        best = action;
      }
    }
  }
  return best;
}
} // namespace veylith
