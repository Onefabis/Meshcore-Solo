"""Compile the real serial parser and dual wrappers against deterministic USB/BLE stubs."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
arduino = r'''
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
using std::size_t;
inline uint32_t millis() { static uint32_t tick = 0; return ++tick; }
inline void yield() {}
class Stream {
public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int availableForWrite() { return 4096; }
  virtual size_t write(const uint8_t*, size_t) = 0;
};
struct DefaultSerial {
  explicit operator bool() const { return true; }
};
inline DefaultSerial Serial;
'''
stream = r'''
#include <string>
#include <vector>
#include <cassert>
struct FakeStream : Stream {
  std::string input, output;
  size_t cursor = 0;
  int available() override { return int(input.size() - cursor); }
  int read() override { return available() ? uint8_t(input[cursor++]) : -1; }
  size_t write(const uint8_t* bytes, size_t n) override { output.append((const char*)bytes, n); return n; }
  explicit operator bool() const { return true; }
  void feed(const std::string& s) { input += s; }
};
std::string frame(const std::string& payload) {
  return std::string("<") + char(payload.size() & 255) + char(payload.size() >> 8) + payload;
}
'''
parser_test = r'''
#include "ArduinoSerialInterface.h"
STREAM
int main() {
  FakeStream port; ArduinoSerialInterface usb; usb.begin(port); usb.enable();
  uint8_t binary[176]; char text[80];
  for (const auto& ending : {"\r", "\n", "\r\n"}) {
    port.feed(std::string("gps status") + ending);
    assert(usb.checkRecvFrame(binary) == 0);
    assert(usb.checkRecvText(text, sizeof(text)) && !strcmp(text, "gps status"));
    assert(usb.checkRecvFrame(binary) == 0 && !usb.checkRecvText(text, sizeof(text)));
  }
  // A command split across USB reads is held until its line terminator.
  port.feed("status inter"); assert(usb.checkRecvFrame(binary) == 0);
  assert(!usb.checkRecvText(text, sizeof(text)));
  port.feed("val 300\n"); usb.checkRecvFrame(binary);
  assert(usb.checkRecvText(text, sizeof(text)) && !strcmp(text, "status interval 300"));
  // Command-looking bytes inside a binary payload never reach the CLI.
  const std::string payload = "gps off\r\nstatus interval 0\n";
  auto packet = frame(payload); port.feed(packet.substr(0, 4));
  assert(usb.checkRecvFrame(binary) == 0 && !usb.checkRecvText(text, sizeof(text)));
  port.feed(packet.substr(4)); assert(usb.checkRecvFrame(binary) == payload.size());
  assert(!memcmp(binary, payload.data(), payload.size()) && !usb.checkRecvText(text, sizeof(text)));
  // Mixed commands and binary frames retain ordering and data.
  port.feed("status show\n" + frame("query") + "gps diag\n");
  assert(usb.checkRecvFrame(binary) == 0 && usb.checkRecvText(text, sizeof(text)));
  assert(!strcmp(text, "status show"));
  assert(usb.checkRecvFrame(binary) == 5 && !memcmp(binary, "query", 5));
  assert(usb.checkRecvFrame(binary) == 0 && usb.checkRecvText(text, sizeof(text)));
  assert(!strcmp(text, "gps diag"));
  // Oversized commands are rejected whole; the next line still works.
  port.feed("gps off" + std::string(100, ' ') + "\nstatus show\n");
  usb.checkRecvFrame(binary);
  assert(usb.checkRecvText(text, sizeof(text)) && !strcmp(text, "status show"));
  assert(port.output == "Error: command too long\r\n");
  port.feed("gps ofx\bf\n"); usb.checkRecvFrame(binary);
  assert(usb.checkRecvText(text, sizeof(text)) && !strcmp(text, "gps off"));
  usb.writeText("reply"); assert(port.output.find("reply\r\n") != std::string::npos);
  usb.enable(); assert(!usb.checkRecvText(text, sizeof(text)));
}
'''.replace('STREAM', stream)
ble = r'''
bool ble_connected = true;
unsigned ble_writes = 0;
class SerialBLEInterface : public BaseSerialInterface {
public:
  void begin(const char*, char*, uint32_t) {}
  void enable() override {}
  void disable() override {}
  bool isEnabled() const override { return true; }
  bool isConnected() const override { return ble_connected; }
  bool isWriteBusy() const override { return false; }
  size_t writeFrame(const uint8_t*, size_t n) override { ++ble_writes; return n; }
  size_t checkRecvFrame(uint8_t*) override { return 0; }
};
'''
dual_test = r'''
#include "ArduinoSerialInterface.h"
STREAM
#include "MultiSerialInterface.h"
BLE
int main() {
  FakeStream port;
  ArduinoSerialInterface usb;
  SerialBLEInterface ble;
  MultiSerialInterface dual;
  usb.begin(port);
  dual.addInterface(InterfaceType::Bluetooth, &ble);
  dual.addInterface(InterfaceType::USB, &usb);
  dual.enable();
  uint8_t binary[176]; char text[80];
  port.feed("status show\n");
  assert(dual.checkRecvFrame(binary) == 0);
  assert(dual.checkRecvText(text, sizeof(text)) && !strcmp(text, "status show"));
  dual.writeText("USB reply"); assert(port.output == "USB reply\r\n" && ble_writes == 0);
  port.feed(frame("gps off\n"));
  assert(dual.checkRecvFrame(binary) == 8 && !dual.checkRecvText(text, sizeof(text)));
  ble_connected = false; dual.checkRecvFrame(binary);
  port.feed(frame("query"));
  assert(dual.checkRecvFrame(binary) == 5 && !memcmp(binary, "query", 5));
}
'''.replace('STREAM', stream).replace('\nBLE\n', '\n' + ble)
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'Arduino.h').write_text(arduino)
    for name, harness in [('parser', parser_test), ('multi', dual_test)]:
        cpp, binary = tmp / 'test.cpp', tmp / name
        cpp.write_text(harness)
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                        '-DPRIVATE_STATUS_REPORTER=1', '-I', str(tmp), '-I', str(root / 'src/helpers'),
                        str(cpp), str(root / 'src/helpers/ArduinoSerialInterface.cpp'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('USB CLI framing, fragmented/mixed input, CR/LF, overflow, and BLE-active USB routing checks passed')
