#pragma once

// The only file that knows Winsock exists, so the platform split is one header
// rather than #ifdefs threaded through the socket code.

#ifdef _WIN32
#include <mutex>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstdint>

namespace cardscanner {
namespace ipc {
namespace net {

#ifdef _WIN32
using Handle = SOCKET;
inline constexpr Handle kInvalid = INVALID_SOCKET;
using SizeType = int;
#else
using Handle = int;
inline constexpr Handle kInvalid = -1;
using SizeType = size_t;
#endif

/// Winsock needs process-wide startup; POSIX does not. Idempotent.
///
/// Inline so the OBS module gets a definition too: it links libobs alone, not
/// the desktop library, and an out-of-line one resolves at load time or not at
/// all.
inline void startup() {
#ifdef _WIN32
  // Once only: the process needs sockets for its whole life, so there is
  // nothing to unwind before exit.
  static std::once_flag once;
  std::call_once(once, [] {
    WSADATA data;
    ::WSAStartup(MAKEWORD(2, 2), &data);
  });
#endif
}

inline bool valid(Handle handle) { return handle != kInvalid; }

inline void closeHandle(Handle handle) {
#ifdef _WIN32
  ::closesocket(handle);
#else
  ::close(handle);
#endif
}

/// Unblocks a peer parked in recv() so a join can complete.
inline void shutdownBoth(Handle handle) {
#ifdef _WIN32
  ::shutdown(handle, SD_BOTH);
#else
  ::shutdown(handle, SHUT_RDWR);
#endif
}

/// send() that never raises SIGPIPE: Linux uses the flag, macOS the socket
/// option set at accept/connect, and Windows has no such signal.
inline long sendAll(Handle handle, const void *data, size_t bytes) {
#if defined(_WIN32)
  return ::send(handle, static_cast<const char *>(data), int(bytes), 0);
#elif defined(MSG_NOSIGNAL)
  return ::send(handle, data, bytes, MSG_NOSIGNAL);
#else
  return ::send(handle, data, bytes, 0);
#endif
}

inline long receive(Handle handle, void *data, size_t bytes) {
#ifdef _WIN32
  return ::recv(handle, static_cast<char *>(data), int(bytes), 0);
#else
  return ::recv(handle, data, bytes, 0);
#endif
}

/// No Nagle delay, and no SIGPIPE where that is a socket option.
inline void configureConnected(Handle handle) {
  int yes = 1;
#ifdef _WIN32
  ::setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&yes),
               sizeof(yes));
#else
  ::setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
#ifdef SO_NOSIGPIPE
  ::setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
#endif
}

} // namespace net
} // namespace ipc
} // namespace cardscanner
