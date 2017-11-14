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
#include <fcntl.h>
#include "Socket.hpp"
#include "Network.hpp"
#include "Log.hpp"

namespace cppsocket
{
    static const int WAITING_QUEUE_SIZE = 5;
    static uint8_t TEMP_BUFFER[65536];

#ifdef _WIN32
    static inline bool initWSA()
    {
        WORD sockVersion = MAKEWORD(2, 2);
        WSADATA wsaData;
        int error = WSAStartup(sockVersion, &wsaData);
        if (error != 0)
        {
            Log(Log::Level::ERR) << "WSAStartup failed, error: " << error;
            return false;
        }

        if (wsaData.wVersion != sockVersion)
        {
            Log(Log::Level::ERR) << "Incorrect Winsock version";
            WSACleanup();
            return false;
        }

        return true;
    }
#endif

    bool Socket::getAddress(const std::string& address, std::pair<uint32_t, uint16_t>& result)
    {
        result.first = ANY_ADDRESS;
        result.second = ANY_PORT;

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
        }

        addrinfo* info;
        int ret = getaddrinfo(addressStr.c_str(), portStr.empty() ? nullptr : portStr.c_str(), nullptr, &info);

#ifdef _WIN32
        if (ret != 0 && WSAGetLastError() == WSANOTINITIALISED)
        {
            if (!initWSA()) return false;

            ret = getaddrinfo(addressStr.c_str(), portStr.empty() ? nullptr : portStr.c_str(), nullptr, &info);
        }
#endif

        if (ret != 0)
        {
            int error = getLastError();
            Log(Log::Level::ERR) << "Failed to get address info of " << address << ", error: " << error;
            return false;
        }

        sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(info->ai_addr);
        result.first = addr->sin_addr.s_addr;
        result.second = ntohs(addr->sin_port);

        freeaddrinfo(info);

        return true;
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

        writeData();
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

