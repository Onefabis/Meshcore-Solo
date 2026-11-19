"""Test actual private-channel command dispatch, including targeting and side effects."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'examples/companion_radio/MyMesh.cpp').read_text()
dispatch = source[source.index('void MyMesh::handleTrackerMessage('):source.index('void MyMesh::startNearbyDiscovery()')]
harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include "TrackerProtocol.h"
uint32_t millis() { return 12345; }
namespace mesh { struct GroupChannel { uint8_t secret[32] = {}; }; struct Packet {}; }
struct ChannelDetails { mesh::GroupChannel channel; };
struct Location { bool hasFreshFix(int) { return false; } };
struct Sensors { Location location; bool gps = false; void setGPSFromCLI(bool on) { gps = on; } Location* getLocationProvider() { return &location; } } sensors;
struct Listener {
 int chirps = 0, alarms = 0;
 std::string message;
 void onTrackerAlert(const char* text, bool alarm) { message=text; if (alarm) ++alarms; else ++chirps; }
};
struct MyMesh {
 struct { char node_name[32] = "Tracker01"; bool gps_enabled = true; uint32_t status_interval = 300; } _prefs;
 decltype(_prefs)& tracker_prefs = _prefs;
 Listener listener; Listener* _listener = &listener;
 bool allowed = true, acquiring_position = false;
 uint32_t next_status = 0;
 int requests=0, status=0, saved=0, scans=0;
 std::vector<std::string> replies, acks;
 const char* verifyTrackerChannel(ChannelDetails&) { return allowed ? nullptr : "blocked"; }
 bool trackerReply(const char* text) { replies.emplace_back(text); return true; }
 void replyRoute(mesh::Packet*, const char* cmd) { acks.emplace_back(cmd); }
 void startPositionRequest() { ++requests; acquiring_position=true; }
 void sendStatus() { ++status; }
 void savePrefs() { ++saved; }
 void startNearbyDiscovery() { ++scans; }
 void handleTrackerMessage(const mesh::GroupChannel&, mesh::Packet*, const char*);
};
DISPATCH
int main() {
 MyMesh m; mesh::GroupChannel channel; mesh::Packet packet;
 auto recv = [&](const char* text) { m.handleTrackerMessage(channel, &packet, text); };
 recv("Parent: @[Other] !kur");
 assert(m.requests==0 && m.replies.empty() && m.acks.empty());
 recv("Parent: @[Other] hello"); assert(m.listener.chirps==0);
 recv("Parent: hello"); assert(m.listener.chirps==0);
 recv("Parent: @[Tracker01] hello");
 assert(m.listener.chirps==1 && m.listener.message=="hello");
 recv("Parent: @[Tracker01] !kur");
 assert(m.requests==1 && m.status==1 && m.acks.back()=="!kur" && m.listener.chirps==1);
 recv("Parent: @[Tracker01] !tikra Come home");
 assert(m.listener.alarms==1 && m.listener.message=="Come home" && m.acks.back()=="!tikra" && m.listener.chirps==1);
 recv("Parent: @[Tracker01] !unknown");
 assert(m.acks.back()=="!unknown" && m.listener.chirps==1);
 recv("Parent: @[Tracker01] !power down");
 assert(!m._prefs.gps_enabled && !m.acquiring_position && !sensors.gps);
 recv("Parent: @[Other] !power up"); assert(!m._prefs.gps_enabled);
 recv("Parent: @[Tracker01] !power up"); assert(m._prefs.gps_enabled && m.acquiring_position);
 recv("Parent: !interval 120"); assert(m.tracker_prefs.status_interval==120);
 recv("Parent: !interval -1"); assert(m.tracker_prefs.status_interval==120);
 recv("Parent: !interval 4294967296"); assert(m.tracker_prefs.status_interval==120);
 recv("Parent: !interval"); assert(m.replies.back().find("120 seconds")!=std::string::npos);
 size_t count=m.replies.size();
 recv("Parent: @[Tracker01] !help");
 assert(m.replies.size()==count+1 && m.replies.back()==tracker::HELP && m.replies.back().size()+33<=133);
 recv("Parent: !triangulate"); assert(m.scans==1 && m.acks.back()=="!triangulate");
 m.allowed=false;
 recv("Parent: !tikra"); assert(m.listener.alarms==1);
 m.allowed=true; channel.secret[0]=1;
 recv("Parent: !kur"); assert(m.requests==2);
}
'''.replace('DISPATCH', dispatch)
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'dispatch.cpp'; binary=Path(tmp)/'dispatch'
    cpp.write_text(harness)
    subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-I',str(root/'examples/companion_radio'),str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Actual command dispatch, private-channel gate, ACKs, power controls, intervals and alert routing passed')
