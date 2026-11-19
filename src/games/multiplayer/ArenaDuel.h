#pragma once
#include "Messages.h"
#include "SecureChannel.h"
#include "../veylith/Battle.h"
namespace solo_games { namespace multiplayer {
class ArenaTeamCodec {
public:
 static constexpr unsigned SIZE=34;
 static bool valid(const veylith::Build team[3]){for(unsigned i=0;i<3;++i){auto& b=team[i];if(b.family>=20||b.training.total()>294)return false;for(auto rank:b.training.rank)if(rank>80)return false;
   for(unsigned slot=0;slot<4;++slot){if(!b.training.eligible(b.family,b.training.equipped[slot]))return false;for(unsigned earlier=0;earlier<slot;++earlier)if(b.training.equipped[slot]==b.training.equipped[earlier])return false;}
   for(unsigned j=0;j<i;++j)if(b.family==team[j].family)return false;
  }return true;}
 static bool encode(const veylith::Build team[3],uint8_t out[SIZE]){if(!valid(team))return false;uint8_t* p=out;*p++=2;for(unsigned i=0;i<3;++i){*p++=team[i].family;memcpy(p,team[i].training.rank,6);p+=6;memcpy(p,team[i].training.equipped,4);p+=4;}return true;}
 static bool decode(const uint8_t input[SIZE],veylith::Build team[3]){if(input[0]!=2)return false;veylith::Build next[3];const uint8_t* p=input+1;for(unsigned i=0;i<3;++i){next[i].family=*p++;memcpy(next[i].training.rank,p,6);p+=6;memcpy(next[i].training.equipped,p,4);p+=4;}if(!valid(next))return false;for(unsigned i=0;i<3;++i)team[i]=next[i];return true;}
};
class ArenaDuel {
 Outbox& _out;Security& _security;uint64_t _session;uint16_t _round;
 veylith::Build _teams[2][3];veylith::Battle _battle;veylith::Action _actions[2];
 uint8_t _slot=0,_roster[ArenaTeamCodec::SIZE]={},_rosterParts=0;
 uint8_t _seedNonce[2][16]={},_seedCommit[2][32]={},_actionNonce[2][16]={},_actionCommit[2][32]={};
 bool _team[2]={},_hasSeedCommit[2]={},_seedRevealed[2]={},_hasCommit[2]={},_revealed[2]={};
 bool _active=false,_error=false,_waitingHash=false,_synced=false;
 uint8_t _resolvedRound=0;uint16_t _revision=0;
 void seedHash(unsigned slot,const uint8_t nonce[16],uint8_t out[32])const{uint8_t bytes[39]={};memcpy(bytes,"ArenaSeed-v2",12);Codec::put(bytes+12,_session,8);Codec::put(bytes+20,_round,2);bytes[22]=slot;memcpy(bytes+23,nonce,16);digest(bytes,sizeof(bytes),out);}
 void actionHash(unsigned slot,const veylith::Action& action,const uint8_t nonce[16],unsigned turn,uint8_t out[32])const{uint8_t bytes[43]={};memcpy(bytes,"ArenaTurn-v2",12);Codec::put(bytes+12,_session,8);Codec::put(bytes+20,_round,2);bytes[22]=turn;bytes[23]=slot;
   bytes[24]=action.switching?0:action.move;bytes[25]=action.target;bytes[26]=action.switching;memcpy(bytes+27,nonce,16);digest(bytes,sizeof(bytes),out);}
 void pump(){if(_error)return;unsigned other=1-_slot;
  if(!_active){
   if(_team[0]&&_team[1]&&_hasSeedCommit[0]&&_hasSeedCommit[1]&&!_seedRevealed[_slot]){
    if(!_out.push(ARENA_SEED_REVEAL,_seedNonce[_slot],16,255,true)){_error=true;return;}_seedRevealed[_slot]=true;
   }
   if(_seedRevealed[0]&&_seedRevealed[1]){uint8_t bytes[42],hash[32];memcpy(bytes,_seedNonce,32);Codec::put(bytes+32,_session,8);Codec::put(bytes+40,_round,2);digest(bytes,sizeof(bytes),hash);uint32_t seed=Codec::get(hash,4);_battle.begin(_teams[0],_teams[1],seed?seed:1);_active=true;_synced=true;}
   return;
  }
  if(_waitingHash||_battle.result())return;
  if(_hasCommit[_slot]&&_hasCommit[other]&&!_revealed[_slot]){uint8_t bytes[20];bytes[0]=_battle.roundNumber();bytes[1]=_actions[_slot].switching?0:_actions[_slot].move;bytes[2]=_actions[_slot].target;bytes[3]=_actions[_slot].switching;memcpy(bytes+4,_actionNonce[_slot],16);
   if(!_out.push(ARENA_ACTION_REVEAL,bytes,sizeof(bytes))){_error=true;return;}_revealed[_slot]=true;}
  if(_revealed[0]&&_revealed[1]){_resolvedRound=_battle.roundNumber();
   if(!_battle.resolve(_actions[0],_actions[1])){_error=true;return;}
   uint8_t bytes[5];bytes[0]=_resolvedRound;Codec::put(bytes+1,_battle.stateHash(),4);if(!_out.push(ARENA_STATE,bytes,sizeof(bytes))){_error=true;return;}
   ++_revision;_waitingHash=true;_synced=false;
  }
 }
public:
 ArenaDuel(Outbox& out,Security& security,uint64_t session,unsigned round,unsigned slot):_out(out),_security(security),_session(session),_round(round),_slot(slot){}
 unsigned slot()const{return _slot;}bool error()const{return _error;}bool active()const{return _active;}bool synced()const{return _synced;}unsigned revision()const{return _revision;}
 bool teamReady(unsigned slot)const{return slot<2&&_team[slot];}
 const veylith::Build* team(unsigned slot)const{return _teams[slot];}
 veylith::Battle* battle(){return _active?&_battle:nullptr;}
 const veylith::Battle* battle()const{return _active?&_battle:nullptr;}
 bool finished()const{return _active&&_synced&&!_error&&_battle.result();}
 bool waiting()const{return _waitingHash||_hasCommit[_slot]||!_active;}
 bool setTeam(const veylith::Build team[3]){if(_team[_slot]||_error||_out.room()<3)return false;uint8_t bytes[ArenaTeamCodec::SIZE];if(!ArenaTeamCodec::encode(team,bytes))return false;
  if(!_security.random(_seedNonce[_slot],16)){_error=true;return false;}seedHash(_slot,_seedNonce[_slot],_seedCommit[_slot]);
  _out.push(ARENA_ROSTER0,bytes,32,255,true);_out.push(ARENA_ROSTER1,bytes+32,2,255,true);_out.push(ARENA_SEED_COMMIT,_seedCommit[_slot],32,255,true);
  for(unsigned i=0;i<3;++i)_teams[_slot][i]=team[i];_team[_slot]=true;_hasSeedCommit[_slot]=true;pump();return true;
 }
 bool choose(const veylith::Action& action){if(!_active||_error||!_synced||_waitingHash||_hasCommit[_slot]||_battle.result()||!_battle.legal(_slot,action)||!_out.room())return false;
  if(!_security.random(_actionNonce[_slot],16)){_error=true;return false;}_actions[_slot]=action;actionHash(_slot,action,_actionNonce[_slot],_battle.roundNumber(),_actionCommit[_slot]);
  if(!_out.push(ARENA_ACTION_COMMIT,_actionCommit[_slot],32))return false;_hasCommit[_slot]=true;pump();return true;
 }
 bool forceSwitch(unsigned target){if(!_active||_error||!_synced||_waitingHash||!_battle.needsSwitch(_slot)||!_out.room())return false;uint8_t bytes[]={uint8_t(_battle.roundNumber()),uint8_t(target)};
  if(!_battle.forceSwitch(_slot,target))return false;if(!_out.push(ARENA_FORCE,bytes,2)){_error=true;return false;}return true;
 }
 void receive(const Message& m,unsigned actor){if(_error||actor>1||actor==_slot)return;
  if(m.type==ARENA_ROSTER0||m.type==ARENA_ROSTER1){unsigned part=m.type-ARENA_ROSTER0;if(_active||_team[actor]||m.size!=(part?2:32)){_error=true;return;}memcpy(_roster+(part?32:0),m.data,m.size);_rosterParts|=1U<<part;
   if(_rosterParts==3){if(!ArenaTeamCodec::decode(_roster,_teams[actor])){_error=true;return;}_team[actor]=true;}pump();return;
  }
  if(m.type==ARENA_SEED_COMMIT){if(m.size!=32||_hasSeedCommit[actor]){_error=true;return;}memcpy(_seedCommit[actor],m.data,32);_hasSeedCommit[actor]=true;pump();return;}
  if(m.type==ARENA_SEED_REVEAL){if(m.size!=16||!_hasSeedCommit[0]||!_hasSeedCommit[1]||_seedRevealed[actor]){_error=true;return;}uint8_t hash[32];seedHash(actor,m.data,hash);if(memcmp(hash,_seedCommit[actor],32)){_error=true;return;}memcpy(_seedNonce[actor],m.data,16);_seedRevealed[actor]=true;pump();return;}
  if(!_active){_error=true;return;}
  if(m.type==ARENA_FORCE){if(m.size!=2||_waitingHash||m.data[0]!=_battle.roundNumber()||!_battle.forceSwitch(actor,m.data[1]))_error=true;return;}
  if(m.type==ARENA_ACTION_COMMIT){if(m.size!=32||_hasCommit[actor]||_waitingHash||_battle.result()){_error=true;return;}memcpy(_actionCommit[actor],m.data,32);_hasCommit[actor]=true;pump();return;}
  if(m.type==ARENA_ACTION_REVEAL){if(m.size!=20||!_hasCommit[0]||!_hasCommit[1]||_revealed[actor]||m.data[0]!=_battle.roundNumber()||m.data[3]>1||m.data[2]>=3||(m.data[3]?m.data[1]!=0:m.data[1]<1||m.data[1]>80)){_error=true;return;}
   veylith::Action action;action.move=m.data[1];action.target=m.data[2];action.switching=m.data[3];uint8_t hash[32];actionHash(actor,action,m.data+4,m.data[0],hash);
   if(memcmp(hash,_actionCommit[actor],32)||!_battle.legal(actor,action)){_error=true;return;}_actions[actor]=action;_revealed[actor]=true;pump();return;
  }
  if(m.type==ARENA_STATE){if(m.size!=5||!_waitingHash||m.data[0]!=_resolvedRound||Codec::get(m.data+1,4)!=_battle.stateHash()){_error=true;return;}
   _waitingHash=false;_synced=true;memset(_hasCommit,0,sizeof(_hasCommit));memset(_revealed,0,sizeof(_revealed));return;
  }
  _error=true;
 }
};
} }
