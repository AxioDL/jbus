#ifndef JBUS_SOCKET_HPP
#define JBUS_SOCKET_HPP

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>
#include <string>

#include "Common.hpp"

namespace jbus
{
namespace net
{

/* Define the low-level send/receive flags, which depend on the OS */
#ifdef __linux__
static const int _flags = MSG_NOSIGNAL;
#else
static const int _flags = 0;
#endif

/** IP address class derived from SFML */
class IPAddress
{
    uint32_t m_address = 0;
    bool m_valid = false;

    void resolve(const std::string& address)
    {
        m_address = 0;
        m_valid = false;

        if (address == "255.255.255.255")
        {
            /* The broadcast address needs to be handled explicitly,
             * because it is also the value returned by inet_addr on error */
            m_address = INADDR_BROADCAST;
            m_valid = true;
        }
        else if (address == "0.0.0.0")
        {
            m_address = INADDR_ANY;
            m_valid = true;
        }
        else
        {
            /* Try to convert the address as a byte representation ("xxx.xxx.xxx.xxx") */
            uint32_t ip = inet_addr(address.c_str());
            if (ip != INADDR_NONE)
            {
                m_address = ip;
                m_valid = true;
            }
            else
            {
                /* Not a valid address, try to convert it as a host name */
                addrinfo hints;
                memset(&hints, 0, sizeof(hints));
                hints.ai_family = AF_INET;
                addrinfo* result = NULL;
                if (getaddrinfo(address.c_str(), NULL, &hints, &result) == 0)
                {
                    if (result)
                    {
                        ip = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr.s_addr;
                        freeaddrinfo(result);
                        m_address = ip;
                        m_valid = true;
                    }
                }
            }
        }
    }

public:
    IPAddress(const std::string& address)
    {
        resolve(address);
    }

    uint32_t toInteger() const
    {
        return ntohl(m_address);
    }

    operator bool() const { return m_valid; }
};

/** Server-oriented TCP socket class derived from SFML */
class Socket
{
    int m_socket = -1;
    bool m_isBlocking;

    static sockaddr_in createAddress(uint32_t address, unsigned short port)
    {
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_addr.s_addr = htonl(address);
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(port);

#ifdef __APPLE__
        addr.sin_len = sizeof(addr);
#endif

        return addr;
    }

    bool openSocket()
    {
        if (isOpen())
            return false;

        m_socket = socket(PF_INET, SOCK_STREAM, 0);
        if (m_socket == -1)
        {
            fprintf(stderr, "Can't allocate socket");
            return false;
        }

        int one = 1;
        setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&one), sizeof(one));
#ifdef __APPLE__
        setsockopt(m_socket, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<char*>(&one), sizeof(one));
#endif

        setBlocking(m_isBlocking);

        return true;
    }

    void setRemoteSocket(int remSocket)
    {
        close();
        m_socket = remSocket;
        setBlocking(m_isBlocking);
    }

public:
    enum class EResult
    {
        OK,
        Error,
        Busy
    };

    Socket(bool blocking)
    : m_isBlocking(blocking) {}
    ~Socket() { close(); }

    Socket(const Socket& other) = delete;
    Socket& operator=(const Socket& other) = delete;
    Socket(Socket&& other)
    : m_socket(other.m_socket), m_isBlocking(other.m_isBlocking)
    {
        other.m_socket = -1;
    }
    Socket& operator=(Socket&& other)
    {
        close();
        m_socket = other.m_socket;
        other.m_socket = -1;
        m_isBlocking = other.m_isBlocking;
        return *this;
    }

    void setBlocking(bool blocking)
    {
        m_isBlocking = blocking;
        int status = fcntl(m_socket, F_GETFL);
        if (m_isBlocking)
            fcntl(m_socket, F_SETFL, status & ~O_NONBLOCK);
        else
            fcntl(m_socket, F_SETFL, status | O_NONBLOCK);
    }

    bool isOpen() const { return m_socket != -1; }
    bool openAndListen(const IPAddress& address, uint32_t port)
    {
        if (!openSocket())
            return false;

        sockaddr_in addr = createAddress(address.toInteger(), port);
        if (bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1)
        {
            /* Not likely to happen, but... */
            fprintf(stderr, "Failed to bind listener socket to port %d", port);
            return false;
        }

        if (::listen(m_socket, 0) == -1)
        {
            /* Oops, socket is deaf */
            fprintf(stderr, "Failed to listen to port %d", port);
            return false;
        }

        return true;
    }

    EResult accept(Socket& remoteSocketOut, sockaddr_in& fromAddress)
    {
        if (!isOpen())
            return EResult::Error;

        /* Accept a new connection */
        socklen_t length = sizeof(sockaddr_in);
        int remoteSocket = ::accept(m_socket, reinterpret_cast<sockaddr*>(&fromAddress), &length);

        /* Check for errors */
        if (remoteSocket == -1)
        {
            EResult res = (errno == EAGAIN) ? EResult::Busy : EResult::Error;
            if (res == EResult::Error)
                fprintf(stderr, "Failed to accept incoming connection: %s", strerror(errno));
            return res;
        }

        /* Initialize the new connected socket */
        remoteSocketOut.setRemoteSocket(remoteSocket);

        return EResult::OK;
    }

    EResult accept(Socket& remoteSocketOut)
    {
        sockaddr_in fromAddress;
        return accept(remoteSocketOut, fromAddress);
    }

    EResult accept(Socket& remoteSocketOut, std::string& fromHostname)
    {
        sockaddr_in fromAddress;
        socklen_t len = sizeof(fromAddress);
        char name[NI_MAXHOST];
        EResult res = accept(remoteSocketOut, fromAddress);
        if (res == EResult::OK)
            if (getnameinfo((sockaddr*)&fromAddress, len, name, NI_MAXHOST, nullptr, 0, 0) == 0)
                fromHostname.assign(name);
        return res;
    }

    void close()
    {
        if (!isOpen())
            return;
        ::close(m_socket);
        m_socket = -1;
    }

    EResult send(const void* buf, size_t len, size_t& transferred)
    {
        transferred = 0;
        if (!isOpen())
            return EResult::Error;

        if (!buf || !len)
            return EResult::Error;

        /* Loop until every byte has been sent */
        ssize_t result = 0;
        for (size_t sent = 0; sent < len; sent += result)
        {
            /* Send a chunk of data */
            result = ::send(m_socket, static_cast<const char*>(buf) + sent, len - sent, _flags);

            /* Check for errors */
            if (result < 0)
                return (errno == EAGAIN) ? EResult::Busy : EResult::Error;
        }

        transferred = len;
        return EResult::OK;
    }

    EResult send(const void* buf, size_t len)
    {
        size_t transferred;
        return send(buf, len, transferred);
    }

    EResult recv(void* buf, size_t len, size_t& transferred)
    {
        transferred = 0;
        if (!isOpen())
            return EResult::Error;

        if (!buf)
            return EResult::Error;

        if (!len)
            return EResult::OK;

        /* Receive a chunk of bytes */
        int result = ::recv(m_socket, static_cast<char*>(buf), static_cast<int>(len), _flags);

        if (result < 0)
            return (errno == EAGAIN) ? EResult::Busy : EResult::Error;
        else if (result == 0)
            return EResult::Error;

        transferred = result;
        return EResult::OK;
    }

    EResult recv(void* buf, size_t len)
    {
        size_t transferred;
        return recv(buf, len, transferred);
    }

    operator bool() const { return isOpen(); }

    int GetInternalSocket() const { return m_socket; }
};

}
}

#endif // JBUS_SOCKET_HPP