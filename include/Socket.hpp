//
//  cppsocket
//

#ifndef CPPSOCKET_HPP
#define CPPSOCKET_HPP

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>
#ifdef _WIN32
#  pragma push_macro("WIN32_LEAN_AND_MEAN")
#  pragma push_macro("NOMINMAX")
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma pop_macro("WIN32_LEAN_AND_MEAN")
#  pragma pop_macro("NOMINMAX")
#else
#  include <sys/socket.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <poll.h>
#  include <unistd.h>
#endif
#include <errno.h>
#include <fcntl.h>

namespace cppsocket
{
#ifdef _WIN32
    using socket_t = SOCKET;
    static constexpr socket_t NULL_SOCKET = INVALID_SOCKET;
#else
    using socket_t = int;
    static constexpr socket_t NULL_SOCKET = -1;
#endif

    static constexpr uint32_t ANY_ADDRESS = 0;
    static constexpr uint16_t ANY_PORT = 0;
    static constexpr int WAITING_QUEUE_SIZE = 5;

    inline std::string ipToString(uint32_t ip)
    {
        uint8_t* ptr = reinterpret_cast<uint8_t*>(&ip);

        return std::to_string(static_cast<uint32_t>(ptr[0])) + "." +
            std::to_string(static_cast<uint32_t>(ptr[1])) + "." +
            std::to_string(static_cast<uint32_t>(ptr[2])) + "." +
            std::to_string(static_cast<uint32_t>(ptr[3]));
    }

#ifdef _WIN32
    class WinSock final
    {
    public:
        WinSock():
            version(start(MAKEWORD(2, 2)))
        {
        }

        ~WinSock()
        {
            if (version) WSACleanup();
        }

        WinSock(const WinSock& other):
            version(other.version ? start(other.version) : 0)
        {
        }

        WinSock& operator=(const WinSock& other)
        {
            if (&other == this) return *this;
            if (version != other.version)
            {
                if (version)
                {
                    WSACleanup();
                    version = 0;
                }

                if (other.version) version = start(other.version);
            }
        }

        WinSock(WinSock&& other) noexcept:
            version(other.version)
        {
            other.version = 0;
        }

        WinSock& operator=(WinSock&& other) noexcept
        {
            if (&other == this) return *this;
            if (version) WSACleanup();
            version = other.version;
            other.version = 0;
            return *this;
        }

    private:
        static WORD start(WORD version)
        {
            WSADATA wsaData;
            const int error = WSAStartup(version, &wsaData);
            if (error != 0)
                throw std::system_error(error, std::system_category(), "WSAStartup failed");

            if (wsaData.wVersion != version)
            {
                WSACleanup();
                throw std::runtime_error("Invalid WinSock version");
            }

            return wsaData.wVersion;
        }

        WORD version = 0;
    };
#endif

    inline int getLastError() noexcept
    {
#ifdef _WIN32
        return WSAGetLastError();
#else
        return errno;
#endif
    }

    inline std::pair<uint32_t, uint16_t> getAddress(const std::string& address)
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

        if (ret != 0)
            throw std::system_error(getLastError(), std::system_category(), "Failed to get address info of " + address);

        sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(info->ai_addr);
        result.first = addr->sin_addr.s_addr;
        result.second = ntohs(addr->sin_port);

        freeaddrinfo(info);

