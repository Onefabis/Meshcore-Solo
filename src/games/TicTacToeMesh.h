#pragma once
#include "TicTacToe.h"
#include <stddef.h>
#include <string.h>

namespace solo_games {

// Compact channel text protocol. Group encryption supplies privacy; these
// public player IDs and CRC are routing/corruption checks, not authentication.
struct GameEvent {
  enum Type : uint8_t { OPEN=1, JOIN, ACCEPT, READY_REQUEST, READY, START,
    MOVE_REQUEST, MOVE, END, CANCEL, LEAVE, ACK, REMATCH_REQUEST, RESTART };
  uint64_t session = 0;
  uint32_t host = 0, actor = 0, peer = 0;
  uint16_t seq = 0, action = 0, x = 0, o = 0;
  uint8_t type = 0, cell = 255, aux = 0, round = 0;
};

class GameCodec {
  static uint16_t crc(const uint8_t* bytes, size_t count) {
    uint16_t value = 0xFFFF;
    while (count--) {
      value ^= uint16_t(*bytes++) << 8;
      for (unsigned i=0; i<8; ++i) value = value & 0x8000 ? (value << 1) ^ 0x1021 : value << 1;
    }
    return value;
  }
  static void pack(const GameEvent& e, uint8_t* bytes) {
    bytes[0]=2; bytes[1]=1; bytes[2]=1; bytes[3]=e.type;
    for (unsigned i=0;i<8;++i) bytes[4+i]=uint8_t(e.session>>(8*i));
    const uint32_t ids[]={e.host,e.actor,e.peer};
    for (unsigned j=0;j<3;++j) for (unsigned i=0;i<4;++i) bytes[12+j*4+i]=uint8_t(ids[j]>>(8*i));
    const uint16_t values[]={uint16_t((uint16_t(e.round)<<4)|e.seq),e.action,e.x,e.o};
    for (unsigned j=0;j<4;++j) {bytes[24+j*2]=uint8_t(values[j]);bytes[25+j*2]=uint8_t(values[j]>>8);}
    bytes[32]=e.cell;bytes[33]=e.aux;
    uint16_t check=crc(bytes,34);bytes[34]=uint8_t(check);bytes[35]=uint8_t(check>>8);
  }
public:
  static bool equal(const GameEvent& a,const GameEvent& b) {
    uint8_t aa[36],bb[36];pack(a,aa);pack(b,bb);return !memcmp(aa,bb,36);
  }
  static bool encode(const GameEvent& e,char* text,size_t size) {
    if(size<53)return false;
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    uint8_t bytes[36];pack(e,bytes);memcpy(text,"~G1:",4);
    for(unsigned i=0,j=4;i<36;i+=3,j+=4){
      uint32_t n=uint32_t(bytes[i])<<16|uint32_t(bytes[i+1])<<8|bytes[i+2];
      text[j]=alphabet[n>>18];text[j+1]=alphabet[(n>>12)&63];text[j+2]=alphabet[(n>>6)&63];text[j+3]=alphabet[n&63];
    }
    text[52]=0;return true;
  }
  static bool decode(const char* text,GameEvent& e) {
    if(!text || strncmp(text,"~G1:",4) || strlen(text)!=52)return false;
    uint8_t bytes[36];
    for(unsigned i=0,j=4;i<36;i+=3,j+=4){
      uint32_t n=0;
      for(unsigned k=0;k<4;++k){
        char c=text[j+k];int v=c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:
          c>='0'&&c<='9'?c-'0'+52:c=='-'?62:c=='_'?63:-1;
        if(v<0)return false;n=(n<<6)|unsigned(v);
      }
      bytes[i]=uint8_t(n>>16);bytes[i+1]=uint8_t(n>>8);bytes[i+2]=uint8_t(n);
    }
    if(bytes[0]!=2 || bytes[1]!=1 || bytes[2]!=1 || bytes[3]<GameEvent::OPEN || bytes[3]>GameEvent::RESTART ||
      crc(bytes,34)!=(uint16_t(bytes[34])|uint16_t(bytes[35])<<8))return false;
    GameEvent decoded;decoded.type=bytes[3];
    for(unsigned i=0;i<8;++i)decoded.session|=uint64_t(bytes[4+i])<<(8*i);
    uint32_t* ids[]={&decoded.host,&decoded.actor,&decoded.peer};
    for(unsigned j=0;j<3;++j)for(unsigned i=0;i<4;++i)*ids[j]|=uint32_t(bytes[12+j*4+i])<<(8*i);
    uint16_t* values[]={&decoded.seq,&decoded.action,&decoded.x,&decoded.o};
    for(unsigned j=0;j<4;++j)*values[j]=uint16_t(bytes[24+j*2])|uint16_t(bytes[25+j*2])<<8;
    if(decoded.seq>4095)return false;
    decoded.round=uint8_t(decoded.seq>>4);decoded.seq&=15;
    decoded.cell=bytes[32];decoded.aux=bytes[33];
    if(!decoded.session || !decoded.host || !decoded.actor || decoded.seq>=16 ||
      (decoded.x|decoded.o)>511 || (decoded.x&decoded.o))return false;
    e=decoded;return true;
  }
};

// Entire runtime belongs to the visible GamesScreen. No background lobbies,
// history files or gameplay heartbeat. Lobby traffic runs only while visible.
class TicTacToeMesh {
public:
  enum Phase : uint8_t { IDLE, HOSTING, JOINING, JOINED, PLAYER_READY, PLAYING, FINISHED, CLOSED };
  struct Lobby { GameEvent open; uint32_t seen=0; uint8_t channel=0; char name[19]={}; };
  static constexpr unsigned LOBBY_COUNT=4;
  Phase phase() const {return _phase;}
  bool isHost() const {return _self && _self==_host;}
  uint8_t channel() const {return _channel;}
  uint8_t mark() const {return isHost()?1:2;}
  uint32_t candidate() const {return _candidate;}
  bool busy() const {return _reliable_active;}
  bool stalled() const {return _reliable_active && _timed_out;}
  const TicTacToePosition& position() const {return _position;}
  uint16_t sequence() const {return _count ? _count-1 : 0;}
  const Lobby* lobby(unsigned i,uint32_t now) const {
    return i<LOBBY_COUNT && _lobbies[i].open.session && uint32_t(now-_lobbies[i].seen)<5000 ? &_lobbies[i] : nullptr;
  }
  void reset() {*this=TicTacToeMesh();}

