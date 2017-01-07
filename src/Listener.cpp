#include "jbus/Listener.hpp"
#include "jbus/Endpoint.hpp"

namespace jbus
{

void Listener::listenerProc()
{
    printf("JoyBus listener started\n");

    net::IPAddress localhost("0.0.0.0");
    bool dataBound = false;
    bool clockBound = false;
    while (m_running && (!dataBound || !clockBound))
    {
        if (!dataBound)
        {
            if (!(dataBound = m_dataServer.openAndListen(localhost, DataPort)))
            {
                m_dataServer = net::Socket(false);
                printf("data open failed %s; will retry\n", strerror(errno));
                sleep(1);
            }
            else
                printf("data listening on port %d\n", DataPort);
        }
        if (!clockBound)
        {
            if (!(clockBound = m_clockServer.openAndListen(localhost, ClockPort)))
            {
                m_clockServer = net::Socket(false);
                printf("clock open failed %s; will retry\n", strerror(errno));
                sleep(1);
            }
            else
                printf("clock listening on port %d\n", ClockPort);
        }
    }

    net::Socket acceptData = {false};
    net::Socket acceptClock = {false};
    std::string hostname;
    u8 chan = 1;
    while (m_running && chan < 4)
    {
        if (m_dataServer.accept(acceptData, hostname) == net::Socket::EResult::OK)
            printf("accepted data connection from %s\n", hostname.c_str());
        if (m_clockServer.accept(acceptClock, hostname) == net::Socket::EResult::OK)
            printf("accepted clock connection from %s\n", hostname.c_str());
        if (acceptData && acceptClock)
        {
            std::unique_lock<std::mutex> lk(m_queueLock);
            m_endpointQueue.push(std::make_unique<Endpoint>(
                chan++, std::move(acceptData), std::move(acceptClock)));
        }
        sleep(1);
    }

    m_dataServer.close();
    m_clockServer.close();
    printf("JoyBus listener stopped\n");
}

void Listener::start()
{
    stop();
    m_running = true;
    m_listenerThread = std::thread(std::bind(&Listener::listenerProc, this));
}

void Listener::stop()
{
    m_running = false;
    if (m_listenerThread.joinable())
        m_listenerThread.join();
}

std::unique_ptr<Endpoint> Listener::accept()
{
    std::unique_lock<std::mutex> lk(m_queueLock);
    if (m_endpointQueue.size())
    {
        std::unique_ptr<Endpoint> ret;
        ret = std::move(m_endpointQueue.front());
        m_endpointQueue.pop();
        return ret;
    }
    return {};
}

Listener::~Listener() { stop(); }

}