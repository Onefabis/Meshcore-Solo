#pragma once
#include <games/TicTacToe.h>
#include <games/TicTacToeMesh.h>
#include <new>
#ifdef SOLO_GAME_SNAKE
#include "SnakeScreen.h"
#endif
#ifdef SOLO_GAME_CHECKERS
#include "CheckersScreen.h"
#endif
#ifdef SOLO_GAME_BATTLESHIP
#include "BattleshipScreen.h"
#endif
#ifdef SOLO_GAME_ARTILLERY
#include "ArtilleryScreen.h"
#endif

#ifdef SOLO_GAME_HOLDEM
#include "HoldemScreen.h"
#endif

#ifdef SOLO_GAME_VEYLITH
#include "VeylithScreen.h"
#endif

#ifdef SOLO_GAMES_MULTIPLAYER
#include "ArenaOnlineScreen.h"
#endif

// Uses the existing framebuffer/input. Optional multiplayer storage is created
// only on entry and destroyed on exit, never allocated in the frame loop.
class GamesScreen : public UIScreen {
  UITask* _task;
  solo_games::TicTacToePosition _position;
  solo_games::TicTacToeAI _ai;
  enum View : uint8_t { MENU, SETUP, PLAY, PAUSE, NET_MENU, CHANNEL_PICK, LOBBIES,
    LOBBY, NET_PLAY, NET_PAUSE, NET_ERROR, SNAKE_VIEW, CHECKERS_VIEW, BATTLESHIP_VIEW, ARTILLERY_VIEW, HOLDEM_VIEW, VEYLITH_VIEW, ARENA_ONLINE_VIEW };
  View _view = MENU;
  uint8_t _row = 0, _cell = 4, _human = 1;
  solo_games::TicTacToeAI::Difficulty _difficulty = solo_games::TicTacToeAI::NORMAL;
#ifdef ZYGIS_GAME_DIAGNOSTICS
  uint32_t _max_poll_us = 0, _max_render_us = 0, _max_frame_us = 0;
#endif
  struct NetState {
    solo_games::TicTacToeMesh game;
    uint8_t channels[40] = {}, count = 0, digest[16] = {};
    bool bound = false;
    const char* error = "Session closed";
  };
  static_assert(sizeof(NetState) <= 1200, "Multiplayer storage must remain bounded");
  NetState* _net = nullptr;
#ifdef SOLO_GAME_BATTLESHIP
  BattleshipScreen* _battleship = nullptr;
  void closeBattleship() { delete _battleship; _battleship = nullptr; }
#else
  void closeBattleship() {}
#endif
#ifdef SOLO_GAME_ARTILLERY
  ArtilleryScreen* _artillery = nullptr;
  void closeArtillery() { delete _artillery; _artillery = nullptr; }
#else
  void closeArtillery() {}
#endif
#ifdef SOLO_GAME_HOLDEM
  HoldemScreen* _holdem = nullptr;
  void closeHoldem() { delete _holdem; _holdem = nullptr; }
#else
  void closeHoldem() {}
#endif
#if defined(SOLO_GAMES_MULTIPLAYER)
  static const uint8_t MENU_COUNT = 9;
#elif defined(SOLO_GAME_VEYLITH)
  static const uint8_t MENU_COUNT = 8;
#elif defined(SOLO_GAME_HOLDEM)
  static const uint8_t MENU_COUNT = 7;
#elif defined(SOLO_GAME_ARTILLERY)
  static const uint8_t MENU_COUNT = 6;
#else
#if defined(SOLO_GAME_BATTLESHIP)
  static const uint8_t MENU_COUNT = 5;
#elif defined(SOLO_GAME_CHECKERS)
  static const uint8_t MENU_COUNT = 4;
#elif defined(SOLO_GAME_SNAKE)
  static const uint8_t MENU_COUNT = 3;
#else
  static const uint8_t MENU_COUNT = 2;
#endif
#endif

#ifdef SOLO_GAME_SNAKE
  SnakeScreen* _snake = nullptr;
  void closeSnake() { delete _snake; _snake = nullptr; }
#else
  void closeSnake() {}
#endif
#ifdef SOLO_GAME_CHECKERS
  CheckersScreen* _checkers = nullptr;
  void closeCheckers() { delete _checkers; _checkers = nullptr; }
#else
  void closeCheckers() {}
#endif

#ifdef SOLO_GAME_VEYLITH
  VeylithScreen* _veylith = nullptr;
  void closeVeylith() {delete _veylith;_veylith=nullptr;}
#else
  void closeVeylith() {}
#endif
#ifdef SOLO_GAMES_MULTIPLAYER
  ArenaOnlineScreen* _arenaOnline=nullptr;
  void closeArenaOnline(){delete _arenaOnline;_arenaOnline=nullptr;}
#else
  void closeArenaOnline(){}
#endif
  bool makeNet() {
    if (!_net) _net = new (std::nothrow) NetState;
    if (!_net) { _task->showAlert("Not enough RAM", 1500); return false; }
    return true;
  }
  void leaveNet() {
    if (!_net) return;
    solo_games::GameEvent event; char text[53];
    if (_net->bound && _net->game.leaveEvent(event) && solo_games::GameCodec::encode(event, text, sizeof(text)))
      _task->sendGameText(_net->game.channel(), text, _net->digest); // one best-effort leave
    _net->game.reset(); _net->bound = false;
  }
  void closeNet() { leaveNet(); delete _net; _net = nullptr; }
  void renderNet(DisplayDriver& d) {
    using Net = solo_games::TicTacToeMesh;
    if (_view == NET_MENU) {
      text(d,2,18,_row == 0 ? "> Host game" : "  Host game");
      text(d,2,34,_row == 1 ? "> Find game" : "  Find game");
      text(d,2,51,"Menu: back");
    } else if (_view == CHANNEL_PICK) {
      if (!_net->count) {
        text(d,2,16,"No private channels");text(d,2,29,"configured. Add one");
        text(d,2,42,"in Solo or the app.");text(d,2,55,"Menu: back");
      } else for (unsigned i = (_row / 4) * 4; i < _net->count && i < unsigned((_row / 4) * 4 + 4); ++i) {
        char label[19]; uint8_t digest[16];
        text(d,2,16+(i%4)*12,_row == i ? ">" : " ");
        if (_task->gameChannel(_net->channels[i],label,sizeof(label),digest)) text(d,12,16+(i%4)*12,label);
      }
    } else if (_view == LOBBIES) {
      bool found = false;
      for (unsigned i=0;i<Net::LOBBY_COUNT;++i) if (const auto* lobby=_net->game.lobby(i,millis())) {
        char label[21];snprintf(label,sizeof(label),"%c %.18s",_row==i?'>':' ',lobby->name[0]?lobby->name:"Unknown host");
        text(d,2,16+i*12,label);found=true;
      }
      if (!found) {text(d,2,20,"Listening...");text(d,2,36,"Waiting for hosts");}
    } else if (_view == NET_ERROR) {
      text(d,2,20,_net->error);text(d,2,40,"Press: multiplayer");
    } else if (_view == NET_PAUSE) {
      text(d,2,20,_row==0?"> Resume":"  Resume");text(d,2,38,_row==1?"> Leave match":"  Leave match");
    } else {
      Net& g = _net->game;
      text(d,2,16,g.phase()==Net::HOSTING && !g.candidate()?"Waiting for player":"Joining game...");
      text(d,2,30,g.isHost()?"You: X (host)":"You: O (guest)");
      text(d,2,44,"Menu: leave");
    }
  }
  bool netInput(uint8_t key) {
    using Net = solo_games::TicTacToeMesh;
    if (key == KEY_CANCEL) {
      if (_view == NET_MENU) {closeNet();_view=MENU;_row=0;}
      else if (_view == NET_PLAY) {_view=NET_PAUSE;_row=0;}
      else if (_view == NET_PAUSE) _view=NET_PLAY;
      else {leaveNet();_view=NET_MENU;_row=0;}
      return true;
    }
    if (_view == NET_MENU || _view == NET_PAUSE) {
      if (key==KEY_UP || key==KEY_DOWN) _row=1-_row;
      if (key==KEY_ENTER && _view==NET_PAUSE) {
        if (_row==0) _view=NET_PLAY;
        else {leaveNet();_view=NET_MENU;_row=0;}
      } else if (key==KEY_ENTER) {
        if (_row==1) {_view=LOBBIES;_row=0;}
        else {
          _net->count=0;
          for (unsigned slot=0;slot<40;++slot) {
            char name[2];uint8_t digest[16];
            if (_task->gameChannel(slot,name,sizeof(name),digest)) _net->channels[_net->count++]=slot;
          }
          _view=CHANNEL_PICK;_row=0;
        }
      }
      return true;
    }
    if (_view==NET_ERROR) {if(key==KEY_ENTER){leaveNet();_view=NET_MENU;_row=0;}return true;}
    if (_view==CHANNEL_PICK || _view==LOBBIES) {
      unsigned count=_view==CHANNEL_PICK?_net->count:Net::LOBBY_COUNT;
      if (count && key==KEY_UP) _row=(_row+count-1)%count;
      if (count && key==KEY_DOWN) _row=(_row+1)%count;
      if (count && key==KEY_ENTER) {
        uint8_t slot;
        if (_view==CHANNEL_PICK) slot=_net->channels[_row];
        else {
          const auto* lobby=_net->game.lobby(_row,millis());
          if (!lobby) return true;
          slot=lobby->channel;
        }
        char name[2];uint8_t digest[16];
        if (!_task->gameChannel(slot,name,sizeof(name),digest)) return true;
        bool ok=_view==CHANNEL_PICK ? _net->game.host(slot,_task->gameSessionId(),_task->gamePlayerId(),millis()) :
          _net->game.join(_row,_task->gamePlayerId(),millis());
        if (ok) {memcpy(_net->digest,digest,16);_net->bound=true;_view=LOBBY;_row=0;_cell=4;}
      }
      return true;
    }
    if (_view==LOBBY) {
      if (key==KEY_ENTER) {
        Net& game=_net->game;
        if (game.stalled()) game.retry(millis());
      }
      return true;
    }
    if (_view==NET_PLAY) {
      if (_net->game.stalled() && key==KEY_ENTER) {_net->game.retry(millis());return true;}
      if (_position.result()) {
        if(key==KEY_ENTER && _net->game.rematch())_cell=4;
        return true;
      }
      if (key==KEY_LEFT && _cell%3) --_cell;
      if (key==KEY_RIGHT && _cell%3<2) ++_cell;
      if (key==KEY_UP && _cell>=3) _cell-=3;
      if (key==KEY_DOWN && _cell<6) _cell+=3;
      if (key==KEY_ENTER) _net->game.move(_cell);
      return true;
    }
    return true;
  }

