//
//  cppsocket
//

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif
#include <cstring>
#include <stdexcept>
#include <fcntl.h>
#include "Socket.hpp"
#include "Network.hpp"

namespace cppsocket
{
    static const int WAITING_QUEUE_SIZE = 5;
    static uint8_t TEMP_BUFFER[65536];

#ifdef _WIN32
    static inline void initWSA()
    {
        WORD sockVersion = MAKEWORD(2, 2);
        WSADATA wsaData;
        int error = WSAStartup(sockVersion, &wsaData);
        if (error != 0)
            throw std::runtime_error("WSAStartup failed, error: " << error);

        if (wsaData.wVersion != sockVersion)
        {
            WSACleanup();
            throw std::runtime_error("Incorrect Winsock version");
        }
    }
#endif

    std::pair<uint32_t, uint16_t> Socket::getAddress(const std::string& address)
    {
        std::pair<uint32_t, uint16_t> result(ANY_ADDRESS, ANY_PORT);

        size_t i = address.find(':');
        std::string addressStr;
        std::string portStr;

        if (i != std::string::npos)
        {
            addressStr = address.substr(0, i);
            portStr = address.substr(i + 1);
        }
        else
            addressStr = address;

        addrinfo* info;
        int ret = getaddrinfo(addressStr.c_str(), portStr.empty() ? nullptr : portStr.c_str(), nullptr, &info);

#ifdef _WIN32
        if (ret != 0 && WSAGetLastError() == WSANOTINITIALISED)
        {
            initWSA();
            ret = getaddrinfo(addressStr.c_str(), portStr.empty() ? nullptr : portStr.c_str(), nullptr, &info);
        }
#endif

        if (ret != 0)
        {
            int error = getLastError();
            throw std::runtime_error("Failed to get address info of " + address + ", error: " + std::to_string(error));
        }

        sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(info->ai_addr);
        result.first = addr->sin_addr.s_addr;
        result.second = ntohs(addr->sin_port);

        freeaddrinfo(info);

        return result;
    }

    Socket::Socket(Network& aNetwork):
        network(aNetwork)
    {
        network.addSocket(*this);
    }

    Socket::Socket(Network& aNetwork, socket_t aSocketFd, bool aReady,
                   uint32_t aLocalIPAddress, uint16_t aLocalPort,
                   uint32_t aRemoteIPAddress, uint16_t aRemotePort):
        network(aNetwork), socketFd(aSocketFd), ready(aReady),
        localIPAddress(aLocalIPAddress), localPort(aLocalPort),
        remoteIPAddress(aRemoteIPAddress), remotePort(aRemotePort)
    {
        remoteAddressString = ipToString(remoteIPAddress) + ":" + std::to_string(remotePort);
        network.addSocket(*this);
    }

    Socket::~Socket()
    {
        network.removeSocket(*this);

        try
        {
            writeData();
        }
        catch (...)
        {
        }

        closeSocketFd();
    }

    Socket::Socket(Socket&& other):
        network(other.network),
        socketFd(other.socketFd),
        ready(other.ready),
        blocking(other.blocking),
        localIPAddress(other.localIPAddress),
        localPort(other.localPort),
        remoteIPAddress(other.remoteIPAddress),
        remotePort(other.remotePort),
        connectTimeout(other.connectTimeout),
        timeSinceConnect(other.timeSinceConnect),
        accepting(other.accepting),
        connecting(other.connecting),
        readCallback(std::move(other.readCallback)),
        closeCallback(std::move(other.closeCallback)),
        acceptCallback(std::move(other.acceptCallback)),
        connectCallback(std::move(other.connectCallback)),
        connectErrorCallback(std::move(other.connectErrorCallback)),
        outData(std::move(other.outData))
    {
        network.addSocket(*this);

        remoteAddressString = ipToString(remoteIPAddress) + ":" + std::to_string(remotePort);

        other.socketFd = INVALID_SOCKET;
        other.ready = false;
        other.blocking = true;
        other.localIPAddress = 0;
        other.localPort = 0;
        other.remoteIPAddress = 0;
        other.remotePort = 0;
        other.connecting = false;
        other.connectTimeout = 10.0f;
        other.timeSinceConnect = 0.0f;
    }

