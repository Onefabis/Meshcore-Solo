#pragma once
#include <stdint.h>

namespace solo_games {

class PokerHand {
public:
  enum Category:uint8_t { HIGH_CARD, PAIR, TWO_PAIR, TRIPS, STRAIGHT, FLUSH, FULL_HOUSE, QUADS, STRAIGHT_FLUSH };
  static uint32_t rank(const uint8_t* cards,unsigned count) {
    if(count<5 || count>7)return 0;
    uint64_t seen=0;
    for(unsigned i=0;i<count;++i){if(cards[i]>=52 || ((seen>>cards[i])&1))return 0;seen|=uint64_t(1)<<cards[i];}
    uint32_t best=0;
    for(unsigned a=0;a<count-4;++a)for(unsigned b=a+1;b<count-3;++b)
      for(unsigned c=b+1;c<count-2;++c)for(unsigned d=c+1;d<count-1;++d)for(unsigned e=d+1;e<count;++e) {
        uint8_t picked[]={cards[a],cards[b],cards[c],cards[d],cards[e]};uint32_t value=five(picked);if(value>best)best=value;
      }
    return best;
  }
  static const char* name(uint32_t value) {
    static const char* names[]={"High","Pair","2 pair","Trips","Straight","Flush","Fullhouse","Quads","StrFlush"};
    return value && (value>>20)<=STRAIGHT_FLUSH?names[value>>20]:"No hand";
  }
private:
  static uint32_t score(unsigned category,unsigned a=0,unsigned b=0,unsigned c=0,unsigned d=0,unsigned e=0) {
    return (category<<20)|(a<<16)|(b<<12)|(c<<8)|(d<<4)|e;
  }
  static uint32_t five(const uint8_t* cards) {
    uint8_t counts[15]={},descending[5],pairs[2]={},kickers[5]={};
    bool flush=true;for(unsigned i=0;i<5;++i){++counts[cards[i]%13+2];flush&=cards[i]/13==cards[0]/13;}
    unsigned at=0,unique=0,pair_count=0,kicker_count=0,trip=0,quad=0;
    for(int rank=14;rank>=2;--rank)if(counts[rank]) {
      ++unique;for(unsigned i=0;i<counts[rank];++i)descending[at++]=rank;
      if(counts[rank]==4)quad=rank;
      else if(counts[rank]==3)trip=rank;
      else if(counts[rank]==2)pairs[pair_count++]=rank;
      else kickers[kicker_count++]=rank;
    }
    unsigned straight=unique==5 && descending[0]-descending[4]==4?descending[0]:0;
    if(counts[14] && counts[5] && counts[4] && counts[3] && counts[2])straight=5;
    if(flush && straight)return score(STRAIGHT_FLUSH,straight);
    if(quad)return score(QUADS,quad,kickers[0]);
    if(trip && pair_count)return score(FULL_HOUSE,trip,pairs[0]);
    if(flush)return score(FLUSH,descending[0],descending[1],descending[2],descending[3],descending[4]);
    if(straight)return score(STRAIGHT,straight);
    if(trip)return score(TRIPS,trip,kickers[0],kickers[1]);
    if(pair_count==2)return score(TWO_PAIR,pairs[0],pairs[1],kickers[0]);
    if(pair_count)return score(PAIR,pairs[0],kickers[0],kickers[1],kickers[2]);
    return score(HIGH_CARD,descending[0],descending[1],descending[2],descending[3],descending[4]);
  }
};

// Heads-up, 500 virtual chips each, 5/10 blinds. Bets are capped at effective
// stacks; uncalled blind/call excess is returned before showdown. No side pot
// is needed with two players. No network or persistent storage dependency.
class Holdem {
public:
  enum Phase:uint8_t { READY, PREFLOP, FLOP, TURN, RIVER, HAND_OVER };
  enum Action:uint8_t { FOLD, CHECK_CALL, RAISE };
  struct Decision { Action action;uint16_t target; };
  void reset(uint32_t seed) {
    _rng=seed?seed:1;_chips[0]=_chips[1]=500;_street[0]=_street[1]=0;
    _pot=_bet=_won_pot=0;_last_raise=10;_dealer=1;_active=0;_pending=0;_phase=READY;
    _board_count=0;_deck_pos=0;_winner=0;_shown=false;_hand=0;_ranks[0]=_ranks[1]=0;
    for(unsigned i=0;i<5;++i)_board[i]=255;
    for(unsigned side=0;side<2;++side)for(auto& card:_hole[side])card=255;
  }
  bool deal() {
    if((_phase!=READY && _phase!=HAND_OVER) || !_chips[0] || !_chips[1])return false;
    _dealer=1-_dealer;++_hand;
    for(unsigned i=0;i<52;++i)_deck[i]=i;
    for(unsigned i=51;i>0;--i){unsigned j=random()%(i+1);uint8_t card=_deck[i];_deck[i]=_deck[j];_deck[j]=card;}
    _deck_pos=0;
    for(unsigned round=0;round<2;++round){_hole[1-_dealer][round]=draw();_hole[_dealer][round]=draw();}
    for(auto& card:_board)card=255;
    _board_count=0;_street[0]=_street[1]=0;_pot=_bet=_won_pot=0;_last_raise=10;
    _winner=0;_shown=false;_ranks[0]=_ranks[1]=0;_phase=PREFLOP;_pending=3;_active=_dealer;
    pay(_dealer,_chips[_dealer]<5?_chips[_dealer]:5);
    pay(1-_dealer,_chips[1-_dealer]<10?_chips[1-_dealer]:10);
    _bet=_street[0]>_street[1]?_street[0]:_street[1];
    if(!_chips[0] || !_chips[1]) {
      unsigned live=_chips[0]?0:1;
      if(_chips[live] && _street[live]<_bet){_pending=uint8_t(1U<<live);_active=live;}
      else {_pending=0;finishRound();}
    }
    return true;
  }
  bool act(uint8_t side,Action action,uint16_t target=0) {
    if(_phase< PREFLOP || _phase>RIVER || side>1 || side!=_active || !(_pending&(1U<<side)) || !_chips[side])return false;
    unsigned other=1-side;
    if(action==FOLD){_winner=other+1;award();return true;}
    if(action==CHECK_CALL) {
      unsigned amount=toCall(side);if(amount>_chips[side])amount=_chips[side];pay(side,amount);
      _pending&=uint8_t(~(1U<<side));
    } else if(action==RAISE) {
      unsigned maximum=maxRaiseTo(side),minimum=minRaiseTo(side);
      if(!maximum || target<minimum || target>maximum)return false;
      unsigned increase=target-_bet;pay(side,target-_street[side]);
      if(increase>=_last_raise)_last_raise=increase;
      _bet=target;_pending=uint8_t(1U<<other);
    } else return false;
    for(unsigned i=0;i<2;++i)if(!_chips[i])_pending&=uint8_t(~(1U<<i));
    if(!_pending)finishRound();else _active=other;
    return true;
  }
  Decision chooseAction(uint8_t side) {
    Decision decision={CHECK_CALL,0};
    if(side>1 || side!=_active || _phase<PREFLOP || _phase>RIVER)return decision;
    unsigned r0=_hole[side][0]%13+2,r1=_hole[side][1]%13+2;
    unsigned strength=r0+r1+(r0==r1?20:0)+(_hole[side][0]/13==_hole[side][1]/13?3:0);
    if(_board_count>=3) {
      uint8_t known[7]={_hole[side][0],_hole[side][1]};for(unsigned i=0;i<_board_count;++i)known[i+2]=_board[i];
      uint32_t value=PokerHand::rank(known,2+_board_count);strength=(value>>20)*14+((value>>16)&15);
    }
    unsigned roll=random()%100,call=toCall(side),maximum=maxRaiseTo(side);
    if(call>10 && call>_pot/4 && strength<24 && roll>15)decision.action=FOLD;
    else if(maximum && (strength>=38 || roll<12)) {
      decision.action=RAISE;unsigned target=minRaiseTo(side)+_pot/2;
      decision.target=target>maximum?maximum:target;
    }
    return decision;
  }
  Phase phase()const{return _phase;}
  uint8_t active()const{return _active;}
  uint8_t dealer()const{return _dealer;}
  uint16_t chips(unsigned side)const{return side<2?_chips[side]:0;}
  uint16_t pot()const{return _pot;}
  uint16_t wonPot()const{return _won_pot;}
  uint16_t hand()const{return _hand;}
  uint8_t winner()const{return _winner;}
  bool shown()const{return _shown;}
  bool busted()const{return !_chips[0] || !_chips[1];}
  uint8_t boardCount()const{return _board_count;}
  uint8_t board(unsigned i)const{return i<_board_count?_board[i]:255;}
  uint8_t hole(unsigned side,unsigned i)const{return side<2 && i<2?_hole[side][i]:255;}
  uint32_t handRank(unsigned side)const{return side<2?_ranks[side]:0;}
  uint16_t toCall(unsigned side)const{return side<2 && _bet>_street[side]?_bet-_street[side]:0;}
  uint16_t maxRaiseTo(unsigned side)const {
    if(side>1 || !_chips[side] || !_chips[1-side] || _phase<PREFLOP || _phase>RIVER)return 0;
    unsigned own=_street[side]+_chips[side],other=_street[1-side]+_chips[1-side];
    unsigned value=own<other?own:other;return value>_bet?value:0;
  }
  uint16_t minRaiseTo(unsigned side)const {
    unsigned maximum=maxRaiseTo(side),minimum=_bet+_last_raise;
    return maximum?(minimum>maximum?maximum:minimum):0;
  }
private:
  friend struct HoldemFixture;
  uint8_t _deck[52]={},_hole[2][2]={{255,255},{255,255}},_board[5]={255,255,255,255,255};
  uint16_t _chips[2]={500,500},_street[2]={},_pot=0,_bet=0,_last_raise=10,_won_pot=0,_hand=0;
  uint32_t _rng=1,_ranks[2]={};
  uint8_t _dealer=1,_active=0,_pending=0,_deck_pos=0,_board_count=0,_winner=0;
  Phase _phase=READY;bool _shown=false;
  uint32_t random(){_rng^=_rng<<13;_rng^=_rng>>17;_rng^=_rng<<5;return _rng;}
  uint8_t draw(){return _deck[_deck_pos++];}
  void pay(unsigned side,unsigned amount){_chips[side]-=amount;_street[side]+=amount;_pot+=amount;}
  void nextBoard() {
    draw(); // burn before each community-card street
    unsigned count=_board_count?1:3;while(count--)_board[_board_count++]=draw();
  }
  void award() {
    _won_pot=_pot;
    if(_winner==3){_chips[0]+=_pot/2;_chips[1]+=_pot/2;_chips[1-_dealer]+=_pot%2;}
    else _chips[_winner-1]+=_pot;
    _pot=0;_pending=0;_phase=HAND_OVER;
  }
  void showdown() {
    while(_board_count<5)nextBoard();
    for(unsigned side=0;side<2;++side) {
      uint8_t cards[7]={_hole[side][0],_hole[side][1]};for(unsigned i=0;i<5;++i)cards[i+2]=_board[i];
      _ranks[side]=PokerHand::rank(cards,7);
    }
    _shown=true;_winner=_ranks[0]==_ranks[1]?3:_ranks[0]>_ranks[1]?1:2;award();
  }
  void finishRound() {
    // Only an unmatched all-in/short blind can leave unequal contributions.
    if(_street[0]!=_street[1]) {
      unsigned side=_street[0]>_street[1]?0:1,difference=_street[side]-_street[1-side];
      _chips[side]+=difference;_street[side]-=difference;_pot-=difference;
    }
    if(_phase==RIVER || !_chips[0] || !_chips[1]){showdown();return;}
    nextBoard();_phase=Phase(unsigned(_phase)+1);_street[0]=_street[1]=0;_bet=0;_last_raise=10;
    _pending=3;_active=1-_dealer;
  }
};
static_assert(sizeof(Holdem)<=128,"Heads-up Holdem state must remain bounded");
} // namespace solo_games
