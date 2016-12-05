//
//  cppsocket
//

#include <string>
#include <cstring>
#ifdef _MSC_VER
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif
#include "Log.h"
#include "Connector.h"
#include "Network.h"

namespace cppsocket
{
    Connector::Connector(Network& aNetwork):
        Socket(aNetwork)
    {
    }

    Connector::Connector(Connector&& other):
        Socket(std::move(other)),
        connectTimeout(other.connectTimeout),
        timeSinceConnect(other.timeSinceConnect),
        connecting(other.connecting),
        connectCallback(std::move(other.connectCallback)),
        connectErrorCallback(std::move(other.connectErrorCallback))
    {
        other.connecting = false;
        other.connectTimeout = 10.0f;
        other.timeSinceConnect = 0.0f;
    }

    Connector& Connector::operator=(Connector&& other)
    {
        Socket::operator=(std::move(other));
        connectTimeout = other.connectTimeout;
        timeSinceConnect = other.timeSinceConnect;
        connecting = other.connecting;
        connectCallback = std::move(other.connectCallback);
        connectErrorCallback = std::move(other.connectErrorCallback);

        other.connecting = false;
        other.connectTimeout = 10.0f;
        other.timeSinceConnect = 0.0f;

        return *this;
    }

    bool Connector::close()
    {
        connecting = false;

        return Socket::close();
    }

    void Connector::update(float delta)
    {
        Socket::update(delta);

        if (connecting)
        {
            timeSinceConnect += delta;

            if (timeSinceConnect > connectTimeout)
            {
                connecting = false;

                close();

                Log(Log::Level::WARN) << "Failed to connect to " << ipToString(ipAddress) << ":" << port << ", connection timed out";

                if (connectErrorCallback)
                {
                    connectErrorCallback(*this);
                }
            }
        }
    }

    bool Connector::connect(const std::string& address, uint16_t newPort)
    {
        ready = false;
        connecting = false;

        size_t i = address.find(':');
        std::string addressStr;
        std::string portStr;

        if (i != std::string::npos)
        {
            addressStr = address.substr(0, i);
            portStr = address.substr(i + 1);
        }
        else
        {
            addressStr = address;
            portStr = std::to_string(newPort);
        }

        addrinfo* result;
        if (getaddrinfo(addressStr.c_str(), portStr.empty() ? nullptr : portStr.c_str(), nullptr, &result) != 0)
        {
            int error = getLastError();
            Log(Log::Level::ERR) << "Failed to get address info of " << address << ", error: " << error;
            return false;
        }

        struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
        uint32_t ip = addr->sin_addr.s_addr;
        newPort = ntohs(addr->sin_port);

        freeaddrinfo(result);

        return connect(ip, newPort);
    }

    bool Connector::connect(uint32_t address, uint16_t newPort)
    {
        ready = false;
        connecting = false;

        if (socketFd != INVALID_SOCKET)
        {
            close();
        }

        if (!createSocketFd())
        {
            return false;
        }

        ipAddress = address;
        port = newPort;

        Log(Log::Level::INFO) << "Connecting to " << ipToString(ipAddress) << ":" << static_cast<int>(port);

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = ipAddress;

        if (::connect(socketFd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0)
        {
            int error = getLastError();

#ifdef _MSC_VER
            if (error == WSAEWOULDBLOCK)
#else
            if (error == EINPROGRESS)
#endif
            {
                connecting = true;
            }
            else
            {
                Log(Log::Level::WARN) << "Failed to connect to " << ipToString(ipAddress) << ":" << port << ", error: " << error;
                if (connectErrorCallback)
                {
                    connectErrorCallback(*this);
                }
                return false;
            }
        }
        else
        {
            // connected
            ready = true;
            Log(Log::Level::INFO) << "Socket connected to " << ipToString(ipAddress) << ":" << port;
            if (connectCallback)
            {
                connectCallback(*this);
            }
        }

        return true;
    }

    void Connector::setConnectTimeout(float timeout)
    {
        connectTimeout = timeout;
    }

    void Connector::setConnectCallback(const std::function<void(Socket&)>& newConnectCallback)
    {
        connectCallback = newConnectCallback;
    }

    void Connector::setConnectErrorCallback(const std::function<void(Socket&)>& newConnectErrorCallback)
    {
        connectErrorCallback = newConnectErrorCallback;
    }

    bool Connector::write()
    {
        if (connecting)
        {
            connecting = false;
            ready = true;
            Log(Log::Level::INFO) << "Socket connected to " << ipToString(ipAddress) << ":" << port;
            if (connectCallback)
            {
                connectCallback(*this);
            }
        }
        
        if (!Socket::write())
        {
            return false;
        }

        return true;
    }

    bool Connector::disconnected()
    {
        if (connecting)
        {
            connecting = false;
            ready = false;

            Log(Log::Level::WARN) << "Failed to connect to " << ipToString(ipAddress) << ":" << port;

            if (connectErrorCallback)
            {
                connectErrorCallback(*this);
            }
        }
        else
        {
            Socket::disconnected();
        }

        return true;
    }
}
