#pragma once

#include "RotaryInput.h"
#include "MomentaryButton.h"

#ifndef ROTARY_ENCODER_DOUBLE_CLICK_MS
  #define ROTARY_ENCODER_DOUBLE_CLICK_MS 280
#endif

class GPIORotaryInput : public RotaryInput {
  int8_t _pin_a;
  int8_t _pin_b;
  uint8_t _state = 0;
  int8_t _delta = 0;
  bool _initialized = false;
  static const uint8_t EDGE_QUEUE_SIZE = 64;
  volatile uint8_t _edge_states[EDGE_QUEUE_SIZE] = {0};
  volatile uint8_t _edge_head = 0;
  volatile uint8_t _edge_tail = 0;

  static GPIORotaryInput*& interruptInstance() {
    static GPIORotaryInput* instance = nullptr;
    return instance;
  }

  static void interruptThunk() {
    GPIORotaryInput* instance = interruptInstance();
    if (!instance) return;
    const uint8_t state = (uint8_t)((digitalRead(instance->_pin_a) << 1) |
                                    digitalRead(instance->_pin_b));
    const uint8_t head = instance->_edge_head;
    const uint8_t next = (uint8_t)((head + 1) % EDGE_QUEUE_SIZE);
    if (next == instance->_edge_tail) return;
    instance->_edge_states[head] = state;
    instance->_edge_head = next;
  }

  bool popState(uint8_t& state) {
    noInterrupts();
    if (_edge_tail == _edge_head) {
      interrupts();
      return false;
    }
    const uint8_t tail = _edge_tail;
    state = _edge_states[tail];
    _edge_tail = (uint8_t)((tail + 1) % EDGE_QUEUE_SIZE);
    interrupts();
    return true;
  }

public:
  GPIORotaryInput(int8_t pin_a, int8_t pin_b) : _pin_a(pin_a), _pin_b(pin_b) { }

  bool begin() override {
    if (_pin_a < 0 || _pin_b < 0) return false;
    pinMode(_pin_a, INPUT_PULLUP);
    pinMode(_pin_b, INPUT_PULLUP);
    _state = (uint8_t)((digitalRead(_pin_a) << 1) | digitalRead(_pin_b));
    _delta = 0;
    _edge_head = 0;
    _edge_tail = 0;
    interruptInstance() = this;
    attachInterrupt(digitalPinToInterrupt(_pin_a), interruptThunk, CHANGE);
    attachInterrupt(digitalPinToInterrupt(_pin_b), interruptThunk, CHANGE);
    _initialized = true;
    return true;
  }

  RotaryInputEvent poll() override {
    if (!_initialized) return RotaryInputEvent::None;

    uint8_t next;
    if (!popState(next)) {
      next = (uint8_t)((digitalRead(_pin_a) << 1) | digitalRead(_pin_b));
      if (next == _state) return RotaryInputEvent::None;
    }
    if (next == _state) return RotaryInputEvent::None;

    const uint8_t transition = (uint8_t)((_state << 2) | next);
    static const int8_t transition_delta[16] = {
      0, -1, 1, 0,
      1, 0, 0, -1,
      -1, 0, 0, 1,
      0, 1, -1, 0
    };
    _delta += transition_delta[transition];
    _state = next;

    if (_delta >= 4) {
      _delta = 0;
#if defined(ROTARY_ENCODER_REVERSE) && ROTARY_ENCODER_REVERSE
      return RotaryInputEvent::Prev;
#else
      return RotaryInputEvent::Next;
#endif
    }
    if (_delta <= -4) {
      _delta = 0;
#if defined(ROTARY_ENCODER_REVERSE) && ROTARY_ENCODER_REVERSE
      return RotaryInputEvent::Next;
#else
      return RotaryInputEvent::Prev;
#endif
    }
    return RotaryInputEvent::None;
  }

  bool isReady() const override { return _initialized; }
};

class GPIORotaryButton {
  int8_t _pin;
  uint8_t _last_level = HIGH;
  uint32_t _pressed_at = 0;
  uint32_t _click_deadline = 0;
  uint8_t _click_count = 0;
  bool _initialized = false;
  static const uint8_t EDGE_QUEUE_SIZE = 8;
  volatile uint8_t _edge_levels[EDGE_QUEUE_SIZE] = {0};
  volatile uint32_t _edge_times[EDGE_QUEUE_SIZE] = {0};
  volatile uint8_t _edge_head = 0;
  volatile uint8_t _edge_tail = 0;
  volatile uint32_t _last_edge_time = 0;
  volatile bool _has_edge_time = false;
  bool _live_healing = false;
  uint8_t _live_level = HIGH;
  uint32_t _live_since = 0;

