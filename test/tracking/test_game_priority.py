"""Exercise real game transport/audio entry points under tracking queue pressure."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]


def method(path, signature):
    source = (root / path).read_text()
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


ui = 'examples/companion_radio/ui-new/UITask.cpp'
arena = 'bool UITask::playArenaSound(' in (root / ui).read_text()
methods = [method('examples/companion_radio/MyMesh.cpp', 'bool MyMesh::canSendGameMessage('),
           method(ui, 'bool UITask::sendGameText('),
           method(ui, 'void UITask::playGameSound(')]
if arena:
    methods.append(method(ui, 'bool UITask::playArenaSound('))

harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#define PIN_BUZZER 1
#define MAX_TEXT_LEN 133
struct ChannelDetails { struct {} channel; };
struct Manager { int queued=0; int getOutboundTotal() const { return queued; } } manager;
struct Clock { uint32_t getCurrentTimeUnique() { return 123; } } clockState;
struct MyMesh {
  Manager* _mgr=&manager;
  unsigned tracker_reply_count=0, sends=0;
  bool acquiring_position=true;
  bool canSendGameMessage() const;
  bool getGameChannel(uint8_t slot, ChannelDetails&, uint8_t digest[16]) {
    memset(digest, 7, 16); return slot==1;
  }
  const char* getNodeName() { return "Tracker01"; }
  Clock* getRTCClock() { return &clockState; }
  template<class... Args> bool sendGroupMessage(Args...) { ++sends; return true; }
} the_mesh;
struct Buzzer {
  unsigned plays=0; bool playing=false;
  bool isPlaying() { return playing; }
  bool isQuiet() { return false; }
  void play(const char*) { ++plays; }
} buzzer;
namespace veylith { const char* melody(uint8_t) { return "arena"; } }
struct UITask {
  bool tracker_alert_active=false;
  bool sendGameText(uint8_t, const char*, const uint8_t[16]);
  void playGameSound(bool);
  bool playArenaSound(uint8_t);
};
METHODS
int main() {
  UITask ui; uint8_t digest[16]; memset(digest, 7, sizeof(digest));
  char frame[53]; memset(frame, 'A', 52); memcpy(frame, "~G1:", 4); frame[52]=0;
  the_mesh.tracker_reply_count=24;
#ifdef PRIVATE_STATUS_REPORTER
  assert(!ui.sendGameText(1,frame,digest) && the_mesh.sends==0);
  the_mesh.tracker_reply_count=1;
  assert(!ui.sendGameText(1,frame,digest) && the_mesh.sends==0);
#endif
  the_mesh.tracker_reply_count=0; manager.queued=32;
  assert(!ui.sendGameText(1,frame,digest) && the_mesh.sends==0);
  manager.queued=1;
  assert(!ui.sendGameText(1,frame,digest) && the_mesh.sends==0);
  manager.queued=0;
  // Indefinite GPS acquisition leaves games usable once replies have drained.
  assert(the_mesh.acquiring_position && ui.sendGameText(1,frame,digest));
  assert(the_mesh.sends==1);
  ui.tracker_alert_active=true;
#ifdef PRIVATE_STATUS_REPORTER
  assert(!ui.sendGameText(1,frame,digest) && the_mesh.sends==1);
  ui.playGameSound(true); assert(buzzer.plays==0);
  ARENA_ALERT
#endif
  ui.tracker_alert_active=false;
  assert(ui.sendGameText(1,frame,digest) && the_mesh.sends==2);
  ui.playGameSound(true); assert(buzzer.plays==1);
  ARENA_NORMAL
  digest[0]^=1;
  assert(!ui.sendGameText(1,frame,digest) && the_mesh.sends==2);
  digest[0]^=1;
  assert(!ui.sendGameText(0,frame,digest));
  assert(!ui.sendGameText(1,"invalid",digest));
  puts("Game traffic yields to queued radio/tracking work; tracking alerts own audio; GPS acquisition stays nonblocking");
}
'''.replace('METHODS', '\n'.join(methods))
harness = harness.replace('ARENA_ALERT', 'assert(ui.playArenaSound(1) && buzzer.plays==0);' if arena else '')
harness = harness.replace('ARENA_NORMAL', 'assert(ui.playArenaSound(1) && buzzer.plays==2);' if arena else '')
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / 'priority.cpp'
    cpp.write_text(harness)
    for tracking in (False, True):
        binary = Path(tmp) / ('tracker' if tracking else 'solo')
        flags = ['-DPRIVATE_STATUS_REPORTER=1'] if tracking else []
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                        str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
