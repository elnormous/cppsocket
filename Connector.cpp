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

                Log(Log::Level::WARN) << "Failed to connect to " << ipToString(remoteIPAddress) << ":" << remotePort << ", connection timed out";

                if (connectErrorCallback)
                {
                    connectErrorCallback(*this);
                }
            }
        }
    }

    bool Connector::connect(const std::string& address)
    {
        ready = false;
        connecting = false;

        std::pair<uint32_t, uint16_t> addr = getAddress(address);

        return connect(addr.first, addr.second);
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

        remoteIPAddress = address;
        remotePort = newPort;

        Log(Log::Level::INFO) << "Connecting to " << ipToString(remoteIPAddress) << ":" << static_cast<int>(remotePort);

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = remoteIPAddress;
        addr.sin_port = htons(remotePort);

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
                Log(Log::Level::WARN) << "Failed to connect to " << ipToString(remoteIPAddress) << ":" << remotePort << ", error: " << error;
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
            Log(Log::Level::INFO) << "Socket connected to " << ipToString(remoteIPAddress) << ":" << remotePort;
            if (connectCallback)
            {
                connectCallback(*this);
            }
        }

        struct sockaddr_in localAddr;
        socklen_t localAddrSize = sizeof(localAddr);

        if (getsockname(socketFd, reinterpret_cast<sockaddr*>(&localAddr), &localAddrSize) != 0)
        {
            int error = getLastError();
            Log(Log::Level::WARN) << "Failed to get address of the socket connecting to " << ipToString(remoteIPAddress) << ":" << remotePort << ", error: " << error;
            closeSocketFd();
            connecting = false;
            if (connectErrorCallback)
            {
                connectErrorCallback(*this);
            }
            return false;
        }

        localIPAddress = localAddr.sin_addr.s_addr;
        localPort = ntohs(localAddr.sin_port);

        return true;
    }

    void Connector::setConnectTimeout(float timeout)
    {
        connectTimeout = timeout;
    }

    void Connector::setConnectCallback(const std::function<void(Connector&)>& newConnectCallback)
    {
        connectCallback = newConnectCallback;
    }

    void Connector::setConnectErrorCallback(const std::function<void(Connector&)>& newConnectErrorCallback)
    {
        connectErrorCallback = newConnectErrorCallback;
    }

    bool Connector::write()
    {
        if (connecting)
        {
            connecting = false;
            ready = true;
            Log(Log::Level::INFO) << "Socket connected to " << ipToString(remoteIPAddress) << ":" << remotePort;
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

            Log(Log::Level::WARN) << "Failed to connect to " << ipToString(remoteIPAddress) << ":" << remotePort;

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
