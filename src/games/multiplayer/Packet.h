#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace solo_games { namespace multiplayer {
enum Game:uint8_t { TTT=1,CHECKERS,BATTLESHIP,ARTILLERY,POKER,ARENA,SNAKE };
inline unsigned maxPlayers(unsigned game){return game==ARTILLERY||game==POKER||game==SNAKE?4:2;}
struct Packet {
  enum Type:uint8_t { OPEN=1,JOIN,ACCEPT,ACK,LEAVE,START,AGAIN,RESTART,NAME,APP=32 };
  uint64_t session=0;
  uint32_t actor=0,recipient=0,sequence=0,nonce=0;
  uint16_t round=0;
  uint8_t game=0,type=0,size=0,options=0,data[48]={};
};
class Codec {
public:
  static constexpr unsigned HEADER=31,MAX_TEXT=112;
  static void put(uint8_t* p,uint64_t value,unsigned count){while(count--){*p++=uint8_t(value);value>>=8;}}
  static uint64_t get(const uint8_t* p,unsigned count){uint64_t result=0;for(unsigned i=0;i<count;++i)result|=uint64_t(p[i])<<(i*8);return result;}
  static uint16_t crc(const uint8_t* p,unsigned size){uint16_t c=0xFFFF;while(size--){c^=uint16_t(*p++)<<8;for(unsigned i=0;i<8;++i)c=c&0x8000?(c<<1)^0x1021:c<<1;}return c;}
  static void header(const Packet& p,uint8_t out[HEADER]) {
    out[0]=1;out[1]=p.game;out[2]=p.type;out[3]=p.size;
    put(out+4,p.session,8);put(out+12,p.actor,4);put(out+16,p.recipient,4);put(out+20,p.round,2);
    put(out+22,p.sequence,4);put(out+26,p.nonce,4);out[30]=p.options;
  }
  static bool valid(const Packet& p){return p.game>=TTT&&p.game<=SNAKE&&p.type&&p.session&&p.actor&&p.size<=48;}
  static bool encode(const Packet& p,char* out,size_t capacity){
    if(!valid(p))return false;unsigned n=HEADER+p.size+2,chars=(n*8+5)/6;
    if(capacity<5+chars)return false;
    uint8_t bytes[81];header(p,bytes);memcpy(bytes+HEADER,p.data,p.size);put(bytes+n-2,crc(bytes,n-2),2);
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    memcpy(out,"~M1:",4);uint32_t bits=0;unsigned count=0,j=4;
    for(unsigned i=0;i<n;++i){bits=(bits<<8)|bytes[i];count+=8;while(count>=6){count-=6;out[j++]=alphabet[(bits>>count)&63];}}
    if(count)out[j++]=alphabet[(bits<<(6-count))&63];out[j]=0;return true;
  }
  static bool decode(const char* text,Packet& out){
    if(!text||strncmp(text,"~M1:",4))return false;size_t length=strlen(text);if(length<48||length>MAX_TEXT)return false;
    uint8_t bytes[81];unsigned n=0,count=0;uint32_t bits=0;
    for(unsigned i=4;i<length;++i){char c=text[i];int v=c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:c>='0'&&c<='9'?c-'0'+52:c=='-'?62:c=='_'?63:-1;
      if(v<0)return false;bits=(bits<<6)|unsigned(v);count+=6;if(count>=8){count-=8;if(n>=sizeof(bytes))return false;bytes[n++]=uint8_t(bits>>count);}}
    if((count && (bits&((1U<<count)-1)))||n<HEADER+2||bytes[0]!=1||bytes[3]>48||n!=HEADER+bytes[3]+2||
       length!=4+(n*8+5)/6||crc(bytes,n-2)!=get(bytes+n-2,2))return false;
    Packet p;p.game=bytes[1];p.type=bytes[2];p.size=bytes[3];p.session=get(bytes+4,8);p.actor=get(bytes+12,4);p.recipient=get(bytes+16,4);
    p.round=get(bytes+20,2);p.sequence=get(bytes+22,4);p.nonce=get(bytes+26,4);p.options=bytes[30];memcpy(p.data,bytes+HEADER,p.size);
    if(!valid(p))return false;out=p;return true;
  }
};
inline uint32_t publicId(const uint8_t key[32]){uint32_t id=uint32_t(Codec::get(key,4));return id?id:1;}
struct Security {
  virtual ~Security(){}
  virtual void localPublic(uint8_t key[32])=0;
  // May start bounded asynchronous work. Repeated calls return true once ready.
  virtual bool prepare(unsigned link,const uint8_t peer[32],uint64_t session,uint8_t game,bool host)=0;
  virtual bool failed()const=0;
  virtual bool seal(unsigned link,Packet& packet)=0;
  virtual bool open(unsigned link,Packet& packet)=0;
  virtual bool random(uint8_t* bytes,size_t size)=0;
  virtual void clear()=0;
};
struct Message {uint8_t type=0,size=0,data[32]={};uint32_t actor=0;};
} }
