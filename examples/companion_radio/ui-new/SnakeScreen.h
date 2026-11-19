#pragma once
#include <games/Snake.h>

// Owned by GamesScreen only while Snake is selected.
class SnakeScreen {
  UITask* _task;
  solo_games::Snake _game;
  enum View : uint8_t { SETUP, PLAY, PAUSE, RESULT };
  View _view = SETUP;
  uint8_t _row = 0, _speed = 1;
  uint32_t _seed;
  bool _exit = false;

  static void text(DisplayDriver& d, int x, int y, const char* value) { d.setCursor(x,y); d.print(value); }
  const char* speedName() const { return _speed == 0 ? "Slow" : _speed == 1 ? "Normal" : "Fast"; }
  void start() { _game.reset(++_seed, _speed, millis()); _view = PLAY; _row = 0; }

public:
  SnakeScreen(UITask* task, uint32_t seed) : _task(task), _seed(seed) {}
  bool wantsExit() const { return _exit; }
  const solo_games::Snake& game() const { return _game; }
  uint8_t view() const { return _view; }
  void poll() {
    if (_view != PLAY) return;
    auto change = _game.tick(millis());
    if (change == solo_games::Snake::ATE) _task->playGameSound(false);
    if (_game.finished()) { _view = RESULT; _task->playGameSound(true); }
  }
  bool handleInput(char input) {
    uint8_t key = uint8_t(input);
    if (key == KEY_CANCEL) {
      if (_view == SETUP || _view == RESULT) _exit = true;
      else if (_view == PLAY) { _view = PAUSE; _row = 0; }
      else { _view = PLAY; _game.restartClock(millis()); }
      return true;
    }
    if (_view == SETUP) {
      if (key == KEY_UP || key == KEY_DOWN) _row = 1 - _row;
      if ((key == KEY_LEFT || key == KEY_PREV) && _row == 0) _speed = (_speed + 2) % 3;
      if ((key == KEY_RIGHT || key == KEY_NEXT) && _row == 0) _speed = (_speed + 1) % 3;
      if (key == KEY_ENTER) { if (_row == 0) _speed = (_speed + 1) % 3; else start(); }
    } else if (_view == PAUSE) {
      if (key == KEY_UP) _row = (_row + 2) % 3;
      if (key == KEY_DOWN) _row = (_row + 1) % 3;
      if (key == KEY_ENTER) {
        if (_row == 0) { _view = PLAY; _game.restartClock(millis()); }
        else if (_row == 1) start();
        else _exit = true;
      }
    } else if (_view == RESULT) {
      if (key == KEY_ENTER) start();
    } else {
      if (key == KEY_UP) _game.steer(solo_games::Snake::UP);
      if (key == KEY_DOWN) _game.steer(solo_games::Snake::DOWN);
      if (key == KEY_LEFT) _game.steer(solo_games::Snake::LEFT);
      if (key == KEY_RIGHT) _game.steer(solo_games::Snake::RIGHT);
      if (key == KEY_ENTER) { _view = PAUSE; _row = 0; }
    }
    return true;
  }
  int render(DisplayDriver& d) {
    d.setTextSize(1); d.setColor(UIColor::primary_txt);
    if (_view == PLAY) {
      char title[22]; snprintf(title,sizeof(title),"SNAKE  Score:%u",_game.score());
      d.drawCenteredHeader(title);
      d.drawRect(2,12,124,51);
      for (unsigned i=0;i<_game.length();++i) {
        uint16_t cell = _game.body(i);
        int x = 4 + (cell % solo_games::Snake::WIDTH) * 4;
        int y = 14 + (cell / solo_games::Snake::WIDTH) * 4;
        d.fillRect(x,y,i+1==_game.length()?3:2,i+1==_game.length()?3:2);
      }
      uint16_t food = _game.food();
      int x = 4 + (food % solo_games::Snake::WIDTH) * 4;
      int y = 14 + (food / solo_games::Snake::WIDTH) * 4;
      d.fillRect(x+1,y,1,3);d.fillRect(x,y+1,3,1);
      return 40;
    }
    d.drawCenteredHeader(_view == PAUSE ? "SNAKE PAUSED" : _view == RESULT ? (_game.won() ? "BOARD CLEARED" : "GAME OVER") : "SNAKE");
    if (_view == SETUP) {
      text(d,2,18,_row==0?"> Speed:":"  Speed:"); text(d,60,18,speedName());
      text(d,2,35,_row==1?"> Start game":"  Start game"); text(d,2,51,"L/R: speed");
    } else if (_view == PAUSE) {
      text(d,2,16,_row==0?"> Resume":"  Resume");
      text(d,2,32,_row==1?"> Restart":"  Restart");
      text(d,2,48,_row==2?"> Exit Snake":"  Exit Snake");
    } else {
      char score[22];snprintf(score,sizeof(score),"Score: %u",_game.score());
      text(d,2,18,score);text(d,2,35,"Press: restart");text(d,2,51,"Menu: exit");
    }
    return 300;
  }
};

static_assert(sizeof(SnakeScreen) <= 848, "Visible Snake screen must stay small");
