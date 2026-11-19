#pragma once
#include "Messages.h"
#include "SecureChannel.h"
#include "../Snake.h"
#include <new>
namespace solo_games { namespace multiplayer {
class SnakeRace {
 Outbox& _out;Snake* _snake=nullptr;
 uint32_t _started=0,_lastProgress=0,_lastTable=0;
 uint16_t _scores[4]={},_localScore=0;
 uint8_t _slot=0,_count=2,_done=0,_winners=0;
 bool _host=false,_error=false,_dirty=false,_progress=false,_final=false;
 unsigned winnerMask(const uint16_t scores[4])const{unsigned best=0,mask=0;for(unsigned i=0;i<_count;++i){if(scores[i]>best){best=scores[i];mask=1U<<i;}else if(scores[i]==best)mask|=1U<<i;}return mask;}
 void report(uint32_t now,bool final){_scores[_slot]=_localScore;if(final)_done|=1U<<_slot;_lastProgress=now;
  if(_host)_dirty=true;else _progress=true;
 }
public:
 static constexpr uint32_t DURATION=120000;
 SnakeRace(Outbox& out,uint64_t session,unsigned round,unsigned slot,unsigned count,uint32_t now):_out(out),_started(now),_lastProgress(now),_lastTable(now),_slot(slot),_count(count),_host(slot==0){
  uint8_t bytes[11],hash[32];Codec::put(bytes,session,8);Codec::put(bytes+8,round,2);bytes[10]=SNAKE;digest(bytes,sizeof(bytes),hash);_snake=new(std::nothrow) Snake;if(!_snake){_error=true;return;}_snake->reset(Codec::get(hash,4),1,now);
 }
 ~SnakeRace(){delete _snake;}
 const Snake* game()const{return _snake;}
 bool steer(Snake::Direction d){return _snake&&_snake->steer(d);}
 bool finished()const{return _final;}bool localDone()const{return (_done>>_slot)&1;}bool error()const{return _error;}
 unsigned winnerMask()const{return _winners;}unsigned doneMask()const{return _done;}unsigned score(unsigned slot)const{return slot==_slot&&_snake?_snake->score():slot<_count?_scores[slot]:0;}
 unsigned secondsLeft(uint32_t now)const{return uint32_t(now-_started)>=DURATION?0:(DURATION-uint32_t(now-_started)+999)/1000;}
 void poll(uint32_t now){if(_error)return;
  if(_snake){_snake->tick(now);_localScore=_snake->score();bool done=_snake->finished()||uint32_t(now-_started)>=DURATION;
   if(done){report(now,true);delete _snake;_snake=nullptr;}
   else if(_localScore!=_scores[_slot]&&uint32_t(now-_lastProgress)>=5000)report(now,false);
  }
  if(_progress&&_out.room()){uint8_t bytes[5];Codec::put(bytes,_localScore,2);unsigned elapsed=(now-_started)/1000;if(elapsed>120)elapsed=120;Codec::put(bytes+2,elapsed,2);bytes[4]=localDone();if(_out.push(SNAKE_PROGRESS,bytes,5))_progress=false;}
  if(_host&&_dirty&&_out.room()&&(uint32_t(now-_lastTable)>=5000||_done==((1U<<_count)-1))){uint8_t bytes[11]={};bool final=_done==((1U<<_count)-1);bytes[0]=_done;bytes[1]=final?winnerMask(_scores):0;bytes[2]=final;for(unsigned i=0;i<4;++i)Codec::put(bytes+3+2*i,_scores[i],2);
   if(_out.push(SNAKE_TABLE,bytes,sizeof(bytes))){_dirty=false;_lastTable=now;if(final){_final=true;_winners=bytes[1];}}
  }
 }
 void receive(const Message& m,unsigned actor){if(_error||actor>=_count||actor==_slot)return;
  if(m.type==SNAKE_PROGRESS&&_host){if(m.size!=5||m.data[4]>1||Codec::get(m.data,2)>Snake::CAPACITY-3||Codec::get(m.data+2,2)>120||((_done>>actor)&1)||Codec::get(m.data,2)<_scores[actor]){_error=true;return;}
   _scores[actor]=Codec::get(m.data,2);if(m.data[4])_done|=1U<<actor;_dirty=true;return;
  }
  if(m.type==SNAKE_TABLE&&!_host&&actor==0){if(m.size!=11||m.data[0]>=(1U<<_count)||m.data[1]>=(1U<<_count)||m.data[2]>1){_error=true;return;}uint16_t scores[4];for(unsigned i=0;i<4;++i){scores[i]=Codec::get(m.data+3+2*i,2);if(scores[i]>Snake::CAPACITY-3||(i>=_count&&scores[i])){_error=true;return;}}
   if(m.data[2]&&(m.data[0]!=(1U<<_count)-1||m.data[1]!=winnerMask(scores))){_error=true;return;}
   for(unsigned i=0;i<_count;++i)if(i!=_slot&&scores[i]<_scores[i]){_error=true;return;}if((m.data[0]&(1U<<_slot))&&(_snake||scores[_slot]!=_localScore)){_error=true;return;}scores[_slot]=_localScore;memcpy(_scores,scores,sizeof(scores));_done|=m.data[0];_winners=m.data[1];_final=m.data[2];return;
  }
  _error=true;
 }
};
} }
