#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#ifdef _WIN32
#include <BaseTsd.h>
using SOCKET = UINT_PTR;
#endif

struct sockaddr_in;

namespace jbus::net {

/** IP address class derived from SFML */
class IPAddress {
  uint32_t m_address = 0;
  bool m_valid = false;

  void resolve(const std::string& address) noexcept;

public:
  explicit IPAddress(const std::string& address) noexcept { resolve(address); }

  uint32_t toInteger() const noexcept;
  explicit operator bool() const noexcept { return m_valid; }
};

/** Server-oriented TCP socket class derived from SFML */
class Socket {
#ifndef _WIN32
  using SocketTp = int;
#else
  using SocketTp = SOCKET;
#endif
  SocketTp m_socket = -1;
  bool m_isBlocking;

  bool openSocket() noexcept;
  void setRemoteSocket(int remSocket) noexcept;

public:
  enum class EResult { OK, Error, Busy };

#ifdef _WIN32
  static EResult LastWSAError() noexcept;
#endif

  explicit Socket(bool blocking) noexcept : m_isBlocking(blocking) {}
  ~Socket() noexcept { close(); }

  Socket(const Socket& other) = delete;
  Socket& operator=(const Socket& other) = delete;
  Socket(Socket&& other) noexcept : m_socket(other.m_socket), m_isBlocking(other.m_isBlocking) { other.m_socket = -1; }
  Socket& operator=(Socket&& other) noexcept {
    close();
    m_socket = other.m_socket;
    other.m_socket = -1;
    m_isBlocking = other.m_isBlocking;
    return *this;
  }

  void setBlocking(bool blocking) noexcept;
  bool isOpen() const noexcept { return m_socket != -1; }
  bool openAndListen(const IPAddress& address, uint32_t port) noexcept;
  EResult accept(Socket& remoteSocketOut, sockaddr_in& fromAddress) noexcept;
  EResult accept(Socket& remoteSocketOut) noexcept;
  EResult accept(Socket& remoteSocketOut, std::string& fromHostname);
  void close() noexcept;
  EResult send(const void* buf, size_t len, size_t& transferred) noexcept;
  EResult send(const void* buf, size_t len) noexcept;
  EResult recv(void* buf, size_t len, size_t& transferred) noexcept;
  EResult recv(void* buf, size_t len) noexcept;

  explicit operator bool() const noexcept { return isOpen(); }

  SocketTp GetInternalSocket() const noexcept { return m_socket; }
};

} // namespace jbus::net
