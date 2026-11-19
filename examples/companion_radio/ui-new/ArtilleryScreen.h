#pragma once
#include <games/Artillery.h>

class ArtilleryScreen {
  UITask* _task;
  solo_games::Artillery _game;
  solo_games::ArtilleryAI _ai;
  enum View:uint8_t { SETUP, AIM, FLIGHT, PAUSE, RESULT };
  View _view=SETUP,_resume=AIM;
  uint8_t _row=0,_angle=45,_power=85,_weapon=0;
  bool _normal=false,_exit=false,_planning=false;
  uint32_t _seed,_next_step=0,_blast_until=0;
#ifdef ZYGIS_GAME_DIAGNOSTICS
  uint32_t _max_ai_us=0;
#endif
  static void text(DisplayDriver& d,int x,int y,const char* s){d.setCursor(x,y);d.print(s);}
  void reset(){_game.reset(++_seed);_view=SETUP;_row=0;_angle=45;_power=85;_weapon=0;_planning=false;_blast_until=0;}
  void fire(uint8_t angle,uint8_t power,uint8_t weapon) {
    if(_game.fire(_game.turn(),angle,power,weapon)){_view=FLIGHT;_next_step=millis()+35;_planning=false;}
  }
public:
  ArtilleryScreen(UITask* task,uint32_t seed):_task(task),_seed(seed){reset();}
  bool wantsExit()const{return _exit;}
  uint8_t view()const{return _view;}
  uint8_t row()const{return _row;}
  uint8_t angle()const{return _angle;}
  uint8_t power()const{return _power;}
  uint8_t weapon()const{return _weapon;}
#ifdef ZYGIS_GAME_DIAGNOSTICS
  uint32_t maxAiMicros()const{return _max_ai_us;}
#endif
  const solo_games::Artillery& game()const{return _game;}
  void poll() {
    uint32_t now=millis();
    if(_view==FLIGHT && int32_t(now-_next_step)>=0) {
      _next_step=now+35;auto change=_game.step();
      if(!_game.flying()) {
        _blast_until=change==solo_games::Artillery::IMPACT?now+250:0;
        _view=_game.result()?RESULT:AIM;
        if(!_game.heavy(0))_weapon=0;
        if(change==solo_games::Artillery::IMPACT)_task->playGameSound(_game.result()!=0);
      }
    } else if(_view==AIM && _game.turn()==1 && (!_blast_until || int32_t(now-_blast_until)>=0)) {
#ifdef ZYGIS_GAME_DIAGNOSTICS
      uint32_t started=micros();
#endif
      if(!_planning){_ai.begin(_game,_normal);_planning=true;}
      if(_ai.step(1))fire(_ai.angle(),_ai.power(),_ai.weapon());
#ifdef ZYGIS_GAME_DIAGNOSTICS
      uint32_t elapsed=micros()-started;if(elapsed>_max_ai_us)_max_ai_us=elapsed;
#endif
    }
  }
  bool handleInput(char input) {
    uint8_t key=uint8_t(input);
    if(key==KEY_CANCEL) {
      if(_view==SETUP || _view==RESULT)_exit=true;
      else if(_view==PAUSE){_view=_resume;_next_step=millis()+35;}
      else {_resume=_view;_view=PAUSE;_row=0;}
      return true;
    }
    if(_view==SETUP) {
      if(key==KEY_UP || key==KEY_DOWN)_row=1-_row;
      if(_row==0 && (key==KEY_LEFT || key==KEY_RIGHT || key==KEY_PREV || key==KEY_NEXT))_normal=!_normal;
      if(key==KEY_ENTER){if(_row==0)_normal=!_normal;else{_view=AIM;_row=0;}}
    } else if(_view==PAUSE) {
      if(key==KEY_UP)_row=(_row+2)%3;
      if(key==KEY_DOWN)_row=(_row+1)%3;
      if(key==KEY_ENTER){if(_row==0){_view=_resume;_next_step=millis()+35;}else if(_row==1)reset();else _exit=true;}
    } else if(_view==RESULT) {
      if(key==KEY_ENTER)reset();
    } else if(_view==AIM && _game.turn()==0) {
      if(key==KEY_UP)_row=(_row+3)%4;
      if(key==KEY_DOWN)_row=(_row+1)%4;
      int delta=key==KEY_LEFT || key==KEY_PREV?-5:key==KEY_RIGHT || key==KEY_NEXT || key==KEY_ENTER?5:0;
      if(_row==0 && delta){int next=_angle+delta;if(next>=10 && next<=80)_angle=next;}
      if(_row==1 && delta){int next=_power+delta;if(next>=20 && next<=100)_power=next;}
      if(_row==2 && delta)_weapon=_game.heavy(0)?1-_weapon:0;
      if(_row==3 && key==KEY_ENTER)fire(_angle,_power,_weapon);
    }
    return true;
  }
  int render(DisplayDriver& d) {
    d.setTextSize(1);d.setColor(UIColor::primary_txt);
    if(_view==SETUP || _view==PAUSE) {
      d.drawCenteredHeader(_view==SETUP?"SCORCHED EARTH":"ARTILLERY PAUSED");
      if(_view==SETUP) {
        text(d,2,18,_row==0?"> CPU:":"  CPU:");text(d,50,18,_normal?"Normal":"Easy");
        text(d,2,35,_row==1?"> Start game":"  Start game");text(d,2,51,"L/R: level");
      } else {
        text(d,2,16,_row==0?"> Resume":"  Resume");text(d,2,32,_row==1?"> Restart":"  Restart");text(d,2,48,_row==2?"> Exit game":"  Exit game");
      }
      return 300;
    }
    char status[22];snprintf(status,sizeof(status),"Y%u C%u W%+d",_game.health(0),_game.health(1),_game.wind());text(d,2,0,status);
    if(_view==RESULT) {
      text(d,2,11,_game.result()==3?"DRAW":_game.result()==1?"YOU WIN":"CPU WINS");
      text(d,2,22,"Press: new");text(d,66,22,"Menu: exit");
    } else if(_game.turn()==1) {
      text(d,2,11,_view==FLIGHT?"CPU shot":"CPU aiming...");text(d,2,22,_game.weapon()?"Heavy shell":"Shell");
    } else {
      char angle[12],power[12];snprintf(angle,sizeof(angle),"%cA:%u",_row==0?'>':' ',_angle);snprintf(power,sizeof(power),"%cP:%u",_row==1?'>':' ',_power);
      text(d,2,11,angle);text(d,64,11,power);
      char weapon[14];
      if(_weapon)snprintf(weapon,sizeof(weapon),"%cHeavy(%u)",_row==2?'>':' ',_game.heavy(0));
      else snprintf(weapon,sizeof(weapon),"%cShell",_row==2?'>':' ');
      text(d,2,22,weapon);
      text(d,94,22,_view==FLIGHT?" Fly":_row==3?">Fire":" Fire");
    }
    for(unsigned x=0;x<128;++x)d.fillRect(x,_game.terrain(x),1,64-_game.terrain(x));
    for(unsigned side=0;side<2;++side) {
      int x=solo_games::Artillery::tankX(side),y=_game.terrain(x)-3,direction=side?-1:1;
      d.fillRect(x-2,y+1,5,2);d.fillRect(x-1,y,3,1);
      gfx::drawLine(d,x,y,x+direction*3,y-2);
      if(!_game.health(side)){d.setColor(UIColor::window_bkg);d.fillRect(x-1,y+1,3,1);d.setColor(UIColor::primary_txt);}
    }
    if(_game.flying()) {
      int x=_game.projectileX(),y=_game.projectileY();if(x>=0 && x<128 && y>=32 && y<64)d.fillRect(x,y,1,1);
    }
    if(_game.impactX()>=0 && int32_t(_blast_until-millis())>0) {
      int radius=_game.weapon()?8:5;
      for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx) {
        int x=_game.impactX()+dx,y=_game.impactY()+dy,square=dx*dx+dy*dy;
        if(x>=0 && x<128 && y>=32 && y<64 && square<=radius*radius && square>=(radius-1)*(radius-1))d.fillRect(x,y,1,1);
      }
    }
    return _view==FLIGHT?30:100;
  }
};
static_assert(sizeof(ArtilleryScreen)<=464,"Visible artillery screen must remain small");
