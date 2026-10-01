#pragma once

#include <util/Backoff.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace cardscanner {
namespace obsbridge {

/**
 * @brief Runs card-scanner-server as a child of OBS and keeps it running.
 *
 * Makes the two-process design a one-artifact install: no terminal, no token to
 * copy, no start ordering. Respawned with backoff if it dies.
 *
 * On Windows the child joins a KILL_ON_JOB_CLOSE job object, so the OS reaps it
 * if this process dies without unwinding.
 */
class ServerProcess {
public:
  ServerProcess(std::string executable, std::string configPath, std::string logPath,
                uint64_t token, uint16_t framePort, uint16_t controlPort);
  ~ServerProcess();

  ServerProcess(const ServerProcess &) = delete;
  ServerProcess &operator=(const ServerProcess &) = delete;

  /// Spawns the child and starts supervising it.
  void start();
  /// Terminates the child and stops supervising. Safe to call twice.
  void stop();

private:
  void superviseLoop();
  bool spawnOnce();

  std::string executable_;
  std::string configPath_;
  std::string logPath_;
  uint64_t token_;
  uint16_t framePort_;
  uint16_t controlPort_;

  std::atomic<int> pid_{0};
  /// The log is truncated for the first spawn and appended to after that, so
  /// a crash's output survives the respawn that follows it.
  bool firstSpawn_ = true;
#ifdef _WIN32
  /// void* rather than HANDLE so this header stays free of windows.h.
  void *process_ = nullptr;
  void *job_ = nullptr;
#endif
  std::atomic<bool> running_{false};
  /// Respawn delay; also the supervisor's sleeps, so stop() wakes them.
  util::Backoff backoff_{std::chrono::milliseconds(500),
                         std::chrono::milliseconds(8000)};
  std::thread supervisor_;
};

} // namespace obsbridge
} // namespace cardscanner
