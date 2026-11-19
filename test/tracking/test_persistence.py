"""Exercise the port's actual independent tracker record and atomic commit path."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'examples/companion_radio/MyMesh.cpp').read_text()
methods = source[source.index('void MyMesh::loadTrackerPrefs()'):source.index('// Never print the PSK.')]
header = (root / 'examples/companion_radio/MyMesh.h').read_text()
record = header[header.index('  struct TrackerPrefs {'):header.index('  uint32_t next_status = 0;')]
harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define MAX_GROUP_CHANNELS 40
struct File {
  std::shared_ptr<std::vector<uint8_t>> bytes;
  bool fail = false;
  explicit operator bool() const { return bool(bytes); }
  size_t read(uint8_t* out, size_t n) {
    n = n < bytes->size() ? n : bytes->size();
    memcpy(out, bytes->data(), n); return n;
  }
  size_t write(const uint8_t* in, size_t n) {
    if (fail) --n;
    bytes->assign(in, in + n); return n;
  }
  void close() {}
};
struct Store {
  std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;
  bool fail = false;
  unsigned commits = 0;
  File openRead(const char* name) { return {files[name], false}; }
  File openWrite(const char* name) {
    auto& bytes = files[name]; bytes = std::make_shared<std::vector<uint8_t>>();
    return {bytes, fail};
  }
  bool commitFile(const char* tmp, const char* name) {
    ++commits; files[name] = files[tmp]; return true;
  }
} store;
struct { bool position_valid = true; } sensors;
struct MyMesh {
  Store* _store = &store;
RECORD
};
METHODS
int main() {
  MyMesh m;
  m.loadTrackerPrefs();
  assert(m.tracker_prefs.status_channel == 255 && m.tracker_prefs.status_interval == 0);
  m.tracker_prefs.status_channel = 3;
  m.tracker_prefs.status_interval = 86400;
  // Channel names can have nonzero unused bytes after the first NUL.
  memset(m.tracker_prefs.status_name, 0xA5, sizeof(m.tracker_prefs.status_name));
  strcpy(m.tracker_prefs.status_name, "private");
  memset(m.tracker_prefs.status_key, 42, 32);
  m.saveTrackerPrefs();
  assert(store.commits == 1 && store.files["/zygis1"]->size() == 74);
  MyMesh reloaded; reloaded.loadTrackerPrefs();
  assert(reloaded.tracker_prefs.status_channel == 3);
  assert(reloaded.tracker_prefs.status_interval == 86400);
  assert(reloaded.tracker_prefs.status_position_known == 1);
  assert(!strcmp(reloaded.tracker_prefs.status_name, "private"));
  assert(!memcmp(reloaded.tracker_prefs.status_key, m.tracker_prefs.status_key, 32));
  auto good = *store.files["/zygis1"];
  assert(good[41] == 0);
  // Read existing valid names even if unused trailing bytes were not padded.
  (*store.files["/zygis1"])[41] = 0xA5;
  MyMesh legacy; legacy.loadTrackerPrefs();
  assert(legacy.tracker_prefs.status_channel == 3);
  assert(!strcmp(legacy.tracker_prefs.status_name, "private"));
  assert(legacy.tracker_prefs.status_name[31] == 0);
  *store.files["/zygis1"] = good;
  // Short writes never replace the previous complete record.
  store.fail = true; m.tracker_prefs.status_interval = 17; m.saveTrackerPrefs();
  assert(store.commits == 1 && *store.files["/zygis1"] == good);
  auto rejected = [&]() {
    MyMesh fresh; fresh.loadTrackerPrefs();
    assert(fresh.tracker_prefs.status_channel == 255);
    assert(fresh.tracker_prefs.status_interval == 0);
  };
  for (size_t n = 0; n < good.size(); ++n) {
    *store.files["/zygis1"] = std::vector<uint8_t>(good.begin(), good.begin() + n);
    rejected();
  }
  for (size_t offset : {size_t(0), size_t(4), size_t(8), size_t(9)}) {
    *store.files["/zygis1"] = good;
    (*store.files["/zygis1"])[offset] = offset == 8 ? MAX_GROUP_CHANNELS : 255;
    rejected();
  }
  *store.files["/zygis1"] = good;
  memset(store.files["/zygis1"]->data() + 10, 0xA5, 32);
  rejected(); // Unterminated names are still invalid.
}
'''.replace('RECORD', record).replace('METHODS', methods)
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / 'persistence.cpp'
    binary = Path(tmp) / 'persistence'
    cpp.write_text(harness)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Tracker persistence round-trip, truncation, validation and atomic short-write checks passed')
