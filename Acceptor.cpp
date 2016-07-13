//
//  rtmp_relay
//

#include <iostream>
#include <cstring>
#ifdef _MSC_VER
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif
#include "Acceptor.h"
#include "Network.h"

static const int WAITING_QUEUE_SIZE = 5;

Acceptor::Acceptor(Network& network, socket_t socketFd):
    Socket(network, socketFd)
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

bool Acceptor::startAccept(uint16_t newPort)
{
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

    ipAddress = 0;
    port = newPort;
    int value = 1;
    connecting = false;

    if (setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&value), sizeof(value)) < 0)
    {
        int error = Network::getLastError();
        std::cerr << "setsockopt(SO_REUSEADDR) failed, error: " << error << std::endl;
        return false;
    }

    sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(socketFd, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0)
    {
        int error = Network::getLastError();
        std::cerr << "Failed to bind server socket, error: " << error << std::endl;
        return false;
    }
    
    if (listen(socketFd, WAITING_QUEUE_SIZE) < 0)
    {
        int error = Network::getLastError();
        std::cerr << "Failed to listen on port " << port << ", error: " << error << std::endl;
        return false;
    }
    
    std::cout << "Server listening on port " << port << std::endl;
    ready = true;
    
    return true;
}

void Acceptor::setAcceptCallback(const std::function<void(Socket&&)>& newAcceptCallback)
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
        int error = Network::getLastError();
        std::cerr << "Failed to accept client, error: " << error << std::endl;
        return false;
    }
    else
    {
        std::cout << "Client connected from " << Network::ipToString(address.sin_addr.s_addr) << std::endl;
        
        Socket socket(network, clientFd);
        
        if (acceptCallback)
        {
            acceptCallback(std::move(socket));
        }
    }
    
    return true;
}
