#pragma once

#include "Common.hpp"
#include "Socket.hpp"
#include <thread>
#include <queue>
#include <mutex>

namespace jbus {

/** Server interface for accepting incoming connections from GBA emulator instances. */
class Listener {
  net::Socket m_dataServer{false};
  net::Socket m_clockServer{false};
  std::thread m_listenerThread;
  std::mutex m_queueLock;
  std::queue<std::unique_ptr<Endpoint>> m_endpointQueue;
  bool m_running = false;

  static const uint32_t DataPort = 0xd6ba;
  static const uint32_t ClockPort = 0xc10c;

  void listenerProc();

public:
  /** @brief Start listener thread. */
  void start();

  /** @brief Request stop of listener thread and block until joined. */
  void stop();

  /** @brief Pop jbus::Endpoint off Listener's queue.
   *  @return Endpoint instance, ready to issue commands. */
  std::unique_ptr<Endpoint> accept();

  ~Listener();
};

} // namespace jbus