  static const uint32_t LONG_PRESS_MS = 1000;
  static const uint32_t DOUBLE_CLICK_MS = ROTARY_ENCODER_DOUBLE_CLICK_MS;
  static const uint32_t BUTTON_DEBOUNCE_MS = 25;

  static uint32_t interruptMillis() {
#if defined(NRF52_PLATFORM)
    return (uint32_t)tick2ms(xTaskGetTickCountFromISR());
#else
    return millis();
#endif
  }

  static GPIORotaryButton*& interruptInstance() {
    static GPIORotaryButton* instance = nullptr;
    return instance;
  }

  static void interruptThunk() {
    GPIORotaryButton* instance = interruptInstance();
    if (!instance) return;
    const uint8_t next = (uint8_t)digitalRead(instance->_pin);
    const uint32_t now = interruptMillis();
    if (instance->_has_edge_time &&
        (uint32_t)(now - instance->_last_edge_time) < BUTTON_DEBOUNCE_MS) {
      return;
    }
    instance->_last_edge_time = now;
    instance->_has_edge_time = true;
    const uint8_t head = instance->_edge_head;
    const uint8_t new_head = (uint8_t)((head + 1) % EDGE_QUEUE_SIZE);
    if (new_head == instance->_edge_tail) return;
    instance->_edge_levels[head] = next;
    instance->_edge_times[head] = now;
    instance->_edge_head = new_head;
  }

  bool popEdge(uint8_t& level, uint32_t& timestamp) {
    noInterrupts();
    if (_edge_tail == _edge_head) {
      interrupts();
      return false;
    }
    const uint8_t tail = _edge_tail;
    level = _edge_levels[tail];
    timestamp = _edge_times[tail];
    _edge_tail = (uint8_t)((tail + 1) % EDGE_QUEUE_SIZE);
    interrupts();
    return true;
  }

  void applyLevel(uint8_t level, uint32_t now, int& event) {
    if (level == _last_level) return;
    if (level == LOW) {
      _pressed_at = now;
    } else if (_pressed_at != 0) {
      const uint32_t pressed_for = (uint32_t)(now - _pressed_at);
      if (pressed_for >= LONG_PRESS_MS) {
        // A long press is decided on release. This keeps a short tap from
        // being held for the full long-press timeout before click detection.
        event = BUTTON_EVENT_LONG_PRESS;
        _click_count = 0;
        _click_deadline = 0;
      } else {
        if (_click_count < 3) _click_count++;
        _click_deadline = now + DOUBLE_CLICK_MS;
      }
      _pressed_at = 0;
    }
    _last_level = level;
  }

public:
  explicit GPIORotaryButton(int8_t pin) : _pin(pin) { }

  void begin() {
    if (_pin < 0) return;
    pinMode(_pin, INPUT_PULLUP);
    _last_level = (uint8_t)digitalRead(_pin);
    _pressed_at = 0;
    _click_deadline = 0;
    _click_count = 0;
    _edge_head = 0;
    _edge_tail = 0;
    _last_edge_time = 0;
    _has_edge_time = false;
    _live_healing = false;
    _live_level = _last_level;
    _live_since = 0;
    interruptInstance() = this;
    attachInterrupt(digitalPinToInterrupt(_pin), interruptThunk, CHANGE);
    _initialized = true;
  }

  int check() {
    if (!_initialized) return BUTTON_EVENT_NONE;
    const uint32_t now = millis();
    const uint8_t level = (uint8_t)digitalRead(_pin);
    int event = BUTTON_EVENT_NONE;

    uint8_t edge_level;
    uint32_t edge_time;
    while (popEdge(edge_level, edge_time)) applyLevel(edge_level, edge_time, event);
    if (level != _last_level) {
      // The ISR normally supplies every edge. If one is missed, reconcile
      // against the live pin only after it has stayed at the new level for
      // the debounce interval; otherwise a single bounce read would recreate
      // the phantom press/release pair that the ISR filter discarded.
      if (!_live_healing || level != _live_level) {
        _live_healing = true;
        _live_level = level;
        _live_since = now;
      } else if ((uint32_t)(now - _live_since) >= BUTTON_DEBOUNCE_MS) {
        applyLevel(level, now, event);
        _live_healing = false;
      }
    } else {
      _live_healing = false;
    }

    if (event == BUTTON_EVENT_NONE && _pressed_at == 0 && _click_count != 0 &&
        (int32_t)(now - _click_deadline) >= 0) {
      event = _click_count == 1 ? BUTTON_EVENT_CLICK :
              (_click_count == 2 ? BUTTON_EVENT_DOUBLE_CLICK : BUTTON_EVENT_TRIPLE_CLICK);
      _click_count = 0;
      _click_deadline = 0;
    }
    return event;
  }
};
