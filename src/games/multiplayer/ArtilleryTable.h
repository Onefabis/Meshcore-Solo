#pragma once
#include "../Artillery.h"
namespace solo_games { namespace multiplayer {
// 2-4 independent tanks, with one authoritative shot followed by local physics.
class ArtilleryTable {
  uint8_t _terrain[128]={},_health[4]={},_heavy[4]={};
  uint32_t _rng=1;int32_t _x=0,_y=0,_vx=0,_vy=0;
  uint16_t _steps=0,_shots=0;int16_t _impactX=-1,_impactY=-1;
  uint8_t _players=2,_turn=0,_weapon=0,_winner=0;int8_t _wind=0;bool _flying=false,_finished=false;
  uint32_t random(){_rng^=_rng<<13;_rng^=_rng>>17;_rng^=_rng<<5;return _rng;}
  static int absolute(int n){return n<0?-n:n;}
  void damage(unsigned side,unsigned amount){_health[side]=amount>=_health[side]?0:_health[side]-amount;}
  void finish(){_flying=false;++_shots;unsigned count=0;for(unsigned i=0;i<_players;++i)if(_health[i]){++count;_winner=1U<<i;}
    if(count<2||_shots>=40*(_players-1)){_finished=true;if(count!=1)_winner=0;}
    if(!_finished)do{_turn=(_turn+1)%_players;}while(!_health[_turn]);_wind=int(random()%5)-2;
  }
  void explode(int x,int y){if(y>63)y=63;_impactX=x;_impactY=y;unsigned radius=_weapon?8:5;int reach=radius+4;uint8_t before[4];
    for(unsigned i=0;i<_players;++i){before[i]=terrain(tankX(i));int distance=absolute(tankX(i)-x)+absolute(int(before[i])-2-y);if(distance<reach)damage(i,(reach-distance)*(_weapon?7:6));}
    for(int dx=-int(radius);dx<=int(radius);++dx)if(x+dx>=0&&x+dx<128){int square=radius*radius-dx*dx,depth=0;while((depth+1)*(depth+1)<=square)++depth;int bottom=y+depth;if(bottom>63)bottom=63;if(bottom>_terrain[x+dx])_terrain[x+dx]=bottom;}
    for(unsigned i=0;i<_players;++i)damage(i,(terrain(tankX(i))-before[i])*3);
  }
public:
  void reset(unsigned players,uint32_t seed){*this=ArtilleryTable();_players=players>=2&&players<=4?players:2;_rng=seed?seed:1;int height=49;
    for(auto& h:_terrain){height+=int(random()%5)-2;if(height<40)height=40;if(height>60)height=60;h=height;}
    for(unsigned i=0;i<_players;++i){_health[i]=100;_heavy[i]=3;}_wind=int(random()%5)-2;
  }
  int tankX(unsigned side)const{return _players==2?(side?111:16):_players==3?(side==0?16:side==1?64:111):12+int(side)*103/3;}
  bool fire(unsigned side,unsigned angle,unsigned power,unsigned weapon){if(_finished||_flying||side!=_turn||side>=_players||!_health[side]||angle<10||angle>170||angle%5||power<20||power>100||power%5||weapon>1||(weapon&&!_heavy[side]))return false;
    unsigned small=angle>90?180-angle:angle;int direction=angle>90?-1:1;_weapon=weapon;if(weapon)--_heavy[side];_x=(tankX(side)+direction*3)*256;_y=(terrain(tankX(side))-4)*256;
    _vx=direction*Artillery::sin1024(90-small)*power*9/1024;_vy=-Artillery::sin1024(small)*power*9/1024;_steps=0;_flying=true;_impactX=_impactY=-1;return true;
  }
  void step(){if(!_flying)return;_x+=_vx;_y+=_vy;_vx+=_wind;_vy+=24;++_steps;int x=_x/256,y=_y/256;
    if(x<0||x>=128||_steps>=256){finish();return;}bool hit=false;
    if(_steps>2)for(unsigned i=0;i<_players;++i)if(_health[i]&&absolute(x-tankX(i))<=2&&y>=terrain(tankX(i))-4&&y<terrain(tankX(i)))hit=true;
    if(y>=terrain(x)||hit){explode(x,y);finish();}
  }
  bool shot(unsigned side,unsigned angle,unsigned power,unsigned weapon){if(!fire(side,angle,power,weapon))return false;while(_flying)step();return true;}
  bool flying()const{return _flying;}bool finished()const{return _finished;}unsigned winnerMask()const{return _finished?_winner:0;}
  unsigned players()const{return _players;}unsigned turn()const{return _turn;}unsigned shots()const{return _shots;}
  unsigned health(unsigned i)const{return i<_players?_health[i]:0;}unsigned heavy(unsigned i)const{return i<_players?_heavy[i]:0;}
  int wind()const{return _wind;}uint8_t terrain(unsigned x)const{return x<128?_terrain[x]:63;}
  int projectileX()const{return _x/256;}int projectileY()const{return _y/256;}int impactX()const{return _impactX;}int impactY()const{return _impactY;}
  uint32_t stateHash()const{uint32_t h=2166136261U;auto add=[&](unsigned n){for(unsigned i=0;i<4;++i){h^=uint8_t(n>>(8*i));h*=16777619U;}};
    add(_players);for(auto x:_terrain)add(x);for(auto x:_health)add(x);for(auto x:_heavy)add(x);add(_rng);add(_turn);add(_wind);add(_shots);add(_winner);add(_finished);return h;}
};
} }
