#include <Arduino.h>
#include "TrackerAudio.h"
#ifdef PRIVATE_STATUS_REPORTER
#ifdef TECHO_KIT_AUDIO
#include <Wire.h>
#include <memory>
#include <bus/i2c/hardware_i2c.h>
#include <bus/i2s/hardware_i2s.h>
#include <chip/i2c_i2s/es8311.h>

namespace {
using namespace cpp_bus_driver;
auto audio_i2c = std::make_shared<HardwareI2c>(PIN_WIRE_SDA, PIN_WIRE_SCL, &Wire);
auto audio_i2s = std::make_shared<HardwareI2s>(23, 38, 25, 10, 9);
Es8311 codec(audio_i2c, audio_i2s, 0x18);
uint32_t audio_buffers[2][512];
bool initialized = false, playing = false, siren = false;
uint32_t started = 0, phase = 0;
uint8_t buffer_index = 0;
bool initialize() {
  if (initialized) return true;
  Logger::SetMinimumLogLevel(Logger::LogLevel::kNone);
  Wire.beginTransmission(0x18);
  if (Wire.endTransmission() != 0) return false;
  if (!codec.Init() || !codec.Init(32, 44100, 16)) return false;
  Es8311::PowerStatus power = {};
  power.contorl.analog_circuits = true;
  power.contorl.analog_bias_circuits = true;
  power.contorl.analog_adc_bias_circuits = false;
  power.contorl.analog_adc_reference_circuits = false;
  power.contorl.analog_dac_reference_circuit = true;
  power.contorl.internal_reference_circuits = false;
  power.vmid = Es8311::Vmid::kStartUpVmidNormalSpeedCharge;
  initialized = codec.SetPowerStatus(power) && codec.SetAdcPower(false) &&
    codec.SetDacPower(true) && codec.SetOutputToHpDrive(true) &&
    codec.SetDacEqualizer(false) && codec.SetDacVolume(191);
  return initialized;
}
void fill(uint32_t* buffer) {
  uint32_t elapsed = millis() - started;
  bool audible = siren || (elapsed < 600 && elapsed % 240 < 120);
  uint32_t frequency = siren ? ((elapsed / 350) % 2 ? 1320 : 880) : 1100;
  int amplitude = siren ? 8000 : 1200;
  for (unsigned i = 0; i < 512; ++i) {
    phase = (phase + frequency) % 44100;
    int16_t sample = audible ? (phase < 22050 ? amplitude : -amplitude) : 0;
    buffer[i] = uint16_t(sample) | (uint32_t(uint16_t(sample)) << 16);
  }
}
}
bool trackerAudioAvailable() {
  uint16_t id = 0;
  for (uint8_t reg = 0xFD; reg != 0xFF; ++reg) {
    Wire.beginTransmission(0x18);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(uint8_t(0x18), uint8_t(1)) != 1) return false;
    id = (id << 8) | Wire.read();
  }
  return id == 0x8311;
}
void trackerAudioStop() {
  if (playing) codec.StopTransmitI2s();
  if (initialized) codec.SetDacPower(false);
  playing = false;
}
void trackerAudioStart(bool alarm) {
  if (playing && siren && !alarm) return;
  trackerAudioStop();
  if (!initialize() || !codec.SetDacPower(true)) return;
  siren = alarm; started = millis(); phase = 0; buffer_index = 0;
  fill(audio_buffers[0]);
  playing = codec.StartTransmitI2s(audio_buffers[0], nullptr, 512);
  buffer_index = 1;
}
void trackerAudioLoop() {
  if (!playing) return;
  if (!siren && uint32_t(millis() - started) >= 600) { trackerAudioStop(); return; }
  if (codec.GetWriteI2sEventFlag()) {
    fill(audio_buffers[buffer_index]);
    if (!codec.SetNextWriteI2s(audio_buffers[buffer_index])) { trackerAudioStop(); return; }
    buffer_index ^= 1;
  }
}
#elif defined(PIN_BUZZER)
namespace { bool sounding = false, alarm_active = false; uint32_t sound_started, next_note; }
bool trackerAudioAvailable() { return true; }
void trackerAudioStop() { noTone(PIN_BUZZER); sounding = false; }
void trackerAudioStart(bool alarm) {
  if (sounding && alarm_active && !alarm) return;
  trackerAudioStop();
  sounding = true; alarm_active = alarm; sound_started = next_note = millis();
}
void trackerAudioLoop() {
  if (!sounding) return;
  uint32_t elapsed = millis() - sound_started;
  if (!alarm_active && elapsed >= 600) { trackerAudioStop(); return; }
  if ((int32_t)(millis() - next_note) < 0) return;
  next_note = millis() + 20;
  if (alarm_active) tone(PIN_BUZZER, elapsed / 350 % 2 ? 1320 : 880, 30);
  else if (elapsed % 240 < 120) tone(PIN_BUZZER, 1100, 20);
  else noTone(PIN_BUZZER);
}
#else
bool trackerAudioAvailable() { return false; }
void trackerAudioStart(bool) { }
void trackerAudioStop() { }
void trackerAudioLoop() { }
#endif
#endif