  const char* difficultyName() const {
    return _difficulty == solo_games::TicTacToeAI::EASY ? "Easy" :
      _difficulty == solo_games::TicTacToeAI::NORMAL ? "Normal" : "Hard";
  }
  void cpuBegin() {
    if (!_position.result() && _position.turn != _human) _ai.begin(_position, _difficulty, millis());
  }
  void feedback() { _task->playGameSound(_position.result() != 0); }
  void newGame() { _position = {}; _cell = 4; _view = PLAY; cpuBegin(); }
  static void text(DisplayDriver& d, int x, int y, const char* value) { d.setCursor(x,y); d.print(value); }

public:
  GamesScreen(UITask* task) : _task(task) {}
  ~GamesScreen() { closeArenaOnline();closeVeylith(); closeHoldem(); closeArtillery(); closeBattleship(); closeCheckers(); closeSnake(); closeNet(); }
  void onShow() override { closeArenaOnline();closeVeylith(); closeHoldem(); closeArtillery(); closeBattleship(); closeCheckers(); closeSnake(); closeNet(); _view = MENU; _row = 0; }
  void openTicTacToe() { closeArenaOnline();closeVeylith(); _view = SETUP; _row = 0; }
  void openMultiplayer() { closeArenaOnline();closeVeylith(); if (makeNet()) {_view=NET_MENU;_row=0;} }
#ifdef SOLO_GAME_SNAKE
  void openSnake() {
    closeArenaOnline();closeVeylith();
    closeHoldem(); closeArtillery(); closeBattleship(); closeCheckers(); closeNet(); closeSnake();
    _snake = new (std::nothrow) SnakeScreen(_task, uint32_t(_task->gameSessionId()));
    if (_snake) _view = SNAKE_VIEW;
    else _task->showAlert("Not enough RAM", 1500);
  }
#endif
#ifdef SOLO_GAME_CHECKERS
  void openCheckers() {
    closeArenaOnline();closeVeylith();
    closeHoldem(); closeArtillery(); closeBattleship(); closeNet(); closeSnake(); closeCheckers();
    _checkers = new (std::nothrow) CheckersScreen(_task);
    if (_checkers) _view = CHECKERS_VIEW;
    else _task->showAlert("Not enough RAM", 1500);
  }
#endif
#ifdef SOLO_GAME_BATTLESHIP
  void openBattleship() {
    closeArenaOnline();closeVeylith();
    closeHoldem(); closeArtillery(); closeNet(); closeSnake(); closeCheckers(); closeBattleship();
    _battleship = new (std::nothrow) BattleshipScreen(_task, uint32_t(_task->gameSessionId()));
    if (_battleship) _view = BATTLESHIP_VIEW;
    else _task->showAlert("Not enough RAM", 1500);
  }
#endif
#ifdef SOLO_GAME_ARTILLERY
  void openArtillery() {
    closeArenaOnline();closeVeylith();
    closeNet(); closeSnake(); closeCheckers(); closeBattleship(); closeHoldem(); closeArtillery();
    _artillery = new (std::nothrow) ArtilleryScreen(_task,uint32_t(_task->gameSessionId()));
    if (_artillery) _view = ARTILLERY_VIEW;
    else _task->showAlert("Not enough RAM", 1500);
  }
#endif
#ifdef SOLO_GAME_HOLDEM
  void openHoldem() {
    closeArenaOnline();closeVeylith();
    closeNet(); closeSnake(); closeCheckers(); closeBattleship(); closeHoldem(); closeArtillery();
    _holdem = new (std::nothrow) HoldemScreen(_task,uint32_t(_task->gameSessionId()));
    if (_holdem) _view = HOLDEM_VIEW;
    else _task->showAlert("Not enough RAM",1500);
  }
#endif
#ifdef SOLO_GAME_VEYLITH
  void openVeylith() {
    closeNet();closeSnake();closeCheckers();closeBattleship();closeHoldem();closeArtillery();closeArenaOnline();closeVeylith();
    _veylith=new(std::nothrow) VeylithScreen(_task,uint32_t(_task->gameSessionId()));
    if(_veylith)_view=VEYLITH_VIEW;else _task->showAlert("Not enough RAM",1500);
  }
#endif
#ifdef SOLO_GAMES_MULTIPLAYER
  void openArenaMultiplayer(){
    closeArenaOnline();closeVeylith();closeHoldem();closeArtillery();closeBattleship();closeCheckers();closeSnake();closeNet();
    _arenaOnline=new(std::nothrow) ArenaOnlineScreen(_task);
    if(_arenaOnline)_view=ARENA_ONLINE_VIEW;else _task->showAlert("Not enough RAM",1500);
  }
#endif
  void receiveText(uint8_t channel, const char* body, const char* sender=nullptr) {
#ifdef SOLO_GAMES_MULTIPLAYER
    if(_arenaOnline){_arenaOnline->receiveText(channel,body,sender);return;}
#endif
    if (!_net) return;
    solo_games::GameEvent event;
    if (!solo_games::GameCodec::decode(body,event)) return;
    char name[2];uint8_t digest[16];
    if (!_task->gameChannel(channel,name,sizeof(name),digest)) return;
    if (_net->bound && channel==_net->game.channel() && memcmp(digest,_net->digest,16)) return;
    _net->game.receive(channel,event,millis(),sender);
  }