  bool host(uint8_t channel,uint64_t session,uint32_t self,uint32_t now) {
    if(!session || !self)return false;
    reset();_channel=channel;_session=session;_host=_self=self;_phase=HOSTING;
    GameEvent e=event(GameEvent::OPEN);_commits[0]=e;_count=1;
    _notice=e;_notice_active=true;_last_announce=now;_next_send=now;return true;
  }
  bool announce(uint32_t now) {
    if(!isHost() || _phase!=HOSTING || _candidate || uint32_t(now-_last_announce)<1000)return false;
    _notice=_commits[0];_notice_active=true;_last_announce=now;return true;
  }
  bool join(unsigned index,uint32_t self,uint32_t now) {
    const Lobby* found=lobby(index,now);
    if(!found || self==found->open.host || !self)return false;
    Lobby chosen=*found;reset();_channel=chosen.channel;_session=chosen.open.session;
    _host=chosen.open.host;_self=self;_commits[0]=chosen.open;_count=1;_phase=JOINING;
    GameEvent e=event(GameEvent::JOIN);e.action=++_next_action;_next_send=now;reliable(e);return true;
  }
  bool accept() {
    if(!isHost() || _phase!=HOSTING || !_candidate || busy())return false;
    GameEvent e=event(GameEvent::ACCEPT);e.peer=_candidate;e.action=_candidate_action;e.seq=_count;
    _notice_active=false;return commit(e);
  }
  bool ready() {
    if(isHost() || _phase!=JOINED || busy())return false;
    GameEvent e=event(GameEvent::READY_REQUEST);e.action=++_next_action;reliable(e);return true;
  }
  bool start() {
    if(!isHost() || _phase!=PLAYER_READY || busy())return false;
    GameEvent e=event(GameEvent::START);e.seq=_count;return commit(e);
  }
  bool move(uint8_t cell) {
    if(_phase!=PLAYING || busy() || _position.turn!=mark() || !_position.legal(cell))return false;
    GameEvent e=event(isHost()?GameEvent::MOVE:GameEvent::MOVE_REQUEST);
    e.cell=cell;e.action=++_next_action;e.aux=mark();
    if(!isHost()){reliable(e);return true;}
    e.seq=_count;TicTacToePosition after=_position;after.play(cell);e.x=after.x;e.o=after.o;
    return commit(e);
  }
  bool rematch() {
    if(_phase!=FINISHED || busy() || _round==255)return false;
    GameEvent e=event(isHost()?GameEvent::RESTART:GameEvent::REMATCH_REQUEST);
    e.action=++_next_action;
    if(!isHost()){reliable(e);return true;}
    e.round=_round+1;e.seq=0;e.x=e.o=0;
    beginRound(e);reliable(e);return true;
  }
  bool canRematch() const {return _round<255;}
  bool leaveEvent(GameEvent& out) const {
    if(_phase==IDLE || _phase==FINISHED || _phase==CLOSED)return false;
    out=event(isHost()?GameEvent::CANCEL:GameEvent::LEAVE);
    if(isHost())out.seq=_count;
    return true;
  }
  void retry(uint32_t now) {if(stalled() && int32_t(now-_deadline)>=0){_attempts=0;_deadline=now;_timed_out=false;}}

