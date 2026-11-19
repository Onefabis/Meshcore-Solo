#pragma once
#include <stdint.h>
namespace veylith {
extern const uint8_t creatureArt[7680];
extern const uint8_t miniArt[1920];
extern const uint8_t trainerArt[384];
extern const uint8_t iconArt[184];
extern const uint8_t progressArt[112];
inline const uint8_t* sprite(unsigned family,unsigned stage) {
  return creatureArt+((family<20?family:0)*3+(stage<3?stage:0))*128;
}
inline const uint8_t* mini(unsigned family,unsigned stage) {
  return miniArt+((family<20?family:0)*3+(stage<3?stage:0))*32;
}
}
