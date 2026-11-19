#pragma once
#include <games/Holdem.h>

class HoldemScreen {
  UITask* _task;
  solo_games::Holdem _game;
  enum View:uint8_t { SETUP, PLAY, PAUSE, RESULT };
  View _view=SETUP;
  uint8_t _row=1;
  uint16_t _raise_to=20;
  uint32_t _seed,_cpu_due=0;
#ifdef ZYGIS_GAME_DIAGNOSTICS
  uint32_t _max_cpu_us=0;
#endif
  bool _exit=false;
  static void text(DisplayDriver& d,int x,int y,const char* s){d.setCursor(x,y);d.print(s);}
  void reset(){_game.reset(++_seed);_view=SETUP;_row=1;_raise_to=20;}
  void afterAction() {
    if(_game.phase()==solo_games::Holdem::HAND_OVER){_view=RESULT;_task->playGameSound(true);}
    else {_view=PLAY;_raise_to=_game.minRaiseTo(0);_cpu_due=millis()+600;}
  }
  void card(DisplayDriver& d,int x,int y,uint8_t value,bool face) {
    d.drawRect(x,y,22,17);
    if(!face || value>=52) {
      if(value<52)for(int row=3;row<14;row+=4)for(int col=3;col<19;col+=4)d.fillRect(x+col,y+row,2,2);
      return;
    }
    char rank[3];unsigned number=value%13+2;
    if(number<=10)snprintf(rank,sizeof(rank),"%u",number);
    else {rank[0]="JQKA"[number-11];rank[1]=0;}
    text(d,x+2,y+2,rank);
    // Original 5x5 suit glyphs, distinct without colour.
    static const uint8_t suits[4][5]={{4,14,21,31,4},{4,14,31,14,4},{10,31,31,14,4},{4,14,31,10,4}};
    for(unsigned row=0;row<5;++row)for(unsigned col=0;col<5;++col)if((suits[value/13][row]>>col)&1)d.fillRect(x+14+col,y+9+row,1,1);
  }
public:
  HoldemScreen(UITask* task,uint32_t seed):_task(task),_seed(seed){reset();}
  bool wantsExit()const{return _exit;}
  uint8_t view()const{return _view;}
  uint8_t row()const{return _row;}
#ifdef ZYGIS_GAME_DIAGNOSTICS
  uint32_t maxCpuMicros()const{return _max_cpu_us;}
#endif
  const solo_games::Holdem& game()const{return _game;}
  void poll() {
    if(_view!=PLAY || _game.active()!=1 || int32_t(millis()-_cpu_due)<0)return;
#ifdef ZYGIS_GAME_DIAGNOSTICS
    uint32_t start=micros();
#endif
    auto action=_game.chooseAction(1);_game.act(1,action.action,action.target);afterAction();
#ifdef ZYGIS_GAME_DIAGNOSTICS
    uint32_t elapsed=micros()-start;if(elapsed>_max_cpu_us)_max_cpu_us=elapsed;
#endif
  }
  bool handleInput(char input) {
    uint8_t key=uint8_t(input);
    if(key==KEY_CANCEL) {
      if(_view==SETUP || _view==RESULT)_exit=true;
      else if(_view==PLAY){_view=PAUSE;_row=0;}
      else {_view=PLAY;_row=1;_cpu_due=millis()+600;}
      return true;
    }
    if(_view==SETUP) {
      if(key==KEY_ENTER && _game.deal()){_row=1;afterAction();}
    } else if(_view==PAUSE) {
      if(key==KEY_UP)_row=(_row+2)%3;
      if(key==KEY_DOWN)_row=(_row+1)%3;
      if(key==KEY_ENTER){if(_row==0){_view=PLAY;_row=1;_cpu_due=millis()+600;}else if(_row==1)reset();else _exit=true;}
    } else if(_view==RESULT) {
      if(key==KEY_ENTER){if(_game.busted())reset();else if(_game.deal()){_row=1;afterAction();}}
    } else if(_game.active()==0) {
      unsigned count=_game.maxRaiseTo(0)?3:2;
      if(_row>=count)_row=1;
      if(key==KEY_UP)_row=(_row+count-1)%count;
      if(key==KEY_DOWN)_row=(_row+1)%count;
      if(_row==2) {
        unsigned minimum=_game.minRaiseTo(0),maximum=_game.maxRaiseTo(0);
        if(_raise_to<minimum)_raise_to=minimum;
        if(key==KEY_LEFT || key==KEY_PREV)_raise_to=_raise_to>minimum+10?_raise_to-10:minimum;
        if(key==KEY_RIGHT || key==KEY_NEXT)_raise_to=_raise_to+10<maximum?_raise_to+10:maximum;
        if(key==KEY_CONTEXT_MENU)_raise_to=maximum;
      }
      if(key==KEY_ENTER) {
        auto action=_row==0?solo_games::Holdem::FOLD:_row==1?solo_games::Holdem::CHECK_CALL:solo_games::Holdem::RAISE;
        if(_game.act(0,action,_raise_to)){_row=1;afterAction();}
      }
    }
    return true;
  }
  int render(DisplayDriver& d) {
    d.setTextSize(1);d.setColor(UIColor::primary_txt);
    if(_view==SETUP || _view==PAUSE) {
      d.drawCenteredHeader(_view==SETUP?"TEXAS HOLD'EM":"HOLDEM PAUSED");
      if(_view==SETUP){text(d,2,18,"Heads-up vs CPU");text(d,2,35,"> Start match");text(d,2,51,"500 virtual chips");}
      else {text(d,2,16,_row==0?"> Resume":"  Resume");text(d,2,32,_row==1?"> New match":"  New match");text(d,2,48,_row==2?"> Exit Holdem":"  Exit Holdem");}
      return 300;
    }
    char status[22];snprintf(status,sizeof(status),"Y%u C%u P%u",_game.chips(0),_game.chips(1),_view==RESULT?_game.wonPot():_game.pot());text(d,2,0,status);
    for(unsigned i=0;i<2;++i){card(d,2+i*24,12,_game.hole(0,i),true);card(d,78+i*24,12,_game.hole(1,i),_game.shown());}
    const char* phases[]={"","PRE","FLOP","TURN","RIV","SHOW"};
    text(d,52,16,_game.phase()==solo_games::Holdem::HAND_OVER && !_game.shown()?"FOLD":phases[_game.phase()]);
    for(unsigned i=0;i<5;++i)card(d,2+i*24,33,_game.board(i),i<_game.boardCount());
    if(_view==RESULT) {
      char outcome[18];
      if(_game.shown())snprintf(outcome,sizeof(outcome),"%s%s",_game.winner()==1?"WIN ":_game.winner()==2?"LOSE ":"SPLIT ",solo_games::PokerHand::name(_game.handRank(_game.winner()==2?1:0)));
      else snprintf(outcome,sizeof(outcome),"%s",_game.winner()==1?"CPU folded":"You folded");
      text(d,2,54,outcome);text(d,94,54,_game.busted()?"Reset":"Next");
    } else if(_game.active()==1)text(d,2,54,"CPU thinking...");
    else {
      text(d,0,54,_row==0?">Fold":" Fold");
      char call[10],raise[10];unsigned amount=_game.toCall(0);
      if(amount)snprintf(call,sizeof(call),"%cCall%u",_row==1?'>':' ',amount);
      else snprintf(call,sizeof(call),"%cCheck",_row==1?'>':' ');
      if(_game.maxRaiseTo(0))snprintf(raise,sizeof(raise),"%cR%u",_row==2?'>':' ',_raise_to);
      else snprintf(raise,sizeof(raise)," --");
      text(d,34,54,call);text(d,86,54,raise);
    }
    return _game.active()==1 && _view==PLAY?100:300;
  }
};
static_assert(sizeof(HoldemScreen)<=176,"Visible Holdem screen must stay small");