  void receive(uint8_t channel,const GameEvent& e,uint32_t now,const char* sender=nullptr) {
    if(e.type==GameEvent::OPEN && e.actor==e.host && !e.round && e.seq==0 && !e.peer && !e.x && !e.o) {
      if(_phase==IDLE) {
        unsigned slot=LOBBY_COUNT,empty=LOBBY_COUNT,oldest=0;
        for(unsigned i=0;i<LOBBY_COUNT;++i) {
          if(_lobbies[i].channel==channel && _lobbies[i].open.session==e.session && _lobbies[i].open.host==e.host){slot=i;break;}
          if(!_lobbies[i].open.session && empty==LOBBY_COUNT)empty=i;
          if(uint32_t(now-_lobbies[i].seen)>uint32_t(now-_lobbies[oldest].seen))oldest=i;
        }
        if(slot==LOBBY_COUNT)slot=empty<LOBBY_COUNT?empty:oldest;
        _lobbies[slot].open=e;_lobbies[slot].seen=now;_lobbies[slot].channel=channel;
        char* name=_lobbies[slot].name;unsigned n=0;
        if(sender)while(sender[n] && n<sizeof(_lobbies[slot].name)-1){unsigned char c=sender[n];name[n]=c>=32 && c<=126?char(c):'?';++n;}
        name[n]=0;
      }
      return;
    }
    if (_phase==IDLE && e.actor==e.host && (e.type==GameEvent::ACCEPT || e.type==GameEvent::START || e.type==GameEvent::END || e.type==GameEvent::CANCEL)) {
      for(auto& lobby:_lobbies)if(lobby.channel==channel && lobby.open.session==e.session && lobby.open.host==e.host)lobby.open.session=0;
      return;
    }
    if(_phase==IDLE || channel!=_channel || e.session!=_session || e.host!=_host || e.actor==_self)return;
    if(!isHost() && e.type==GameEvent::RESTART && e.actor==_host && e.peer==_self &&
       _phase==FINISHED && _round<255 && e.round==_round+1 && !e.seq && !e.x && !e.o) {
      beginRound(e);queueAck(e);return;
    }
    if(e.round!=_round)return;
    if(isHost()) {
      if(e.type==GameEvent::REMATCH_REQUEST && e.actor==_peer && e.peer==_peer && _phase==FINISHED &&
         e.seq==sequence() && e.x==_position.x && e.o==_position.o) {rematch();return;}
      if(e.type==GameEvent::ACK && e.actor==_peer && _reliable_active && e.seq==_reliable.seq &&
        e.action==_reliable.action && e.x==_reliable.x && e.o==_reliable.o){_reliable_active=false;return;}
      if(e.type==GameEvent::JOIN && _phase==HOSTING && !e.peer && e.seq==0){
        if(!_candidate || _candidate==e.actor){_candidate=e.actor;_candidate_action=e.action;}
        return;
      }
      if(e.type==GameEvent::LEAVE && _phase==HOSTING && e.actor==_candidate){_candidate=0;return;}
      if(e.actor!=_peer || e.peer!=_peer)return;
      if(e.type==GameEvent::LEAVE){_phase=CLOSED;_reliable_active=false;return;}
      if(busy())return;
      if(e.seq!=sequence() || e.x!=_position.x || e.o!=_position.o)return;
      if(e.type==GameEvent::READY_REQUEST && _phase==JOINED){
        GameEvent c=event(GameEvent::READY);c.seq=_count;c.action=e.action;c.aux=2;commit(c);
      } else if(e.type==GameEvent::MOVE_REQUEST && _phase==PLAYING && _position.turn==2 && e.aux==2 && _position.legal(e.cell)){
        GameEvent c=event(GameEvent::MOVE);c.seq=_count;c.action=e.action;c.cell=e.cell;c.aux=2;
        TicTacToePosition after=_position;after.play(e.cell);c.x=after.x;c.o=after.o;commit(c);
      }
      return;
    }
    if(e.actor!=_host || (e.type!=GameEvent::ACCEPT && e.peer!=_self))return;
    if(e.type==GameEvent::ACCEPT && e.peer!=_self)return;
    if(e.seq<_count){
      if(GameCodec::equal(e,_commits[e.seq]))queueAck(e);
      return;
    }
    if(e.seq>_count){
      for(auto& held:_held)if(!held.type || held.seq==e.seq){held=e;break;}
      return;
    }
    if(!apply(e))return;
    _commits[_count++]=e;_reliable_active=false;queueAck(e);
    // At most two bounded out-of-order events; never invent a missing move.
    for(unsigned i=0;i<2;++i)for(auto& held:_held)if(held.type && held.seq==_count){
      GameEvent next=held;held.type=0;
      if(apply(next)){_commits[_count++]=next;queueAck(next);}
    }
  }