    Socket& Socket::operator=(Socket&& other)
    {
        if (&other != this)
        {
            closeSocketFd();

            socketFd = other.socketFd;
            ready = other.ready;
            blocking = other.blocking;
            localIPAddress = other.localIPAddress;
            localPort = other.localPort;
            remoteIPAddress = other.remoteIPAddress;
            remotePort = other.remotePort;
            connectTimeout = other.connectTimeout;
            timeSinceConnect = other.timeSinceConnect;
            accepting = other.accepting;
            connecting = other.connecting;
            readCallback = std::move(other.readCallback);
            closeCallback = std::move(other.closeCallback);
            acceptCallback = std::move(other.acceptCallback);
            connectCallback = std::move(other.connectCallback);
            connectErrorCallback = std::move(other.connectErrorCallback);
            outData = std::move(other.outData);

            remoteAddressString = ipToString(remoteIPAddress) + ":" + std::to_string(remotePort);

            other.socketFd = INVALID_SOCKET;
            other.ready = false;
            other.blocking = true;
            other.localIPAddress = 0;
            other.localPort = 0;
            other.remoteIPAddress = 0;
            other.remotePort = 0;
            other.accepting = false;
            other.connecting = false;
            other.connectTimeout = 10.0f;
            other.timeSinceConnect = 0.0f;
        }

        return *this;
    }

    void Socket::close()
    {
        if (socketFd != INVALID_SOCKET)
        {
            if (ready)
            {
                try
                {
                    writeData();
                }
                catch (...)
                {
                }
            }

            closeSocketFd();
        }

        localIPAddress = 0;
        localPort = 0;
        remoteIPAddress = 0;
        remotePort = 0;
        ready = false;
        accepting = false;
        connecting = false;
        outData.clear();
        inData.clear();
    }

    void Socket::update(float delta)
    {
        if (connecting)
        {
            timeSinceConnect += delta;

            if (timeSinceConnect > connectTimeout)
            {
                connecting = false;

                close();

                if (connectErrorCallback)
                    connectErrorCallback(*this);
            }
        }
    }

    void Socket::startRead()
    {
        if (socketFd == INVALID_SOCKET)
            throw std::runtime_error("Can not start reading, invalid socket");

        ready = true;
    }

    void Socket::startAccept(const std::string& address)
    {
        ready = false;

        std::pair<uint32_t, uint16_t> addr = getAddress(address);

        startAccept(addr.first, addr.second);
    }

