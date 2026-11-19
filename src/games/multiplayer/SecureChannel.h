#pragma once
#include "Packet.h"
#include <ChaChaPoly.h>
#include <HKDF.h>
#include <SHA256.h>
#include <Crypto.h>
#include <new>
#ifdef NRF52_PLATFORM
#include <Arduino.h>
#endif
namespace solo_games { namespace multiplayer {
struct IdentityCallbacks {
  void* context=nullptr;
  void (*publicKey)(void*,uint8_t*)=nullptr;
  void (*sharedSecret)(void*,const uint8_t*,uint8_t*)=nullptr;
  bool (*randomBytes)(void*,uint8_t*,size_t)=nullptr;
};
inline void digest(const uint8_t* bytes,size_t size,uint8_t out[32]){SHA256 hash;hash.update(bytes,size);hash.finalize(out,32);}
// Only the visible online screen owns keys. ECDH/HKDF work uses a temporary
// nRF task so the mesh loop retains its stack and continues tracking/radio work.
class SecureChannel : public Security {
  struct Job {
    IdentityCallbacks identity;
    uint8_t peer[32]={},local[32]={},keys[64]={};
    uint64_t session=0;uint8_t game=0,link=0;bool host=false,ok=false;
    volatile bool done=false;
#ifdef NRF52_PLATFORM
    TaskHandle_t task=nullptr;
#endif
  };
  IdentityCallbacks _identity;
  uint8_t _keys[3][64]={},_peer[3][32]={};bool _ready[3]={},_failed=false;
  Job* _job=nullptr;
  ChaChaPoly _cipher;
  static void calculate(Job* job){
    uint8_t secret[32]={};job->identity.sharedSecret(job->identity.context,job->peer,secret);
    bool nonzero=false;for(auto b:secret)nonzero|=b!=0;
    if(nonzero){uint8_t salt[9],info[76]={};Codec::put(salt,job->session,8);salt[8]=job->game;
      memcpy(info,"Zygis-MP-v1",11);memcpy(info+12,job->host?job->local:job->peer,32);memcpy(info+44,job->host?job->peer:job->local,32);
      hkdf<SHA256>(job->keys,64,secret,32,salt,sizeof(salt),info,sizeof(info));job->ok=true;
    }
    clean(secret,sizeof(secret));
#ifdef NRF52_PLATFORM
    __DMB();
#endif
    job->done=true;
  }
#ifdef NRF52_PLATFORM
  static void worker(void* input){auto* job=static_cast<Job*>(input);calculate(job);vTaskSuspend(nullptr);}
#endif
  void releaseJob(){if(!_job)return;
#ifdef NRF52_PLATFORM
    if(_job->task)vTaskDelete(_job->task);
#endif
    clean(_job->keys,sizeof(_job->keys));delete _job;_job=nullptr;
  }
  bool crypt(unsigned link,Packet& p,bool encrypt){
    if(link>=3||!_ready[link]||!p.nonce||p.size>(encrypt?32:48)||(!encrypt&&p.size<16))return false;
    // Derivation always yields host->guest then guest->host keys. Caller
    // selects the direction from the peer's role, recorded when preparing.
    bool host=_host[link];unsigned offset=(encrypt?host:!host)?0:32;
    uint8_t nonce[12],aad[Codec::HEADER];Codec::put(nonce,p.session,8);Codec::put(nonce+8,p.nonce,4);
    if(encrypt)p.size+=16;Codec::header(p,aad);
    _cipher.clear();if(!_cipher.setKey(_keys[link]+offset,32)||!_cipher.setIV(nonce,sizeof(nonce)))return false;
    _cipher.addAuthData(aad,sizeof(aad));unsigned size=p.size-16;uint8_t plain[32]={};
    if(encrypt){_cipher.encrypt(p.data,p.data,size);_cipher.computeTag(p.data+size,16);}
    else{_cipher.decrypt(plain,p.data,size);if(!_cipher.checkTag(p.data+size,16)){clean(plain,sizeof(plain));_cipher.clear();return false;}memcpy(p.data,plain,size);p.size=size;}
    clean(plain,sizeof(plain));_cipher.clear();return true;
  }
  bool _host[3]={};
public:
  explicit SecureChannel(IdentityCallbacks callbacks):_identity(callbacks){}
  ~SecureChannel()override{clear();}
  void localPublic(uint8_t out[32])override{_identity.publicKey(_identity.context,out);}
  bool random(uint8_t* p,size_t n)override{if(_identity.randomBytes(_identity.context,p,n))return true;clean(p,n);_failed=true;return false;}
  bool prepare(unsigned link,const uint8_t peer[32],uint64_t session,uint8_t game,bool host)override{
    if(link>=3||_failed)return false;
    if(_ready[link]){if(memcmp(_peer[link],peer,32))_failed=true;return !_failed;}
    if(_job && _job->done){
#ifdef NRF52_PLATFORM
      __DMB();
#endif
      auto index=_job->link;_failed=!_job->ok;if(!_failed){memcpy(_keys[index],_job->keys,64);memcpy(_peer[index],_job->peer,32);_host[index]=_job->host;_ready[index]=true;}releaseJob();
      if(_failed)return false;if(_ready[link])return true;
    }
    if(_job)return false;
    _job=new(std::nothrow) Job;if(!_job){_failed=true;return false;}
    _job->identity=_identity;_job->link=link;_job->host=host;_job->session=session;_job->game=game;memcpy(_job->peer,peer,32);localPublic(_job->local);
#ifdef NRF52_PLATFORM
    if(xTaskCreate(worker,"game-key",2048,_job,1,&_job->task)!=pdPASS){_failed=true;releaseJob();}
    return false;
#else
    calculate(_job);return prepare(link,peer,session,game,host);
#endif
  }
  bool failed()const override{return _failed;}
  bool seal(unsigned link,Packet& p)override{return crypt(link,p,true);}
  bool open(unsigned link,Packet& p)override{return crypt(link,p,false);}
  void clear()override{releaseJob();clean(_keys,sizeof(_keys));clean(_peer,sizeof(_peer));memset(_ready,0,sizeof(_ready));_cipher.clear();_failed=false;}
};
} }
