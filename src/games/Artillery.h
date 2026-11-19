#pragma once
#include <stdint.h>

namespace solo_games {

class Artillery {
public:
  enum Flight:uint8_t { IDLE, FLYING, IMPACT, MISSED };
  void reset(uint32_t seed) {
    _rng=seed?seed:1;_health[0]=_health[1]=100;_heavy[0]=_heavy[1]=3;
    _turn=_result=_shots=_weapon=0;_flying=false;_impact_x=_impact_y=-1;
    _x=_y=_vx=_vy=0;_steps=0;
    int height=49;
    for(unsigned x=0;x<128;++x){height+=int(random()%5)-2;if(height<40)height=40;if(height>60)height=60;_terrain[x]=height;}
    _wind=int(random()%5)-2;
  }
  static int sin1024(unsigned angle) {
    static const int16_t values[]={0,89,178,265,350,433,512,587,658,724,784,839,887,928,962,989,1008,1020,1024};
    return values[angle/5];
  }
  bool fire(uint8_t side,uint8_t angle,uint8_t power,uint8_t weapon) {
    if(_result || _flying || side!=_turn || side>1 || angle<10 || angle>80 || angle%5 || power<20 || power>100 || power%5 || weapon>1 || (weapon && !_heavy[side]))return false;
    _weapon=weapon;if(weapon)--_heavy[side];
    int direction=side? -1:1;
    _x=(tankX(side)+direction*3)*256;_y=(terrain(tankX(side))-4)*256;
    _vx=direction*sin1024(90-angle)*power*9/1024;_vy=-sin1024(angle)*power*9/1024;
    _steps=0;_flying=true;_impact_x=_impact_y=-1;return true;
  }
  Flight step() {
    if(!_flying)return IDLE;
    _x+=_vx;_y+=_vy;_vx+=_wind;_vy+=24;++_steps;
    int x=_x/256,y=_y/256;
    if(x<0 || x>=128 || _steps>=256){finish();return MISSED;}
    bool hit=false;
    if(_steps>2)for(unsigned side=0;side<2;++side)if(absolute(x-tankX(side))<=2 && y>=terrain(tankX(side))-4 && y<terrain(tankX(side)))hit=true;
    if(y>=terrain(x) || hit){explode(x,y);finish();return IMPACT;}
    return FLYING;
  }
  uint8_t terrain(unsigned x)const{return x<128?_terrain[x]:63;}
  static int tankX(unsigned side){return side?111:16;}
  uint8_t health(unsigned side)const{return side<2?_health[side]:0;}
  uint8_t heavy(unsigned side)const{return side<2?_heavy[side]:0;}
  int wind()const{return _wind;}
  uint8_t turn()const{return _turn;}
  uint8_t result()const{return _result;} // 1 human, 2 CPU, 3 draw
  uint8_t shots()const{return _shots;}
  bool flying()const{return _flying;}
  int projectileX()const{return _x/256;}
  int projectileY()const{return _y/256;}
  int impactX()const{return _impact_x;}
  int impactY()const{return _impact_y;}
  uint8_t weapon()const{return _weapon;}
private:
  friend struct ArtilleryFixture;
  uint8_t _terrain[128]={},_health[2]={100,100},_heavy[2]={3,3};
  uint32_t _rng=1;
  int32_t _x=0,_y=0,_vx=0,_vy=0;
  int16_t _impact_x=-1,_impact_y=-1;
  uint16_t _steps=0;
  int8_t _wind=0;
  uint8_t _turn=0,_result=0,_shots=0,_weapon=0;
  bool _flying=false;
  static int absolute(int n){return n<0?-n:n;}
  uint32_t random(){_rng^=_rng<<13;_rng^=_rng>>17;_rng^=_rng<<5;return _rng;}
  void damage(unsigned side,int amount){_health[side]=amount>=_health[side]?0:_health[side]-amount;}
  void explode(int x,int y) {
    if(y>63)y=63;
    _impact_x=x;_impact_y=y;
    int radius=_weapon?8:5,reach=radius+4;
    uint8_t old[2]={terrain(tankX(0)),terrain(tankX(1))};
    for(unsigned side=0;side<2;++side){int distance=absolute(tankX(side)-x)+absolute(int(old[side])-2-y);if(distance<reach)damage(side,(reach-distance)*(_weapon?7:6));}
    for(int dx=-radius;dx<=radius;++dx)if(x+dx>=0 && x+dx<128) {
      int square=radius*radius-dx*dx,depth=0;while((depth+1)*(depth+1)<=square)++depth;
      int bottom=y+depth;if(bottom>63)bottom=63;
      if(bottom>_terrain[x+dx])_terrain[x+dx]=bottom;
    }
    for(unsigned side=0;side<2;++side)damage(side,(terrain(tankX(side))-old[side])*3);
  }
  void finish() {
    _flying=false;++_shots;
    if(!_health[0] && !_health[1])_result=3;
    else if(!_health[0])_result=2;
    else if(!_health[1])_result=1;
    else if(_shots>=40)_result=3;
    _turn=1-_turn;_wind=int(random()%5)-2;
  }
};

// A single candidate trajectory per poll. Public terrain/wind/health are the
// only inputs; the live game is never changed while planning.
class ArtilleryAI {
public:
  void begin(const Artillery& game,bool normal) {
    _position=game;_increment=normal?5:10;_angle=10;_power=20;_weapon=0;
    _best_score=-30000;_best_angle=45;_best_power=85;_best_weapon=0;_busy=true;
  }
  bool step(unsigned budget=1) {
    while(_busy && budget--) {
      Artillery trial=_position;unsigned side=trial.turn(),enemy=1-side;
      if(trial.fire(side,_angle,_power,_weapon)) {
        while(trial.flying())trial.step();
        int distance=trial.impactX()<0?150:trial.impactX()-Artillery::tankX(enemy);if(distance<0)distance=-distance;
        int score=20*(_position.health(enemy)-trial.health(enemy))-25*(_position.health(side)-trial.health(side))-2*distance;
        if(trial.result()==side+1)score+=20000;
        if(trial.result()==enemy+1)score-=20000;
        if(score>_best_score){_best_score=score;_best_angle=_angle;_best_power=_power;_best_weapon=_weapon;}
      }
      _power+=_increment;
      if(_power>100){_power=20;_angle+=_increment;}
      if(_angle>80){_angle=10;if(++_weapon>1)_busy=false;}
    }
    return !_busy;
  }
  uint8_t angle()const{return _best_angle;}
  uint8_t power()const{return _best_power;}
  uint8_t weapon()const{return _best_weapon;}
private:
  Artillery _position;
  int _best_score=-30000;
  uint8_t _increment=5,_angle=10,_power=20,_weapon=0,_best_angle=45,_best_power=85,_best_weapon=0;
  bool _busy=false;
};
static_assert(sizeof(Artillery)+sizeof(ArtilleryAI)<=416,"Artillery state and planner must remain small");
} // namespace solo_games
