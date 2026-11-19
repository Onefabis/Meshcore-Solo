#pragma once
#include <games/multiplayer/Session.h>
#include <games/multiplayer/SecureChannel.h>
#include <games/multiplayer/ArenaDuel.h>
#include "VeylithScreen.h"
class ArenaOnlineScreen {
  using Session=solo_games::multiplayer::Session;
  using Packet=solo_games::multiplayer::Packet;
  using Codec=solo_games::multiplayer::Codec;
  using SecureChannel=solo_games::multiplayer::SecureChannel;
  UITask* _task;
  SecureChannel _security;
  Session _session;
  struct Match {
    solo_games::multiplayer::Outbox out;
    solo_games::multiplayer::ArenaDuel duel;
    Match(Session& session,SecureChannel& security):duel(out,security,session.id(),session.matchRound(),session.slot()){}
  };
  Match* _match=nullptr;
  VeylithScreen* _arena=nullptr;
  enum View:uint8_t { MENU,CHANNELS,FIND,WAITING,PLAY,ERROR };
  View _view=MENU;
  uint8_t _row=0,_channels[40]={},_count=0,_digest[16]={};
  uint16_t _generation=0;
  uint64_t _selection=0;
  bool _bound=false,_exit=false,_again=false;
  const char* _error="Connection failed";
  static solo_games::multiplayer::IdentityCallbacks callbacks(UITask* task){
    solo_games::multiplayer::IdentityCallbacks c;c.context=task;
    c.publicKey=[](void* p,uint8_t* out){static_cast<UITask*>(p)->gamePublicKey(out);};
    c.sharedSecret=[](void* p,const uint8_t* peer,uint8_t* out){static_cast<UITask*>(p)->gameSharedSecret(peer,out);};
    c.randomBytes=[](void* p,uint8_t* out,size_t n){return static_cast<UITask*>(p)->gameRandomBytes(out,n);};return c;
  }
  static void text(DisplayDriver& d,int x,int y,const char* value){d.setCursor(x,y);d.print(value);}
  void closeMatch(){delete _arena;_arena=nullptr;delete _match;_match=nullptr;}
  void leave(){
    if(_bound)for(unsigned i=0;i<3;++i){Packet packet;char frame[113];if(_session.leave(i,packet)&&Codec::encode(packet,frame,sizeof(frame)))_task->sendGameText(_session.channel(),frame,_digest);}
    closeMatch();_session.find(solo_games::multiplayer::ARENA);_bound=false;_generation=0;_again=false;
  }
  void error(const char* reason){_error=reason;leave();_view=ERROR;}
  unsigned lobbyCount()const{unsigned n=0;for(unsigned i=0;i<Session::LOBBIES;++i)n+=_session.lobby(i,millis())!=nullptr;return n;}
  unsigned lobbyIndex(unsigned row)const{for(unsigned i=0;i<Session::LOBBIES;++i)if(_session.lobby(i,millis())){if(!row)return i;--row;}return Session::LOBBIES;}
  void find(){leave();_view=FIND;_row=0;_selection=0;}
  void synchronizeSelection(){unsigned count=lobbyCount();if(!count){_row=0;_selection=0;return;}
    unsigned row=0;for(unsigned i=0;i<Session::LOBBIES;++i)if(auto lobby=_session.lobby(i,millis())){if(lobby->open.session==_selection){_row=row;return;}++row;}
    _row=0;auto lobby=_session.lobby(lobbyIndex(0),millis());_selection=lobby?lobby->open.session:0;
  }
public:
  explicit ArenaOnlineScreen(UITask* task):_task(task),_security(callbacks(task)),_session(_security){_session.find(solo_games::multiplayer::ARENA);}
  ~ArenaOnlineScreen(){leave();}
  bool wantsExit()const{return _exit;}
  void receiveText(uint8_t channel,const char* body,const char* sender){Packet packet;if(!Codec::decode(body,packet)||packet.game!=solo_games::multiplayer::ARENA)return;
    char name[2];uint8_t digest[16];if(!_task->gameChannel(channel,name,sizeof(name),digest))return;
    if(_bound&&(channel!=_session.channel()||memcmp(digest,_digest,16)))return;
    _session.receive(channel,packet,millis(),sender);
  }
  void poll(){
    if(_bound){char name[2];uint8_t digest[16];if(!_task->gameChannel(_session.channel(),name,sizeof(name),digest)||memcmp(digest,_digest,16)){_bound=false;error("Channel changed");return;}}
    _session.poll(millis());
    if(_session.phase()==Session::ERROR){error("Connection failed");return;}
    if(_session.phase()==Session::CLOSED){error("Other player left");return;}
    if(_view==FIND)synchronizeSelection();
    if((_session.phase()==Session::PLAYING||_session.phase()==Session::FINISHED)&&_generation!=_session.generation()){
      closeMatch();_match=new(std::nothrow) Match(_session,_security);if(!_match){error("Not enough RAM");return;}
      _arena=new(std::nothrow) VeylithScreen(_task,uint32_t(_session.id()),&_match->duel);
      if(!_arena){error("Not enough RAM");return;}_generation=_session.generation();_again=false;_view=PLAY;
    }
    if(_match){solo_games::multiplayer::Message message;
      for(unsigned i=0;i<4&&_session.take(message);++i){int actor=_session.slotFor(message.actor);if(actor<0){error("Game out of sync");return;}_match->duel.receive(message,actor);}
      if(_match->duel.error()){error("Game out of sync");return;}
      _arena->poll();
      if(_arena->wantsExit()){leave();_view=MENU;_row=0;return;}
      if(auto outgoing=_match->out.front())if(_session.send(outgoing->message.type,outgoing->message.data,outgoing->message.size,outgoing->fast))_match->out.pop();
      if(_match->duel.finished()&&_arena->resultStored()&&_match->out.empty()&&!_session.busy())_session.finish();
      if(_arena->wantsRematch()&&!_again&&_session.phase()==Session::FINISHED){if(_session.rematch())_again=true;}
    }
    if(_bound){Packet packet;char frame[113];if(_session.outgoing(millis(),packet)){
      if(Codec::encode(packet,frame,sizeof(frame))&&_task->sendGameText(_session.channel(),frame,_digest))_session.sent(millis());
      else _session.backpressure(millis());
    }}
  }
  bool handleInput(char input){uint8_t key=uint8_t(input);
    if(_view==PLAY&&_arena){if(_session.stalled()&&key==KEY_CANCEL){leave();_view=MENU;_row=0;return true;}if(_session.stalled()&&key==KEY_ENTER){_session.retry(millis());return true;}_arena->handleInput(input);return true;}
    if(key==KEY_CANCEL){if(_view==MENU)_exit=true;else{leave();_view=MENU;_row=0;}return true;}
    if(_view==MENU){if(key==KEY_UP||key==KEY_DOWN)_row=1-_row;
      if(key==KEY_ENTER){if(_row)find();else{_count=0;for(unsigned i=0;i<40;++i){char name[2];uint8_t digest[16];if(_task->gameChannel(i,name,sizeof(name),digest))_channels[_count++]=i;}_row=0;_view=CHANNELS;}}return true;}
    if(_view==ERROR){if(key==KEY_ENTER){leave();_view=MENU;_row=0;}return true;}
    unsigned count=_view==CHANNELS?_count:_view==FIND?lobbyCount():0;
    if(count){if(key==KEY_UP)_row=(_row+count-1)%count;if(key==KEY_DOWN)_row=(_row+1)%count;
      if(_view==FIND){auto lobby=_session.lobby(lobbyIndex(_row),millis());_selection=lobby?lobby->open.session:0;}
    }
    if(count&&key==KEY_ENTER){unsigned slot;
      if(_view==CHANNELS)slot=_channels[_row];else{auto lobby=_session.lobby(lobbyIndex(_row),millis());if(!lobby)return true;slot=lobby->channel;}
      char name[2];uint8_t digest[16];if(!_task->gameChannel(slot,name,sizeof(name),digest))return true;bool ok;
      if(_view==CHANNELS){uint64_t session=0;if(!_security.random(reinterpret_cast<uint8_t*>(&session),sizeof(session))){error("Connection failed");return true;}
        ok=_session.host(solo_games::multiplayer::ARENA,2,slot,session?session:1,_task->gameNodeName());
      }else ok=_session.join(lobbyIndex(_row),millis(),_task->gameNodeName());
      if(ok){memcpy(_digest,digest,16);_bound=true;_view=WAITING;_row=0;}
    }
    return true;
  }
  int render(DisplayDriver& d){d.setTextSize(1);d.setColor(UIColor::primary_txt);
    if(_view==PLAY&&_arena){if(_session.stalled()){d.drawCenteredHeader("WAITING FOR PLAYER");text(d,0,20,"Link interrupted");text(d,0,34,"Enter: retry");text(d,0,49,"Menu: leave");return 200;}
      return _arena->render(d);}
    d.drawCenteredHeader("ARENA MULTIPLAYER");
    if(_view==MENU){text(d,0,18,_row==0?"> Host game":"  Host game");text(d,0,35,_row==1?"> Find game":"  Find game");text(d,0,53,"Menu: Games");}
    else if(_view==CHANNELS){if(!_count){text(d,0,16,"No private channels");text(d,0,29,"configured. Add one");text(d,0,42,"in Solo or the app.");}
      for(unsigned i=(_row/4)*4;i<_count&&i<unsigned((_row/4)*4+4);++i){char name[19];uint8_t digest[16];if(_task->gameChannel(_channels[i],name,sizeof(name),digest)){text(d,0,16+(i%4)*12,_row==i?">":" ");text(d,12,16+(i%4)*12,name);}}
    }else if(_view==FIND){unsigned count=lobbyCount();if(!count){text(d,0,22,"Listening for hosts");text(d,0,40,"Waiting for a game");}
      for(unsigned i=(_row/2)*2;i<count&&i<unsigned((_row/2)*2+2);++i)if(auto lobby=_session.lobby(lobbyIndex(i),millis())){char name[22];snprintf(name,sizeof(name),"%c %.18s",_row==i?'>':' ',lobby->name[0]?lobby->name:"Unknown host");text(d,0,15+(i%2)*24,name);text(d,12,26+(i%2)*24,"Veylith Arena");}
    }else if(_view==ERROR){text(d,0,21,_error);text(d,0,42,"Enter: multiplayer");}
    else{bool hosting=_session.isHost();text(d,0,18,hosting?"Waiting for player":"Joining game...");if(!hosting)text(d,0,34,_session.name(0));text(d,0,52,"Menu: leave");}
    return 200;
  }
#ifdef ZYGIS_GAME_DIAGNOSTICS
  size_t diagnostics(uint8_t* out,size_t size,size_t ownerSize)const{if(size<44)return 0;uint8_t child[44]={};if(_arena)_arena->diagnostics(child,sizeof(child),0);unsigned childView=_arena?Codec::get(child+20,4):_row;uint32_t values[]={uint32_t(ownerSize+sizeof(*this)+(_match?sizeof(Match)+sizeof(VeylithScreen):0)),uint32_t(_session.phase()),uint32_t(_session.slot()),uint32_t(_session.matchRound()),uint32_t(_session.generation()),uint32_t(_view),17,childView,_match?uint32_t(_match->duel.revision()):0,_match&&_match->duel.battle()?_match->duel.battle()->stateHash():0,_arena?uint32_t(_arena->resultStored()):0};size_t n=0;for(auto value:values)for(unsigned shift=0;shift<32;shift+=8)out[n++]=uint8_t(value>>shift);return n;}
#endif
};
