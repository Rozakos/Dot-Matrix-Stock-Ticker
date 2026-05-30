#pragma once

namespace fs_lock {

// Serialize all LittleFS access across tasks. LittleFS is not safe under
// concurrent access from multiple FreeRTOS tasks — SettingsStore::save/load
// (boot-time NET task) and the AsyncTCP worker handling a web-admin POST must
// take this lock around every LittleFS call. Failing to do so manifests as an
// lfs assertion crash inside lfs_file_read / lfs_mlist_isopen.
void lock();
void unlock();

class Guard {
 public:
  Guard()  { lock(); }
  ~Guard() { unlock(); }
  Guard(const Guard&)            = delete;
  Guard& operator=(const Guard&) = delete;
};

}  // namespace fs_lock
