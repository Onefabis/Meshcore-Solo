#ifdef HAS_DRV2605

#include "DRV2605Vibration.h"
#include <Wire.h>
#include "I2CBusLock.h"

#if defined(NRF52_PLATFORM) && defined(CARDKB_I2C)
void* meshcore_i2c_bus_mutex __attribute__((weak)) = nullptr;
#endif

void DRV2605Vibration::begin() {
  I2CBusLock bus_lock;
#ifdef PIN_DRV_EN
  pinMode(PIN_DRV_EN, OUTPUT);
  digitalWrite(PIN_DRV_EN, HIGH);   // power up the haptic driver
  delay(10);
#endif
  if (!drv.begin(&Wire)) {
    return;   // no haptic driver found, stay silent
  }
#ifdef DRV2605_USE_LRA
  // LRA mode: 4x brake factor, medium loop gain, back-EMF gain 2
  drv.writeRegister8(DRV2605_REG_FEEDBACK, 0xB6);
#endif
  drv.selectLibrary(1);
  drv.setMode(DRV2605_MODE_INTTRIG);
  _ready = true;
}

void DRV2605Vibration::trigger(uint8_t pattern, bool force) {
  I2CBusLock bus_lock;
  if (!_ready || _quiet) return;
  unsigned long now = millis();
  if (!force && _last_trigger != 0 && now - _last_trigger < VIBRATION_TIMEOUT) return;
  stopUnlocked();
  _last_trigger = now;
  if (pattern >= 3) pattern = 0;
  if (pattern == 0) {
    _sequence_active = false;
    drv.setMode(DRV2605_MODE_INTTRIG);
    drv.setWaveform(0, DRV2605_EFFECT);
    drv.setWaveform(1, 0);
    drv.go();
    return;
  }
  _pattern = pattern;
  _phase = 0;
  _phase_started = now;
  _sequence_active = true;
  drv.setMode(DRV2605_MODE_REALTIME);
  drv.setRealtimeValue(0x7F);
}

void DRV2605Vibration::loop() {
  I2CBusLock bus_lock;
  if (!_ready || !_sequence_active) return;
  static const uint16_t two_mids[] = { 350, 600, 350 };
  static const uint16_t three_shorts[] = { 200, 200, 200, 200, 200 };
  const uint16_t* phases = _pattern == 1 ? two_mids : three_shorts;
  const uint8_t phase_count = _pattern == 1 ? 3 : 5;
  unsigned long now = millis();
  if (now - _phase_started < phases[_phase]) return;
  _phase++;
  _phase_started = now;
  if (_phase >= phase_count) {
    drv.setRealtimeValue(0);
    drv.setMode(DRV2605_MODE_INTTRIG);
    _sequence_active = false;
  } else {
    drv.setRealtimeValue((_phase & 1) ? 0 : 0x7F);
  }
}

bool DRV2605Vibration::isVibrating() {
  return _sequence_active;
}

void DRV2605Vibration::stopUnlocked() {
  if (_ready) {
    _sequence_active = false;
    drv.setRealtimeValue(0);
    drv.stop();
    drv.setMode(DRV2605_MODE_INTTRIG);
  }
}

void DRV2605Vibration::stop() {
  I2CBusLock bus_lock;
  stopUnlocked();
}

#endif // ifdef HAS_DRV2605