        return result;
    }

    class Network;

    class Socket final
    {
        friend Network;
    public:
        Socket(Network& aNetwork);
        ~Socket();

        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;

        Socket(Socket&& other);
        Socket& operator=(Socket&& other)
        {
            if (&other != this)
            {
                closeSocketFd();

                socketFd = other.socketFd;
                ready = other.ready;
                blocking = other.blocking;
                localAddress = other.localAddress;
                localPort = other.localPort;
                remoteAddress = other.remoteAddress;
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

                remoteAddressString = ipToString(remoteAddress) + ":" + std::to_string(remotePort);

                other.socketFd = NULL_SOCKET;
                other.ready = false;
                other.blocking = true;
                other.localAddress = 0;
                other.localPort = 0;
                other.remoteAddress = 0;
                other.remotePort = 0;
                other.accepting = false;
                other.connecting = false;
                other.connectTimeout = 10.0f;
                other.timeSinceConnect = 0.0f;
            }

            return *this;
        }

        void close()
        {
            if (socketFd != NULL_SOCKET)
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

            localAddress = 0;
            localPort = 0;
            remoteAddress = 0;
            remotePort = 0;
            ready = false;
            accepting = false;
            connecting = false;
            outData.clear();
            inData.clear();
        }

        void update(float delta)
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

        void startRead()
        {
            if (socketFd == NULL_SOCKET)
                throw std::runtime_error("Can not start reading, invalid socket");

            ready = true;
        }

        void startAccept(const std::string& address)
        {
            ready = false;

            std::pair<uint32_t, uint16_t> addr = getAddress(address);

            startAccept(addr.first, addr.second);
        }

        void startAccept(uint32_t address, uint16_t port)
        {
            ready = false;

            if (socketFd != NULL_SOCKET)
                close();

            createSocketFd();

            localAddress = address;
            localPort = port;
            int value = 1;

            if (setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&value), sizeof(value)) < 0)
                throw std::system_error(getLastError(), std::system_category(), "setsockopt(SO_REUSEADDR) failed");

            sockaddr_in serverAddress;
            memset(&serverAddress, 0, sizeof(serverAddress));
            serverAddress.sin_family = AF_INET;
            serverAddress.sin_port = htons(localPort);
            serverAddress.sin_addr.s_addr = address;

            if (bind(socketFd, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0)
                throw std::system_error(getLastError(), std::system_category(), "Failed to bind server socket to port " + std::to_string(localPort));

            if (listen(socketFd, WAITING_QUEUE_SIZE) < 0)
                throw std::system_error(getLastError(), std::system_category(), "Failed to listen on " + ipToString(localAddress) + ":" + std::to_string(localPort));

            accepting = true;
            ready = true;
        }

        void connect(const std::string& address)
        {
            ready = false;
            connecting = false;

            std::pair<uint32_t, uint16_t> addr = getAddress(address);

            connect(addr.first, addr.second);
        }

        void connect(uint32_t address, uint16_t newPort)
        {
            ready = false;
            connecting = false;

            if (socketFd != NULL_SOCKET)
                close();

            createSocketFd();

            remoteAddress = address;
            remotePort = newPort;

            remoteAddressString = ipToString(remoteAddress) + ":" + std::to_string(remotePort);

            sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = remoteAddress;
            addr.sin_port = htons(remotePort);

            if (::connect(socketFd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0)
            {
                int error = getLastError();

#ifdef _WIN32
                if (error != WSAEWOULDBLOCK &&
                    error != WSAEINPROGRESS)
#else
                if (error != EAGAIN &&
                    error != EWOULDBLOCK &&
                    error != EINPROGRESS)
#endif
                {
                    if (connectErrorCallback)
                        connectErrorCallback(*this);

                    throw std::system_error(error, std::system_category(), "Failed to connect to " + remoteAddressString);
                }

                connecting = true;
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
                throw std::system_error(error, std::system_category(), "Failed to get address of the socket connecting to " + remoteAddressString);
            }

            localAddress = localAddr.sin_addr.s_addr;
            localPort = ntohs(localAddr.sin_port);
        }

        bool isConnecting() const { return connecting; }

        float getConnectTimeout() const { return connectTimeout; }
        void setConnectTimeout(float timeout) { connectTimeout = timeout; }

        void setReadCallback(const std::function<void(Socket&, const std::vector<uint8_t>&)>& newReadCallback)
        {
            readCallback = newReadCallback;
        }

        void setCloseCallback(const std::function<void(Socket&)>& newCloseCallback)
        {
            closeCallback = newCloseCallback;
        }

        void setAcceptCallback(const std::function<void(Socket&, Socket&)>& newAcceptCallback)
        {
            acceptCallback = newAcceptCallback;
        }

        void setConnectCallback(const std::function<void(Socket&)>& newConnectCallback)
        {
            connectCallback = newConnectCallback;
        }

        void setConnectErrorCallback(const std::function<void(Socket&)>& newConnectErrorCallback)
        {
            connectErrorCallback = newConnectErrorCallback;
        }

        void send(std::vector<uint8_t> buffer)
        {
            if (socketFd == NULL_SOCKET)
                throw std::runtime_error("Invalid socket");

            outData.insert(outData.end(), buffer.begin(), buffer.end());
        }

        uint32_t getLocalAddress() const { return localAddress; }
        uint16_t getLocalPort() const { return localPort; }

        uint32_t getRemoteAddress() const { return remoteAddress; }
        uint16_t getRemotePort() const { return remotePort; }

        bool isBlocking() const { return blocking; }
        void setBlocking(bool newBlocking)
        {
            blocking = newBlocking;

            if (socketFd != NULL_SOCKET)
                setFdBlocking(newBlocking);
        }

        bool isReady() const { return ready; }
        bool hasOutData() const { return !outData.empty(); }

    private:
        Socket(Network& aNetwork, socket_t aSocketFd, bool aReady,
               uint32_t aLocalAddress, uint16_t aLocalPort,
               uint32_t aRemoteAddress, uint16_t aRemotePort);

        void read()
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

                if (clientFd == NULL_SOCKET)
                {
                    int error = getLastError();

#ifdef _WIN32
                    if (error != WSAEWOULDBLOCK &&
                        error != WSAEINPROGRESS)
#else
                    if (error != EAGAIN &&
                        error != EWOULDBLOCK &&
                        error != EINPROGRESS)
#endif
                        throw std::system_error(error, std::system_category(), "Failed to accept client");
                }
                else
                {
                    Socket socket(network, clientFd, true,
                                  localAddress, localPort,
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

        void write()
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

        void readData()
        {
#if defined(__APPLE__)
            int flags = 0;
#elif defined(_WIN32)
            int flags = 0;
#else
            int flags = MSG_NOSIGNAL;
#endif

#ifdef _WIN32
            int size = recv(socketFd, reinterpret_cast<char*>(tempBuffer), sizeof(tempBuffer), flags);
#else
            ssize_t size = recv(socketFd, reinterpret_cast<char*>(tempBuffer), sizeof(tempBuffer), flags);
#endif

            if (size > 0)
            {
                inData.assign(tempBuffer, tempBuffer + size);

                if (readCallback)
                    readCallback(*this, inData);
            }
            else if (size < 0)
            {
                int error = getLastError();

#ifdef _WIN32
                if (error != WSAEWOULDBLOCK &&
                    error != WSAEINPROGRESS)
#else
                if (error != EAGAIN &&
                    error != EWOULDBLOCK &&
                    error != EINPROGRESS)
#endif
                {
                    disconnected();

                    if (error == ECONNRESET)
                        throw std::system_error(error, std::system_category(), "Connection to " + remoteAddressString + " reset by peer");
                    else if (error == ECONNREFUSED)
                        throw std::system_error(error, std::system_category(), "Connection to " + remoteAddressString + " refused");
                    else
                        throw std::system_error(error, std::system_category(), "Failed to read from " + remoteAddressString);
                }
            }
            else // size == 0
                disconnected();

        }

        void writeData()
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
#ifdef _WIN32
                    if (error != WSAEWOULDBLOCK &&
                        error != WSAEINPROGRESS)
#else
                    if (error != EAGAIN &&
                        error != EWOULDBLOCK &&
                        error != EINPROGRESS)
#endif
                    {
                        disconnected();

                        if (error == EPIPE)
                            throw std::system_error(error, std::system_category(), "Failed to send data to " + remoteAddressString + ", socket has been shut down");
                        else if (error == ECONNRESET)
                            throw std::system_error(error, std::system_category(), "Connection to " + remoteAddressString + " reset by peer");
                        else
                            throw std::system_error(error, std::system_category(), "Failed to write to socket " + remoteAddressString);
                    }
                }

                if (size > 0)
                    outData.erase(outData.begin(), outData.begin() + size);
            }
        }

        void disconnected()
        {
            if (connecting)
            {
                connecting = false;
                ready = false;

                if (socketFd != NULL_SOCKET)
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

                    if (socketFd != NULL_SOCKET)
                        closeSocketFd();

                    localAddress = 0;
                    localPort = 0;
                    remoteAddress = 0;
                    remotePort = 0;
                    ready = false;
                    outData.clear();
                }
            }
        }

        void createSocketFd()
        {
            socketFd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

            if (socketFd == NULL_SOCKET)
                throw std::system_error(getLastError(), std::system_category(), "Failed to create socket");

            if (!blocking)
                setFdBlocking(false);

#ifdef __APPLE__
            int set = 1;
            if (setsockopt(socketFd, SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(int)) != 0)
                throw std::system_error(errno, std::system_category(), "Failed to set socket option");
#endif
        }

        void closeSocketFd()
        {
            if (socketFd != NULL_SOCKET)
            {
#ifdef _WIN32
                closesocket(socketFd);
#else
                ::close(socketFd);
#endif
                socketFd = NULL_SOCKET;
            }
        }

        void setFdBlocking(bool block)
        {
            if (socketFd == NULL_SOCKET)
                throw std::runtime_error("Invalid socket");

#ifdef _WIN32
            unsigned long mode = block ? 0 : 1;
            if (ioctlsocket(socketFd, FIONBIO, &mode) != 0)
                throw std::system_error(WSAGetLastError(), std::system_category(), "Failed to set socket mode");
#else
            int flags = fcntl(socketFd, F_GETFL, 0);
            if (flags < 0)
                throw std::system_error(errno, std::system_category(), "Failed to get socket flags");
            flags = block ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);

            if (fcntl(socketFd, F_SETFL, flags) != 0)
                throw std::system_error(errno, std::system_category(), "Failed to set socket flags");
