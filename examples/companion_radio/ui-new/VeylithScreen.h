#pragma once
#include <games/veylith/Assets.h>
#include <games/veylith/Battle.h>
#include <games/veylith/Bitmap.h>
#include <games/veylith/Sound.h>
#include <new>
#ifdef SOLO_GAMES_MULTIPLAYER
#include <games/multiplayer/ArenaDuel.h>
#endif
namespace solo_games {namespace multiplayer {class ArenaDuel;}}

// All mutable Arena state belongs to this visible screen. Art is const flash data.
class VeylithScreen {
  using Progress = veylith::Progress;
  using Battle = veylith::Battle;
  using Action = veylith::Action;
  enum View : uint8_t {
    HUB,
    TEAM,
    STATS,
    BUY,
    LOADOUT,
    LIBRARY,
    MOVE_INFO,
    READY,
    FIELD,
    ACTIONS,
    MOVES,
    TARGET,
    SWITCH,
    STATUS,
    ANIMATE,
    RESULT,
    SAVE_ERROR,
    CORRUPT,
    RESET_CONFIRM,
    PAUSE,
    HELP,
    LEVEL_UP,
    CREATURE_INFO,
    NET_WAITING
  };
  UITask *_task;
  Progress _profile;
  Battle *_battle = nullptr;
  solo_games::multiplayer::ArenaDuel* _net=nullptr;
#ifdef SOLO_GAMES_MULTIPLAYER
  uint16_t _netRevision=0;
#endif
  bool _requestRematch=false,_resultSound=false;
  veylith::Build _player[3], _cpu[3];
  Action _cpuAction, _action;
  View _view = HUB, _resume = FIELD, _infoResume = LIBRARY;
  uint8_t _infoPage = 0, _libraryRow = 0, _creatureStage = 0, _unlockRow = 0;
  uint8_t _newMoves[10] = {};
  uint8_t _row = 0, _slot = 0, _stat = 0, _moveSlot = 0, _move = 1;
  uint8_t _statusSide = 0, _statusMember = 0, _statusPage = 0;
  uint8_t _event = 0, _frame = 0, _difficulty = 0, _oldStage = 0, _oldLevel = 1;
  uint8_t _lineup[3] = {};
  uint16_t _earned = 0;
  uint32_t _seed, _resultId = 0, _nextFrame = 0;
  uint8_t _bytes[veylith::SaveCodec::SIZE];
  bool _exit = false, _awarded = false;
  uint8_t _sound=veylith::SILENT;
  uint32_t _soundUntil=0;
#ifdef ZYGIS_GAME_DIAGNOSTICS
  uint32_t _maxAiUs = 0;
#endif

