#pragma once

#ifdef HAS_DRV2605

#include <Arduino.h>
#include <Adafruit_DRV2605.h>

/*
 * Vibration control class for boards where the motor is behind a
 * DRV2605 haptic driver on I2C (e.g. Seeed SenseCAP MeshTracker X1).
 * Same interface as GenericVibration.
 */

#ifndef VIBRATION_TIMEOUT
#define VIBRATION_TIMEOUT 5000 // cooldown between vibrations
#endif

#ifndef DRV2605_EFFECT
#define DRV2605_EFFECT 16   // "1000 ms alert" from the DRV2605 effect library
#endif

class DRV2605Vibration {
public:
  void begin();       // power up and init the DRV2605
  // pattern: 0 = 1 s long effect, 1 = 350/600/350 ms, 2 = 200 ms x3
  void trigger(uint8_t pattern = 0, bool force = false);
  void trigger(bool force) { trigger((uint8_t)0, force); }
  void loop();
  bool isVibrating();
  void stop();        // stop vibration immediately
  void quiet(bool q) { _quiet = q; }
  bool isQuiet() const { return _quiet; }

private:
  Adafruit_DRV2605 drv;
  bool _ready = false;
  bool _quiet = false;
  bool _sequence_active = false;
  uint8_t _pattern = 0;
  uint8_t _phase = 0;
  unsigned long _phase_started = 0;
  unsigned long _last_trigger = 0;

  void stopUnlocked();
};

#endif // ifdef HAS_DRV2605
