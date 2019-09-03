#include "jbus/Listener.hpp"

#include "jbus/Common.hpp"
#include "jbus/Endpoint.hpp"

#define LOG_LISTENER 0

#if LOG_LISTENER
#include <cstdio>
#endif

namespace jbus {

void Listener::listenerProc() {
#if LOG_LISTENER
  printf("JoyBus listener started\n");
#endif

  net::IPAddress localhost("127.0.0.1");
  bool dataBound = false;
  bool clockBound = false;
  while (m_running && (!dataBound || !clockBound)) {
    if (!dataBound) {
      if (!(dataBound = m_dataServer.openAndListen(localhost, DataPort))) {
        m_dataServer = net::Socket(false);
#if LOG_LISTENER
        printf("data open failed %s; will retry\n", strerror(errno));
#endif
        WaitGCTicks(GetGCTicksPerSec());
      } else {
#if LOG_LISTENER
        printf("data listening on port %d\n", DataPort);
#endif
      }
    }
    if (!clockBound) {
      if (!(clockBound = m_clockServer.openAndListen(localhost, ClockPort))) {
        m_clockServer = net::Socket(false);
#if LOG_LISTENER
        printf("clock open failed %s; will retry\n", strerror(errno));
#endif
        WaitGCTicks(GetGCTicksPerSec());
      } else {
#if LOG_LISTENER
        printf("clock listening on port %d\n", ClockPort);
#endif
      }
    }
  }

  /* We use blocking I/O since we have a dedicated transfer thread */
  net::Socket acceptData{true};
  net::Socket acceptClock{true};
  std::string hostname;
  while (m_running) {
    if (m_dataServer.accept(acceptData, hostname) == net::Socket::EResult::OK) {
#if LOG_LISTENER
      printf("accepted data connection from %s\n", hostname.c_str());
#endif
    }
    if (m_clockServer.accept(acceptClock, hostname) == net::Socket::EResult::OK) {
#if LOG_LISTENER
      printf("accepted clock connection from %s\n", hostname.c_str());
#endif
    }
    if (acceptData && acceptClock) {
      std::unique_lock<std::mutex> lk(m_queueLock);
      m_endpointQueue.push(std::make_unique<Endpoint>(0, std::move(acceptData), std::move(acceptClock)));
    }
    WaitGCTicks(GetGCTicksPerSec());
  }

  m_dataServer.close();
  m_clockServer.close();
#if LOG_LISTENER
  printf("JoyBus listener stopped\n");
#endif
}

void Listener::start() {
  stop();
  m_running = true;
  m_listenerThread = std::thread(std::bind(&Listener::listenerProc, this));
}

void Listener::stop() {
  m_running = false;
  if (m_listenerThread.joinable())
    m_listenerThread.join();
}

std::unique_ptr<Endpoint> Listener::accept() {
  std::unique_lock<std::mutex> lk(m_queueLock);
  if (m_endpointQueue.size()) {
    std::unique_ptr<Endpoint> ret;
    ret = std::move(m_endpointQueue.front());
    m_endpointQueue.pop();
    return ret;
  }
  return {};
}

Listener::Listener() = default;

Listener::~Listener() { stop(); }

} // namespace jbus
