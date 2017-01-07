#ifndef JBUS_LISTENER_HPP
#define JBUS_LISTENER_HPP

#include "Common.hpp"
#include "Socket.hpp"
#include <thread>
#include <queue>
#include <mutex>

namespace jbus
{

class Listener
{
    net::Socket m_dataServer = {false};
    net::Socket m_clockServer = {false};
    std::thread m_listenerThread;
    std::mutex m_queueLock;
    std::queue<std::unique_ptr<Endpoint>> m_endpointQueue;
    bool m_running = false;

    static const uint32_t DataPort = 0xd6ba;
    static const uint32_t ClockPort = 0xc10c;

    void listenerProc();

public:
    void start();
    void stop();
    std::unique_ptr<Endpoint> accept();
    ~Listener();
};

}

#endif // JBUS_LISTENER_HPP
