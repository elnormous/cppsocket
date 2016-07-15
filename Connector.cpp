//
//  cppsocket
//

#include <iostream>
#include <string>
#include <cstring>
#ifdef _MSC_VER
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif
#include "Connector.h"
#include "Network.h"

namespace cppsocket
{
    Connector::Connector(Network& network):
        Socket(network, INVALID_SOCKET)
    {
    }

    Connector::~Connector()
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

    void Connector::update(float delta)
    {
        Socket::update(delta);
        
        if (connecting)
        {
            timeSinceConnect += delta;

            if (timeSinceConnect > connectTimeout)
            {
                connecting = false;

                disconnect();

                std::cerr << "Failed to connect to " << Network::ipToString(ipAddress) << ":" << port << ", connection timed out" << std::endl;

                if (connectErrorCallback)
                {
                    connectErrorCallback();
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
            int error = Network::getLastError();
            std::cerr << "Failed to get address info, error: " << error << std::endl;
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

        if (socketFd == INVALID_SOCKET)
        {
            createSocketFd();
        }

        ipAddress = address;
        port = newPort;

        std::cout << "Connecting to " << Network::ipToString(ipAddress) << ":" << static_cast<int>(port) << std::endl;

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = ipAddress;

        if (::connect(socketFd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0)
        {
            int error = Network::getLastError();

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
                std::cerr << "Failed to connect to " << Network::ipToString(ipAddress) << ":" << port << ", error: " << error << std::endl;
                if (connectErrorCallback)
                {
                    connectErrorCallback();
                }
                return false;
            }
        }
        else
        {
            // connected
            ready = true;
            std::cout << "Socket connected to " << Network::ipToString(ipAddress) << ":" << port << std::endl;
            if (connectCallback)
            {
                connectCallback();
            }
        }

        return true;
    }

    bool Connector::disconnect()
    {
        ready = false;

        if (socketFd != INVALID_SOCKET)
        {
            if (shutdown(socketFd, 0) < 0)
            {
                int error = Network::getLastError();
                std::cerr << "Failed to shut down socket, error: " << error << std::endl;
                return false;
            }
            else
            {
                std::cout << "Socket shut down" << std::endl;
            }
        }

        return true;
    }

    void Connector::setConnectTimeout(float timeout)
    {
        connectTimeout = timeout;
    }

    void Connector::setConnectCallback(const std::function<void()>& newConnectCallback)
    {
        connectCallback = newConnectCallback;
    }

    void Connector::setConnectErrorCallback(const std::function<void()>& newConnectErrorCallback)
    {
        connectErrorCallback = newConnectErrorCallback;
    }

    bool Connector::write()
    {
        if (!Socket::write())
        {
            return false;
        }

        if (connecting)
        {
            connecting = false;
            ready = true;
            std::cout << "Socket connected to " << Network::ipToString(ipAddress) << ":" << port << std::endl;
            if (connectCallback)
            {
                connectCallback();
            }
        }

        return true;
    }

    bool Connector::disconnected()
    {
        if (connecting)
        {
            connecting = false;
            ready = false;
            
            std::cerr << "Failed to connect to " << Network::ipToString(ipAddress) << ":" << port << std::endl;

            if (connectErrorCallback)
            {
                connectErrorCallback();
            }
        }
        else
        {
            Socket::disconnected();
        }

        return true;
    }
}
