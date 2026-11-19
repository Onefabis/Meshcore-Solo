#pragma once
#include "Session.h"
#include "Messages.h"
#include "FleetDuel.h"
#include "ArenaDuel.h"
#include "SnakeRace.h"
#include "ArtilleryTable.h"
#include "PokerTable.h"
#include "../TicTacToe.h"
#include "../Checkers.h"
#include <new>
namespace solo_games { namespace multiplayer {
class Rules {
protected:
 Session& _session;Security& _security;bool _error=false;
public:
 Outbox out;
 Rules(Session& s,Security& security):_session(s),_security(security){}
 virtual ~Rules(){}
 virtual void poll(uint32_t now)=0;
 virtual void receive(const Message& message,unsigned actor)=0;
 virtual bool finished()const=0;
 virtual unsigned winnerMask()const=0;
 virtual bool error()const{return _error;}
 virtual bool action(unsigned a,unsigned b=0,unsigned c=0){return false;}
 virtual unsigned turn()const{return 0;}
 virtual unsigned revision()const{return 0;}
 virtual bool submitted()const{return false;}
 virtual const TicTacToePosition* ttt()const{return nullptr;}
 virtual const Checkers* checkers()const{return nullptr;}
 virtual const ArtilleryTable* artillery()const{return nullptr;}
 virtual FleetDuel* fleet(){return nullptr;}
 virtual const PokerView* poker()const{return nullptr;}
 virtual bool nextHand(){return false;}
 virtual SnakeRace* race(){return nullptr;}
 virtual ArenaDuel* arena(){return nullptr;}
 bool transmit(){auto item=out.front();if(!item)return false;bool ok=item->target==255?_session.send(item->message.type,item->message.data,item->message.size,item->fast):_session.sendTo(item->target,item->message.type,item->message.data,item->message.size,item->fast);if(ok)out.pop();return ok;}
 bool idle()const{return out.empty()&&!_session.busy();}
};
class BoardRules : public Rules {
 TicTacToePosition* _ttt=nullptr;Checkers* _checkers=nullptr;ArtilleryTable* _artillery=nullptr;ArtilleryTable* _animation=nullptr;
 uint32_t _step=0,_nextFrame=0;uint8_t _proposal[3]={};bool _submitted=false,_deferred=false;Message _held;
 unsigned _heldActor=0;
 uint32_t hash()const{if(_checkers)return _checkers->stateHash();if(_artillery)return _artillery->stateHash();return uint32_t(_ttt->x)|(uint32_t(_ttt->o)<<9)|(uint32_t(_ttt->turn)<<18);}
 bool apply(unsigned a,unsigned b,unsigned c){if(_ttt)return _ttt->play(a);if(_checkers)return _checkers->play(a,b);
  auto* animation=new(std::nothrow) ArtilleryTable(*_artillery);if(animation&&!animation->fire(_artillery->turn(),a,b,c)){delete animation;return false;}
  if(!_artillery->shot(_artillery->turn(),a,b,c)){delete animation;return false;}delete _animation;_animation=animation;_nextFrame=0;return true;
 }
 bool commit(unsigned actor,unsigned a,unsigned b,unsigned c){if(_step==0xFFFFFFFFU||!out.room()||finished()||actor!=turn())return false;uint8_t data[16];data[0]=actor;data[1]=a;data[2]=b;data[3]=c;Codec::put(data+4,hash(),4);Codec::put(data+8,_step+1,4);
  if(!apply(a,b,c))return false;++_step;Codec::put(data+12,hash(),4);if(!out.push(COMMIT,data,sizeof(data)))_error=true;return !_error;
 }
 bool proposal(const Message& message,unsigned actor){if(message.size!=12||actor!=turn()||message.data[0]!=actor||Codec::get(message.data+4,4)!=hash()||Codec::get(message.data+8,4)!=_step)return false;
  return commit(actor,message.data[1],message.data[2],message.data[3]);}
public:
 BoardRules(Session& session,Security& security):Rules(session,security){uint8_t bytes[11],seed[32];Codec::put(bytes,session.id(),8);Codec::put(bytes+8,session.matchRound(),2);bytes[10]=session.game();digest(bytes,sizeof(bytes),seed);
  if(session.game()==TTT)_ttt=new(std::nothrow) TicTacToePosition;
  else if(session.game()==CHECKERS){_checkers=new(std::nothrow) Checkers;if(_checkers)_checkers->reset();}
  else{_artillery=new(std::nothrow) ArtilleryTable;if(_artillery)_artillery->reset(session.count(),Codec::get(seed,4));}
  if(!_ttt&&!_checkers&&!_artillery)_error=true;
 }
 ~BoardRules()override{delete _ttt;delete _checkers;delete _artillery;delete _animation;}
 const TicTacToePosition* ttt()const override{return _ttt;}const Checkers* checkers()const override{return _checkers;}
 const ArtilleryTable* artillery()const override{return _animation?_animation:_artillery;}
 unsigned turn()const override{return _ttt?_ttt->turn-1:_checkers?(_checkers->turn()==1?0:1):_artillery->turn();}
 unsigned revision()const override{return _step;}
 bool submitted()const override{return _submitted;}
 bool finished()const override{return _ttt?_ttt->result()!=0:_checkers?_checkers->result()!=0:_artillery->finished();}
 unsigned winnerMask()const override{unsigned result=_ttt?_ttt->result():_checkers?_checkers->result():0;return _artillery?_artillery->winnerMask():result==1?1:result==2?2:0;}
 bool action(unsigned a,unsigned b,unsigned c)override{if(_error||!idle()||_submitted||finished()||turn()!=_session.slot()||(_animation&&_animation->flying()))return false;
  if(_ttt&&!_ttt->legal(a))return false;
  if(_checkers){Checkers trial=*_checkers;if(!trial.play(a,b))return false;}
  if(_artillery){ArtilleryTable trial=*_artillery;if(!trial.fire(_session.slot(),a,b,c))return false;}
  if(_session.isHost())return commit(0,a,b,c);
  uint8_t data[12];data[0]=_session.slot();data[1]=a;data[2]=b;data[3]=c;Codec::put(data+4,hash(),4);Codec::put(data+8,_step,4);
  if(!out.push(PROPOSE,data,sizeof(data)))return false;_proposal[0]=a;_proposal[1]=b;_proposal[2]=c;_submitted=true;return true;
 }
 void poll(uint32_t now)override{if(_animation&&int32_t(now-_nextFrame)>=0){_nextFrame=now+35;_animation->step();if(!_animation->flying()){delete _animation;_animation=nullptr;}}
  if(_deferred&&idle()){auto message=_held;_deferred=false;if(!proposal(message,_heldActor))_error=true;}
 }
 void receive(const Message& m,unsigned actor)override{if(_error)return;
  if(_session.isHost()&&m.type==PROPOSE&&actor){if(m.size!=12||actor!=turn()||finished()||Codec::get(m.data+4,4)!=hash()||Codec::get(m.data+8,4)!=_step){_error=true;return;}
   if(!idle()){if(_deferred){_error=true;return;}_held=m;_heldActor=actor;_deferred=true;}else if(!proposal(m,actor))_error=true;return;}
  if(!_session.isHost()&&actor==0&&m.type==COMMIT){if(m.size!=16||m.data[0]!=turn()||Codec::get(m.data+4,4)!=hash()||Codec::get(m.data+8,4)!=uint64_t(_step)+1||(_submitted&&m.data[0]==_session.slot()&&memcmp(_proposal,m.data+1,3))||!apply(m.data[1],m.data[2],m.data[3])||Codec::get(m.data+12,4)!=hash()){_error=true;return;}
   ++_step;_submitted=false;return;}
  _error=true;
 }
};
class PokerRules : public Rules {
 PokerTable* _table=nullptr;PokerView _view;
 uint32_t _revision=0,_receivingRevision=0;uint8_t _fragments[PokerView::SIZE]={},_parts=0,_next[4]={};
 bool _submitted=false,_request=false,_held=false;Message _heldMessage;unsigned _heldActor=0;
 bool viewsPending()const{for(unsigned i=1;i<_session.count();++i)if(_next[i]<2)return true;return false;}
 void refresh(){++_revision;for(unsigned i=1;i<_session.count();++i)_next[i]=0;_view.capture(*_table,0);}
 void deal(){uint8_t deck[52];if(!shuffledDeck(_security,deck)||!_table->deal(deck))_error=true;else refresh();clean(deck,sizeof(deck));}
 bool apply(const Message& m,unsigned actor){if(m.size!=7||Codec::get(m.data,4)!=_revision||actor!=_table->active()||!_table->act(actor,m.data[4],Codec::get(m.data+5,2)))return false;refresh();return true;}
public:
 PokerRules(Session& session,Security& security):Rules(session,security){memset(_next,2,sizeof(_next));if(session.isHost()){_table=new(std::nothrow) PokerTable;if(!_table){_error=true;return;}_table->reset(session.count());deal();}}
 ~PokerRules()override{delete _table;clean(_fragments,sizeof(_fragments));}
 const PokerView* poker()const override{return &_view;}unsigned turn()const override{return _view.active;}unsigned revision()const override{return _revision;}bool submitted()const override{return _submitted;}
 bool finished()const override{if(_view.phase!=PokerTable::HAND_OVER)return false;unsigned live=0;for(auto n:_view.chips)live+=n!=0;return live<2;}
 unsigned winnerMask()const override{if(!finished())return _view.winners;unsigned mask=0;for(unsigned i=0;i<_session.count();++i)if(_view.chips[i])mask|=1U<<i;return mask;}
 bool action(unsigned action,unsigned target,unsigned unused)override{if(_error||_submitted||!idle()||_view.phase<PokerTable::PREFLOP||_view.phase>PokerTable::RIVER||_view.active!=_session.slot())return false;
  uint8_t data[7];Codec::put(data,_revision,4);data[4]=action;Codec::put(data+5,target,2);
  if(_session.isHost()){if(viewsPending()||!_table->act(0,action,target))return false;refresh();return true;}
  if(!out.push(POKER_ACTION,data,sizeof(data)))return false;_submitted=true;return true;
 }
 bool nextHand()override{if(_view.phase!=PokerTable::HAND_OVER||finished()||_submitted)return false;if(_session.isHost()){_request=true;return true;}
  uint8_t data[2];Codec::put(data,_view.hand,2);if(!out.push(POKER_NEXT,data,2))return false;_submitted=true;return true;
 }
 void poll(uint32_t now)override{if(_error||!_table)return;
  if(!viewsPending()&&idle()){
   if(_held){auto m=_heldMessage;_held=false;if(!apply(m,_heldActor))_error=true;}
   else if(_request){_request=false;if(_table->phase()==PokerTable::HAND_OVER&&!_table->finished())deal();}
  }
  for(unsigned slot=1;slot<_session.count()&&out.room();++slot)if(_next[slot]<2){PokerView view;uint8_t bytes[PokerView::SIZE],frame[32];view.capture(*_table,slot);view.encode(bytes);unsigned part=_next[slot],size=part?PokerView::SIZE-28:28;Codec::put(frame,_revision,4);memcpy(frame+4,bytes+(part?28:0),size);
    if(out.push(POKER_VIEW0+part,frame,size+4,slot,true))++_next[slot];}
 }
 void receive(const Message& m,unsigned actor)override{if(_error)return;
  if(_table){
   if(m.type==POKER_NEXT){if(m.size!=2){_error=true;return;}if(Codec::get(m.data,2)==_table->hand()&&_table->phase()==PokerTable::HAND_OVER&&!_table->finished())_request=true;return;}
   if(m.type!=POKER_ACTION||!actor||actor>=_session.count()||m.size!=7||actor!=_table->active()||Codec::get(m.data,4)!=_revision){_error=true;return;}
   if(!idle()||viewsPending()){if(_held){_error=true;return;}_heldMessage=m;_heldActor=actor;_held=true;}else if(!apply(m,actor))_error=true;return;
  }
  if(actor!=0||(m.type!=POKER_VIEW0&&m.type!=POKER_VIEW1)||m.size!=(m.type==POKER_VIEW0?32:19)){_error=true;return;}
  uint32_t revision=Codec::get(m.data,4);if(revision<=_revision)return;if(revision!=_revision+1){_error=true;return;}
  if(revision!=_receivingRevision){_receivingRevision=revision;_parts=0;}
  unsigned part=m.type-POKER_VIEW0;memcpy(_fragments+(part?28:0),m.data+4,m.size-4);_parts|=1U<<part;
  if(_parts==3){if(!_view.decode(_fragments,_session.slot())||_view.count!=_session.count()){_error=true;return;}_revision=revision;_parts=0;_submitted=false;}
 }
};
class FleetRules : public Rules {
 FleetDuel _fleet;
public:
 FleetRules(Session& session,Security& security):Rules(session,security),_fleet(out,security,session.id(),session.matchRound(),session.slot()){}
 FleetDuel* fleet()override{return &_fleet;}void poll(uint32_t)override{}
 void receive(const Message& m,unsigned actor)override{_fleet.receive(m,actor);}
 bool finished()const override{return _fleet.finished();}unsigned winnerMask()const override{return _fleet.winnerMask();}
 bool error()const override{return _error||_fleet.error();}
 unsigned turn()const override{return _fleet.turn();}
};
class ArenaRules : public Rules {
 ArenaDuel _arena;
public:
 ArenaRules(Session& session,Security& security):Rules(session,security),_arena(out,security,session.id(),session.matchRound(),session.slot()){}
 ArenaDuel* arena()override{return &_arena;}void poll(uint32_t)override{}
 void receive(const Message& m,unsigned actor)override{_arena.receive(m,actor);}
 bool finished()const override{return _arena.finished();}unsigned winnerMask()const override{auto b=_arena.battle();unsigned result=b?b->result():0;return result==1?1:result==2?2:0;}
 bool error()const override{return _error||_arena.error();}
};
class RaceRules : public Rules {
 SnakeRace _race;
public:
 RaceRules(Session& session,Security& security,uint32_t now):Rules(session,security),_race(out,session.id(),session.matchRound(),session.slot(),session.count(),now){}
 SnakeRace* race()override{return &_race;}void poll(uint32_t now)override{_race.poll(now);}
 void receive(const Message& m,unsigned actor)override{_race.receive(m,actor);}
 bool finished()const override{return _race.finished();}unsigned winnerMask()const override{return _race.winnerMask();}
 bool error()const override{return _error||_race.error();}
};
inline Rules* createRules(Session& session,Security& security,uint32_t now){switch(session.game()){
 case TTT:case CHECKERS:case ARTILLERY:return new(std::nothrow) BoardRules(session,security);
 case POKER:return new(std::nothrow) PokerRules(session,security);
 case BATTLESHIP:return new(std::nothrow) FleetRules(session,security);
 case ARENA:return new(std::nothrow) ArenaRules(session,security);
 case SNAKE:return new(std::nothrow) RaceRules(session,security,now);
 default:return nullptr;
}}
} }