        return *this;
    }

    bool Socket::close()
    {
        bool result = true;

        if (socketFd != INVALID_SOCKET)
        {
            if (ready)
            {
                writeData();
            }

            if (!closeSocketFd())
            {
                result = false;
            }
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

        return result;
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

                Log(Log::Level::WARN) << "Failed to connect to " << remoteAddressString << ", connection timed out";

                if (connectErrorCallback)
                {
                    connectErrorCallback(*this);
                }
            }
        }
    }

    bool Socket::startRead()
    {
        if (socketFd == INVALID_SOCKET)
        {
            Log(Log::Level::ERR) << "Can not start reading, invalid socket";
            return false;
        }

        ready = true;

        return true;
    }

    bool Socket::startAccept(const std::string& address)
    {
        ready = false;

        std::pair<uint32_t, uint16_t> addr;

        if (!getAddress(address, addr))
        {
            return false;
        }

        return startAccept(addr.first, addr.second);
    }

    bool Socket::startAccept(uint32_t address, uint16_t newPort)
    {
        ready = false;

        if (socketFd != INVALID_SOCKET)
        {
            close();
        }

        if (!createSocketFd())
        {
            return false;
        }

        localIPAddress = address;
        localPort = newPort;
        int value = 1;

        if (setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&value), sizeof(value)) < 0)
        {
            int error = getLastError();
            Log(Log::Level::ERR) << "setsockopt(SO_REUSEADDR) failed, error: " << error;
            return false;
        }

        sockaddr_in serverAddress;
        memset(&serverAddress, 0, sizeof(serverAddress));
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(localPort);
        serverAddress.sin_addr.s_addr = address;

        if (bind(socketFd, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0)
        {
            int error = getLastError();
            Log(Log::Level::ERR) << "Failed to bind server socket to port " << localPort << ", error: " << error;
            return false;
        }

        if (listen(socketFd, WAITING_QUEUE_SIZE) < 0)
        {
            int error = getLastError();
            Log(Log::Level::ERR) << "Failed to listen on " << ipToString(localIPAddress) << ":" << localPort << ", error: " << error;
            return false;
        }

        Log(Log::Level::INFO) << "Server listening on " << ipToString(localIPAddress) << ":" << localPort;
        
        accepting = true;
        ready = true;
        
        return true;
    }

    bool Socket::connect(const std::string& address)
    {
        ready = false;
        connecting = false;

        std::pair<uint32_t, uint16_t> addr;
        if (!getAddress(address, addr))
        {
            return false;
        }

        return connect(addr.first, addr.second);
    }

    bool Socket::connect(uint32_t address, uint16_t newPort)
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

        remoteAddressString = ipToString(remoteIPAddress) + ":" + std::to_string(remotePort);

        Log(Log::Level::INFO) << "Connecting to " << remoteAddressString;

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
                {
                    connecting = true;
                }
                else
                {
                    Log(Log::Level::WARN) << "Failed to connect to " << remoteAddressString << ", error: " << error;
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
            Log(Log::Level::INFO) << "Socket connected to " << remoteAddressString;
            if (connectCallback)
            {
                connectCallback(*this);
            }
        }

        sockaddr_in localAddr;
        socklen_t localAddrSize = sizeof(localAddr);

        if (getsockname(socketFd, reinterpret_cast<sockaddr*>(&localAddr), &localAddrSize) != 0)
        {
            int error = getLastError();
            Log(Log::Level::WARN) << "Failed to get address of the socket connecting to " << remoteAddressString << ", error: " << error;
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

    bool Socket::setBlocking(bool newBlocking)
    {
        blocking = newBlocking;

        if (socketFd != INVALID_SOCKET)
        {
            return setFdBlocking(newBlocking);
        }

        return true;
    }

    bool Socket::createSocketFd()
    {
        socketFd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

#ifdef _WIN32
        if (socketFd == INVALID_SOCKET && WSAGetLastError() == WSANOTINITIALISED)
        {
            if (!initWSA()) return false;

            socketFd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        }
#endif

        if (socketFd == INVALID_SOCKET)
        {
            int error = getLastError();
            Log(Log::Level::ERR) << "Failed to create socket, error: " << error;
            return false;
        }

        if (!blocking)
        {
            if (!setFdBlocking(false))
            {
                return false;
            }
        }

#ifdef __APPLE__
        int set = 1;
        if (setsockopt(socketFd, SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(int)) != 0)
        {
            int error = getLastError();
            Log(Log::Level::ERR) << "Failed to set socket option, error: " << error;
            return false;
        }
#endif

        return true;
    }

    bool Socket::closeSocketFd()
    {
        if (socketFd != INVALID_SOCKET)
        {
#ifdef _WIN32
            int result = closesocket(socketFd);
#else
            int result = ::close(socketFd);
#endif
            socketFd = INVALID_SOCKET;

            if (result < 0)
            {
                int error = getLastError();
                Log(Log::Level::ERR) << "Failed to close socket " << ipToString(localIPAddress) << ":" << localPort << ", error: " << error;
                return false;
            }
            else
            {
                Log(Log::Level::INFO) << "Socket " << ipToString(localIPAddress) << ":" << localPort << " closed";
            }
        }

        return true;
    }

    bool Socket::setFdBlocking(bool block)
    {
        if (socketFd == INVALID_SOCKET)
        {
            return false;
        }

#ifdef _WIN32
        unsigned long mode = block ? 0 : 1;
        if (ioctlsocket(socketFd, FIONBIO, &mode) != 0)
        {
            return false;
        }
#else
        int flags = fcntl(socketFd, F_GETFL, 0);
        if (flags < 0) return false;
        flags = block ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);

        if (fcntl(socketFd, F_SETFL, flags) != 0)
        {
            return false;
        }
#endif

        return true;
    }

    bool Socket::send(std::vector<uint8_t> buffer)
    {
        if (socketFd == INVALID_SOCKET)
        {
            return false;
        }

        outData.insert(outData.end(), buffer.begin(), buffer.end());

        return true;
    }

    bool Socket::read()
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

                if (error == EAGAIN ||
#ifdef _WIN32
                    error == WSAEWOULDBLOCK ||
#endif
                    error == EWOULDBLOCK)
                {
                    Log(Log::Level::ERR) << "No sockets to accept";
                }
                else
                {
                    Log(Log::Level::ERR) << "Failed to accept client, error: " << error;
                    return false;
                }
            }
            else
            {
                Log(Log::Level::INFO) << "Client connected from " << ipToString(address.sin_addr.s_addr) << ":" << ntohs(address.sin_port) << " to " << ipToString(localIPAddress) << ":" << localPort;

                Socket socket(network, clientFd, true,
                              localIPAddress, localPort,
                              address.sin_addr.s_addr,
                              ntohs(address.sin_port));
                
                if (acceptCallback)
                {
                    acceptCallback(*this, socket);
                }
            }
        }
        else
        {
            return readData();
        }

        return true;
    }

    bool Socket::write()
    {
        if (connecting)
        {
            connecting = false;
            ready = true;
            Log(Log::Level::INFO) << "Socket connected to " << remoteAddressString;
            if (connectCallback)
            {
                connectCallback(*this);
            }
        }

        return writeData();
    }

    bool Socket::readData()
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

        if (size < 0)
        {
            int error = getLastError();

            if (error == EAGAIN ||
#ifdef _WIN32
                error == WSAEWOULDBLOCK ||
#endif
                error == EWOULDBLOCK)
            {
                Log(Log::Level::WARN) << "Nothing to read from " << remoteAddressString;
                return true;
            }
            else if (error == ECONNRESET)
            {
                Log(Log::Level::INFO) << "Connection to " << remoteAddressString << " reset by peer";
                disconnected();
                return false;
            }
            else if (error == ECONNREFUSED)
            {
                Log(Log::Level::INFO) << "Connection to " << remoteAddressString << " refused";
                disconnected();
                return false;
            }
            else
            {
                Log(Log::Level::ERR) << "Failed to read from " << remoteAddressString << ", error: " << error;
                disconnected();
                return false;
            }
        }
        else if (size == 0)
        {
            disconnected();

            return true;
        }

        Log(Log::Level::ALL) << "Socket received " << size << " bytes from " << remoteAddressString;

        inData.assign(TEMP_BUFFER, TEMP_BUFFER + size);

        if (readCallback)
        {
            readCallback(*this, inData);
        }
        
        return true;
    }

    bool Socket::writeData()
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
            ssize_t dataSize = static_cast<ssize_t>(outData.size());
            ssize_t size = ::send(socketFd, reinterpret_cast<const char*>(outData.data()), outData.size(), flags);
