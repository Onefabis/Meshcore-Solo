#pragma once
#include <stdint.h>

namespace solo_games {

class Battleship {
public:
  enum Shot : uint8_t { INVALID, MISS, HIT, SUNK, WIN };
  static uint8_t shipLength(uint8_t ship) { const uint8_t lengths[]={5,4,3,3,2};return ship<5?lengths[ship]:0; }
  void reset(uint32_t seed) {
    _rng=seed?seed:1;_started=false;_turn=0;_result=0;
    for(unsigned side=0;side<2;++side){_placed[side]=0;for(unsigned cell=0;cell<100;++cell)_fleet[side][cell]=_shots[side][cell]=0;}
    autoPlace(1);
  }
  bool clearFleet(uint8_t side) {
    if(_started || side>1)return false;
    for(auto& cell:_fleet[side])cell=0;_placed[side]=0;return true;
  }
  bool canPlace(uint8_t side,uint8_t ship,uint8_t cell,bool vertical) const {
    if(_started || side>1 || ship>=5 || ship!=_placed[side] || cell>=100)return false;
    uint8_t length=shipLength(ship);
    if(vertical?cell/10+length>10:cell%10+length>10)return false;
    for(unsigned i=0;i<length;++i)if(_fleet[side][cell+i*(vertical?10:1)])return false;
    return true;
  }
  bool place(uint8_t side,uint8_t ship,uint8_t cell,bool vertical) {
    if(!canPlace(side,ship,cell,vertical))return false;
    for(unsigned i=0;i<shipLength(ship);++i)_fleet[side][cell+i*(vertical?10:1)]=ship+1;
    ++_placed[side];return true;
  }
  bool autoPlace(uint8_t side) {
    if(!clearFleet(side))return false;
    bool complete=true;
    for(uint8_t ship=0;ship<5;++ship) {
      bool done=false;
      for(unsigned attempt=0;attempt<128 && !done;++attempt) {
        uint8_t cell=random()%100;bool vertical=random()&1;
        done=place(side,ship,cell,vertical);
      }
      if(!done){complete=false;break;}
    }
    if(!complete){clearFleet(side);for(uint8_t ship=0;ship<5;++ship)place(side,ship,ship*10,false);}
    return _placed[side]==5;
  }
  bool start() {
    if(_started || _placed[0]!=5 || _placed[1]!=5)return false;
    _started=true;_turn=0;return true;
  }
  Shot fire(uint8_t attacker,uint8_t cell) {
    if(!_started || _result || attacker>1 || attacker!=_turn || cell>=100 || _shots[attacker][cell])return INVALID;
    uint8_t defender=1-attacker,id=_fleet[defender][cell];
    Shot result=id?HIT:MISS;_shots[attacker][cell]=id?2:1;
    if(id) {
      bool sunk=true,won=true;
      for(unsigned i=0;i<100;++i)if(_fleet[defender][i] && !_shots[attacker][i]){won=false;if(_fleet[defender][i]==id)sunk=false;}
      if(sunk){result=SUNK;for(unsigned i=0;i<100;++i)if(_fleet[defender][i]==id)_shots[attacker][i]=3;}
      if(won){_result=attacker+1;result=WIN;}
    }
    _turn=defender;return result;
  }
  // Hunt/target uses only this attacker's known hit/miss/sunk grid. It never
  // looks up unhit enemy ship positions, even though both fleets are local.
  uint8_t chooseShot(uint8_t attacker) {
    if(attacker>1 || !_started || _result || _turn!=attacker)return 255;
    for(unsigned mode=0;mode<3;++mode) {
      unsigned count=0;uint8_t choice=255;
      for(uint8_t cell=0;cell<100;++cell)if(!_shots[attacker][cell]) {
        bool target=false;
        if(cell%10 && _shots[attacker][cell-1]==2)target=true;
        if(cell%10<9 && _shots[attacker][cell+1]==2)target=true;
        if(cell>=10 && _shots[attacker][cell-10]==2)target=true;
        if(cell<90 && _shots[attacker][cell+10]==2)target=true;
        if(mode==0 && !target)continue;
        if(mode==1 && ((cell/10+cell%10)&1))continue;
        if(random()%++count==0)choice=cell;
      }
      if(count)return choice;
    }
    return 255;
  }
  uint8_t placed(uint8_t side)const{return side<2?_placed[side]:0;}
  uint8_t shipAt(uint8_t side,uint8_t cell)const{return side<2 && cell<100?_fleet[side][cell]:0;}
  uint8_t shotAt(uint8_t attacker,uint8_t cell)const{return attacker<2 && cell<100?_shots[attacker][cell]:0;}
  uint8_t turn()const{return _turn;}
  uint8_t result()const{return _result;}
  unsigned shotCount(uint8_t side)const {unsigned n=0;if(side<2)for(auto cell:_shots[side])n+=cell!=0;return n;}
private:
  friend struct BattleshipFixture;
  uint8_t _fleet[2][100]={},_shots[2][100]={},_placed[2]={};
  uint32_t _rng=1;
  uint8_t _turn=0,_result=0;
  bool _started=false;
  uint32_t random(){_rng^=_rng<<13;_rng^=_rng>>17;_rng^=_rng<<5;return _rng;}
};
static_assert(sizeof(Battleship)<=416,"Battleship state must stay bounded");
} // namespace solo_games
