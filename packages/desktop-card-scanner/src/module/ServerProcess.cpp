#include "ServerProcess.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

#include <chrono>
#include <string>
#include <vector>

namespace cardscanner {
namespace obsbridge {

ServerProcess::ServerProcess(std::string executable, std::string configPath,
                             std::string logPath, uint64_t token, uint16_t framePort,
                             uint16_t controlPort)
    : executable_(std::move(executable)), configPath_(std::move(configPath)),
      logPath_(std::move(logPath)), token_(token), framePort_(framePort),
      controlPort_(controlPort) {}

ServerProcess::~ServerProcess() { stop(); }

#ifdef _WIN32

namespace {

/// OBS hands paths over as UTF-8; the narrow Win32 entry points would read them
/// in the ANSI code page and break on any non-ASCII user name.
std::wstring toWide(const std::string &utf8) {
  const int size =
      ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
  std::wstring wide(size_t(size), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), wide.data(), size);
  return wide;
}

/// KILL_ON_JOB_CLOSE is how Windows expresses "this child dies with me". It
/// replaces the POSIX build's watchdog and also covers the parent being killed.
HANDLE createKillOnCloseJob() {
  HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
  if (job == nullptr) {
    return nullptr;
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  ::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
                            sizeof(limits));
  return job;
}

} // namespace

bool ServerProcess::spawnOnce() {
  std::wstring command =
      toWide("\"" + executable_ + "\" \"" + configPath_ + "\" --token " +
             std::to_string(token_) + " --frame-port " + std::to_string(framePort_) +
             " --control-port " + std::to_string(controlPort_));

  SECURITY_ATTRIBUTES inherit = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  // Wide: a log path through a profile with non-ASCII characters is common.
  // Truncated on the first spawn only, so a respawn's output appends.
  HANDLE log = ::CreateFileW(toWide(logPath_).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                             &inherit, firstSpawn_ ? CREATE_ALWAYS : OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);

  SIZE_T attrSize = 0;
  ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
  std::vector<char> attrBuffer(attrSize);
  auto *attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuffer.data());
  const bool listInit = log != INVALID_HANDLE_VALUE &&
                        ::InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize);
  const bool haveAttrs =
      listInit && ::UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                              &log, sizeof(log), nullptr, nullptr);

  STARTUPINFOEXW startup = {};
  startup.StartupInfo.cb =
      DWORD(haveAttrs ? sizeof(STARTUPINFOEXW) : sizeof(STARTUPINFOW));
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdOutput = log;
  startup.StartupInfo.hStdError = log;
  startup.lpAttributeList = haveAttrs ? attrs : nullptr;

  // Suspended until it is in the job, so there is no moment at which OBS dying
  // would leave it behind: the watchdog as no-op here.
  DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
  if (haveAttrs) {
    flags |= EXTENDED_STARTUPINFO_PRESENT;
  }
  PROCESS_INFORMATION info = {};
  const BOOL ok = ::CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, flags,
                                   nullptr, nullptr, &startup.StartupInfo, &info);
  if (listInit) {
    ::DeleteProcThreadAttributeList(attrs);
  }
  if (log != INVALID_HANDLE_VALUE) {
    ::CloseHandle(log);
  }
  if (!ok) {
    return false;
  }
  firstSpawn_ = false;

  if (job_ == nullptr) {
    job_ = createKillOnCloseJob();
  }
  if (job_ != nullptr) {
    ::AssignProcessToJobObject(static_cast<HANDLE>(job_), info.hProcess);
  }

  ::ResumeThread(info.hThread);
  ::CloseHandle(info.hThread);
  process_ = info.hProcess;
  pid_ = int(info.dwProcessId);
  return true;
}

#else

