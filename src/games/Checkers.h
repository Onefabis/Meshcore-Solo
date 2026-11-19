#pragma once
#include <stdint.h>

namespace solo_games {

// 8x8 draughts: men move forward but capture in all four directions. Kings
// fly along diagonals; promotion is immediate and capture chains continue.
// Captured pieces block the diagonal until the whole capture turn is finished.
class Checkers {
public:
  static const uint8_t NO_SQUARE = 255, MAX_MOVES = 160;
  struct Move { uint8_t from, to; };
  void reset() {
    for (uint8_t i=0;i<32;++i) _board[i] = i<12 ? -1 : i>=20 ? 1 : 0;
    _turn=1;_forced=NO_SQUARE;_quiet=0;_result=0;_captured=0;
  }
  static int square(int row,int col) {
    if(row<0 || row>=8 || col<0 || col>=8 || !((row+col)&1))return -1;
    return row*4+col/2;
  }
  static int row(uint8_t square) { return square/4; }
  static int col(uint8_t square) { return (square%4)*2+((row(square)+1)&1); }
  int8_t at(uint8_t square) const { return square<32?_board[square]:0; }
  int8_t turn() const { return _turn; }
  uint8_t forced() const { return _forced; }
  uint8_t result() const { return _result; } // 1 human, 2 CPU, 3 draw
  bool mustCapture() const { return hasCapture(); }
  uint8_t legalMoves(Move out[MAX_MOVES]) const {
    if(_result)return 0;
    return generate(out,hasCapture());
  }
  bool legalMoveAt(uint8_t index,Move& move) const {
    return !_result && generate(&move,hasCapture(),NO_SQUARE,NO_SQUARE,index)!=0;
  }
  uint32_t captured() const { return _captured; }
  bool play(uint8_t from,uint8_t to) {
    if(_result || from>=32 || to>=32 || !generate(nullptr,hasCapture(),from,to))return false;
    int dy=row(to)>row(from)?1:-1,dx=col(to)>col(from)?1:-1,victim=-1;
    for(int r=row(from)+dy,c=col(from)+dx;r!=row(to);r+=dy,c+=dx) {
      int i=square(r,c);if(_board[i])victim=i;
    }
    bool capture=victim>=0;
    int8_t piece=_board[from];_board[from]=0;_board[to]=piece;
    if(capture)_captured|=uint32_t(1)<<victim;
    bool crown=(piece==1 && row(to)==0) || (piece==-1 && row(to)==7);
    if(crown)_board[to]=piece*2;
    if(capture || crown)_quiet=0;
    _forced=NO_SQUARE;
    if(capture) {
      _forced=to;
      if(hasCapture())return true;
      _forced=NO_SQUARE;
    }
    for(unsigned i=0;i<32;++i)if((_captured>>i)&1)_board[i]=0;
    _captured=0;
    if(!capture && !crown)++_quiet;
    _turn=-_turn;
    if(!generate(nullptr,hasCapture()))_result=_turn==1?2:1;
    else if(_quiet>=80)_result=3;
    return true;
  }
  uint32_t stateHash() const {
    uint32_t hash=2166136261U;
    for(unsigned i=0;i<32;++i){hash^=uint8_t(_board[i]);hash*=16777619U;}
    for(unsigned shift=0;shift<32;shift+=8){hash^=uint8_t(_captured>>shift);hash*=16777619U;}
    const uint8_t tail[]={uint8_t(_turn),_forced,_quiet,_result};
    for(uint8_t value:tail){hash^=value;hash*=16777619U;}
    return hash;
  }
  int evaluate() const {
    if(_result)return _result==3?0:_result==1?20000:-20000;
    int score=0;
    for(unsigned i=0;i<32;++i)if(_board[i] && !((_captured>>i)&1)) {
      int value=(_board[i]==2 || _board[i]==-2)?175:100;
      value+=2*(_board[i]>0?7-row(i):row(i));
      if(col(i)>=2 && col(i)<=5)value+=3;
      score+=_board[i]>0?value:-value;
    }
    return score;
  }
  uint32_t pieces(bool human) const {
    uint32_t bits=0;for(unsigned i=0;i<32;++i)if(_board[i] && !((_captured>>i)&1) && (_board[i]>0)==human)bits|=uint32_t(1)<<i;
    return bits;
  }
  uint32_t kings() const {
    uint32_t bits=0;for(unsigned i=0;i<32;++i)if(_board[i]==2 || _board[i]==-2)bits|=uint32_t(1)<<i;
    return bits;
  }
private:
  friend struct CheckersFixture;
  int8_t _board[32]={};
  int8_t _turn=1;
  uint8_t _forced=NO_SQUARE,_quiet=0,_result=0;
  uint32_t _captured=0;
  bool owned(uint8_t i) const { return _board[i] && ((_board[i]>0)==(_turn>0)); }
  bool hasCapture() const { return generate(nullptr,true)!=0; }
  uint8_t generate(Move* out,bool captures,uint8_t wanted_from=NO_SQUARE,uint8_t wanted_to=NO_SQUARE,int nth=-1) const {
    uint8_t count=0;
    for(uint8_t i=0;i<32;++i)if(owned(i) && (_forced==NO_SQUARE || _forced==i) && (wanted_from==NO_SQUARE || wanted_from==i)) {
      bool king=_board[i]==2 || _board[i]==-2;
      for(int dy=-1;dy<=1;dy+=2) {
        if(!king && !captures && dy!=(_turn>0?-1:1))continue;
        for(int dx=-1;dx<=1;dx+=2) {
          int victim=-1,limit=king?7:captures?2:1;
          for(int distance=1;distance<=limit;++distance) {
            int to=square(row(i)+distance*dy,col(i)+distance*dx);
            if(to<0)break;
            if(_board[to]) {
              if(!captures || victim>=0 || owned(to) || ((_captured>>to)&1))break;
              victim=to;
            } else if(!captures || victim>=0) {
              if(wanted_from!=NO_SQUARE) {if(to==wanted_to)return 1;continue;}
              if(!out)return 1;
              if(nth>=0) {if(count++==nth){out[0]={i,uint8_t(to)};return 1;}}
              else if(count<MAX_MOVES)out[count++]={i,uint8_t(to)};
            }
          }
        }
      }
    }
    return nth>=0 || wanted_from!=NO_SQUARE?0:count;
  }

};

// Normal searches one reply per poll, with no recursion or long candidate loop.
class CheckersAI {
public:
  void begin(const Checkers& position,bool normal,uint32_t seed) {
    _position=position;_count=position.legalMoves(_moves);_index=0;_side=position.turn();
    _seed=seed?seed:1;_best_score=-30000;_best={255,255};_busy=_count!=0;_has_child=false;
    if(!normal && _count) {_best=_moves[random()%_count];_busy=false;}
  }
  bool step(unsigned budget=1) {
    while(_busy && budget--) {
      if(!_has_child) {
        _child=_position;auto move=_moves[_index];_child.play(move.from,move.to);
        _candidate_score=_child.evaluate()*_side;_reply_index=0;
        if(_child.result()){finishCandidate();continue;}
        _maximize=_child.turn()==_side;_reply_score=_maximize?-30000:30000;_has_child=true;
      } else {
        Checkers::Move reply;
        if(_child.legalMoveAt(_reply_index,reply)) {
          ++_reply_index;Checkers after=_child;after.play(reply.from,reply.to);
          int value=after.evaluate()*_side;
          if(_maximize?value>_reply_score:value<_reply_score)_reply_score=value;
        } else {
          if(_reply_index)_candidate_score=_reply_score;
          finishCandidate();
        }
      }
    }
    return !_busy;
  }
  bool busy() const { return _busy; }
  Checkers::Move choice() const { return _best; }
private:
  Checkers _position,_child;
  Checkers::Move _moves[Checkers::MAX_MOVES],_best={255,255};
  int _best_score=-30000,_candidate_score=0,_reply_score=0;
  uint32_t _seed=1;
  uint8_t _count=0,_index=0,_reply_index=0;
  int8_t _side=-1;
  bool _busy=false,_has_child=false,_maximize=false;
  uint32_t random() { _seed^=_seed<<13;_seed^=_seed>>17;_seed^=_seed<<5;return _seed; }
  void finishCandidate() {
    if(_candidate_score>_best_score || (_candidate_score==_best_score && (random()&1))) {_best_score=_candidate_score;_best=_moves[_index];}
    ++_index;_has_child=false;if(_index==_count)_busy=false;
  }
};
static_assert(sizeof(Checkers)+sizeof(CheckersAI)<=512,"Checkers rules and CPU must stay bounded");
} // namespace solo_games
