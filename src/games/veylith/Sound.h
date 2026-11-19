#pragma once
#include <stdint.h>
namespace veylith {
enum SoundCue:uint8_t { SILENT,ATTACK_SOUND,HIT_SOUND,HEAL_SOUND,SHIELD_SOUND,BOOST_SOUND,DEBUFF_SOUND,
  SWITCH_SOUND,FAINT_SOUND,REVIVE_SOUND,TRAIN_SOUND,LEVEL_SOUND,EVOLVE_SOUND,WIN_SOUND,LOSS_SOUND,DRAW_SOUND,SOUND_COUNT };
// RTTTL strings stay in flash. Ordinary buzzer.play respects mute/volume;
// the screen waits briefly for higher-priority notification playback to finish.
inline const char* melody(unsigned cue){
  switch(cue){
    case ATTACK_SOUND:return "Aatk:d=64,o=5,b=240:c6,g5";
    case HIT_SOUND:return "Ahit:d=64,o=4,b=240:g,c";
    case HEAL_SOUND:return "Aheal:d=64,o=5,b=240:c,e,g";
    case SHIELD_SOUND:return "Ashld:d=64,o=4,b=240:c,g";
    case BOOST_SOUND:return "Abuff:d=64,o=5,b=240:e,g,b";
    case DEBUFF_SOUND:return "Aweak:d=64,o=5,b=240:b,g,e";
    case SWITCH_SOUND:return "Aswap:d=64,o=5,b=240:g,c6";
    case FAINT_SOUND:return "Afaint:d=64,o=4,b=240:g,e,c,p";
    case REVIVE_SOUND:return "Arise:d=64,o=4,b=240:c,e,g,c5,e5";
    case TRAIN_SOUND:return "Atrain:d=64,o=5,b=240:c,d,e";
    case LEVEL_SOUND:return "Alevel:d=32,o=5,b=240:c,e,g,c6";
    case EVOLVE_SOUND:return "Aevolve:d=32,o=5,b=240:c,e,g,c6,e6,g6";
    case WIN_SOUND:return "Awin:d=16,o=5,b=240:c,e,g,8c6";
    case LOSS_SOUND:return "Aloss:d=16,o=4,b=200:g,e,d,8c";
    case DRAW_SOUND:return "Adraw:d=32,o=5,b=200:e,p,e,c";
    default:return nullptr;
  }
}
}
