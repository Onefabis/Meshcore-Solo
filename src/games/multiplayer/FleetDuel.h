#pragma once
#include "Messages.h"
#include "SecureChannel.h"
#include "../Battleship.h"
namespace solo_games { namespace multiplayer {
class FleetDuel {
  Outbox& _out;Security& _security;uint64_t _session;uint16_t _round,_step=0;
  uint8_t _side=0,_placed=0,_turn=0,_target=255,_winner=0,_layout[10]={},_nonce[16]={},_peerCommit[32]={};
  uint8_t _fleet[100]={},_incoming[100]={},_outgoing[100]={},_cells[100]={},_answers[100]={},_attacks=0,_sunk=0;
  bool _locked=false,_haveCommit=false,_pending=false,_ending=false,_audited=false,_revealed=false,_error=false;
  static bool layout(const uint8_t positions[10],uint8_t grid[100]){memset(grid,0,100);for(unsigned ship=0;ship<5;++ship){unsigned cell=positions[ship*2],vertical=positions[ship*2+1],length=Battleship::shipLength(ship);
      if(cell>=100||vertical>1||(vertical?cell/10+length>10:cell%10+length>10))return false;
      for(unsigned i=0;i<length;++i){unsigned at=cell+i*(vertical?10:1);if(grid[at])return false;grid[at]=ship+1;}}return true;}
  void hash(unsigned side,const uint8_t positions[10],const uint8_t nonce[16],uint8_t out[32])const{uint8_t bytes[44]={};memcpy(bytes,"Fleet-1",7);Codec::put(bytes+7,_session,8);Codec::put(bytes+15,_round,2);bytes[17]=side;memcpy(bytes+18,positions,10);memcpy(bytes+28,nonce,16);digest(bytes,sizeof(bytes),out);}
  void lock(){uint8_t commitment[32];hash(_side,_layout,_nonce,commitment);if(!_out.push(FLEET_COMMIT,commitment,32,255,true))_error=true;else _locked=true;}
  void reveal(){if(_revealed)return;uint8_t bytes[26];memcpy(bytes,_layout,10);memcpy(bytes+10,_nonce,16);if(!_out.push(FLEET_REVEAL,bytes,sizeof(bytes),255,true))_error=true;else _revealed=true;}
  static unsigned result(unsigned cell,const uint8_t grid[100],uint8_t hits[100]){unsigned id=grid[cell];hits[cell]=id?2:1;if(!id)return Battleship::MISS;bool sunk=true,win=true;
    for(unsigned i=0;i<100;++i)if(grid[i]&&hits[i]!=2){win=false;if(grid[i]==id)sunk=false;}
    return win?Battleship::WIN:sunk?Battleship::SUNK:Battleship::HIT;}
public:
  FleetDuel(Outbox& out,Security& security,uint64_t session,unsigned round,unsigned side):_out(out),_security(security),_session(session),_round(round),_side(side){if(!security.random(_nonce,sizeof(_nonce)))_error=true;_turn=round%2;}
  bool error()const{return _error;}unsigned placed()const{return _placed;}unsigned turn()const{return _turn;}bool pending()const{return _pending;}
  bool ready()const{return _locked&&_haveCommit;}bool ending()const{return _ending;}bool finished()const{return _ending&&_audited&&_revealed&&!_error;}
  unsigned winnerMask()const{return finished()?(1U<<_winner):0;}
  unsigned ownAt(unsigned cell)const{return cell<100?_fleet[cell]:0;}unsigned incoming(unsigned cell)const{return cell<100?_incoming[cell]:0;}unsigned outgoing(unsigned cell)const{return cell<100?_outgoing[cell]:0;}
  bool canPlace(unsigned cell,bool vertical)const{if(_locked||_placed>=5||cell>=100)return false;unsigned length=Battleship::shipLength(_placed);if(vertical?cell/10+length>10:cell%10+length>10)return false;for(unsigned i=0;i<length;++i)if(_fleet[cell+i*(vertical?10:1)])return false;return true;}
  bool place(unsigned cell,bool vertical){if(!canPlace(cell,vertical))return false;_layout[2*_placed]=cell;_layout[2*_placed+1]=vertical;unsigned length=Battleship::shipLength(_placed);for(unsigned i=0;i<length;++i)_fleet[cell+i*(vertical?10:1)]=_placed+1;if(++_placed==5)lock();return true;}
  bool autoPlace(){if(_locked)return false;memset(_fleet,0,sizeof(_fleet));_placed=0;RandomStream rng(_security);
    while(_placed<5){bool done=false;for(unsigned tries=0;tries<1024&&!done;++tries){unsigned cell,direction;if(!rng.uniform(100,cell)||!rng.uniform(2,direction)){_error=true;return false;}done=place(cell,direction);}
      if(!done){_error=true;return false;}}
    return true;
  }
  bool fire(unsigned cell){if(!ready()||_ending||_error||_turn!=_side||_pending||cell>=100||_outgoing[cell]||!_out.room())return false;
    uint8_t bytes[3];Codec::put(bytes,_step,2);bytes[2]=cell;if(!_out.push(SHOT,bytes,3))return false;_target=cell;_pending=true;return true;}
  void receive(const Message& m,unsigned actor){if(actor==_side||actor>1||_error)return;
    if(m.type==FLEET_COMMIT){if(m.size!=32||(_haveCommit&&memcmp(_peerCommit,m.data,32))){_error=true;return;}memcpy(_peerCommit,m.data,32);_haveCommit=true;return;}
    if(m.type==SHOT){if(m.size!=3||!ready()||_ending||_turn!=actor||Codec::get(m.data,2)!=_step||m.data[2]>=100||_incoming[m.data[2]]){_error=true;return;}
      unsigned cell=m.data[2],answer=result(cell,_fleet,_incoming);uint8_t bytes[11]={};Codec::put(bytes,_step,2);bytes[2]=cell;bytes[3]=answer;
      unsigned n=6;if(answer>=Battleship::SUNK){bytes[4]=_fleet[cell];for(unsigned i=0;i<100;++i)if(_fleet[i]==bytes[4])bytes[n++]=i;bytes[5]=n-6;}
      if(!_out.push(SHOT_RESULT,bytes,n))_error=true;++_step;_turn=_side;
      if(answer==Battleship::WIN){_ending=true;_winner=actor;reveal();}return;
    }
    if(m.type==SHOT_RESULT){if(m.size<6||m.size>11||!_pending||_ending||_turn!=_side||Codec::get(m.data,2)!=_step||m.data[2]!=_target||m.data[3]<Battleship::MISS||m.data[3]>Battleship::WIN){_error=true;return;}
      unsigned answer=m.data[3],id=m.data[4],count=m.data[5];
      if((answer<Battleship::SUNK&&(id||count||m.size!=6))||(answer>=Battleship::SUNK&&(id<1||id>5||count!=Battleship::shipLength(id-1)||m.size!=6+count||(_sunk&(1U<<(id-1)))))){_error=true;return;}
      _outgoing[_target]=answer==Battleship::MISS?1:2;
      if(answer>=Battleship::SUNK){for(unsigned i=0;i<count;++i){unsigned cell=m.data[6+i];if(cell>=100||_outgoing[cell]!=2){_error=true;return;}for(unsigned j=0;j<i;++j)if(m.data[6+j]==cell){_error=true;return;}}
        for(unsigned i=0;i<count;++i)_outgoing[m.data[6+i]]=3;_sunk|=1U<<(id-1);}
      if(_attacks>=100){_error=true;return;}_cells[_attacks]=_target;_answers[_attacks++]=answer;_pending=false;++_step;_turn=actor;
      if(answer==Battleship::WIN){unsigned hits=0;for(auto cell:_outgoing)hits+=cell>=2;if(hits!=17||_sunk!=31){_error=true;return;}_ending=true;_winner=_side;reveal();}return;
    }
    if(m.type==FLEET_REVEAL){if(m.size!=26||!_ending||!_haveCommit){_error=true;return;}uint8_t commitment[32];hash(actor,m.data,m.data+10,commitment);
      if(memcmp(commitment,_peerCommit,32)){_error=true;return;}uint8_t grid[100],hits[100]={};if(!layout(m.data,grid)){_error=true;return;}
      for(unsigned i=0;i<_attacks;++i)if(result(_cells[i],grid,hits)!=_answers[i]){_error=true;return;}_audited=true;return;
    }
    _error=true;
  }
};
} }