  void poll() override {
#ifdef SOLO_GAMES_MULTIPLAYER
    if(_arenaOnline){_arenaOnline->poll();return;}
#endif
#ifdef SOLO_GAME_VEYLITH
    if(_veylith){_veylith->poll();return;}
#endif
#ifdef SOLO_GAME_HOLDEM
    if (_holdem) { _holdem->poll(); return; }
#endif
#ifdef SOLO_GAME_ARTILLERY
    if (_artillery) { _artillery->poll(); return; }
#endif
#ifdef SOLO_GAME_BATTLESHIP
    if (_battleship) { _battleship->poll(); return; }
#endif
#ifdef SOLO_GAME_CHECKERS
    if (_checkers) { _checkers->poll(); return; }
#endif
#ifdef SOLO_GAME_SNAKE
    if (_snake) { _snake->poll(); return; }
#endif
    if (_net) {
      solo_games::GameEvent event;char text[53];
      if (_net->bound && _net->game.outgoing(millis(),event)) {
        char name[2];uint8_t digest[16];
        if (!_task->gameChannel(_net->game.channel(),name,sizeof(name),digest) || memcmp(digest,_net->digest,16)) {
          _net->game.reset();_net->bound=false;_net->error="Channel changed";_view=NET_ERROR;
        } else if (solo_games::GameCodec::encode(event,text,sizeof(text)) && _task->sendGameText(_net->game.channel(),text,_net->digest))
          _net->game.sent(millis());
        else _net->game.backpressure(millis());
      }
      _position=_net->game.position();_human=_net->game.mark();
      if (_net->game.phase()==solo_games::TicTacToeMesh::CLOSED) {_net->error="Peer left match";_view=NET_ERROR;}
      if ((_net->game.phase()==solo_games::TicTacToeMesh::PLAYING || _net->game.phase()==solo_games::TicTacToeMesh::FINISHED) && _view==LOBBY) _view=NET_PLAY;
      return;
    }
    if (_view != PLAY || _position.result() || _position.turn == _human) return;
#ifdef ZYGIS_GAME_DIAGNOSTICS
    uint32_t started = micros();
#endif
    if (_ai.step(128) && _position.play(_ai.choice())) feedback();
#ifdef ZYGIS_GAME_DIAGNOSTICS
    uint32_t elapsed = micros() - started;
    if (elapsed > _max_poll_us) _max_poll_us = elapsed;
#endif
  }