#endif
        }

        Network& network;

        socket_t socketFd = NULL_SOCKET;

        bool ready = false;
        bool blocking = true;

        uint32_t localAddress = 0;
        uint16_t localPort = 0;

        uint32_t remoteAddress = 0;
        uint16_t remotePort = 0;

        float connectTimeout = 10.0f;
        float timeSinceConnect = 0.0f;
        bool accepting = false;
        bool connecting = false;

        std::function<void(Socket&, const std::vector<uint8_t>&)> readCallback;
        std::function<void(Socket&)> closeCallback;
        std::function<void(Socket&, Socket&)> acceptCallback;
        std::function<void(Socket&)> connectCallback;
        std::function<void(Socket&)> connectErrorCallback;

        std::vector<uint8_t> inData;
        std::vector<uint8_t> outData;

        std::string remoteAddressString;

        uint8_t tempBuffer[1024];
    };

    class Network final
    {
        friend Socket;
    public:
        Network()
        {
            previousTime = std::chrono::steady_clock::now();
        }

        void update()
        {
            for (Socket* socket : socketDeleteSet)
            {
                auto i = std::find(sockets.begin(), sockets.end(), socket);

                if (i != sockets.end())
                    sockets.erase(i);
            }

            socketDeleteSet.clear();

            for (Socket* socket : socketAddSet)
            {
                auto i = std::find(sockets.begin(), sockets.end(), socket);

                if (i == sockets.end())
                    sockets.push_back(socket);
            }

            socketAddSet.clear();

            auto currentTime = std::chrono::steady_clock::now();
            auto diff = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - previousTime);

            float delta = diff.count() / 1000000000.0f;
            previousTime = currentTime;

            std::vector<pollfd> pollFds;
            pollFds.reserve(sockets.size());

            for (auto socket : sockets)
            {
                if (socket->socketFd != NULL_SOCKET)
                {
                    pollfd pollFd;
                    pollFd.fd = socket->socketFd;
                    pollFd.events = POLLIN | POLLOUT;

                    pollFds.push_back(pollFd);
                }
            }

            if (!pollFds.empty())
            {
#ifdef _WIN32
                if (WSAPoll(pollFds.data(), static_cast<ULONG>(pollFds.size()), 0) < 0)
                    throw std::system_error(WSAGetLastError(), std::system_category(), "Poll failed");
#else
                if (poll(pollFds.data(), static_cast<nfds_t>(pollFds.size()), 0) < 0)
                    throw std::system_error(errno, std::system_category(), "Poll failed");
#endif


                for (pollfd& pollFd : pollFds)
                {
                    for (Socket* deleteSocket : socketDeleteSet)
                    {
                        auto i = std::find(sockets.begin(), sockets.end(), deleteSocket);

                        if (i != sockets.end())
                            sockets.erase(i);
                    }

                    socketDeleteSet.clear();

                    auto i = std::find_if(sockets.begin(), sockets.end(), [&pollFd](Socket* socket) {
                        return socket->socketFd == pollFd.fd;
                    });

                    if (i != sockets.end())
                    {
                        Socket* socket = *i;

                        if (pollFd.revents & POLLIN)
                            socket->read();

                        if (pollFd.revents & POLLOUT)
                            socket->write();

                        socket->update(delta);
                    }
                }
            }
        }

    private:
        void addSocket(Socket& socket)
        {
            socketAddSet.insert(&socket);

            auto setIterator = socketDeleteSet.find(&socket);

            if (setIterator != socketDeleteSet.end())
                socketDeleteSet.erase(setIterator);
        }

        void removeSocket(Socket& socket)
        {
            socketDeleteSet.insert(&socket);

            auto setIterator = socketAddSet.find(&socket);

            if (setIterator != socketAddSet.end())
                socketAddSet.erase(setIterator);
        }

