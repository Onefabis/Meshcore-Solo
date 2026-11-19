#pragma once
#include "Packet.h"
namespace solo_games { namespace multiplayer {
class Session {
public:
  enum Phase:uint8_t { IDLE,HOSTING,JOINING,STARTING,PLAYING,FINISHED,CLOSED,ERROR };
  static constexpr unsigned LOBBIES=8;
  struct Lobby {Packet open;uint32_t seen=0;uint8_t channel=0;char name[19]={};};
private:
  enum LinkState:uint8_t { EMPTY,KEYING,OFFERED,READY,RUNNING };
  struct Link {
    uint8_t key[32]={};uint32_t id=0,nextTx=1,nextRx=1,nonce=0,deadline=0;
    Packet pending,ack,lastRx;
    uint8_t state=EMPTY,attempts=0,nameNext=1;bool sending=false,ackPending=false,timedOut=false;
  };
  Security& _security;
  Link _links[3];Lobby _lobbies[LOBBIES];
  Packet _inbox;
  uint32_t _ids[4]={},_self=0,_host=0,_nextSend=0,_lastNotice=0;
  uint64_t _session=0;
  char _names[4][19]={},_selfName[19]={};
  uint16_t _round=0,_generation=0;
  uint8_t _game=0,_capacity=2,_slot=0,_channel=0,_filter=0,_cursor=0;
  Phase _phase=IDLE;
  bool _hasInbox=false,_again=false,_hasSent=false;
  int8_t _sendingLink=-1;uint8_t _sendingKind=0;
  static bool due(uint32_t now,uint32_t deadline){return int32_t(now-deadline)>=0;}
  static void nameCopy(char out[19],const char* in){unsigned i=0;if(in)while(in[i]&&i<18){unsigned char c=in[i];out[i]=c>=32&&c<=126?char(c):'?';++i;}out[i]=0;}
  void fail(){_phase=ERROR;}
  unsigned peers()const{return _capacity-1;}
  Packet packet(unsigned type,unsigned recipient=0)const{Packet p;p.type=type;p.game=_game;p.actor=_self;p.recipient=recipient;p.session=_session;p.round=_round;return p;}
  bool protect(unsigned link,Packet& p){auto& l=_links[link];if(l.nonce==0xFFFFFFFFU){fail();return false;}p.nonce=++l.nonce;if(!_security.seal(link,p)){fail();return false;}return true;}
  bool queue(unsigned index,unsigned type,const uint8_t* bytes,unsigned size,bool fast=false,uint16_t round=65535){
    if(index>=3||size>32)return false;auto& l=_links[index];if(l.sending||l.nextTx==0xFFFFFFFFU)return false;
    Packet p=packet(type,l.id);p.size=size;p.sequence=l.nextTx++;p.options=fast?128:0;if(round!=65535)p.round=round;
    if(size)memcpy(p.data,bytes,size);if(!protect(index,p))return false;
    l.pending=p;l.sending=true;l.attempts=0;l.timedOut=false;return true;
  }
  void ack(unsigned index,const Packet& message){auto& l=_links[index];Packet p=packet(Packet::ACK,l.id);p.sequence=message.sequence;p.round=message.round;
    if(protect(index,p)){l.ack=p;l.ackPending=true;}}
  void bind(unsigned index,const Packet& p,const char* sender){auto& l=_links[index];l.id=p.actor;memcpy(l.key,p.data,32);l.state=KEYING;
    if(isHost()){_ids[index+1]=p.actor;nameCopy(_names[index+1],sender);}else{_ids[0]=p.actor;nameCopy(_names[0],sender);}}
  void remember(uint8_t channel,const Packet& p,uint32_t now,const char* sender){
    unsigned slot=LOBBIES,empty=LOBBIES,oldest=0;
    for(unsigned i=0;i<LOBBIES;++i){if(_lobbies[i].channel==channel&&_lobbies[i].open.session==p.session&&_lobbies[i].open.actor==p.actor){slot=i;break;}
      if(!_lobbies[i].open.session&&empty==LOBBIES)empty=i;if(uint32_t(now-_lobbies[i].seen)>uint32_t(now-_lobbies[oldest].seen))oldest=i;}
    if(slot==LOBBIES)slot=empty<LOBBIES?empty:oldest;
    auto& l=_lobbies[slot];l.open=p;l.channel=channel;l.seen=now;nameCopy(l.name,sender);
  }
  bool control(unsigned index,const Packet& p){
    auto& l=_links[index];
    if(p.type==Packet::ACCEPT){
      if(isHost()||p.round||p.size!=2||p.data[0]<1||p.data[0]>=_capacity||p.data[1]!=_capacity||(_phase!=JOINING&&_phase!=STARTING))return false;
      _slot=p.data[0];_ids[_slot]=_self;nameCopy(_names[_slot],_selfName);l.state=READY;_phase=STARTING;return true;
    }
    if(p.type==Packet::NAME){
      if(isHost()||_phase!=STARTING||p.size<5||p.size>23)return false;unsigned slot=p.data[0];uint32_t id=Codec::get(p.data+1,4);
      if(slot>=_capacity||!id||(_ids[slot]&&_ids[slot]!=id))return false;
      char name[19]={};memcpy(name,p.data+5,p.size-5);_ids[slot]=id;nameCopy(_names[slot],name);return true;
    }
    if(p.type==Packet::START){
      if(isHost()||_phase!=STARTING||p.round||p.size!=4*_capacity)return false;
      uint32_t ids[4]={};for(unsigned i=0;i<_capacity;++i){ids[i]=Codec::get(p.data+4*i,4);if(!ids[i]||(_ids[i]&&_ids[i]!=ids[i]))return false;for(unsigned j=0;j<i;++j)if(ids[i]==ids[j])return false;}
      if(ids[0]!=_host||ids[_slot]!=_self)return false;memcpy(_ids,ids,sizeof(ids));_phase=PLAYING;++_generation;return true;
    }
    if(p.type==Packet::AGAIN){if(!isHost()||p.round>_round||p.size)return false;if(p.round==_round)_again=true;return true;}
    if(p.type==Packet::RESTART){
      if(isHost()||_phase!=FINISHED||_round==65535||p.round!=_round+1||p.size)return false;
      _round=p.round;_phase=PLAYING;_again=false;++_generation;return true;
    }
    return false;
  }
  void introductions(){
    bool all=true;for(unsigned i=0;i<peers();++i)all&=_links[i].state>=READY;if(!all)return;
    _phase=STARTING;bool namesDone=true;
    for(unsigned i=0;i<peers();++i){auto& l=_links[i];if(l.sending){namesDone=false;continue;}
      while(l.nameNext<_capacity&&l.nameNext==i+1)++l.nameNext;
      if(l.nameNext<_capacity){uint8_t payload[23]={};unsigned slot=l.nameNext++;payload[0]=slot;Codec::put(payload+1,_ids[slot],4);unsigned n=strlen(_names[slot]);memcpy(payload+5,_names[slot],n);
        queue(i,Packet::NAME,payload,5+n,true);namesDone=false;}
    }
    if(!namesDone)return;
    bool running=true;
    for(unsigned i=0;i<peers();++i){auto& l=_links[i];running&=l.state==RUNNING;if(l.state==READY&&!l.sending){uint8_t payload[16];for(unsigned s=0;s<_capacity;++s)Codec::put(payload+4*s,_ids[s],4);queue(i,Packet::START,payload,4*_capacity,true);l.state=OFFERED;}}
    if(running){_phase=PLAYING;++_generation;}
  }
public:
  explicit Session(Security& security):_security(security){uint8_t key[32];security.localPublic(key);_self=publicId(key);}
  ~Session(){_security.clear();}
  void reset(){_security.clear();for(auto& l:_links)l=Link();for(auto& l:_lobbies)l=Lobby();memset(_ids,0,sizeof(_ids));memset(_names,0,sizeof(_names));_host=0;_session=0;_round=_generation=0;_phase=IDLE;_hasInbox=_again=_hasSent=false;_sendingLink=-1;_nextSend=_lastNotice=0;_slot=0;}
  Phase phase()const{return _phase;}bool isHost()const{return _self==_host&&_host;}
  unsigned game()const{return _game;}unsigned slot()const{return _slot;}unsigned count()const{return _capacity;}
  unsigned channel()const{return _channel;}unsigned matchRound()const{return _round;}unsigned generation()const{return _generation;}
  uint64_t id()const{return _session;}uint32_t self()const{return _self;}
  uint32_t member(unsigned slot)const{return slot<_capacity?_ids[slot]:0;}
  const char* name(unsigned slot)const{return slot<_capacity&&_names[slot][0]?_names[slot]:"Player";}
  int slotFor(uint32_t id)const{for(unsigned s=0;s<_capacity;++s)if(_ids[s]==id)return s;return -1;}
  unsigned joined()const{unsigned n=1;for(auto& l:_links)n+=l.state>=READY;return n;}
  bool busy()const{for(auto& l:_links)if(l.sending)return true;return false;}
  bool stalled()const{for(auto& l:_links)if(l.sending&&l.timedOut)return true;return false;}
  void find(unsigned game=0){reset();_filter=game;}
  const Lobby* lobby(unsigned i,uint32_t now)const{return i<LOBBIES&&_lobbies[i].open.session&&uint32_t(now-_lobbies[i].seen)<5000?&_lobbies[i]:nullptr;}
  bool host(unsigned game,unsigned capacity,unsigned channel,uint64_t session,const char* name){
    if(game<TTT||game>SNAKE||capacity<2||capacity>maxPlayers(game)||!session)return false;reset();_game=game;_capacity=capacity;_channel=channel;_session=session;_host=_self;_ids[0]=_self;nameCopy(_selfName,name);nameCopy(_names[0],name);_phase=HOSTING;return true;
  }
  bool join(unsigned i,uint32_t now,const char* name){const auto* found=lobby(i,now);if(!found||found->open.actor==_self)return false;auto chosen=*found;reset();_game=chosen.open.game;_capacity=chosen.open.options;_channel=chosen.channel;_session=chosen.open.session;_host=chosen.open.actor;nameCopy(_selfName,name);_phase=JOINING;bind(0,chosen.open,chosen.name);return true;}
  void poll(uint32_t now){
    if(_phase==CLOSED||_phase==ERROR||_phase==IDLE)return;
    for(unsigned i=0;i<3;++i){auto& l=_links[i];if(l.state==KEYING&&_security.prepare(i,l.key,_session,_game,isHost())){
        if(isHost()){uint8_t payload[]={uint8_t(i+1),_capacity};queue(i,Packet::ACCEPT,payload,2,true);l.state=OFFERED;}
        else l.state=OFFERED;
      }
      if(l.sending&&l.attempts>=3&&!(l.pending.options&128)&&due(now,l.deadline))l.timedOut=true;
    }
    if(_security.failed()){fail();return;}
    if(isHost()&&(_phase==HOSTING||_phase==STARTING))introductions();
    if(isHost()&&_phase==FINISHED&&_again&&!busy()){
      if(_round==65535){fail();return;}
      for(unsigned i=0;i<peers();++i)queue(i,Packet::RESTART,nullptr,0,true,_round+1);
      ++_round;++_generation;_phase=PLAYING;_again=false;
    }
  }
  bool send(unsigned type,const uint8_t* bytes,unsigned size,bool fast=false){
    if(type<Packet::APP||size>32||(_phase!=PLAYING&&_phase!=FINISHED)||busy())return false;
    if(isHost()){for(unsigned i=0;i<peers();++i)if(!queue(i,type,bytes,size,fast))return false;return true;}
    return queue(0,type,bytes,size,fast);
  }
  bool sendTo(unsigned slot,unsigned type,const uint8_t* bytes,unsigned size,bool fast=false){
    if(!isHost()||slot<1||slot>=_capacity||type<Packet::APP||size>32||_links[slot-1].sending||(_phase!=PLAYING&&_phase!=FINISHED))return false;
    return queue(slot-1,type,bytes,size,fast);
  }
  bool take(Message& message){if(!_hasInbox)return false;message.type=_inbox.type;message.actor=_inbox.actor;message.size=_inbox.size;memcpy(message.data,_inbox.data,message.size);_hasInbox=false;return true;}
  void finish(){if(_phase==PLAYING)_phase=FINISHED;}
  bool rematch(){if(_phase!=FINISHED||_round==65535)return false;if(isHost()){_again=true;return true;}if(busy())return false;return queue(0,Packet::AGAIN,nullptr,0,true);}
  void retry(uint32_t now){for(auto& l:_links)if(l.timedOut){l.timedOut=false;l.attempts=0;l.deadline=now;}}
  bool leave(unsigned link,Packet& out){if(link>=3||_links[link].state<READY)return false;out=packet(Packet::LEAVE,_links[link].id);return protect(link,out);}
  void receive(uint8_t channel,const Packet& wire,uint32_t now,const char* sender){
    if(wire.type==Packet::OPEN){if(wire.size!=32||wire.nonce||wire.sequence||wire.round||wire.recipient||wire.actor!=publicId(wire.data)||wire.options<2||wire.options>maxPlayers(wire.game))return;
      if(_phase==IDLE&&(!_filter||wire.game==_filter))remember(channel,wire,now,sender);return;}
    if(_phase==IDLE||_phase==ERROR||_phase==CLOSED||channel!=_channel||wire.session!=_session||wire.game!=_game||wire.recipient!=_self||wire.actor==_self)return;
    if(wire.type==Packet::JOIN){
      if(!isHost()||(_phase!=HOSTING&&_phase!=STARTING)||wire.size!=32||wire.nonce||wire.sequence||wire.round||publicId(wire.data)!=wire.actor)return;
      unsigned empty=3;for(unsigned i=0;i<peers();++i){if(_links[i].id==wire.actor){return;}if(_links[i].state==EMPTY&&empty==3)empty=i;}
      if(empty<3)bind(empty,wire,sender);return;
    }
    unsigned index=3;for(unsigned i=0;i<3;++i)if(_links[i].id==wire.actor){index=i;break;}if(index==3)return;
    auto& l=_links[index];Packet p=wire;if(!_security.open(index,p))return;
    if(p.type==Packet::LEAVE){_phase=CLOSED;return;}
    if(p.type==Packet::ACK){
      if(p.size||!l.sending||p.sequence!=l.pending.sequence||p.round!=l.pending.round)return;
      if(l.pending.type==Packet::ACCEPT)l.state=READY;if(l.pending.type==Packet::START)l.state=RUNNING;
      l.sending=false;l.timedOut=false;return;
    }
    if(p.sequence<l.nextRx){if(p.sequence+1==l.nextRx)ack(index,p);return;}
    if(p.sequence!=l.nextRx)return;
    if(p.type>=Packet::APP){if(p.round!=_round||_hasInbox||(_phase!=PLAYING&&_phase!=FINISHED&&_phase!=STARTING))return;_inbox=p;_hasInbox=true;}
    else if(!control(index,p))return;
    l.lastRx=p;++l.nextRx;ack(index,p);
  }
  bool outgoing(uint32_t now,Packet& out){
    poll(now);if(_phase==CLOSED||_phase==ERROR||(_hasSent&&!due(now,_nextSend)))return false;
    for(unsigned n=0;n<3;++n){unsigned i=(_cursor+n)%3;auto& l=_links[i];if(l.ackPending){out=l.ack;_sendingLink=i;_sendingKind=1;_cursor=(i+1)%3;return true;}}
    for(unsigned n=0;n<3;++n){unsigned i=(_cursor+n)%3;auto& l=_links[i];if(l.sending&&!l.timedOut&&(!l.attempts||due(now,l.deadline))){out=l.pending;_sendingLink=i;_sendingKind=2;_cursor=(i+1)%3;return true;}}
    if((_phase==HOSTING||_phase==JOINING)&&(!_lastNotice||uint32_t(now-_lastNotice)>=1000)){
      out=packet(_phase==HOSTING?Packet::OPEN:Packet::JOIN,_phase==HOSTING?0:_host);out.size=32;out.options=_capacity;_security.localPublic(out.data);_sendingLink=-1;_sendingKind=3;return true;
    }
    return false;
  }
  void sent(uint32_t now){
    if(_sendingKind==1)_links[_sendingLink].ackPending=false;
    if(_sendingKind==2){auto& l=_links[_sendingLink];if(l.attempts<3)++l.attempts;l.deadline=now+((l.pending.options&128)?1000:10000+_self%997);}
    if(_sendingKind==3)_lastNotice=now;_sendingKind=0;_hasSent=true;_nextSend=now+200;
  }
  void backpressure(uint32_t now){_sendingKind=0;_hasSent=true;_nextSend=now+2000;}
};
static_assert(sizeof(Session)<4096,"One bounded lobby/session belongs to the visible online screen");
} }