  int render(DisplayDriver& d) override {
#ifdef SOLO_GAMES_MULTIPLAYER
    if(_arenaOnline)return _arenaOnline->render(d);
#endif
#ifdef SOLO_GAME_VEYLITH
    if(_veylith)return _veylith->render(d);
#endif
#ifdef SOLO_GAME_HOLDEM
    if (_holdem) return _holdem->render(d);
#endif
#ifdef SOLO_GAME_ARTILLERY
    if (_artillery) return _artillery->render(d);
#endif
#ifdef SOLO_GAME_BATTLESHIP
    if (_battleship) return _battleship->render(d);
#endif
#ifdef SOLO_GAME_CHECKERS
    if (_checkers) return _checkers->render(d);
#endif
#ifdef SOLO_GAME_SNAKE
    if (_snake) return _snake->render(d);
#endif
#ifdef ZYGIS_GAME_DIAGNOSTICS
    uint32_t started = micros();
#endif
    d.setTextSize(1); d.setColor(UIColor::primary_txt);
    if (_view == MENU && MENU_COUNT > 4) {
      char title[20]; snprintf(title,sizeof(title),"GAMES %u/%u",1+_row/4,(MENU_COUNT+3)/4);
      d.drawCenteredHeader(title);
    } else d.drawCenteredHeader(_view == MENU ? "GAMES" : _net ? "TTT MULTIPLAYER" : "TIC-TAC-TOE");
    if (_view == MENU) {
      const char* labels[] = { "Tic-Tac-Toe CPU", "TTT multiplayer", "Snake", "Checkers", "Battleship", "Scorched Earth", "Texas Hold'em", "Veylith Arena", "Arena multiplayer" };
      unsigned start=(_row/4)*4;
      for (unsigned i=start;i<MENU_COUNT && i<start+4;++i) {
        text(d,2,16+(i-start)*12,_row==i?">":" "); text(d,14,16+(i-start)*12,labels[i]);
      }
      if (MENU_COUNT<4) text(d,2,55,"Menu: back");
    } else if (_net && _view != NET_PLAY) {
      renderNet(d);
    } else if (_view == SETUP) {
      text(d,2,16,_row == 0 ? "> CPU:" : "  CPU:"); text(d,50,16,difficultyName());
      text(d,2,30,_row == 1 ? "> You:" : "  You:"); text(d,50,30,_human == 1 ? "X (first)" : "O (second)");
      text(d,2,44,_row == 2 ? "> Start game" : "  Start game"); text(d,2,55,"L/R change");
    } else if (_view == PAUSE) {
      text(d,2,18,_row == 0 ? "> Resume" : "  Resume");
      text(d,2,33,_row == 1 ? "> New game" : "  New game");
      text(d,2,48,_row == 2 ? "> Games menu" : "  Games menu");
    } else {
      d.fillRect(18,13,1,48); d.fillRect(34,13,1,48);
      d.fillRect(2,29,48,1); d.fillRect(2,45,48,1);
      for (uint8_t i = 0; i < 9; ++i) {
        int x = 2 + (i % 3) * 16, y = 13 + (i / 3) * 16;
        uint8_t mark = _position.at(i);
        if (mark == 1) {
          for (int k = 0; k < 10; ++k) { d.fillRect(x+3+k,y+3+k,2,2); d.fillRect(x+12-k,y+3+k,2,2); }
        } else if (mark == 2) {
          d.fillRect(x+5,y+3,6,2); d.fillRect(x+5,y+12,6,2);
          d.fillRect(x+3,y+5,2,7); d.fillRect(x+11,y+5,2,7);
        }
        if (i == _cell && !_position.result() && _position.turn == _human) d.drawRect(x+1,y+1,14,14);
      }
      uint16_t win = _position.winningMask();
      if (win) {
        uint8_t first = 0, last = 8;
        while (!(win & (1U << first))) ++first;
        while (!(win & (1U << last))) --last;
        gfx::drawLine(d, 10 + (first % 3) * 16, 21 + (first / 3) * 16,
                         10 + (last % 3) * 16, 21 + (last / 3) * 16);
      }
      uint8_t result = _position.result();
      text(d,57,14,result == 3 ? "Draw" : result ? (result == _human ? "You win!" : _net ? "You lose" : "CPU wins") :
        _net && _net->game.stalled() ? "Retry?" : _net && _net->game.busy() ? "Sending" :
        _position.turn == _human ? "Your turn" : _net ? "Peer turn" : "Thinking");
      text(d,57,27,_human == 1 ? "You: X" : "You: O"); text(d,57,39,_net?"Mesh":difficultyName());
      text(d,57,51,_net && result ? (_net->game.phase()!=solo_games::TicTacToeMesh::FINISHED?"Finishing":
        !_net->game.canRematch()?"Menu: leave":_net->game.busy()?"Waiting...":">Play again") :
        _net?"Menu: pause":result?"Press: new":"Menu: pause");
    }
#ifdef ZYGIS_GAME_DIAGNOSTICS
    uint32_t elapsed = micros() - started;
    if (elapsed > _max_render_us) _max_render_us = elapsed;
#endif
    return _view == PLAY && _position.turn != _human && !_position.result() ? 80 : 300;
  }