  bool outgoing(uint32_t now,GameEvent& out) {
    if(isHost() && _phase==HOSTING && _candidate && !busy())accept();
    if(!isHost() && _phase==JOINED && !busy())ready();
    if(isHost() && _phase==PLAYER_READY && !busy())start();
    if(isHost() && _phase==HOSTING)announce(now);
    if (_reliable_active && !setupMessage(_reliable) && _attempts>=3 && int32_t(now-_deadline)>=0) _timed_out=true;
    if(int32_t(now-_next_send)<0)return false;
    if(isHost() && _phase==PLAYING && _position.result() && !busy()){
      GameEvent e=event(GameEvent::END);e.seq=_count;e.aux=_position.result();commit(e);
    }
    if(_ack_active){out=_ack;_sending=1;return true;}
    if(_notice_active){out=_notice;_sending=2;return true;}
    if(_reliable_active && (setupMessage(_reliable) || _attempts<3) && (!_attempts || int32_t(now-_deadline)>=0)){
      out=_reliable;_sending=3;return true;
    }
    return false;
  }
  void sent(uint32_t now) {
    if(_sending==1)_ack_active=false;
    if(_sending==2)_notice_active=false;
    bool setup=_phase==HOSTING || _phase==JOINING || _phase==JOINED || _phase==PLAYER_READY ||
      (_reliable_active && setupMessage(_reliable));
    if(_sending==3){if(_attempts<3)++_attempts;_deadline=now+(setup?1000:10000+(_self%997));}
    _next_send=now+(setup?1000:1500+(_self%251));_sending=0;
  }
  void backpressure(uint32_t now){_next_send=now+2000;}

private:
  static bool setupMessage(const GameEvent& e) {return (e.type>=GameEvent::JOIN && e.type<=GameEvent::START) || e.type==GameEvent::REMATCH_REQUEST || e.type==GameEvent::RESTART;}
  void beginRound(const GameEvent& e) {
    _round=e.round;_position={};_phase=PLAYING;_count=1;_commits[0]=e;
    for(auto& held:_held)held={};
    _reliable_active=_ack_active=_notice_active=_timed_out=false;_next_action=0;
  }
  GameEvent event(uint8_t type) const {
    GameEvent e;e.session=_session;e.host=_host;e.actor=_self;e.peer=_peer;
    e.round=_round;e.seq=sequence();e.x=_position.x;e.o=_position.o;e.type=type;return e;
  }
  void reliable(const GameEvent& e){_reliable=e;_reliable_active=true;_attempts=0;_timed_out=false;}
  void queueAck(const GameEvent& e){_ack=event(GameEvent::ACK);_ack.seq=e.seq;_ack.action=e.action;_ack.x=e.x;_ack.o=e.o;_ack_active=true;}
  bool commit(const GameEvent& e) {
    if(_count>=16 || !apply(e))return false;
    _commits[_count++]=e;reliable(e);return true;
  }
  bool apply(const GameEvent& e) {
    if(_count>=16 || e.seq!=_count || e.actor!=_host)return false;
    if(e.type==GameEvent::ACCEPT && (_phase==HOSTING || _phase==JOINING) && e.peer && e.peer!=_host && !e.x && !e.o){
      _peer=e.peer;_phase=JOINED;return true;
    }
    if(e.peer!=_peer)return false;
    if(e.type==GameEvent::MOVE && _phase==PLAYING && e.aux==_position.turn){
      TicTacToePosition after=_position;
      if(!after.play(e.cell) || after.x!=e.x || after.o!=e.o)return false;
      _position=after;return true;
    }
    if(e.x!=_position.x || e.o!=_position.o)return false;
    if(e.type==GameEvent::READY && _phase==JOINED && e.aux==2){_phase=PLAYER_READY;return true;}
    if(e.type==GameEvent::START && _phase==PLAYER_READY){_phase=PLAYING;return true;}
    if(e.type==GameEvent::END && _phase==PLAYING && _position.result() && e.aux==_position.result()){_phase=FINISHED;return true;}
    if(e.type==GameEvent::CANCEL){_phase=CLOSED;return true;}
    return false;
  }
  Lobby _lobbies[LOBBY_COUNT];
  GameEvent _commits[16],_held[2],_reliable,_ack,_notice;
  TicTacToePosition _position;
  uint64_t _session=0;
  uint32_t _self=0,_host=0,_peer=0,_candidate=0;
  uint32_t _deadline=0,_next_send=0,_last_announce=0;
  uint16_t _next_action=0,_candidate_action=0;
  uint8_t _channel=0,_count=0,_attempts=0,_sending=0,_round=0;
  Phase _phase=IDLE;
  bool _reliable_active=false,_ack_active=false,_notice_active=false,_timed_out=false;
};

static_assert(sizeof(TicTacToeMesh)<=1536,"Multiplayer runtime must remain bounded");
} // namespace solo_games