  static void text(DisplayDriver &d, int x, int y, const char *value) {
    d.setCursor(x, y);
    d.print(value);
  }
  static void clipped(DisplayDriver &d, int x, int y, const char *value, unsigned chars) {
    char line[22];
    snprintf(line, sizeof(line), "%.*s", int(chars > 21 ? 21 : chars), value);
    text(d, x, y, line);
  }
  unsigned ownSide()const {
#ifdef SOLO_GAMES_MULTIPLAYER
    return _net?_net->slot():0;
#else
    return 0;
#endif
  }
  unsigned enemySide()const{return 1-ownSide();}
  unsigned battleSide(unsigned perspective)const{return ownSide()^perspective;}
  unsigned ownResult()const{unsigned result=_battle?_battle->result():0;return ownSide()==0||result==0||result==3?result:3-result;}
  bool peerTeamReady()const {
#ifdef SOLO_GAMES_MULTIPLAYER
    return !_net||_net->teamReady(enemySide());
#else
    return true;
#endif
  }
  unsigned family() const { return _profile.selected[_slot]; }
  const veylith::Training &training() const { return _profile.family[family()]; }
  const char *creatureName() const { return veylith::families[family()].names[training().stage()]; }
  void page(View view, unsigned row = 0) {
    _view = view;
    _row = row;
  }
  void alert(const char *message) { _task->showAlert(message, 1200); }
  bool store(const Progress &candidate) {
    if (!veylith::SaveCodec::encode(candidate, _bytes, sizeof(_bytes)) ||
        !_task->saveVeylith(_bytes, sizeof(_bytes))) {
      alert("Arena save failed");
      return false;
    }
    _profile = candidate;
    return true;
  }
  unsigned libraryCount() const {
    unsigned n = 0;
    for (unsigned id = 1; id <= 80; ++id)
      n += training().eligible(family(), id, false);
    return n;
  }
  unsigned libraryId(unsigned index) const {
    for (unsigned id : veylith::moveOrder)
      if (training().eligible(family(), id, false)) {
        if (!index) return id;
        --index;
      }
    return 41;
  }
  static void wrap(DisplayDriver &d, const char *value, int y, unsigned lines = 2) {
    // Fixed 21-character lines, split at spaces; caller chooses the bounded height.
    for (unsigned line = 0; line < lines && *value; ++line) {
      unsigned n = strlen(value);
      if (n > 21) {
        n = 21;
        while (n > 8 && value[n] != ' ')
          --n;
      }
      clipped(d, 0, y + line * 10, value, n);
      value += n;
      while (*value == ' ')
        ++value;
    }
  }
  const veylith::Training &infoTraining() const {
    return _infoResume == MOVES ? _player[_battle->active(ownSide())].training : training();
  }
  unsigned infoFamily() const { return _infoResume == MOVES ? _player[_battle->active(ownSide())].family : family(); }
  unsigned unlockCount() const {
    unsigned n = 0;
    for (unsigned id = 0; id < 80; ++id)
      n += (_newMoves[id / 8] >> (id % 8)) & 1;
    return n;
  }
  unsigned unlockId(unsigned index) const {
    for (unsigned id = 0; id < 80; ++id)
      if (_newMoves[id / 8] & (1U << (id % 8))) {
        if (!index) return id + 1;
        --index;
      }
    return 41;
  }
  void cue(unsigned sound){_sound=sound;_soundUntil=millis()+350;}
  void eventSound(){
    if(!_battle||_event>=_battle->eventCount())return;
    using namespace veylith;const auto& e=_battle->event(_event);
    switch(e.kind){
      case Event::STRIKE:cue(e.move?ATTACK_SOUND:HIT_SOUND);break;
      case Event::HEALING:cue(HEAL_SOUND);break;
      case Event::SHIELDING:cue(SHIELD_SOUND);break;
      case Event::SWITCHED:cue(SWITCH_SOUND);break;
      case Event::FAINTED:cue(FAINT_SOUND);break;
      case Event::REVIVED:cue(REVIVE_SOUND);break;
      case Event::STATUS:cue(e.move&&veylith::move(e.move).role==DEBUFF?DEBUFF_SOUND:BOOST_SOUND);break;
      case Event::PRESSURE:cue(HIT_SOUND);break;
      default:break;
    }
  }
  void startAnimation(){_event=0;_frame=0;_nextFrame=millis()+60;page(ANIMATE);eventSound();}
  void ready() {
    Progress candidate = _profile;
    uint32_t id = candidate.reserveResult();
    if (!id) {
      alert("Result IDs exhausted");
      return;
    }
    if (!store(candidate)) return;
    if(!_net)delete _battle;
    _battle = nullptr;
    _resultId = id;
    _awarded = false;_resultSound=false;
    _earned = 0;
    for (unsigned i = 0; i < 3; ++i) {
      _lineup[i] = _profile.selected[i];
      _player[i].family = _lineup[i];
      _player[i].training = _profile.family[_lineup[i]];
    }
#ifdef SOLO_GAMES_MULTIPLAYER
    if(_net){if(!_net->setTeam(_player)){alert("Team not accepted");return;}page(READY);return;}
#endif
    Battle::generateCPU(_profile, ++_seed, _cpu);
    unsigned level = _profile.averageLevel();
    _difficulty = _profile.consecutiveLosses() >= 3 || level < 15 ? 0 : level < 35 ? 1 : 2;
    page(READY);
  }
  void cpuChoice() {
    if(_net)return;
    // Called before human input for this round; CPU never receives that input.
#ifdef ZYGIS_GAME_DIAGNOSTICS
#ifdef NRF52_PLATFORM
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t start = DWT->CYCCNT;
#else
    uint32_t start = micros();
#endif
#endif
    _cpuAction = _battle->chooseCPU(1, _seed ^ (_battle->roundNumber() * 2654435761U), _difficulty);
#ifdef ZYGIS_GAME_DIAGNOSTICS
#ifdef NRF52_PLATFORM
    unsigned elapsed = (DWT->CYCCNT - start + (F_CPU / 1000000) - 1) / (F_CPU / 1000000);
#else
    unsigned elapsed = micros() - start;
#endif
    if (elapsed > _maxAiUs) _maxAiUs = elapsed;
#endif
  }
  void fight() {
    if(_net)return;
    _battle = new (std::nothrow) Battle;
    if (!_battle) {
      alert("Not enough RAM");
      return;
    }
    _battle->begin(_player, _cpu, ++_seed);
    cpuChoice();
    page(FIELD);
  }
  void award(bool showResult=true) {
    if (!_battle || !_battle->result()) return;
    if (!_awarded) {
      Progress candidate = _profile;
      unsigned cpuLevel = 0, playerLevel = 0;
      for (unsigned i = 0; i < 3; ++i) {
        cpuLevel += _cpu[i].training.level();
        playerLevel += _player[i].training.level();
      }
      unsigned before = candidate.family[_lineup[0]].lifetime;
      if (!candidate.award(_resultId, ownResult(), cpuLevel / 3, _lineup, playerLevel / 3)) {
        page(SAVE_ERROR);
        return;
      }
      _earned = candidate.family[_lineup[0]].lifetime - before;
      if (!store(candidate)) {
        page(SAVE_ERROR);
        return;
      }
      _awarded = true;

    }
    if(showResult){page(RESULT);if(!_resultSound){cue(ownResult()==1?veylith::WIN_SOUND:ownResult()==2?veylith::LOSS_SOUND:veylith::DRAW_SOUND);_resultSound=true;}}
  }
  void nextRound() {
#ifdef SOLO_GAMES_MULTIPLAYER
    if(_net){
      if(_net->finished()){award();return;}
      if(!_net->synced()){page(NET_WAITING);return;}
      if(_battle->needsSwitch(ownSide()))page(SWITCH);
      else if(_net->waiting()||_battle->needsSwitch(enemySide()))page(NET_WAITING);
      else page(FIELD);
      return;
    }
#endif
    if (_battle->result()) {
      award();
      return;
    }
    if (_battle->needsSwitch(enemySide())) {
      unsigned best = 3;
      for (unsigned i = 0; i < 3; ++i)
        if (_battle->member(enemySide(), i).hp && (best == 3 || _battle->member(enemySide(), i).hp > _battle->member(enemySide(), best).hp))
          best = i;
      if (best < 3) _battle->forceSwitch(enemySide(), best);
    }
    if (_battle->needsSwitch(ownSide())) {
      page(SWITCH);
      return;
    }
    cpuChoice();
    page(FIELD);
  }
  void play(Action action) {
#ifdef SOLO_GAMES_MULTIPLAYER
    if(_net){if(_net->choose(action))page(NET_WAITING);else alert("Move unavailable");return;}
#endif
    if (!_battle->legal(ownSide(), action)) {
      alert("Move unavailable");
      return;
    }
    if (!_battle->resolve(action, _cpuAction)) {
      alert("Action cancelled");
      return;
    }
    startAnimation();
  }
  void pickAction(unsigned id) {
    _action = Action();
    _action.move = id;
    const auto &move = veylith::move(id);
    if (move.target == veylith::ALLY || move.target == veylith::FAINT ||
        move.effect == veylith::FLASH_RELAY) {
      page(TARGET);
      return;
    }
    play(_action);
  }
  void statusPage(unsigned side, View resume) {
    _resume = resume;
    _statusSide = side;
    _statusMember = 0;
    _statusPage = 0;
    page(STATUS);
  }
  void exitBattle() {
    if(_net){_exit=true;return;}
    if(!_net)delete _battle;
    _battle = nullptr;
    page(HUB);
  }
  void renderField(DisplayDriver &d) {
    using namespace veylith;
    const auto &foe = _battle->member(enemySide(), _battle->active(enemySide()));
    const auto &own = _battle->member(ownSide(), _battle->active(ownSide()));
    char line[24];
    snprintf(line, sizeof(line), "%.10s L%u", families[foe.family].names[foe.stage], foe.level);
    text(d, 0, 0, line);
    d.drawRect(43, 10, 45, 4);
    d.fillRect(44, 11, 43 * foe.hp / foe.stats[HP], 2);
    int recoil = 0;
    if (_view == ANIMATE && _event < _battle->eventCount() && _frame % 2) recoil = 2;
    d.drawXbm(94 - recoil, 10, sprite(foe.family, foe.stage), 32, 32);
    mirrorRuns(sprite(own.family, own.stage), 32, 32, [&](unsigned x, unsigned y, unsigned length) {
      d.fillRect(2 + recoil + x, 22 + y, length, 1);
    });
    snprintf(line, sizeof(line), "L%u E%u R%u", own.level, own.energy, _battle->roundNumber());
    text(d, 38, 23, line);
    clipped(d, 38, 34, families[own.family].names[own.stage], 9);
    snprintf(line, sizeof(line), "%u/%u", own.hp, own.stats[HP]);
    text(d, 38, 44, line);
    d.drawRect(88, 45, 40, 4);
    d.fillRect(89, 46, 38 * own.hp / own.stats[HP], 2);
    if (_view == ANIMATE && _event < _battle->eventCount()) {
      const auto &e = _battle->event(_event);
      const char* who=e.side==ownSide()?"You":_net?"Peer":"CPU";
      if (e.move)
        snprintf(line, sizeof(line), "%s: %.15s", who, move(e.move).name);
      else
        snprintf(line, sizeof(line), "%s %s %u", who,
                 e.kind == Event::FAINTED    ? "fainted"
                 : e.kind == Event::PRESSURE ? "pressure"
                                             : "hit",
                 e.amount);
      text(d, 0, 55, line);
      if (e.kind == Event::STRIKE)
        for (unsigned i = 0; i < 3; ++i)
          d.fillRect(50 + (_frame * 9 + i * 5) % 35, 15 + i * 8, 2, 2);
    } else
      text(d, 0, 55, _view==NET_WAITING?"Waiting for peer":_battle->needsSwitch(ownSide()) ? "Select replacement" : "Enter: actions");
  }

public:
  VeylithScreen(UITask *task, uint32_t seed) : _task(task), _seed(seed ? seed : 1) {
    _profile.reset();
    int n = _task->loadVeylith(_bytes, sizeof(_bytes));
    if (n != -1 &&
        (n != int(sizeof(_bytes)) || !veylith::SaveCodec::decode(_bytes, sizeof(_bytes), _profile)))
      page(CORRUPT);
  }
  ~VeylithScreen() { if(!_net)delete _battle; }
#ifdef SOLO_GAMES_MULTIPLAYER
  VeylithScreen(UITask* task,uint32_t seed,solo_games::multiplayer::ArenaDuel* net):VeylithScreen(task,seed){_net=net;}
#endif
  bool wantsExit() const { return _exit; }
  bool wantsRematch()const{return _requestRematch;}
  bool resultStored()const{return _awarded;}
  bool viewingResult()const{return _view==RESULT;}
  void poll() {
#ifdef SOLO_GAMES_MULTIPLAYER
    if(_net){
      if(_net->teamReady(enemySide()))for(unsigned i=0;i<3;++i)_cpu[i]=_net->team(enemySide())[i];
      if(_net->active()&&!_battle){_battle=_net->battle();page(FIELD);}
      if(_net->finished()&&!_awarded&&_view!=SAVE_ERROR){award(false);if(!_awarded)return;}
      if(_battle&&_net->synced()&&_netRevision!=_net->revision()){
        _netRevision=_net->revision();startAnimation();
      }else if(_view==NET_WAITING&&_battle&&_net->synced()&&!_net->waiting())nextRound();
    }
#endif
    if(_sound!=veylith::SILENT){if(int32_t(millis()-_soundUntil)>=0||_task->playArenaSound(_sound))_sound=veylith::SILENT;}
    if (_view == ANIMATE && int32_t(millis() - _nextFrame) >= 0) {
      _nextFrame = millis() + 60;
      if (++_frame >= 3) {
        _frame = 0;
        if (++_event >= _battle->eventCount()) nextRound();else eventSound();
      }else if(_frame==1&&_event<_battle->eventCount()&&_battle->event(_event).kind==veylith::Event::STRIKE&&_battle->event(_event).amount)cue(veylith::HIT_SOUND);
    }
  }
  int render(DisplayDriver &d) {
    using namespace veylith;
    d.setTextSize(1);
    d.setColor(UIColor::primary_txt);
    char line[32];
    if (_view == FIELD || _view == ANIMATE || _view==NET_WAITING) {
      renderField(d);
      return _view == ANIMATE ? 50 : 300;
    }
    const char *title = _view == HUB         ? "ARENA TRAINER"
                        : _view == TEAM      ? "FAMILY > INFO"
                        : _view == STATS     ? "TRAIN STATS"
                        : _view == BUY       ? "CONFIRM TRAINING"
                        : _view == LOADOUT   ? "MOVE LOADOUT"
                        : _view == LIBRARY   ? "MOVE LIBRARY"
                        : _view == MOVE_INFO ? move(_move).name
                        : _view == READY     ? (_net?"OPPONENT TEAM":"CPU TEAM READY")
                        : _view == ACTIONS   ? "YOUR ACTION"
                        : _view == MOVES     ? "SELECT MOVE"
                        : _view == TARGET    ? "SELECT TARGET"
                        : _view == SWITCH    ? "SELECT CREATURE"
                        : _view == STATUS    ? "TEAM STATUS"
                        : _view == PAUSE     ? "PAUSED"
                        : _view == HELP      ? "ARENA HELP"
                        : _view == LEVEL_UP  ? "TRAINING COMPLETE"
                        : _view == RESULT    ? (ownResult() == 1   ? "VICTORY"
                                                : ownResult() == 2 ? "DEFEAT"
                                                                         : "DRAW")
                                             : "ARENA SAVE";
    if (_view == CREATURE_INFO) title = families[_row].names[_creatureStage];
    char heading[22];
    if (_view == LOADOUT) {
      unsigned n = 0;
      for (unsigned id = 1; id <= 80; ++id)
        n += training().eligible(family(), id);
      snprintf(heading, sizeof(heading), "%u MOVES, 4 SLOTS", n);
      title = heading;
    }
    d.drawCenteredHeader(title);
    if (_view == HUB) {
      d.drawXbm(3, 20, trainerArt, 24, 32);
      d.drawXbm(0, 57, progressArt + (training().total() % 3) * 28, 32, 7);
      snprintf(line, sizeof(line), "%u/3 %.12s", _slot + 1, creatureName());
      text(d, 32, 14, line);
      snprintf(line, sizeof(line), "L%u XP%lu", training().level(), (unsigned long)training().wallet);
      text(d, 32, 24, line);
      const char *actions[] = { "Team", "Train", "Moves", "Ready" };
      for (unsigned i = 0; i < 4; ++i) {
        snprintf(line, sizeof(line), "%c%s", _row == i ? '>' : ' ', actions[i]);
        text(d, 32 + (i % 2) * 48, 36 + (i / 2) * 11, line);
      }
      text(d, 36, 55, "L/R:slot Menu");
    } else if (_view == TEAM) {
      unsigned first = (_row / 3) * 3;
      for (unsigned i = first; i < 20 && i < first + 3; ++i) {
        const auto &t = _profile.family[i];
        int y = 14 + (i - first) * 17;
        d.drawXbm(8, y, mini(i, t.stage()), 16, 16);
        snprintf(line, sizeof(line), "%c%.10s L%u", _row == i ? '>' : ' ', families[i].names[t.stage()],
                 t.level());
        text(d, 26, y + 2, line);
        if (_row == i) text(d, 0, y + 2, ">");
      }
    } else if (_view == STATS) {
      snprintf(line, sizeof(line), "XP%lu L%u %u/3", (unsigned long)training().wallet, training().level(),
               training().total() % 3);
      text(d, 0, 14, line);
      for (unsigned i = (_row / 4) * 4; i < 6 && i < unsigned((_row / 4) * 4 + 4); ++i) {
        snprintf(line, sizeof(line), "%c%s %u r%u", _row == i ? '>' : ' ', statNames[i],
                 training().value(family(), i), training().rank[i]);
        d.drawXbm(0, 25 + (i % 4) * 10, iconArt + (5 + i) * 8, 8, 8);
        text(d, 10, 25 + (i % 4) * 10, line);
      }
    } else if (_view == BUY) {
      Training preview = training();
      if (preview.rank[_stat] < 80 && preview.level() < 99) ++preview.rank[_stat];
      snprintf(line, sizeof(line), "%s %u > %u", statNames[_stat], training().value(family(), _stat),
               preview.value(family(), _stat));
      text(d, 0, 16, line);
      snprintf(line, sizeof(line), "Cost %u XP", training().cost(_stat));
      text(d, 0, 28, line);
      snprintf(line, sizeof(line), "Wallet %lu", (unsigned long)training().wallet);
      text(d, 0, 40, line);
      text(d, 0, 54, _row == 0 ? ">No       Yes" : " No      >Yes");
    } else if (_view == LOADOUT) {
      for (unsigned i = 0; i < 4; ++i) {
        snprintf(line, sizeof(line), "%c%u %.17s", _row == i ? '>' : ' ', i + 1,
                 move(training().equipped[i]).name);
        text(d, 0, 15 + i * 12, line);
      }
    } else if (_view == LIBRARY) {
      unsigned first = (_row / 4) * 4;
      for (unsigned i = first; i < libraryCount() && i < first + 4; ++i) {
        unsigned id = libraryId(i);
        snprintf(line, sizeof(line), "%c%c%.16s", _row == i ? '>' : ' ',
                 training().eligible(family(), id) ? ' ' : '*', move(id).name);
        text(d, 0, 15 + (i - first) * 12, line);
      }
    } else if (_view == MOVE_INFO) {
      const auto &m = move(_move);
      const auto &t = infoTraining();
      unsigned f = infoFamily();
      if (_infoPage == 0) {
        snprintf(line, sizeof(line), "%s %s P%u", elementNames[m.element], roleNames[m.role], m.power);
        text(d, 0, 14, line);
        snprintf(line, sizeof(line), "E%u CD%u", m.energy, m.cooldown);
        text(d, 0, 24, line);
        text(d, 0, 34, targetNames[m.target]);
        snprintf(line, sizeof(line), "L%u %s", m.level, t.eligible(f, _move) ? "[available]" : "[locked]");
        text(d, 0, 44, line);
      } else if (_infoPage == 1)
        wrap(d, m.description, 14, 4);
      else if (_infoPage == 2)
        wrap(d, m.effectText, 14, 4);
      else {
        snprintf(line, sizeof(line), "%c Level %u/%u", t.level() >= m.level ? '+' : '!', t.level(), m.level);
        text(d, 0, 14, line);
        for (unsigned i = 0; i < 2; ++i) {
          if (!m.requiredValue[i]) continue;
          unsigned value = t.value(f, m.requiredStat[i]);
          snprintf(line, sizeof(line), "%c %s %u/%u", value >= m.requiredValue[i] ? '+' : '!',
                   statNames[m.requiredStat[i]], value, m.requiredValue[i]);
          text(d, 0, 26 + i * 12, line);
        }
        if (!m.requiredValue[0]) text(d, 0, 30, "No stat requirement");
      }
      text(d, 0, 55,
           _infoResume == MOVES   ? "L/R:page Enter:back"
           : t.eligible(f, _move) ? "L/R:page Enter:equip"
                                  : "L/R:page [locked]");
    } else if (_view == CREATURE_INFO) {
      if (!_infoPage) {
        d.drawXbm(0, 16, sprite(_row, _creatureStage), 32, 32);
        snprintf(line, sizeof(line), "%s/%s", elementNames[families[_row].primary],
                 elementNames[families[_row].secondary]);
        text(d, 36, 16, line);
        snprintf(line, sizeof(line), "Form %u at L%u", _creatureStage + 1,
                 _creatureStage == 2   ? 35
                 : _creatureStage == 1 ? 15
                                       : 1);
        text(d, 36, 28, line);
        text(d, 36, 40, _creatureStage > _profile.family[_row].stage() ? "Form preview" : "Form unlocked");
      } else
        wrap(d, families[_row].descriptions[_creatureStage], 14, 4);
      text(d, 0, 55, "L/R:form Enter:notes");
    } else if (_view == READY) {
      if(_net&&!peerTeamReady()){text(d,0,18,"Waiting for opponent");text(d,0,34,"Team sent");text(d,0,50,"Menu: pause");return 200;}
      for (unsigned i = 0; i < 3; ++i) {
        auto &t = _cpu[i].training;
        snprintf(line, sizeof(line), "%.14s L%u", families[_cpu[i].family].names[t.stage()], t.level());
        text(d, 0, 15 + i * 11, line);
      }
      text(d, 0, 48, _net?"Linking battle":_difficulty == 0 ? "CPU: Novice" : _difficulty == 1 ? "CPU: Regular" : "CPU: Veteran");
      text(d,0,55,_net?"R:inspect Menu:pause":"Enter:fight R:inspect");
    } else if (_view == ACTIONS) {
      const char *items[] = { "Moves", "Plain Strike (E0)", "Switch", "Team status" };
      for (unsigned i = 0; i < 4; ++i) {
        snprintf(line, sizeof(line), "%c%s", _row == i ? '>' : ' ', items[i]);
        text(d, 0, 15 + i * 12, line);
      }
    } else if (_view == MOVES) {
      const auto &own = _battle->member(ownSide(), _battle->active(ownSide()));
      for (unsigned i = 0; i < 4; ++i) {
        auto id = own.moves[i];
        int x = (i % 2) * 64, y = 15 + (i / 2) * 24;
        snprintf(line, sizeof(line), "%c%.9s", _row == i ? '>' : ' ', move(id).name);
        text(d, x, y, line);
        snprintf(line, sizeof(line), "E%u CD%u", _battle->cost(ownSide(), id), own.cooldown[i]);
        text(d, x, y + 10, line);
      }
      text(d, 0, 55, "Menu:back");
    } else if (_view == TARGET || _view == SWITCH) {
      for (unsigned i = 0; i < 3; ++i) {
        const auto &m = _battle->member(ownSide(), i);
        snprintf(line, sizeof(line), "%c%.11s %u", _row == i ? '>' : ' ', families[m.family].names[m.stage],
                 m.hp);
        text(d, 0, 16 + i * 15, line);
      }
    } else if (_view == STATUS) {
      unsigned f, level, stage;
      const Training *train = nullptr;
      if (_resume == READY) {
        const auto &build = _statusSide ? _cpu[_statusMember] : _player[_statusMember];
        f = build.family;
        train = &build.training;
        level = train->level();
        stage = train->stage();
      } else {
        auto &m = _battle->member(battleSide(_statusSide), _statusMember);
        f = m.family;
        level = m.level;
        stage = m.stage;
      }
      snprintf(line, sizeof(line), "%s%u %.10s L%u", _statusSide ? (_net?"Peer":"CPU") : "You", _statusMember + 1,
               families[f].names[stage], level);
      text(d, 0, 14, line);
      if (_statusPage == 3) {
        wrap(d, families[f].descriptions[stage], 25, 4);
      } else if (_statusPage == 0) {
        for (unsigned i = 0; i < 6; ++i) {
          unsigned value = train ? train->value(f, i) : _battle->effective(battleSide(_statusSide), _statusMember, i);
          snprintf(line, sizeof(line), "%s %u", statNames[i], value);
          text(d, (i % 2) * 64, 27 + (i / 2) * 10, line);
        }
      } else if (_statusPage == 1) {
        for (unsigned i = 0; i < 4; ++i) {
          auto id = train ? train->equipped[i] : _battle->member(battleSide(_statusSide), _statusMember).moves[i];
          snprintf(line, sizeof(line), "%.17s E%u", move(id).name, move(id).energy);
          text(d, 0, 25 + i * 9, line);
        }
      } else if (train) {
        snprintf(line, sizeof(line), "%s/%s", elementNames[families[f].primary],
                 elementNames[families[f].secondary]);
        text(d, 0, 30, line);
      } else {
        auto &m = _battle->member(battleSide(_statusSide), _statusMember);
        snprintf(line, sizeof(line), "HP %u/%u E%u", m.hp, m.stats[HP], m.energy);
        text(d, 0, 27, line);
        const char *statuses[] = { "Clear", "Burn", "Poison", "Shock", "Root" };
        snprintf(line, sizeof(line), "Shield%u %s", m.shield, statuses[m.status]);
        text(d, 0, 38, line);
        snprintf(line, sizeof(line), "Buffs %u/%u Root %u", m.modifiers[0].turns, m.modifiers[2].turns,
                 m.status == ROOT);
        text(d, 0, 49, line);
      }
    } else if (_view == RESULT) {
      snprintf(line, sizeof(line), "+%u XP / creature", _earned);
      text(d, 0, 17, line);
      text(d, 0, 30, "All three rewarded");
      text(d,0,43,_net?(_requestRematch?"Starting rematch...":"Enter: Play again"):"Enter: Trainer");
      text(d,0,55,_net?"Menu: leave match":"Menu: Games");
    } else if (_view == PAUSE) {
      text(d, 0, 20, _row == 0 ? ">Resume" : " Resume");
      text(d, 0, 37, _row == 1 ? ">Abandon (no XP)" : " Abandon (no XP)");
    } else if (_view == SAVE_ERROR) {
      text(d, 0, 18, "Reward not saved");
      text(d, 0, 33, "Enter: retry save");
      text(d, 0, 48, "Menu: leave unsaved");
    } else if (_view == CORRUPT) {
      text(d, 0, 18, "Unreadable Arena save");
      text(d, 0, 31, "Enter: recovery");
      text(d, 0, 44, "Menu: leave intact");
    } else if (_view == RESET_CONFIRM) {
      text(d, 0, 16, "Back up and reset");
      text(d, 0, 29, "Arena progress only?");
      text(d, 0, 49, _row == 0 ? ">No       Yes" : " No      >Yes");
    } else if (_view == HELP) {
      text(d, 0, 15, "L/R: team slot");
      text(d, 0, 25, "Team Context: notes");
      text(d, 0, 35, "Moves swap for free");
      text(d, 0, 45, "Strike is always E0");
      text(d, 0, 55, "Ready -> CPU -> fight");
    } else if (_view == LEVEL_UP) {
      d.drawXbm(0, 20, sprite(family(), training().stage()), 32, 32);
      snprintf(line, sizeof(line), "L%u > L%u", _oldLevel, training().level());
      text(d, 36, 16, line);
      clipped(d, 36, 27, creatureName(), 15);

      unsigned unlocked = unlockCount();
      snprintf(line, sizeof(line), "%u new moves", unlocked);
      text(d, 36, 39, line);
      if (unlocked)
        clipped(d, 36, 53, move(unlockId(_unlockRow)).name, 15);
      else
        text(d, 36, 53, "Enter: continue");
    }
    return 300;
  }
  bool handleInput(char input) {
    using namespace veylith;
    uint8_t key = uint8_t(input);
    bool up = key == KEY_UP, down = key == KEY_DOWN, left = key == KEY_LEFT, right = key == KEY_RIGHT;
    if (key == KEY_CANCEL) {
      switch (_view) {
      case HUB:
      case CORRUPT:
      case RESULT:
      case SAVE_ERROR:
        _exit = true;
        break;
      case TEAM:
      case STATS:
      case LOADOUT:
      case HELP:
        page(HUB);
        break;
      case READY:
        if(_net){_resume=READY;page(PAUSE);}else page(HUB);break;
      case BUY:
        page(STATS, _stat);
        break;
      case LIBRARY:
        page(LOADOUT, _moveSlot);
        break;
      case MOVE_INFO:
        page(_infoResume, _infoResume == LIBRARY ? _libraryRow : _moveSlot);
        break;
      case CREATURE_INFO:
        page(TEAM, _row);
        break;
      case LEVEL_UP:
        page(STATS, _stat);
        break;
      case FIELD:
      case NET_WAITING:
      case ANIMATE:
        _resume = _view;
        page(PAUSE);
        break;
      case ACTIONS:
        page(FIELD);
        break;
      case MOVES:
      case TARGET:
        page(ACTIONS);
        break;
      case SWITCH:
        if (!_battle->needsSwitch(ownSide()))
          page(ACTIONS);
        else {
          _resume = SWITCH;
          page(PAUSE);
        }
        break;
      case STATUS:
        page(_resume);
        break;
      case RESET_CONFIRM:
        page(CORRUPT);
        break;
      case PAUSE:
        page(_resume);
        _nextFrame = millis() + 60;
        break;
      }
      return true;
    }
    if (_view == MOVE_INFO && (left || right)) {
      _infoPage = (_infoPage + (left ? 3 : 1)) % 4;
      return true;
    }
    if (_view == CREATURE_INFO) {
      if (left) _creatureStage = (_creatureStage + 2) % 3;
      if (right) _creatureStage = (_creatureStage + 1) % 3;
      if (key == KEY_ENTER) _infoPage = 1 - _infoPage;
      return true;
    }
    if (_view == TEAM && (key == KEY_CONTEXT_MENU || right)) {
      _creatureStage = _profile.family[_row].stage();
      _infoPage = 0;
      page(CREATURE_INFO, _row);
      return true;
    }
    if (_view == MOVES && key == KEY_CONTEXT_MENU) {
      _moveSlot = _row;
      _move = _battle->member(ownSide(), _battle->active(ownSide())).moves[_row];
      _infoResume = MOVES;
      _infoPage = 0;
      page(MOVE_INFO);
      return true;
    }
    if (_view == LEVEL_UP && (left || right)) {
      unsigned count = unlockCount();
      if (count) _unlockRow = (_unlockRow + (left ? count - 1 : 1)) % count;
      return true;
    }
    if(_view==NET_WAITING){if(left||right)statusPage(right?1:0,NET_WAITING);return true;}
    if (_view == FIELD) {
      if (key == KEY_ENTER)
        page(ACTIONS);
      else if (left || right || key == KEY_CONTEXT_MENU)
        statusPage(right ? 1 : 0, FIELD);
      return true;
    }
    if (_view == ANIMATE) return true;
    if (_view == STATUS) {
      if (up) _statusMember = (_statusMember + 2) % 3;
      if (down) _statusMember = (_statusMember + 1) % 3;
      if (left || right) {
        _statusSide = 1 - _statusSide;
      }
      if (key == KEY_ENTER) _statusPage = (_statusPage + 1) % 4;
      return true;
    }
    if (_view == HUB) {
      if (left) _slot = (_slot + 2) % 3;
      if (right) _slot = (_slot + 1) % 3;
      if (up) _row = (_row + 3) % 4;
      if (down) _row = (_row + 1) % 4;
      if (key == KEY_CONTEXT_MENU) {
        page(HELP);
        return true;
      }
      if (key == KEY_ENTER) {
        if (_row == 0)
          page(TEAM, family());
        else if (_row == 1)
          page(STATS);
        else if (_row == 2)
          page(LOADOUT);
        else
          ready();
      }
      // Up/down move linearly too, so all actions work on three-button adapters.
      return true;
    }
    unsigned count = _view == TEAM                                              ? 20
                     : _view == STATS                                           ? 6
                     : _view == LOADOUT || _view == ACTIONS || _view == MOVES   ? 4
                     : _view == LIBRARY                                         ? libraryCount()
                     : _view == TARGET || _view == SWITCH                       ? 3
                     : _view == BUY || _view == PAUSE || _view == RESET_CONFIRM ? 2
                                                                                : 0;
    if (count) {
      if (up) _row = (_row + count - 1) % count;
      if (down) _row = (_row + 1) % count;
      if (left) _row = (_row + count - 1) % count;
      if (right) _row = (_row + 1) % count;
    }
    if (_view == READY && (left || right)) {
      if(right&&!peerTeamReady()){alert("Waiting for opponent");return true;}
      statusPage(right ? 1 : 0, READY);
      return true;
    }
    if (key != KEY_ENTER) return true;
    if (_view == TEAM) {
      Progress candidate = _profile;
      if (!candidate.select(_slot, _row)) {
        alert("Family already in team");
        return true;
      }
      if (store(candidate)) page(HUB);
    } else if (_view == STATS) {
      _stat = _row;
      page(BUY);
    } else if (_view == BUY) {
      if (!_row) {
        page(STATS, _stat);
        return true;
      }
      Progress candidate = _profile;
      _oldLevel = training().level();
      _oldStage = training().stage();
      if (!candidate.family[family()].buy(_stat)) {
        alert("Need XP or stat capped");
        return true;
      }
      uint8_t unlocked[10] = {};
      for (unsigned id = 1; id <= 80; ++id)
        if (candidate.family[family()].eligible(family(), id) && !training().eligible(family(), id))
          unlocked[(id - 1) / 8] |= 1U << ((id - 1) % 8);
      if (store(candidate)) {
        memcpy(_newMoves, unlocked, sizeof(_newMoves));
        _unlockRow = 0;
        page(LEVEL_UP);
        cue(_oldStage!=training().stage()?veylith::EVOLVE_SOUND:_oldLevel!=training().level()?veylith::LEVEL_SOUND:veylith::TRAIN_SOUND);
      }
    } else if (_view == LEVEL_UP)
      page(STATS, _stat);
    else if (_view == LOADOUT) {
      _moveSlot = _row;
      page(LIBRARY);
    } else if (_view == LIBRARY) {
      _move = libraryId(_row);
      _libraryRow = _row;
      _infoPage = 0;
      _infoResume = LIBRARY;
      page(MOVE_INFO);
    } else if (_view == MOVE_INFO) {
      if (_infoResume == MOVES) {
        page(MOVES, _moveSlot);
        return true;
      }
      Progress candidate = _profile;
      if (!candidate.family[family()].equip(family(), _moveSlot, _move)) {
        alert("Locked or already set");
        return true;
      }
      if (store(candidate)) page(LOADOUT, _moveSlot);
    } else if (_view == READY)
      fight();
    else if (_view == ACTIONS) {
      if (_row == 0)
        page(MOVES);
      else if (_row == 1)
        pickAction(41);
      else if (_row == 2)
        page(SWITCH);
      else
        statusPage(0, ACTIONS);
    } else if (_view == MOVES)
      pickAction(_battle->member(ownSide(), _battle->active(ownSide())).moves[_row]);
    else if (_view == TARGET) {
      _action.target = _row;
      play(_action);
    } else if (_view == SWITCH) {
      if (_battle->needsSwitch(ownSide())) {
        bool switched=false;
#ifdef SOLO_GAMES_MULTIPLAYER
        if(_net)switched=_net->forceSwitch(_row);else
#endif
        switched=_battle->forceSwitch(ownSide(),_row);
        if(switched)nextRound();
        else
          alert("Choose living creature");
      } else {
        Action action;
        action.switching = true;
        action.target = _row;
        play(action);
      }
    } else if (_view == RESULT){
      if(_net)_requestRematch=true;else exitBattle();
    }
    else if (_view == SAVE_ERROR)
      award();
    else if (_view == PAUSE) {
      if (_row)
        exitBattle();
      else {
        page(_resume);
        _nextFrame = millis() + 60;
      }
    } else if (_view == HELP)
      page(HUB);
    else if (_view == CORRUPT)
      page(RESET_CONFIRM);
    else if (_view == RESET_CONFIRM) {
      if (!_row)
        page(CORRUPT);
      else if (!_task->backupVeylith())
        alert("Backup failed; kept save");
      else {
        Progress candidate;
        candidate.reset();
        if (store(candidate)) page(HUB);
      }
    }
    return true;
  }
#ifdef ZYGIS_GAME_DIAGNOSTICS
  size_t diagnostics(uint8_t *out, size_t size, size_t ownerSize) const {
    if (size < 44) return 0;
    uint32_t values[] = { uint32_t(ownerSize + sizeof(VeylithScreen) + (_battle ? sizeof(Battle) : 0)),
                          _battle ? _battle->roundNumber() : 0,
                          _battle ? _battle->result() : 0,
                          training().level(),
                          training().wallet,
                          _view,
                          16,
                          _row,
                          _maxAiUs,
                          _battle ? _battle->stateHash() : 0,
                          _resultId };
    size_t n = 0;
    for (auto value : values)
      for (unsigned shift = 0; shift < 32; shift += 8)
        out[n++] = uint8_t(value >> shift);
    return n;
  }
#endif
};
static_assert(sizeof(VeylithScreen) < 2048, "Arena screen excludes art and owns only bounded state");
