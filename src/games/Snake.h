#pragma once
#include <stdint.h>

namespace solo_games {

// Fixed storage, one movement per due tick, no allocation or radio dependency.
class Snake {
public:
  static const uint16_t WIDTH = 30, HEIGHT = 12, CAPACITY = WIDTH * HEIGHT;
  static const uint16_t NO_FOOD = 0xFFFF;
  enum Direction : uint8_t { RIGHT, DOWN, LEFT, UP };
  enum Change : uint8_t { NONE, MOVED, ATE, LOST, WON };

  void reset(uint32_t seed, uint8_t speed, uint32_t now) {
    _rng = seed ? seed : 0x6D2B79F5U;
    _speed = speed < 3 ? speed : 1;
    _tail = 0; _length = 3; _direction = _queued = RIGHT;
    _turn_queued = _finished = _won = false;
    for (uint8_t& byte : _occupied) byte = 0;
    for (unsigned i = 0; i < 3; ++i) {
      _body[i] = (HEIGHT / 2) * WIDTH + WIDTH / 2 - 2 + i;
      setOccupied(_body[i], true);
    }
    chooseFood();
    restartClock(now);
  }
  bool steer(Direction wanted) {
    if (_finished || _turn_queued || wanted > UP || ((wanted + 2) % 4) == _direction) return false;
    if (wanted != _direction) { _queued = wanted; _turn_queued = true; }
    return true;
  }
  Change tick(uint32_t now) {
    if (_finished || int32_t(now - _next_tick) < 0) return NONE;
    _direction = _queued; _turn_queued = false;
    int x = head() % WIDTH, y = head() / WIDTH;
    if (_direction == RIGHT) ++x;
    if (_direction == LEFT) --x;
    if (_direction == DOWN) ++y;
    if (_direction == UP) --y;
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return lose();
    uint16_t next = uint16_t(y * WIDTH + x);
    bool growing = next == _food;
    if (occupied(next) && (growing || next != body(0))) return lose();
    if (!growing) {
      setOccupied(body(0), false);
      _tail = (_tail + 1) % CAPACITY;
    } else ++_length;
    _body[(_tail + _length - 1) % CAPACITY] = next;
    setOccupied(next, true);
    if (_length == CAPACITY) {
      _food = NO_FOOD; _finished = _won = true; return WON;
    }
    if (growing) chooseFood();
    restartClock(now);
    return growing ? ATE : MOVED;
  }
  void restartClock(uint32_t now) { _next_tick = now + interval(); }
  uint16_t interval() const {
    const uint16_t start[] = {260, 190, 130};
    unsigned reduction = (score() / 4) * 10;
    return reduction + 70 < start[_speed] ? start[_speed] - reduction : 70;
  }
  uint16_t length() const { return _length; }
  uint16_t score() const { return _length - 3; }
  uint16_t head() const { return body(_length - 1); }
  uint16_t body(uint16_t index) const { return _body[(_tail + index) % CAPACITY]; }
  uint16_t food() const { return _food; }
  Direction direction() const { return _direction; }
  bool finished() const { return _finished; }
  bool won() const { return _won; }

private:
  friend struct SnakeFixture;
  uint16_t _body[CAPACITY] = {};
  uint8_t _occupied[(CAPACITY + 7) / 8] = {};
  uint16_t _tail = 0, _length = 3, _food = NO_FOOD;
  uint32_t _rng = 1, _next_tick = 0;
  Direction _direction = RIGHT, _queued = RIGHT;
  uint8_t _speed = 1;
  bool _turn_queued = false, _finished = false, _won = false;

  bool occupied(uint16_t cell) const { return (_occupied[cell / 8] >> (cell % 8)) & 1; }
  void setOccupied(uint16_t cell, bool value) {
    uint8_t mask = uint8_t(1U << (cell % 8));
    if (value) _occupied[cell / 8] |= mask;
    else _occupied[cell / 8] &= uint8_t(~mask);
  }
  void chooseFood() {
    _rng ^= _rng << 13; _rng ^= _rng >> 17; _rng ^= _rng << 5;
    unsigned pick = _rng % (CAPACITY - _length);
    for (uint16_t cell = 0; cell < CAPACITY; ++cell) if (!occupied(cell)) {
      if (!pick--) { _food = cell; return; }
    }
  }
  Change lose() { _finished = true; return LOST; }
};

static_assert(sizeof(Snake) <= 800, "Snake state must fit in 800 bytes");
} // namespace solo_games
