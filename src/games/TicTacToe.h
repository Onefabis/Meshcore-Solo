#pragma once
#include <stdint.h>

namespace solo_games {

struct TicTacToePosition {
  uint16_t x = 0, o = 0;
  uint8_t turn = 1;

  uint8_t result() const {
    static const uint16_t lines[] = {7,56,448,73,146,292,273,84};
    for (uint16_t line : lines) {
      if ((x & line) == line) return 1;
      if ((o & line) == line) return 2;
    }
    return (x | o) == 511 ? 3 : 0;
  }
  uint16_t winningMask() const {
    static const uint16_t lines[] = {7,56,448,73,146,292,273,84};
    for (uint16_t line : lines) if ((x & line) == line || (o & line) == line) return line;
    return 0;
  }
  bool legal(uint8_t cell) const { return cell < 9 && !result() && !((x | o) & (1U << cell)); }
  bool play(uint8_t cell) {
    if (!legal(cell)) return false;
    (turn == 1 ? x : o) |= uint16_t(1U << cell);
    turn = 3 - turn;
    return true;
  }
  uint8_t at(uint8_t cell) const { return x & (1U << cell) ? 1 : o & (1U << cell) ? 2 : 0; }
};

// Fixed-depth, cooperative alpha-beta search. No recursion, heap allocation,
// hardware, timing or network dependency. Caller bounds work with step().
class TicTacToeAI {
public:
  enum Difficulty : uint8_t { EASY, NORMAL, HARD };

  void begin(const TicTacToePosition& position, Difficulty difficulty, uint32_t seed) {
    _cpu = position.turn; _choice = 255; _depth = 0; _busy = false;
    if (position.result()) return;
    if (difficulty == NORMAL) {
      if (findWin(position, _cpu) || findWin(position, 3 - _cpu)) return;
      if (position.legal(4)) { _choice = 4; return; }
    }
    if (difficulty != HARD) {
      uint8_t cells[9], count = 0;
      for (uint8_t i = 0; i < 9; ++i) if (position.legal(i)) cells[count++] = i;
      seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
      _choice = cells[(seed ? seed : 0xA341316CU) % count];
      return;
    }
    _nodes[0] = makeNode(position, 255, -100, 100);
    _busy = true;
  }

  bool step(unsigned budget = 128) {
    static const uint8_t order[] = {4,0,2,6,8,1,3,5,7};
    while (_busy && budget--) {
      Node& n = _nodes[_depth];
      TicTacToePosition p; p.x = n.x; p.o = n.o; p.turn = n.turn;
      uint8_t result = p.result();
      if (result || n.next == 9 || n.alpha >= n.beta) {
        int8_t value = result == 3 ? 0 : result ? (result == _cpu ? 10 - _depth : _depth - 10) : n.best;
        uint8_t move = n.move;
        if (_depth == 0) { _busy = false; break; }
        Node& parent = _nodes[--_depth];
        bool maximizing = parent.turn == _cpu;
        if ((maximizing && value > parent.best) || (!maximizing && value < parent.best)) {
          parent.best = value;
          if (_depth == 0) _choice = move;
        }
        if (maximizing && parent.best > parent.alpha) parent.alpha = parent.best;
        if (!maximizing && parent.best < parent.beta) parent.beta = parent.best;
        continue;
      }
      uint8_t cell = order[n.next++];
      if (!p.play(cell)) continue;
      ++_depth;
      _nodes[_depth] = makeNode(p, cell, n.alpha, n.beta);
    }
    return !_busy;
  }

  bool busy() const { return _busy; }
  uint8_t choice() const { return _choice; }

private:
  struct Node {
    uint16_t x, o;
    int8_t best, alpha, beta;
    uint8_t next, move, turn;
  };
  Node _nodes[10];
  uint8_t _cpu = 1, _depth = 0, _choice = 255;
  bool _busy = false;

  Node makeNode(const TicTacToePosition& p, uint8_t move, int8_t alpha, int8_t beta) const {
    Node n = {p.x,p.o,int8_t(p.turn == _cpu ? -100 : 100),alpha,beta,0,move,p.turn};
    return n;
  }
  bool findWin(const TicTacToePosition& p, uint8_t mark) {
    for (uint8_t cell = 0; cell < 9; ++cell) {
      TicTacToePosition next = p; next.turn = mark;
      if (next.play(cell) && next.result() == mark) { _choice = cell; return true; }
    }
    return false;
  }
};

static_assert(sizeof(TicTacToePosition) + sizeof(TicTacToeAI) <= 128, "Offline rules and search must stay small");
} // namespace solo_games