  bool handleInput(char input) override {
    uint8_t key = uint8_t(input);
#ifdef SOLO_GAME_VEYLITH
    if(_veylith){_veylith->handleInput(input);if(_veylith->wantsExit()){closeArenaOnline();closeVeylith();_view=MENU;_row=7;}return true;}
#endif
    bool previous = key == KEY_LEFT || key == KEY_PREV;
    bool next = key == KEY_RIGHT || key == KEY_NEXT;
#ifdef SOLO_GAME_SNAKE
    if (_snake) {
      _snake->handleInput(input);
      if (_snake->wantsExit()) {closeSnake();_view=MENU;_row=2;}
      return true;
    }
#endif
#ifdef SOLO_GAME_CHECKERS
    if (_checkers) {
      _checkers->handleInput(input);
      if (_checkers->wantsExit()) {closeCheckers();_view=MENU;_row=3;}
      return true;
    }
#endif
#ifdef SOLO_GAME_BATTLESHIP
    if (_battleship) {
      _battleship->handleInput(input);
      if (_battleship->wantsExit()) {closeBattleship();_view=MENU;_row=4;}
      return true;
    }
#endif
#ifdef SOLO_GAME_ARTILLERY
    if (_artillery) {
      _artillery->handleInput(input);
      if (_artillery->wantsExit()) {closeHoldem(); closeArtillery();_view=MENU;_row=5;}
      return true;
    }
#endif
#ifdef SOLO_GAME_HOLDEM
    if (_holdem) {
      _holdem->handleInput(input);
      if (_holdem->wantsExit()) {closeHoldem();_view=MENU;_row=6;}
      return true;
    }
#endif
#ifdef SOLO_GAMES_MULTIPLAYER
    if(_arenaOnline){_arenaOnline->handleInput(input);if(_arenaOnline->wantsExit()){closeArenaOnline();_view=MENU;_row=8;}return true;}
#endif
    if (_net) return netInput(key);
    if (key == KEY_CANCEL) {
      if (_view == MENU) _task->gotoToolsScreen();
      else if (_view == SETUP) _view = MENU;
      else if (_view == PAUSE) _view = PLAY;
      else { _view = PAUSE; _row = 0; }
      return true;
    }
    if (_view == MENU) {
      if (key==KEY_UP) _row=(_row+MENU_COUNT-1)%MENU_COUNT;
      if (key==KEY_DOWN) _row=(_row+1)%MENU_COUNT;
      if (key==KEY_ENTER) {
#ifdef SOLO_GAMES_MULTIPLAYER
        if(_row==8)openArenaMultiplayer();else
#endif
#ifdef SOLO_GAME_VEYLITH
        if(_row==7)openVeylith();else
#endif
#ifdef SOLO_GAME_HOLDEM
        if (_row==6) openHoldem();
        else
#endif
#ifdef SOLO_GAME_ARTILLERY
        if (_row==5) openArtillery();
        else
#endif
#ifdef SOLO_GAME_BATTLESHIP
        if (_row==4) openBattleship();
        else
#endif
#ifdef SOLO_GAME_CHECKERS
        if (_row==3) openCheckers();
        else
#endif
#ifdef SOLO_GAME_SNAKE
        if (_row==2) openSnake();
        else
#endif
        if (_row==1) openMultiplayer();else{_view=SETUP;_row=0;}
      }
      return true;
    }
    if (_view == SETUP || _view == PAUSE) {
      if (key == KEY_UP) _row = (_row + 2) % 3;
      if (key == KEY_DOWN) _row = (_row + 1) % 3;
      if (_view == SETUP && (previous || next)) {
        if (_row == 0) _difficulty = solo_games::TicTacToeAI::Difficulty((_difficulty + (previous ? 2 : 1)) % 3);
        if (_row == 1) _human = 3 - _human;
      }
      if (key == KEY_ENTER) {
        if (_view == SETUP) { if (_row == 2) newGame(); else if (_row == 0) _difficulty = solo_games::TicTacToeAI::Difficulty((_difficulty+1)%3); else _human=3-_human; }
        else if (_row == 0) _view = PLAY;
        else if (_row == 1) newGame();
        else { _view = MENU; _row = 0; }
      }
      return true;
    }
    if (_position.result()) { if (key == KEY_ENTER) newGame(); return true; }
    if (_position.turn != _human) return true;
    if (key == KEY_LEFT && _cell % 3) --_cell;
    if (key == KEY_RIGHT && _cell % 3 < 2) ++_cell;
    if (key == KEY_UP && _cell >= 3) _cell -= 3;
    if (key == KEY_DOWN && _cell < 6) _cell += 3;
    if (key == KEY_ENTER && _position.play(_cell)) { feedback(); cpuBegin(); }
    return true;
  }

#ifdef ZYGIS_GAME_DIAGNOSTICS
  void recordFrameMicros(uint32_t elapsed) { if (elapsed > _max_frame_us) _max_frame_us = elapsed; }

