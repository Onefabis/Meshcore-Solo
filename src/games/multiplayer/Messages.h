#pragma once
#include "Packet.h"
namespace solo_games { namespace multiplayer {
enum Application:uint8_t { PROPOSE=32,COMMIT,POKER_ACTION,POKER_VIEW0,POKER_VIEW1,POKER_NEXT,FLEET_COMMIT,SHOT,SHOT_RESULT,FLEET_REVEAL,
  ARENA_ROSTER0,ARENA_ROSTER1,ARENA_SEED_COMMIT,ARENA_SEED_REVEAL,ARENA_ACTION_COMMIT,ARENA_ACTION_REVEAL,ARENA_STATE,ARENA_FORCE,SNAKE_PROGRESS,SNAKE_TABLE };
struct Outgoing {Message message;uint8_t target=255;bool fast=false;};
class Outbox {
  Outgoing _items[8];uint8_t _head=0,_count=0;
public:
  bool push(unsigned type,const uint8_t* data,unsigned size,unsigned target=255,bool fast=false){if(size>32||_count>=8)return false;auto& item=_items[(_head+_count)%8];item=Outgoing();item.message.type=type;item.message.size=size;if(size)memcpy(item.message.data,data,size);item.target=target;item.fast=fast;++_count;return true;}
  const Outgoing* front()const{return _count?&_items[_head]:nullptr;}
  void pop(){if(_count){_head=(_head+1)%8;--_count;}}
  bool empty()const{return !_count;}unsigned room()const{return 8-_count;}
};
class RandomStream {
  Security& _security;uint8_t _bytes[64]={};unsigned _at=64;bool _ok=true;
public:
  explicit RandomStream(Security& security):_security(security){}
  ~RandomStream(){volatile uint8_t* p=_bytes;for(unsigned i=0;i<64;++i)p[i]=0;}
  bool uniform(unsigned bound,unsigned& value){if(!bound||bound>256)return false;unsigned limit=256/bound*bound;
    for(unsigned tries=0;tries<1024;++tries){if(_at==64){_ok=_security.random(_bytes,64);_at=0;if(!_ok)return false;}unsigned v=_bytes[_at++];if(v<limit){value=v%bound;return true;}}return false;}
};
inline bool shuffledDeck(Security& security,uint8_t deck[52]){RandomStream rng(security);for(unsigned i=0;i<52;++i)deck[i]=i;for(unsigned i=51;i;--i){unsigned j;if(!rng.uniform(i+1,j))return false;auto card=deck[i];deck[i]=deck[j];deck[j]=card;}return true;}
} }
