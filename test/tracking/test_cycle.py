"""Exercise the actual firmware GPS state-machine functions with deterministic hardware stubs."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'examples/companion_radio/MyMesh.cpp').read_text()
functions = source[source.index('void MyMesh::startPositionRequest()'):source.index('void MyMesh::replyRoute(')]
harness = r'''
#include <cassert>
#include <cstdint>
uint32_t now_ms = 0;
uint32_t millis() { return now_ms; }
struct Location {
 bool fresh = false;
 bool hasFreshFix(uint32_t) { return fresh; }
 long getLatitude() { return 54687200; }
 long getLongitude() { return 25279700; }
};
struct Sensors {
 Location location;
 bool enabled = false, position_valid = false;
 double node_lat = 0, node_lon = 0;
 int starts = 0, stops = 0;
 bool setGPSFromCLI(bool on) {
   if (on) { enabled = true; location.fresh = false; ++starts; }
   else { enabled = false; ++stops; }
   return true;
 }
 Location* getLocationProvider() { return &location; }
} sensors;
struct MyMesh {
 struct { bool gps_enabled = true; uint32_t status_interval = 300; } _prefs;
 decltype(_prefs)& tracker_prefs = _prefs;
 uint32_t next_status = 0;
 bool acquiring_position = false, queue_available = true;
 int reports = 0, fresh_reports = 0;
 bool sendStatus() {
   if (!queue_available) return false;
   ++reports;
   if (acquiring_position && sensors.location.fresh) ++fresh_reports;
   return true;
 }
 void nearbyLoop() {}
 void flushTrackerReplies() {}
 void startPositionRequest();
 void trackerLoop();
};
FUNCTIONS
int main() {
 MyMesh mesh;
 mesh.trackerLoop();
 assert(sensors.enabled && mesh.acquiring_position && mesh.reports == 1);
 assert(sensors.starts == 1 && mesh.fresh_reports == 0);
 now_ms = 300000;
 mesh.trackerLoop();
 assert(sensors.enabled && mesh.acquiring_position && sensors.starts == 1);
 now_ms = 86400000; // Waiting a full day must not time out GPS.
 mesh.trackerLoop();
 assert(sensors.enabled && mesh.acquiring_position && sensors.stops == 0);
 sensors.location.fresh = true;
 mesh.queue_available = false;
 mesh.trackerLoop();
 assert(sensors.enabled && mesh.acquiring_position); // Don't lose the pending fix under queue pressure.
 mesh.queue_available = true;
 mesh.trackerLoop();
 assert(!sensors.enabled && !mesh.acquiring_position && mesh.fresh_reports == 1);
 assert(sensors.position_valid && sensors.node_lat > 54 && sensors.node_lon > 25);
 int reports = mesh.reports;
 now_ms += 299999;
 mesh.trackerLoop();
 assert(!sensors.enabled && mesh.reports == reports);
 now_ms++;
 mesh.trackerLoop();
 assert(sensors.enabled && mesh.acquiring_position && sensors.starts == 2);
 // Power down cancels acquisition and prevents all future scheduled wakes.
 mesh._prefs.gps_enabled = false;
 mesh.acquiring_position = false;
 sensors.setGPSFromCLI(false);
 now_ms += 1000000;
 mesh.trackerLoop();
 assert(!sensors.enabled && !mesh.acquiring_position);
 // An explicit locate request can run while periodic tracking remains paused.
 mesh.startPositionRequest();
 assert(sensors.enabled && mesh.acquiring_position);
 sensors.location.fresh = true;
 mesh.trackerLoop();
 assert(!sensors.enabled && !mesh.acquiring_position);
 now_ms += 1000000;
 mesh.trackerLoop();
 assert(!sensors.enabled);
}
'''.replace('FUNCTIONS', functions)
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / 'cycle.cpp'
    binary = Path(tmp) / 'cycle'
    cpp.write_text(harness)
    subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('GPS indefinite acquisition, fresh-fix sleep, interval, queue-pressure and paused-listening checks passed')
