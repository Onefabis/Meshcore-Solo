#pragma once

#ifdef PIN_VIBRATION

#include <Arduino.h>

/*
 * Vibration motor control class
 *
 * Provides vibration feedback for events like new messages and new contacts
 * Features:
 * - 1-second vibration pulse
 * - 5-second nag timeout (cooldown between vibrations)
 * - Non-blocking operation
 */

#ifndef VIBRATION_TIMEOUT
#define VIBRATION_TIMEOUT 5000 // 5 seconds default
#endif

class GenericVibration {
public:
  void begin();       // set up vibration pin
  // pattern: 0 = 1 s long pulse, 1 = 350/600/350 ms, 2 = 200 ms x3
  void trigger(uint8_t pattern = 0); // trigger vibration if cooldown has passed
  void loop();        // non-blocking timer handling
  bool isVibrating(); // returns true if currently vibrating
  void stop();        // stop vibration immediately

private:
  unsigned long _phase_started;
  uint8_t _pattern;
  uint8_t _phase;
  bool _active;
};

#endif // ifdef PIN_VIBRATION
