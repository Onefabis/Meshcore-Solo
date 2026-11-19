#pragma once
#include <games/Battleship.h>

class BattleshipScreen {
  UITask* _task;
  solo_games::Battleship _game;
  enum View:uint8_t { SETUP, PLACING, PLAY, PAUSE, RESULT };
  View _view=SETUP;
  uint8_t _row=0,_cursor=0;
  bool _vertical=false,_own=false,_exit=false;
  uint32_t _seed,_cpu_due=0;
  const char* _message="Place fleet";
  static void text(DisplayDriver& d,int x,int y,const char* s){d.setCursor(x,y);d.print(s);}
  void reset(){_game.reset(++_seed);_view=SETUP;_row=0;_cursor=0;_vertical=_own=false;_message="Place fleet";}
  void report(solo_games::Battleship::Shot shot,bool cpu) {
    using Game=solo_games::Battleship;
    if(shot==Game::INVALID)return;
    _message=shot==Game::WIN?(cpu?"CPU wins":"You win!"):shot==Game::SUNK?(cpu?"CPU sunk":"Ship sunk"):
      shot==Game::HIT?(cpu?"CPU hit":"Hit!"):(cpu?"CPU miss":"Miss");
    if(shot>=Game::HIT)_task->playGameSound(shot==Game::WIN);
    if(_game.result())_view=RESULT;
  }
  void grid(DisplayDriver& d) {
    bool own=_view==SETUP || _view==PLACING || _own;
    d.drawRect(1,12,52,52);
    for(uint8_t cell=0;cell<100;++cell) {
      int x=2+(cell%10)*5,y=13+(cell/10)*5;
      d.fillRect(x,y,1,1);
      uint8_t shot=_game.shotAt(own?1:0,cell);
      if(own && _game.shipAt(0,cell))d.fillRect(x+1,y+1,3,3);
      if(!own && _game.result() && _game.shipAt(1,cell))d.drawRect(x+1,y+1,3,3);
      if(shot==1)d.fillRect(x+2,y+2,1,1);
      if(shot==2){
        if(own){d.setColor(UIColor::window_bkg);d.fillRect(x+1,y+1,3,3);d.setColor(UIColor::primary_txt);}
        d.fillRect(x+1,y+1,1,1);d.fillRect(x+2,y+2,1,1);d.fillRect(x+3,y+3,1,1);d.fillRect(x+3,y+1,1,1);d.fillRect(x+1,y+3,1,1);
      }
      if(shot==3){
        if(own){d.setColor(UIColor::window_bkg);d.fillRect(x+1,y+1,3,3);d.setColor(UIColor::primary_txt);d.drawRect(x+1,y+1,3,3);}
        else d.fillRect(x+1,y+1,3,3);
      }
    }
    if(_view==PLACING) {
      unsigned length=solo_games::Battleship::shipLength(_game.placed(0));
      for(unsigned i=0;i<length;++i) {
        unsigned row=_cursor/10+(_vertical?i:0),col=_cursor%10+(_vertical?0:i);
        if(row<10 && col<10)d.drawRect(2+col*5,13+row*5,5,5);
      }
    } else if(_view==PLAY && !_own)d.drawRect(2+(_cursor%10)*5,13+(_cursor/10)*5,5,5);
  }
public:
  BattleshipScreen(UITask* task,uint32_t seed):_task(task),_seed(seed){reset();}
  bool wantsExit()const{return _exit;}
  uint8_t view()const{return _view;}
  uint8_t cursor()const{return _cursor;}
  bool vertical()const{return _vertical;}
  bool ownView()const{return _own;}
  const solo_games::Battleship& game()const{return _game;}
  void poll() {
    if(_view==PLAY && _game.turn()==1 && int32_t(millis()-_cpu_due)>=0) {
      uint8_t cell=_game.chooseShot(1);report(_game.fire(1,cell),true);
    }
  }
  bool handleInput(char input) {
    uint8_t key=uint8_t(input);
    if(key==KEY_CANCEL) {
      if(_view==SETUP || _view==RESULT)_exit=true;
      else if(_view==PLACING){_view=SETUP;_row=0;}
      else if(_view==PLAY){_view=PAUSE;_row=0;}
      else _view=PLAY;
      return true;
    }
    if(_view==SETUP) {
      if(key==KEY_UP)_row=(_row+2)%3;
      if(key==KEY_DOWN)_row=(_row+1)%3;
      if(key==KEY_ENTER) {
        if(_row==0){if(_game.placed(0)==5)_game.clearFleet(0);_view=PLACING;_cursor=0;_vertical=false;}
        else if(_row==1){_game.autoPlace(0);_message="Fleet ready";_row=2;}
        else if(_game.start()){_view=PLAY;_cursor=0;_own=false;_message="Pick target";}
        else _task->showAlert("Place all five ships",1200);
      }
    } else if(_view==PAUSE) {
      if(key==KEY_UP)_row=(_row+2)%3;
      if(key==KEY_DOWN)_row=(_row+1)%3;
      if(key==KEY_ENTER){if(_row==0)_view=PLAY;else if(_row==1)reset();else _exit=true;}
    } else if(_view==RESULT) {
      if(key==KEY_ENTER)reset();
      if(key==KEY_CONTEXT_MENU)_own=!_own;
    } else {
      if(key==KEY_LEFT && _cursor%10)--_cursor;
      if(key==KEY_RIGHT && _cursor%10<9)++_cursor;
      if(key==KEY_UP && _cursor>=10)_cursor-=10;
      if(key==KEY_DOWN && _cursor<90)_cursor+=10;
      if(key==KEY_CONTEXT_MENU){if(_view==PLACING)_vertical=!_vertical;else _own=!_own;}
      if(key==KEY_ENTER) {
        if(_view==PLACING) {
          if(!_game.place(0,_game.placed(0),_cursor,_vertical))_task->showAlert("Cannot place there",1000);
          if(_game.placed(0)==5){_view=SETUP;_row=2;_message="Fleet ready";}
        } else if(_own)_own=false;
        else if(_game.turn()==0) {
          auto shot=_game.fire(0,_cursor);report(shot,false);
          if(shot!=solo_games::Battleship::INVALID)_cpu_due=millis()+600;
        }
      }
    }
    return true;
  }
  int render(DisplayDriver& d) {
    d.setTextSize(1);d.setColor(UIColor::primary_txt);
    if(_view==PAUSE) {
      d.drawCenteredHeader("BATTLESHIP PAUSED");
      text(d,2,16,_row==0?"> Resume":"  Resume");text(d,2,32,_row==1?"> Restart":"  Restart");
      text(d,2,48,_row==2?"> Exit Battleship":"  Exit Battleship");return 300;
    }
    d.drawCenteredHeader(_view==SETUP || _view==PLACING?"BATTLESHIP":_own?"YOUR FLEET":"TARGET GRID");
    grid(d);
    if(_view==SETUP) {
      text(d,58,16,_row==0?"> Manual":"  Manual");text(d,58,29,_row==1?"> Auto":"  Auto");text(d,58,42,_row==2?"> Start":"  Start");
      char count[16];snprintf(count,sizeof(count),"%u/5 ships",_game.placed(0));text(d,58,55,count);
    } else if(_view==PLACING) {
      char ship[16];snprintf(ship,sizeof(ship),"Ship %u/5",_game.placed(0)+1);text(d,58,16,ship);
      char length[16];snprintf(length,sizeof(length),"%s len %u",_vertical?"Ver":"Hor",solo_games::Battleship::shipLength(_game.placed(0)));text(d,58,31,length);
      text(d,58,46,"Hold: turn");
    } else {
      text(d,58,16,_game.result()?(_game.result()==1?"You win!":"CPU wins"):_game.turn()==0?"Your turn":"CPU turn");
      text(d,58,29,_message);
      char aim[16];snprintf(aim,sizeof(aim),"Aim: %c%u",'A'+_cursor/10,1+_cursor%10);text(d,58,42,aim);
      text(d,58,55,_view==RESULT?"Press: new":_own?"Press: aim":"Hold: fleet");
    }
    return _view==PLAY?100:300;
  }
};
static_assert(sizeof(BattleshipScreen)<=464,"Visible Battleship screen must stay bounded");