#endif

            if (size < 0)
            {
                int error = getLastError();
                if (error == EAGAIN ||
#ifdef _WIN32
                    error == WSAEWOULDBLOCK ||
#endif
                    error == EWOULDBLOCK)
                {
                    Log(Log::Level::WARN) << "Can not write to " << remoteAddressString << " now";
                    return true;
                }
                else if (error == EPIPE)
                {
                    Log(Log::Level::ERR) << "Failed to send data to " << remoteAddressString << ", socket has been shut down";
                    disconnected();
                    return false;
                }
                else if (error == ECONNRESET)
                {
                    Log(Log::Level::INFO) << "Connection to " << remoteAddressString << " reset by peer";
                    disconnected();
                    return false;
                }
                else
                {
                    Log(Log::Level::ERR) << "Failed to write to socket " << remoteAddressString << ", error: " << error;
                    disconnected();
                    return false;
                }
            }
            else if (size != dataSize)
            {
                Log(Log::Level::ALL) << "Socket did not send all data to " << remoteAddressString << ", sent " << size << " out of " << outData.size() << " bytes";
            }
            else
            {
                Log(Log::Level::ALL) << "Socket sent " << size << " bytes to " << remoteAddressString;
            }

            if (size > 0)
            {
                outData.erase(outData.begin(), outData.begin() + size);
            }
        }
        
        return true;
    }

    bool Socket::disconnected()
    {
        bool result = true;

        if (connecting)
        {
            connecting = false;
            ready = false;

            Log(Log::Level::WARN) << "Failed to connect to " << remoteAddressString;

            if (socketFd != INVALID_SOCKET)
            {
                if (!closeSocketFd())
                {
                    result = false;
                }
            }

            if (connectErrorCallback)
            {
                connectErrorCallback(*this);
            }
        }
        else
        {
            if (ready)
            {
                Log(Log::Level::INFO) << "Socket disconnected from " << remoteAddressString << " disconnected";

                ready = false;

                if (closeCallback)
                {
                    closeCallback(*this);
                }

                if (socketFd != INVALID_SOCKET)
                {
                    if (!closeSocketFd())
                    {
                        result = false;
                    }
                }

                localIPAddress = 0;
                localPort = 0;
                remoteIPAddress = 0;
                remotePort = 0;
                ready = false;
                outData.clear();
            }
        }

        return result;
    }
}
