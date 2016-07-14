//
//  cppsocket
//

#include <iostream>
#include <string>
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
        connectCallback(std::move(other.connectCallback))
    {
        other.connectCallback = nullptr;
    }

    Connector& Connector::operator=(Connector&& other)
    {
        Socket::operator=(std::move(other));
        connectCallback = std::move(other.connectCallback);

        other.connectCallback = nullptr;

        return *this;
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
            socketFd = socket(AF_INET, SOCK_STREAM, 0);

            if (socketFd == INVALID_SOCKET)
            {
                int error = Network::getLastError();
                std::cerr << "Failed to create socket, error: " << error << std::endl;
                return false;
            }
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

            if (error == EINPROGRESS)
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
}