#ifdef _WIN32
        WinSock winSock;
#endif

        std::vector<Socket*> sockets;
        std::set<Socket*> socketAddSet;
        std::set<Socket*> socketDeleteSet;

        std::chrono::steady_clock::time_point previousTime;
    };

    Socket::Socket(Network& aNetwork):
        network(aNetwork)
    {
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
        localAddress(other.localAddress),
        localPort(other.localPort),
        remoteAddress(other.remoteAddress),
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

        remoteAddressString = ipToString(remoteAddress) + ":" + std::to_string(remotePort);

        other.socketFd = NULL_SOCKET;
        other.ready = false;
        other.blocking = true;
        other.localAddress = 0;
        other.localPort = 0;
        other.remoteAddress = 0;
        other.remotePort = 0;
        other.connecting = false;
        other.connectTimeout = 10.0f;
        other.timeSinceConnect = 0.0f;
    }

    Socket::Socket(Network& aNetwork, socket_t aSocketFd, bool aReady,
           uint32_t aLocalAddress, uint16_t aLocalPort,
           uint32_t aRemoteAddress, uint16_t aRemotePort):
        network(aNetwork), socketFd(aSocketFd), ready(aReady),
        localAddress(aLocalAddress), localPort(aLocalPort),
        remoteAddress(aRemoteAddress), remotePort(aRemotePort)
    {
        remoteAddressString = ipToString(remoteAddress) + ":" + std::to_string(remotePort);
        network.addSocket(*this);
    }
}
#endif // CPPSOCKET_HPP