  size_t diagnostics(uint8_t* out, size_t size) const {
    if (size < 44) return 0;
#ifdef SOLO_GAMES_MULTIPLAYER
    if(_arenaOnline)return _arenaOnline->diagnostics(out,size,sizeof(GamesScreen));
#endif
#ifdef SOLO_GAME_VEYLITH
    if(_veylith)return _veylith->diagnostics(out,size,sizeof(GamesScreen));
#endif
#ifdef SOLO_GAME_SNAKE
    if (_snake) {
      const auto& game = _snake->game();
      uint32_t values[] = {uint32_t(sizeof(GamesScreen)+sizeof(SnakeScreen)), game.head(), game.food(), game.length(),
        uint32_t(game.direction()), _snake->view(), SNAKE_VIEW, game.score(), 0, 0, _max_frame_us};
      size_t n=0;
      for (uint32_t value:values) for (unsigned shift=0;shift<32;shift+=8) out[n++]=uint8_t(value>>shift);
      return n;
    }
#endif
#ifdef SOLO_GAME_CHECKERS
    if (_checkers) {
      const auto& game = _checkers->game();
      uint32_t values[] = {uint32_t(sizeof(GamesScreen)+sizeof(CheckersScreen)), game.pieces(true), game.pieces(false),
        uint32_t(game.turn()==1?1:2), game.forced(), _checkers->view(), CHECKERS_VIEW, _checkers->cursor(),
        _checkers->maxAiMicros(), game.kings(), game.captured()};
      size_t n=0;
      for (uint32_t value:values) for (unsigned shift=0;shift<32;shift+=8) out[n++]=uint8_t(value>>shift);
      return n;
    }
#endif
#ifdef SOLO_GAME_BATTLESHIP
    if (_battleship) {
      const auto& game = _battleship->game();
      uint32_t values[] = {uint32_t(sizeof(GamesScreen)+sizeof(BattleshipScreen)), game.turn(), game.placed(0), game.result(),
        _battleship->vertical(), _battleship->view(), BATTLESHIP_VIEW, _battleship->cursor(),
        uint32_t(game.shotCount(0)), uint32_t(game.shotCount(1)), _battleship->ownView()};
      size_t n=0;
      for (uint32_t value:values) for (unsigned shift=0;shift<32;shift+=8) out[n++]=uint8_t(value>>shift);
      return n;
    }
#endif
#ifdef SOLO_GAME_ARTILLERY
    if (_artillery) {
      const auto& game = _artillery->game();
      uint32_t values[] = {uint32_t(sizeof(GamesScreen)+sizeof(ArtilleryScreen)), game.turn(), game.health(0), game.health(1),
        uint32_t(game.wind()+2), _artillery->view(), ARTILLERY_VIEW, _artillery->row(), _artillery->angle(), _artillery->power(),
        uint32_t(_artillery->weapon()) | (_artillery->maxAiMicros()<<8)};
      size_t n=0;
      for (uint32_t value:values) for (unsigned shift=0;shift<32;shift+=8) out[n++]=uint8_t(value>>shift);
      return n;
    }
#endif
#ifdef SOLO_GAME_HOLDEM
    if (_holdem) {
      const auto& game = _holdem->game();
      uint32_t values[] = {uint32_t(sizeof(GamesScreen)+sizeof(HoldemScreen)),game.phase(),game.active(),game.chips(0),game.chips(1),
        _holdem->view(),HOLDEM_VIEW,_holdem->row(),game.pot(),game.toCall(0),uint32_t(game.winner()) | (_holdem->maxCpuMicros()<<8)};
      size_t n=0;
      for (uint32_t value:values) for (unsigned shift=0;shift<32;shift+=8) out[n++]=uint8_t(value>>shift);
      return n;
    }
#endif
    uint32_t values[] = { uint32_t(sizeof(GamesScreen) + (_net ? sizeof(NetState) : 0)), _position.x, _position.o, _position.turn,
      _human, uint32_t(_difficulty), uint32_t(_view), _cell, _max_poll_us, _max_render_us, _max_frame_us };
    size_t n = 0;
    for (uint32_t v : values) for (unsigned shift = 0; shift < 32; shift += 8) out[n++] = uint8_t(v >> shift);
    return n;
  }
#endif
};

static_assert(sizeof(GamesScreen) <= 256, "Offline screen must fit within 256 bytes");
