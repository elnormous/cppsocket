//
//  cppsocket
//

#include <iostream>
#include <cstring>
#ifdef _MSC_VER
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#endif
#include "Acceptor.h"
#include "Network.h"

namespace cppsocket
{
    static const int WAITING_QUEUE_SIZE = 5;

    Acceptor::Acceptor(Network& network):
        Socket(network, INVALID_SOCKET)
    {
    }

    Acceptor::~Acceptor()
    {
    }

    Acceptor::Acceptor(Acceptor&& other):
        Socket(std::move(other)),
        acceptCallback(std::move(other.acceptCallback))
    {
        other.acceptCallback = nullptr;
    }

    Acceptor& Acceptor::operator=(Acceptor&& other)
    {
        Socket::operator=(std::move(other));
        acceptCallback = std::move(other.acceptCallback);

        other.acceptCallback = nullptr;

        return *this;
    }

    bool Acceptor::startAccept(const std::string& address, uint16_t newPort)
    {
        ready = false;

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
            std::cerr << "Failed to get address info, error: " << error << std::endl;
            return false;
        }

        struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
        uint32_t ip = addr->sin_addr.s_addr;
        newPort = ntohs(addr->sin_port);

        freeaddrinfo(result);

        return startAccept(ip, newPort);
    }

    bool Acceptor::startAccept(uint32_t address, uint16_t newPort)
    {
        ready = false;

        if (socketFd == INVALID_SOCKET)
        {
            if (!createSocketFd())
            {
                return false;
            }
        }

        ipAddress = address;
        port = newPort;
        int value = 1;

        if (setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&value), sizeof(value)) < 0)
        {
            int error = getLastError();
            std::cerr << "setsockopt(SO_REUSEADDR) failed, error: " << error << std::endl;
            return false;
        }

        sockaddr_in serverAddress;
        memset(&serverAddress, 0, sizeof(serverAddress));
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(port);
        serverAddress.sin_addr.s_addr = address;

        if (bind(socketFd, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0)
        {
            int error = getLastError();
            std::cerr << "Failed to bind server socket, error: " << error << std::endl;
            return false;
        }

        if (listen(socketFd, WAITING_QUEUE_SIZE) < 0)
        {
            int error = getLastError();
            std::cerr << "Failed to listen on " << ipToString(ipAddress) << ":" << port << ", error: " << error << std::endl;
            return false;
        }

        std::cout << "Server listening on " << ipToString(ipAddress) << ":" << port << std::endl;
        ready = true;

        return true;
    }

    void Acceptor::setAcceptCallback(const std::function<void(Socket&)>& newAcceptCallback)
    {
        acceptCallback = newAcceptCallback;
    }

    bool Acceptor::read()
    {
        if (!ready)
        {
            return false;
        }

        sockaddr_in address;
#ifdef _MSC_VER
        int addressLength = static_cast<int>(sizeof(address));
#else
        socklen_t addressLength = sizeof(address);
#endif

        socket_t clientFd = ::accept(socketFd, reinterpret_cast<sockaddr*>(&address), &addressLength);

        if (clientFd == INVALID_SOCKET)
        {
            int error = getLastError();
            std::cerr << "Failed to accept client, error: " << error << std::endl;
            return false;
        }
        else
        {
            std::cout << "Client connected from " << ipToString(address.sin_addr.s_addr) << std::endl;

            Socket socket(network, clientFd);

            if (acceptCallback)
            {
                acceptCallback(socket);
            }
        }

        return true;
    }
}
