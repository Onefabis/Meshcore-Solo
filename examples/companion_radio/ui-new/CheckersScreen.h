#pragma once
#include <games/Checkers.h>

class CheckersScreen {
  UITask* _task;
  solo_games::Checkers _game;
  solo_games::CheckersAI _ai;
  enum View : uint8_t { SETUP, PLAY, PAUSE, RESULT };
  View _view=SETUP;
  uint8_t _row=0,_cursor=40,_selected=255;
  bool _normal=true,_exit=false,_thinking=false;
#ifdef ZYGIS_GAME_DIAGNOSTICS
  uint32_t _max_ai_us=0;
#endif
  static void text(DisplayDriver& d,int x,int y,const char* value) {d.setCursor(x,y);d.print(value);}
  void start() {_game.reset();_view=PLAY;_cursor=40;_selected=255;_thinking=false;}
public:
  explicit CheckersScreen(UITask* task):_task(task){}
  bool wantsExit()const{return _exit;}
  const solo_games::Checkers& game()const{return _game;}
  uint8_t view()const{return _view;}
  uint8_t cursor()const{return _cursor;}
#ifdef ZYGIS_GAME_DIAGNOSTICS
  uint32_t maxAiMicros()const{return _max_ai_us;}
#endif
  void poll() {
    if(_view!=PLAY)return;
    if(_game.result()){_view=RESULT;return;}
    if(_game.turn()!=-1)return;
#ifdef ZYGIS_GAME_DIAGNOSTICS
    uint32_t begin=micros();
#endif
    if(!_thinking){_ai.begin(_game,_normal,millis());_thinking=true;}
    if(_ai.step(1)) {
      auto move=_ai.choice();_game.play(move.from,move.to);_thinking=false;
      _task->playGameSound(_game.result()!=0);
    }
#ifdef ZYGIS_GAME_DIAGNOSTICS
    uint32_t elapsed=micros()-begin;if(elapsed>_max_ai_us)_max_ai_us=elapsed;
#endif
    if(_game.result())_view=RESULT;
  }
  bool handleInput(char input) {
    uint8_t key=uint8_t(input);
    if(key==KEY_CANCEL) {
      if(_view==SETUP || _view==RESULT)_exit=true;
      else if(_view==PLAY){_view=PAUSE;_row=0;}
      else _view=PLAY;
      return true;
    }
    if(_view==SETUP) {
      if(key==KEY_UP || key==KEY_DOWN)_row=1-_row;
      if((key==KEY_LEFT || key==KEY_RIGHT || key==KEY_PREV || key==KEY_NEXT) && _row==0)_normal=!_normal;
      if(key==KEY_ENTER){if(_row==0)_normal=!_normal;else start();}
    } else if(_view==PAUSE) {
      if(key==KEY_UP)_row=(_row+2)%3;
      if(key==KEY_DOWN)_row=(_row+1)%3;
      if(key==KEY_ENTER){if(_row==0)_view=PLAY;else if(_row==1)start();else _exit=true;}
    } else if(_view==RESULT) {
      if(key==KEY_ENTER)start();
    } else if(_game.turn()==1) {
      if(key==KEY_LEFT && _cursor%8)--_cursor;
      if(key==KEY_RIGHT && _cursor%8<7)++_cursor;
      if(key==KEY_UP && _cursor>=8)_cursor-=8;
      if(key==KEY_DOWN && _cursor<56)_cursor+=8;
      if(key==KEY_ENTER) {
        int square=solo_games::Checkers::square(_cursor/8,_cursor%8);
        if(square<0)return true;
        if(_selected!=255 && _game.play(_selected,square)) {
          _selected=_game.forced();_task->playGameSound(_game.result()!=0);
          if(_game.result())_view=RESULT;
        } else if(_game.at(square)>0 && (_game.forced()==255 || _game.forced()==square))_selected=square;
      }
    }
    return true;
  }
  int render(DisplayDriver& d) {
    d.setTextSize(1);d.setColor(UIColor::primary_txt);
    if(_view==SETUP || _view==PAUSE) {
      d.drawCenteredHeader(_view==SETUP?"CHECKERS":"CHECKERS PAUSED");
      if(_view==SETUP) {
        text(d,2,18,_row==0?"> CPU:":"  CPU:");text(d,50,18,_normal?"Normal":"Easy");
        text(d,2,35,_row==1?"> Start game":"  Start game");text(d,2,51,"L/R: level");
      } else {
        text(d,2,16,_row==0?"> Resume":"  Resume");text(d,2,32,_row==1?"> Restart":"  Restart");
        text(d,2,48,_row==2?"> Exit Checkers":"  Exit Checkers");
      }
      return 300;
    }
    solo_games::Checkers::Move moves[solo_games::Checkers::MAX_MOVES];unsigned count=_game.legalMoves(moves);
    for(int row=0;row<8;++row)for(int col=0;col<8;++col) {
      int x=2+col*7,y=6+row*7,square=solo_games::Checkers::square(row,col);
      if(square<0){d.setColor(UIColor::primary_txt);d.fillRect(x,y,7,7);}
      else {
        int piece=_game.at(square);d.setColor(UIColor::primary_txt);
        if(piece>0)d.fillRect(x+1,y+1,5,5);
        if(piece<0)d.drawRect(x+1,y+1,5,5);
        if(piece==2 || piece==-2){d.setColor(piece>0?UIColor::window_bkg:UIColor::primary_txt);d.fillRect(x+3,y+3,1,1);}
        if((_game.captured()>>square)&1) {
          d.setColor(UIColor::window_bkg);d.fillRect(x+1,y+2,5,1);
          d.setColor(UIColor::primary_txt);d.fillRect(x+1,y+3,5,1);
        }
        d.setColor(UIColor::primary_txt);
        if(_selected==square)d.drawRect(x,y,7,7);
        for(unsigned i=0;i<count;++i)if(moves[i].from==_selected && moves[i].to==square)d.fillRect(x+3,y+3,1,1);
      }
      if(_cursor==row*8+col){d.setColor(square<0?UIColor::window_bkg:UIColor::primary_txt);d.drawRect(x,y,7,7);}
    }
    d.setColor(UIColor::primary_txt);text(d,62,1,"CHECKERS");
    uint8_t result=_game.result();
    text(d,62,15,result?(result==3?"Draw":result==1?"You win!":"CPU wins"):_game.turn()==1?"Your turn":"Thinking");
    text(d,62,28,_normal?"CPU Normal":"CPU Easy");
    text(d,62,41,_game.forced()!=255?"Jump again":_game.mustCapture()?"Must jump":_selected!=255?"Select dest":"Pick piece");
    text(d,62,54,result?"Press: new":"Menu: pause");
    return _game.turn()==-1 && !result?60:300;
  }
};
static_assert(sizeof(CheckersScreen)<=576,"Visible Checkers screen must stay small");
