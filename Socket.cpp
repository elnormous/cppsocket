//
//  cppsocket
//

#ifdef _MSC_VER
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif
#include <fcntl.h>
#include "Log.h"
#include "Socket.h"
#include "Network.h"

namespace cppsocket
{
    static uint8_t TEMP_BUFFER[65536];

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
        {
            addressStr = address;
        }

        addrinfo* info;
        if (getaddrinfo(addressStr.c_str(), portStr.empty() ? nullptr : portStr.c_str(), nullptr, &info) != 0)
        {
            int error = getLastError();
            Log(Log::Level::ERR) << "Failed to get address info of " << address << ", error: " << error;
            return result;
        }

        struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(info->ai_addr);
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
        connecting(other.connecting),
        connectCallback(std::move(other.connectCallback)),
        connectErrorCallback(std::move(other.connectErrorCallback)),
        readCallback(std::move(other.readCallback)),
        closeCallback(std::move(other.closeCallback)),
        outData(std::move(other.outData))
    {
        network.addSocket(*this);

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
        connecting = other.connecting;
        connectCallback = std::move(other.connectCallback);
        connectErrorCallback = std::move(other.connectErrorCallback);
        readCallback = std::move(other.readCallback);
        closeCallback = std::move(other.closeCallback);
        outData = std::move(other.outData);

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
        connecting = false;
        outData.clear();

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

                Log(Log::Level::WARN) << "Failed to connect to " << ipToString(remoteIPAddress) << ":" << remotePort << ", connection timed out";

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

    bool Socket::connect(const std::string& address)
    {
        ready = false;
        connecting = false;

        std::pair<uint32_t, uint16_t> addr = getAddress(address);

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

    void Socket::setConnectTimeout(float timeout)
    {
        connectTimeout = timeout;
    }

    void Socket::setConnectCallback(const std::function<void(Socket&)>& newConnectCallback)
    {
        connectCallback = newConnectCallback;
    }

    void Socket::setConnectErrorCallback(const std::function<void(Socket&)>& newConnectErrorCallback)
    {
        connectErrorCallback = newConnectErrorCallback;
    }

    void Socket::setReadCallback(const std::function<void(Socket&, const std::vector<uint8_t>&)>& newReadCallback)
    {
        readCallback = newReadCallback;
    }

    void Socket::setCloseCallback(const std::function<void(Socket&)>& newCloseCallback)
    {
        closeCallback = newCloseCallback;
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
        socketFd = socket(AF_INET, SOCK_STREAM, 0);

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

        return true;
    }

    bool Socket::closeSocketFd()
    {
        if (socketFd != INVALID_SOCKET)
        {
#ifdef _MSC_VER
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

#ifdef _MSC_VER
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
        return readData();
    }

    bool Socket::write()
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

        return writeData();
    }

    bool Socket::readData()
    {
        int size = static_cast<int>(recv(socketFd, reinterpret_cast<char*>(TEMP_BUFFER), sizeof(TEMP_BUFFER), 0));

        if (size < 0)
        {
            int error = getLastError();

            if (error == EAGAIN ||
#ifdef _MSC_VER
                error == WSAEWOULDBLOCK ||
#endif
                error == EWOULDBLOCK)
            {
                Log(Log::Level::WARN) << "Nothing to read from " << ipToString(remoteIPAddress) << ":" << remotePort;
                return true;
            }
            else if (error == ECONNRESET)
            {
                Log(Log::Level::INFO) << "Connection to " << ipToString(remoteIPAddress) << ":" << remotePort << " reset by peer";
                disconnected();
                return false;
            }
            else if (error == ECONNREFUSED)
            {
                Log(Log::Level::INFO) << "Connection to " << ipToString(remoteIPAddress) << ":" << remotePort << " refused";
                disconnected();
                return false;
            }
            else
            {
                Log(Log::Level::ERR) << "Failed to read from " << ipToString(remoteIPAddress) << ":" << remotePort << ", error: " << error;
                disconnected();
                return false;
            }
        }
        else if (size == 0)
        {
            disconnected();

            return true;
        }

        Log(Log::Level::ALL) << "Socket received " << size << " bytes from " << ipToString(remoteIPAddress) << ":" << remotePort;

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
#ifdef _MSC_VER
            int dataSize = static_cast<int>(outData.size());
            int size = ::send(socketFd, reinterpret_cast<const char*>(outData.data()), dataSize, 0);
#else
            ssize_t dataSize = static_cast<ssize_t>(outData.size());
            ssize_t size = ::send(socketFd, reinterpret_cast<const char*>(outData.data()), outData.size(), 0);
#endif

            if (size < 0)
            {
                int error = getLastError();
                if (error == EAGAIN ||
#ifdef _MSC_VER
                    error == WSAEWOULDBLOCK ||
#endif
                    error == EWOULDBLOCK)
                {
                    Log(Log::Level::WARN) << "Can not write to " << ipToString(remoteIPAddress) << ":" << remotePort << " now";
                    return true;
                }
                else if (error == EPIPE)
                {
                    Log(Log::Level::ERR) << "Failed to send data to " << ipToString(remoteIPAddress) << ":" << remotePort << ", socket has been shut down";
                    disconnected();
                    return false;
                }
                else if (error == ECONNRESET)
                {
                    Log(Log::Level::INFO) << "Connection to " << ipToString(remoteIPAddress) << ":" << remotePort << " reset by peer";
                    disconnected();
                    return false;
                }
                else
                {
                    Log(Log::Level::ERR) << "Failed to write to socket " << ipToString(remoteIPAddress) << ":" << remotePort << ", error: " << error;
                    disconnected();
                    return false;
                }
            }
            else if (size != dataSize)
            {
                Log(Log::Level::ALL) << "Socket did not send all data to " << ipToString(remoteIPAddress) << ":" << remotePort << ", sent " << size << " out of " << outData.size() << " bytes";
            }
            else
            {
                Log(Log::Level::ALL) << "Socket sent " << size << " bytes to " << ipToString(remoteIPAddress) << ":" << remotePort;
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

            Log(Log::Level::WARN) << "Failed to connect to " << ipToString(remoteIPAddress) << ":" << remotePort;

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
                Log(Log::Level::INFO) << "Socket disconnected from " << ipToString(remoteIPAddress) << ":" << remotePort << " disconnected";

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