    void Socket::startAccept(uint32_t address, uint16_t newPort)
    {
        ready = false;

        if (socketFd != INVALID_SOCKET)
            close();

        createSocketFd();

        localIPAddress = address;
        localPort = newPort;
        int value = 1;

        if (setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&value), sizeof(value)) < 0)
        {
            int error = getLastError();
            throw std::runtime_error("setsockopt(SO_REUSEADDR) failed, error: " + std::to_string(error));
        }

        sockaddr_in serverAddress;
        memset(&serverAddress, 0, sizeof(serverAddress));
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(localPort);
        serverAddress.sin_addr.s_addr = address;

        if (bind(socketFd, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0)
        {
            int error = getLastError();
            throw std::runtime_error("Failed to bind server socket to port " + std::to_string(localPort) + ", error: " + std::to_string(error));
        }

        if (listen(socketFd, WAITING_QUEUE_SIZE) < 0)
        {
            int error = getLastError();
            throw std::runtime_error("Failed to listen on " + ipToString(localIPAddress) + ":" + std::to_string(localPort) + ", error: " + std::to_string(error));
        }
        
        accepting = true;
        ready = true;
    }

    void Socket::connect(const std::string& address)
    {
        ready = false;
        connecting = false;

        std::pair<uint32_t, uint16_t> addr = getAddress(address);

        connect(addr.first, addr.second);
    }

    void Socket::connect(uint32_t address, uint16_t newPort)
    {
        ready = false;
        connecting = false;

        if (socketFd != INVALID_SOCKET)
            close();

        createSocketFd();

        remoteIPAddress = address;
        remotePort = newPort;

        remoteAddressString = ipToString(remoteIPAddress) + ":" + std::to_string(remotePort);

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = remoteIPAddress;
        addr.sin_port = htons(remotePort);

        if (::connect(socketFd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0)
        {
            int error = getLastError();

#ifdef _WIN32
            if (error == WSAEWOULDBLOCK)
#else
                if (error == EINPROGRESS)
#endif
                    connecting = true;
                else
                {
                    if (connectErrorCallback)
                        connectErrorCallback(*this);

                    throw std::runtime_error("Failed to connect to " + remoteAddressString + ", error: " + std::to_string(error));
                }
        }
        else
        {
            // connected
            ready = true;
            if (connectCallback)
                connectCallback(*this);
        }

        sockaddr_in localAddr;
        socklen_t localAddrSize = sizeof(localAddr);

        if (getsockname(socketFd, reinterpret_cast<sockaddr*>(&localAddr), &localAddrSize) != 0)
        {
            int error = getLastError();
            closeSocketFd();
            connecting = false;
            if (connectErrorCallback)
                connectErrorCallback(*this);
            throw std::runtime_error("Failed to get address of the socket connecting to " + remoteAddressString + ", error: " + std::to_string(error));
        }

        localIPAddress = localAddr.sin_addr.s_addr;
        localPort = ntohs(localAddr.sin_port);
    }

    void Socket::setConnectTimeout(float timeout)
    {
        connectTimeout = timeout;
    }

    void Socket::setReadCallback(const std::function<void(Socket&, const std::vector<uint8_t>&)>& newReadCallback)
    {
        readCallback = newReadCallback;
    }

    void Socket::setCloseCallback(const std::function<void(Socket&)>& newCloseCallback)
    {
        closeCallback = newCloseCallback;
    }

    void Socket::setAcceptCallback(const std::function<void(Socket&, Socket&)>& newAcceptCallback)
    {
        acceptCallback = newAcceptCallback;
    }

    void Socket::setConnectCallback(const std::function<void(Socket&)>& newConnectCallback)
    {
        connectCallback = newConnectCallback;
    }

    void Socket::setConnectErrorCallback(const std::function<void(Socket&)>& newConnectErrorCallback)
    {
        connectErrorCallback = newConnectErrorCallback;
    }

    void Socket::setBlocking(bool newBlocking)
    {
        blocking = newBlocking;

        if (socketFd != INVALID_SOCKET)
            setFdBlocking(newBlocking);
    }

    void Socket::createSocketFd()
    {
        socketFd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

#ifdef _WIN32
        if (socketFd == INVALID_SOCKET && WSAGetLastError() == WSANOTINITIALISED)
        {
            initWSA();
            socketFd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        }
#endif

        if (socketFd == INVALID_SOCKET)
        {
            int error = getLastError();
            throw std::runtime_error("Failed to create socket, error: " + std::to_string(error));
        }

        if (!blocking)
            setFdBlocking(false);

#ifdef __APPLE__
        int set = 1;
        if (setsockopt(socketFd, SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(int)) != 0)
        {
            int error = getLastError();
            throw std::runtime_error("Failed to set socket option, error: " + std::to_string(error));
        }
#endif
    }

    void Socket::closeSocketFd()
    {
        if (socketFd != INVALID_SOCKET)
        {
#ifdef _WIN32
            closesocket(socketFd);
#else
            ::close(socketFd);
#endif
            socketFd = INVALID_SOCKET;
        }
    }

    void Socket::setFdBlocking(bool block)
    {
        if (socketFd == INVALID_SOCKET)
            throw std::runtime_error("Invalid socket");

#ifdef _WIN32
        unsigned long mode = block ? 0 : 1;
        if (ioctlsocket(socketFd, FIONBIO, &mode) != 0)
            throw std::runtime_error("Failed to set socket mode");
#else
        int flags = fcntl(socketFd, F_GETFL, 0);
        if (flags < 0)
            throw std::runtime_error("Failed to get socket flags");
        flags = block ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);

        if (fcntl(socketFd, F_SETFL, flags) != 0)
            throw std::runtime_error("Failed to set socket flags");
#endif
    }

    void Socket::send(std::vector<uint8_t> buffer)
    {
        if (socketFd == INVALID_SOCKET)
            throw std::runtime_error("Invalid socket");

        outData.insert(outData.end(), buffer.begin(), buffer.end());
    }

    void Socket::read()
    {
        if (accepting)
        {
            sockaddr_in address;
#ifdef _WIN32
            int addressLength = static_cast<int>(sizeof(address));
#else
            socklen_t addressLength = sizeof(address);
#endif

            socket_t clientFd = ::accept(socketFd, reinterpret_cast<sockaddr*>(&address), &addressLength);

            if (clientFd == INVALID_SOCKET)
            {
                int error = getLastError();

                if (error != EAGAIN &&
#ifdef _WIN32
                    error != WSAEWOULDBLOCK &&
#endif
                    error != EWOULDBLOCK)
                    throw std::runtime_error("Failed to accept client, error: " + std::to_string(error));
            }
            else
            {
                Socket socket(network, clientFd, true,
                              localIPAddress, localPort,
                              address.sin_addr.s_addr,
                              ntohs(address.sin_port));
                
                if (acceptCallback)
                    acceptCallback(*this, socket);
            }
        }
        else
        {
            return readData();
        }
    }

    void Socket::write()
    {
        if (connecting)
        {
            connecting = false;
            ready = true;
            if (connectCallback)
                connectCallback(*this);
        }

        return writeData();
    }

    void Socket::readData()
    {
#if defined(__APPLE__)
        int flags = 0;
#elif defined(_WIN32)
        int flags = 0;
#else
        int flags = MSG_NOSIGNAL;
#endif

#ifdef _WIN32
        int size = recv(socketFd, reinterpret_cast<char*>(TEMP_BUFFER), sizeof(TEMP_BUFFER), flags);
#else
        ssize_t size = recv(socketFd, reinterpret_cast<char*>(TEMP_BUFFER), sizeof(TEMP_BUFFER), flags);
#endif

        if (size > 0)
        {
            inData.assign(TEMP_BUFFER, TEMP_BUFFER + size);

            if (readCallback)
                readCallback(*this, inData);
        }
        else if (size < 0)
        {
            int error = getLastError();

            if (error != EAGAIN &&
#ifdef _WIN32
                error != WSAEWOULDBLOCK &&
#endif
                error != EWOULDBLOCK)
            {
                disconnected();

                if (error == ECONNRESET)
                    throw std::runtime_error("Connection to " + remoteAddressString + " reset by peer");
                else if (error == ECONNREFUSED)
                    throw std::runtime_error("Connection to " + remoteAddressString + " refused");
                else
                    throw std::runtime_error("Failed to read from " + remoteAddressString + ", error: " + std::to_string(error));
            }
        }
        else // size == 0
            disconnected();

    }

    void Socket::writeData()
    {
        if (ready && !outData.empty())
        {
#if defined(__APPLE__)
            int flags = 0;
#elif defined(_WIN32)
            int flags = 0;
#else
            int flags = MSG_NOSIGNAL;
#endif

#ifdef _WIN32
            int dataSize = static_cast<int>(outData.size());
            int size = ::send(socketFd, reinterpret_cast<const char*>(outData.data()), dataSize, flags);
#else
            size_t dataSize = static_cast<size_t>(outData.size());
            ssize_t size = ::send(socketFd, reinterpret_cast<const char*>(outData.data()), dataSize, flags);
#endif

            if (size < 0)
            {
                int error = getLastError();
                if (error != EAGAIN &&
#ifdef _WIN32
                    error != WSAEWOULDBLOCK &&
#endif
                    error != EWOULDBLOCK)
                {
                    disconnected();

                    if (error == EPIPE)
                        throw std::runtime_error("Failed to send data to " + remoteAddressString + ", socket has been shut down");
                    else if (error == ECONNRESET)
                        throw std::runtime_error("Connection to " + remoteAddressString + " reset by peer");
                    else
                        throw std::runtime_error("Failed to write to socket " + remoteAddressString + ", error: " + std::to_string(error));
                }
            }

            if (size > 0)
                outData.erase(outData.begin(), outData.begin() + size);
        }
    }

    void Socket::disconnected()
    {
        if (connecting)
        {
            connecting = false;
            ready = false;

            if (socketFd != INVALID_SOCKET)
                closeSocketFd();

            if (connectErrorCallback)
                connectErrorCallback(*this);
        }
        else
        {
            if (ready)
            {
                ready = false;

                if (closeCallback)
                    closeCallback(*this);

                if (socketFd != INVALID_SOCKET)
                    closeSocketFd();

                localIPAddress = 0;
                localPort = 0;
                remoteIPAddress = 0;
                remotePort = 0;
                ready = false;
                outData.clear();
            }
        }
    }
}