bool ServerProcess::spawnOnce() {
  const std::string token = std::to_string(token_);
  const std::string framePort = std::to_string(framePort_);
  const std::string controlPort = std::to_string(controlPort_);

  std::vector<char *> argv = {
      executable_.data(),          configPath_.data(),
      const_cast<char *>("--token"),         const_cast<char *>(token.c_str()),
      const_cast<char *>("--frame-port"),    const_cast<char *>(framePort.c_str()),
      const_cast<char *>("--control-port"),  const_cast<char *>(controlPort.c_str()),
      nullptr,
  };

  // Its own log: OBS's stdout is not attached to anything when launched from
  // the Finder. stdin is opened explicitly because CLOEXEC_DEFAULT below would
  // otherwise leave fd 0 closed for the server's first socket to land on.
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(
      &actions, STDOUT_FILENO, logPath_.c_str(),
      O_WRONLY | O_CREAT | O_APPEND | (firstSpawn_ ? O_TRUNC : 0), 0644);
  posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
#ifdef __APPLE__
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif

  pid_t pid = 0;
  const int rc =
      posix_spawn(&pid, executable_.c_str(), &actions, &attr, argv.data(), environ);
  posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&actions);

  if (rc != 0) {
    return false;
  }
  firstSpawn_ = false;
  pid_ = pid;
  return true;
}

#endif

void ServerProcess::start() {
  if (running_.exchange(true)) {
    return;
  }
  backoff_.rearm();
  supervisor_ = std::thread([this] { superviseLoop(); });
}

void ServerProcess::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  backoff_.wake();
  // The supervisor spawns and reaps. Once it is joined the child is ours alone,
  // so nothing can reap it, or close its handle, between the checks below.
  if (supervisor_.joinable()) {
    supervisor_.join();
  }

#ifdef _WIN32
  pid_ = 0;
  if (process_ != nullptr) {
    ::TerminateProcess(static_cast<HANDLE>(process_), 0);
    ::WaitForSingleObject(static_cast<HANDLE>(process_), 2000);
    ::CloseHandle(static_cast<HANDLE>(process_));
    process_ = nullptr;
  }
  if (job_ != nullptr) {
    // Closing the job kills anything still in it.
    ::CloseHandle(static_cast<HANDLE>(job_));
    job_ = nullptr;
  }
#else
  const int pid = pid_.exchange(0);
  if (pid > 0) {
    ::kill(pid, SIGTERM);
    // The server handles SIGTERM and unwinds; allow that before insisting, so
    // the database closes cleanly.
    bool reaped = false;
    for (int waited = 0; waited < 2000; waited += 50) {
      // -1: no longer our child, and the pid may be reused.
      if (::waitpid(pid, nullptr, WNOHANG) != 0) {
        reaped = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    // Only if still ours: once reaped the pid is free for reuse.
    if (!reaped) {
      ::kill(pid, SIGKILL);
      ::waitpid(pid, nullptr, 0);
    }
  }
#endif
}

void ServerProcess::superviseLoop() {
  while (running_) {
    if (!spawnOnce()) {
      // Almost always a packaging problem, which retrying fast will not fix.
      backoff_.sleepFor(std::chrono::seconds(5));
      continue;
    }

    const auto spawnedAt = std::chrono::steady_clock::now();
    // Polled so shutdown need not interrupt a blocking wait; stop() cuts the
    // poll interval short.
    while (running_) {
      const int pid = pid_.load();
      if (pid <= 0) {
        break;
      }
#ifdef _WIN32
      if (process_ != nullptr &&
          ::WaitForSingleObject(static_cast<HANDLE>(process_), 0) == WAIT_OBJECT_0) {
        ::CloseHandle(static_cast<HANDLE>(process_));
        process_ = nullptr;
        pid_ = 0;
        break;
      }
#else
      int status = 0;
      if (::waitpid(pid, &status, WNOHANG) == pid) {
        pid_ = 0;
        break;
      }
#endif
      backoff_.sleepFor(std::chrono::milliseconds(200));
    }

    if (!running_) {
      return;
    }

    // A child that ran for a while was healthy, so its crash is a fresh
    // incident rather than the next round of a startup loop.
    if (std::chrono::steady_clock::now() - spawnedAt > std::chrono::seconds(30)) {
      backoff_.reset();
    }

    // Back off, so a server failing instantly at startup does not spin.
    backoff_.sleep();
  }
}

} // namespace obsbridge
} // namespace cardscanner
