#pragma once
#include <stdint.h>
namespace veylith {
enum Element : uint8_t { EMBER, TIDE, THORN, STONE, SPARK, UNIVERSAL };
enum Stat : uint8_t { HP, ATK, DEF, FOC, WARD, SPD };
enum Role : uint8_t { PHY, ARC, HEAL, GUARD, BUFF, DEBUFF, CLEAN, REVIVE };
enum Target : uint8_t { EN, SELF, ALLY, FAINT, TEAM, TEAM_FAINT };
enum Effect : uint8_t {
  NONE,
  BURN_PROC,
  PIERCE_DEF,
  PIERCE_WARD,
  ATK_DOWN,
  BURN_BARE,
  DRAIN_SELF,
  SHIELD_EXTRA,
  SLOW_PROC,
  SLOW,
  WET_MARK,
  ROOT_PROC,
  POISON_PROC,
  POISON_BARE,
  DRAIN_ALLY,
  DEF_DOWN_PROC,
  DEF_DOWN,
  ROOT_HIT,
  SHIELD_ON_HIT,
  SHIELD_BYPASS,
  SHOCK_PROC,
  CHARGE_MARK,
  ENERGY_HIT,
  NEXT_PRIORITY,
  HEAL_PERCENT,
  GUARD_HIT,
  ENERGY_RESTORE,
  SHIELD_PERCENT,
  ATK_TEAM,
  ATK_WEAKEN,
  CLEAN_ONE,
  COUNTER_DEALT,
  DEF_WARD_WEAKEN,
  SPD_TEAM,
  TRANSFER_HP,
  ANCHOR_SOUL,
  REVIVE_ONE,
  CLEAN_TEAM,
  FOC_TEAM,
  PRIORITY_SHIELD,
  SILENCE,
  MASS_REVIVE,
  ANTHEM,
  HEAT_MANTLE,
  HEAL_BURN,
  HEAL_TEAM_BURN,
  REGEN_TEAM,
  COUNTER_BLOCKED,
  SPD_SELF,
  FLASH_RELAY
};
struct Family {
  const char *names[3];
  uint8_t primary, secondary, base[6];
  const char *descriptions[3];
};
struct Move {
  const char *name;
  const char *description;
  const char *effectText;
  uint8_t element, role, power, energy, cooldown, target, level, effect, amount, chance;
  uint8_t requiredStat[2];
  uint16_t requiredValue[2];
};
extern const Family families[20];
extern const Move moves[80];
extern const uint8_t moveOrder[80];
extern const char *const elementNames[6];
extern const char *const statNames[6];
extern const char *const roleNames[8];
extern const char *const targetNames[6];
inline const Move &move(unsigned id) {
  return moves[id - 1];
}
inline uint16_t effectiveness(uint8_t attack, uint8_t first, uint8_t second) {
  if (attack >= 5) return 10000;
  const uint8_t wheel[] = { 0, 4, 1, 2, 3 };
  uint32_t value = 10000;
  const uint8_t defense[] = { first, second };
  for (uint8_t type : defense) {
    unsigned distance = (wheel[type] + 5 - wheel[attack]) % 5;
    value = value * (distance == 0 ? 10000 : distance <= 2 ? 12000 : 8333) / 10000;
  }
  return uint16_t(value < 7000 ? 7000 : value > 14400 ? 14400 : value);
}
} // namespace veylith
