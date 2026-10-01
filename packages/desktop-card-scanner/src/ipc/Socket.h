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

#include <cerrno>
#include <cstddef>
#include <span>

namespace cardscanner {
namespace ipc {
namespace net {

#ifdef _WIN32
using Handle = SOCKET;
constexpr Handle kInvalidHandle = INVALID_SOCKET;
#else
using Handle = int;
constexpr Handle kInvalidHandle = -1;
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

namespace detail {

/// One SIGPIPE-free send(); may send fewer bytes than asked for, hence
/// detail: whole-record writes go through sendFully().
inline long sendSome(Handle handle, std::span<const std::byte> data) {
#if defined(_WIN32)
  return ::send(handle, reinterpret_cast<const char *>(data.data()),
                int(data.size()), 0);
#elif defined(MSG_NOSIGNAL)
  return ::send(handle, data.data(), data.size(), MSG_NOSIGNAL);
#else
  return ::send(handle, data.data(), data.size(), 0);
#endif
}

/// One recv(); may return fewer bytes than asked for, hence detail:
/// whole-record reads go through receiveExactly().
inline long receiveSome(Handle handle, std::span<std::byte> buffer) {
#ifdef _WIN32
  return ::recv(handle, reinterpret_cast<char *>(buffer.data()),
                int(buffer.size()), 0);
#else
  return ::recv(handle, buffer.data(), buffer.size(), 0);
#endif
}

} // namespace detail

/// send() until the whole buffer is out; false on any error. A short write
/// would desynchronise the peer for good.
inline bool sendFully(Handle handle, std::span<const std::byte> data) {
  size_t sentBytes = 0;
  while (sentBytes < data.size()) {
    const long sentNow = detail::sendSome(handle, data.subspan(sentBytes));
    if (sentNow <= 0) {
#ifndef _WIN32
      if (sentNow < 0 && errno == EINTR) {
        continue; // a stray signal must not cost the connection
      }
#endif
      return false;
    }
    sentBytes += size_t(sentNow);
  }
  return true;
}

/// recv() until the buffer is full; false on EOF or error. A single recv()
/// on a stream can return a partial record.
inline bool receiveExactly(Handle handle, std::span<std::byte> buffer) {
  size_t receivedBytes = 0;
  while (receivedBytes < buffer.size()) {
    const long receivedNow =
        detail::receiveSome(handle, buffer.subspan(receivedBytes));
    if (receivedNow <= 0) {
#ifndef _WIN32
      if (receivedNow < 0 && errno == EINTR) {
        continue; // a stray signal must not cost the connection
      }
#endif
      return false;
    }
    receivedBytes += size_t(receivedNow);
  }
  return true;
}

/// Bounds send() so a peer that stops reading fails the send instead of
/// parking the sender forever. Per call, not per record.
inline void setSendTimeout(Handle handle, long milliseconds) {
#ifdef _WIN32
  const DWORD timeout = DWORD(milliseconds);
  ::setsockopt(handle, SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char *>(&timeout), sizeof(timeout));
#else
  timeval timeout{};
  timeout.tv_sec = milliseconds / 1000;
  timeout.tv_usec = suseconds_t((milliseconds % 1000) * 1000);
  ::setsockopt(handle, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
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
