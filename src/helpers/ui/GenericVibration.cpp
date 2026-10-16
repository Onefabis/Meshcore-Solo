#ifdef PIN_VIBRATION
#include "GenericVibration.h"

void GenericVibration::begin() {
  pinMode(PIN_VIBRATION, OUTPUT);
  digitalWrite(PIN_VIBRATION, LOW);
  _phase_started = 0;
  _pattern = 0;
  _phase = 0;
  _active = false;
}

void GenericVibration::trigger(uint8_t pattern) {
  _pattern = pattern < 3 ? pattern : 0;
  _phase = 0;
  _phase_started = millis();
  _active = true;
  digitalWrite(PIN_VIBRATION, HIGH);
}

void GenericVibration::loop() {
  if (!_active) return;
  static const uint16_t long_pulse[] = { 1000 };
  static const uint16_t two_mids[] = { 350, 600, 350 };
  static const uint16_t three_shorts[] = { 200, 200, 200, 200, 200 };
  const uint16_t* phases = _pattern == 0 ? long_pulse : (_pattern == 1 ? two_mids : three_shorts);
  const uint8_t phase_count = _pattern == 0 ? 1 : (_pattern == 1 ? 3 : 5);
  if (millis() - _phase_started < phases[_phase]) return;
  _phase++;
  _phase_started = millis();
  if (_phase >= phase_count) { stop(); return; }
  digitalWrite(PIN_VIBRATION, (_phase & 1) ? LOW : HIGH);
}

bool GenericVibration::isVibrating() {
  return _active;
}

void GenericVibration::stop() {
  _active = false;
  _phase_started = 0;
  digitalWrite(PIN_VIBRATION, LOW);
}

#endif // ifdef PIN_VIBRATION
