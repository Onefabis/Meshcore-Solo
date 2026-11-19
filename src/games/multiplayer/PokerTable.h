#pragma once
#include "../Holdem.h"
#include "Packet.h"
namespace solo_games { namespace multiplayer {
// No-limit, virtual-chip 2-4-player table. The host supplies a cryptographically
// shuffled deck; public views redact every unrevealed opponent hole card.
class PokerTable {
public:
  enum Phase:uint8_t { READY,PREFLOP,FLOP,TURN,RIVER,HAND_OVER };
  enum Action:uint8_t { FOLD,CALL,RAISE };
private:
  uint8_t _count=2,_deck[52]={},_holes[4][2]={},_board[5]={},_boardCount=0,_draw=0;
  uint8_t _dealer=0,_active=0,_folded=0,_pending=0,_canRaise=0,_winners=0,_shown=0;
  uint16_t _chips[4]={},_street[4]={},_contribution[4]={},_won[4]={};
  uint16_t _bet=0,_lastRaise=10,_pot=0,_wonPot=0,_hand=0;Phase _phase=READY;
  static unsigned bits(unsigned mask){unsigned n=0;while(mask){n+=mask&1;mask>>=1;}return n;}
  unsigned liveMask()const{return ((1U<<_count)-1)&~_folded;}
  unsigned bettingMask()const{unsigned mask=0;for(unsigned i=0;i<_count;++i)if(!(_folded&(1U<<i))&&_chips[i])mask|=1U<<i;return mask;}
  unsigned nextSeat(unsigned after,unsigned mask)const{for(unsigned i=1;i<=_count;++i){unsigned seat=(after+i)%_count;if(mask&(1U<<seat))return seat;}return after;}
  void pay(unsigned side,unsigned amount){_chips[side]-=amount;_street[side]+=amount;_contribution[side]+=amount;_pot+=amount;}
  void nextBoard(){++_draw;unsigned n=_boardCount?1:3;while(n--)_board[_boardCount++]=_deck[_draw++];}
  void refund(){unsigned maximum=0,second=0,owner=0;for(unsigned i=0;i<_count;++i){if(_street[i]>maximum){second=maximum;maximum=_street[i];owner=i;}else if(_street[i]>second)second=_street[i];}
    unsigned extra=maximum-second;if(extra){_street[owner]-=extra;_contribution[owner]-=extra;_pot-=extra;_chips[owner]+=extra;}
  }
  void finishFold(unsigned winner){_wonPot=_pot;_won[winner]=_pot;_chips[winner]+=_pot;_pot=0;_winners=1U<<winner;_phase=HAND_OVER;_pending=0;}
  void showdown(){while(_boardCount<5)nextBoard();_shown=liveMask();uint32_t ranks[4]={};
    for(unsigned i=0;i<_count;++i)if(_shown&(1U<<i)){uint8_t cards[7]={_holes[i][0],_holes[i][1]};memcpy(cards+2,_board,5);ranks[i]=PokerHand::rank(cards,7);}
    _wonPot=_pot;unsigned previous=0;
    for(unsigned step=0;step<_count;++step){unsigned level=65535;for(auto c:_contribution)if(c>previous&&c<level)level=c;if(level==65535)break;
      unsigned contributors=0,winners=0;uint32_t best=0;
      for(unsigned i=0;i<_count;++i)if(_contribution[i]>=level){++contributors;if(_shown&(1U<<i)){if(ranks[i]>best){best=ranks[i];winners=1U<<i;}else if(ranks[i]==best)winners|=1U<<i;}}
      unsigned amount=(level-previous)*contributors,number=bits(winners);if(!number){_phase=READY;return;}unsigned share=amount/number,remainder=amount%number;
      for(unsigned i=0;i<_count;++i)if(winners&(1U<<i)){_chips[i]+=share;_won[i]+=share;}
      unsigned seat=_dealer;while(remainder--){seat=nextSeat(seat,winners);++_chips[seat];++_won[seat];}_winners|=winners;previous=level;
    }
    _pot=0;_pending=0;_phase=HAND_OVER;
  }
  void advance(){
    unsigned live=liveMask();if(bits(live)==1){finishFold(nextSeat(_dealer,live));return;}
    _pending&=bettingMask();
    if(bits(bettingMask())==1){unsigned seat=nextSeat(_dealer,bettingMask());if(!toCall(seat))_pending=0;}
    if(_pending){_active=nextSeat(_active,_pending);return;}
    refund();
    if(_phase==RIVER||bits(bettingMask())<2){showdown();return;}
    nextBoard();_phase=Phase(unsigned(_phase)+1);memset(_street,0,sizeof(_street));_bet=0;_lastRaise=10;_pending=bettingMask();_canRaise=_pending;_active=_dealer;
    if(_pending)_active=nextSeat(_dealer,_pending);
  }
public:
  void reset(unsigned count){*this=PokerTable();_count=count>=2&&count<=4?count:2;_dealer=_count-1;memset(_holes,255,sizeof(_holes));memset(_board,255,sizeof(_board));for(unsigned i=0;i<_count;++i)_chips[i]=500;}
  bool deal(const uint8_t deck[52]){
    if((_phase!=READY&&_phase!=HAND_OVER)||finished())return false;uint64_t seen=0;for(unsigned i=0;i<52;++i){if(deck[i]>=52||(seen&(uint64_t(1)<<deck[i])))return false;seen|=uint64_t(1)<<deck[i];}
    unsigned alive=0;for(unsigned i=0;i<_count;++i)if(_chips[i])alive|=1U<<i;
    _dealer=nextSeat(_dealer,alive);memcpy(_deck,deck,52);_draw=0;_boardCount=0;memset(_board,255,sizeof(_board));memset(_holes,255,sizeof(_holes));memset(_street,0,sizeof(_street));memset(_contribution,0,sizeof(_contribution));memset(_won,0,sizeof(_won));
    _pot=_wonPot=0;_bet=10;_lastRaise=10;_winners=_shown=0;_folded=((1U<<_count)-1)&~alive;++_hand;_phase=PREFLOP;
    unsigned seat=_dealer;for(unsigned round=0;round<2;++round)for(unsigned i=0;i<bits(alive);++i){seat=nextSeat(seat,alive);_holes[seat][round]=_deck[_draw++];}
    unsigned small=bits(alive)==2?_dealer:nextSeat(_dealer,alive),big=nextSeat(small,alive);
    pay(small,_chips[small]<5?_chips[small]:5);pay(big,_chips[big]<10?_chips[big]:10);_pending=bettingMask();_canRaise=_pending;_active=big;advance();return true;
  }
  bool act(unsigned side,unsigned action,unsigned target=0){
    if(_phase<PREFLOP||_phase>RIVER||side>=_count||side!=_active||!(_pending&(1U<<side))||!_chips[side])return false;
    if(action==FOLD){_folded|=1U<<side;_pending&=~(1U<<side);_canRaise&=~(1U<<side);}
    else if(action==CALL){unsigned amount=toCall(side);if(amount>_chips[side])amount=_chips[side];pay(side,amount);_pending&=~(1U<<side);_canRaise&=~(1U<<side);}
    else if(action==RAISE){unsigned maximum=maxRaiseTo(side),minimum=minRaiseTo(side);if(!maximum||target<minimum||target>maximum)return false;
      unsigned raise=target-_bet;pay(side,target-_street[side]);_bet=target;
      if(raise>=_lastRaise){_lastRaise=raise;_pending=bettingMask()&~(1U<<side);_canRaise=_pending;}
      else{for(unsigned i=0;i<_count;++i)if(i!=side&&_chips[i]&&!(_folded&(1U<<i))&&_street[i]<_bet)_pending|=1U<<i;_pending&=~(1U<<side);_canRaise&=~(1U<<side);}
    } else return false;
    advance();return true;
  }
  unsigned players()const{return _count;}unsigned active()const{return _active;}unsigned dealer()const{return _dealer;}Phase phase()const{return _phase;}
  unsigned chips(unsigned side)const{return side<_count?_chips[side]:0;}unsigned pot()const{return _pot;}unsigned hand()const{return _hand;}
  unsigned wonPot()const{return _wonPot;}unsigned won(unsigned side)const{return side<_count?_won[side]:0;}unsigned winnerMask()const{return _winners;}unsigned shownMask()const{return _shown;}
  unsigned foldedMask()const{return _folded;}unsigned boardCount()const{return _boardCount;}unsigned board(unsigned i)const{return i<_boardCount?_board[i]:255;}
  unsigned hole(unsigned viewer,unsigned side,unsigned card)const{return side<_count&&card<2&&(viewer==side||(_shown&(1U<<side)))?_holes[side][card]:255;}
  unsigned toCall(unsigned side)const{if(side>=_count)return 0;unsigned bet=_bet;bool opposingChips=false;unsigned opposingStreet=0;
    for(unsigned i=0;i<_count;++i)if(i!=side&&!(_folded&(1U<<i))){opposingChips|=_chips[i]!=0;if(_street[i]>opposingStreet)opposingStreet=_street[i];}
    if(!opposingChips&&opposingStreet<bet)bet=opposingStreet;return bet>_street[side]?bet-_street[side]:0;
  }
  unsigned maxRaiseTo(unsigned side)const{return side<_count&&(_canRaise&(1U<<side))&&bits(bettingMask())>1&&_street[side]+_chips[side]>_bet?_street[side]+_chips[side]:0;}
  unsigned minRaiseTo(unsigned side)const{unsigned max=maxRaiseTo(side),min=_bet+_lastRaise;return max?(min>max?max:min):0;}
  bool finished()const{unsigned live=0;for(unsigned i=0;i<_count;++i)live+=_chips[i]!=0;return (_phase==READY||_phase==HAND_OVER)&&live<2;}
  unsigned matchWinnerMask()const{if(!finished())return 0;unsigned mask=0;for(unsigned i=0;i<_count;++i)if(_chips[i])mask|=1U<<i;return mask;}
  friend struct PokerFixture;
};
struct PokerView {
  static constexpr unsigned SIZE=43;
  uint8_t count=2,phase=0,active=0,dealer=0,boardCount=0,folded=0,winners=0,shown=0;
  uint16_t hand=0,chips[4]={},pot=0,wonPot=0,won=0,call=0,minRaise=0,maxRaise=0;
  uint8_t board[5]={255,255,255,255,255},holes[4][2]={{255,255},{255,255},{255,255},{255,255}};
  void capture(const PokerTable& table,unsigned viewer){count=table.players();phase=table.phase();active=table.active();dealer=table.dealer();boardCount=table.boardCount();folded=table.foldedMask();winners=table.winnerMask();shown=table.shownMask();hand=table.hand();pot=table.pot();wonPot=table.wonPot();won=table.won(viewer);call=table.toCall(viewer);minRaise=table.minRaiseTo(viewer);maxRaise=table.maxRaiseTo(viewer);
    for(unsigned i=0;i<4;++i){chips[i]=table.chips(i);for(unsigned c=0;c<2;++c)holes[i][c]=table.hole(viewer,i,c);}for(unsigned i=0;i<5;++i)board[i]=table.board(i);}
  void encode(uint8_t out[SIZE])const{uint8_t* p=out;*p++=count;*p++=phase;*p++=active;*p++=dealer;*p++=boardCount;*p++=folded;*p++=winners;*p++=shown;Codec::put(p,hand,2);p+=2;for(auto c:chips){Codec::put(p,c,2);p+=2;}const unsigned values[]={pot,wonPot,won,call,minRaise,maxRaise};for(unsigned n:values){Codec::put(p,n,2);p+=2;}memcpy(p,board,5);p+=5;memcpy(p,holes,8);}
  bool decode(const uint8_t in[SIZE],unsigned viewer){const uint8_t* p=in;PokerView next;next.count=*p++;next.phase=*p++;next.active=*p++;next.dealer=*p++;next.boardCount=*p++;next.folded=*p++;next.winners=*p++;next.shown=*p++;next.hand=Codec::get(p,2);p+=2;for(auto& c:next.chips){c=Codec::get(p,2);p+=2;}uint16_t* values[]={&next.pot,&next.wonPot,&next.won,&next.call,&next.minRaise,&next.maxRaise};for(auto n:values){*n=Codec::get(p,2);p+=2;}memcpy(next.board,p,5);p+=5;memcpy(next.holes,p,8);
    if(next.count<2||next.count>4||viewer>=next.count||next.phase>PokerTable::HAND_OVER||next.active>=next.count||next.dealer>=next.count||next.folded>=(1U<<next.count)||next.winners>=(1U<<next.count)||next.shown>=(1U<<next.count)||(next.boardCount!=0&&next.boardCount!=3&&next.boardCount!=4&&next.boardCount!=5))return false;
    unsigned total=next.pot;for(auto c:next.chips)total+=c;if(total!=500*next.count||next.minRaise>next.maxRaise)return false;uint64_t seen=0;
    for(unsigned i=0;i<5;++i){auto c=next.board[i];if(i>=next.boardCount){if(c!=255)return false;}else{if(c>=52||(seen&(uint64_t(1)<<c)))return false;seen|=uint64_t(1)<<c;}}
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<2;++j){auto c=next.holes[i][j];if(c==255)continue;if(i>=next.count||(i!=viewer&&!(next.shown&(1U<<i)))||c>=52||(seen&(uint64_t(1)<<c)))return false;seen|=uint64_t(1)<<c;}
    *this=next;return true;
  }
};
} }
