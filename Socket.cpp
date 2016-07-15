//
//  cppsocket
//

#include <iostream>
#include <cstring>
#ifdef _MSC_VER
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif
#include <fcntl.h>
#include "Socket.h"
#include "Network.h"

namespace cppsocket
{
    static uint8_t TEMP_BUFFER[65536];

    Socket::Socket(Network& pNetwork, socket_t pSocketFd):
        network(pNetwork), socketFd(pSocketFd)
    {
        if (socketFd == INVALID_SOCKET)
        {
            socketFd = socket(AF_INET, SOCK_STREAM, 0);

            if (socketFd == INVALID_SOCKET)
            {
                int error = Network::getLastError();
                std::cerr << "Failed to create socket, error: " << error << std::endl;
            }
        }

        network.addSocket(*this);
    }

    Socket::~Socket()
    {
        network.removeSocket(*this);

        if (socketFd != INVALID_SOCKET)
        {
#ifdef _MSC_VER
            if (closesocket(socketFd) < 0)
#else
            if (::close(socketFd) < 0)
#endif
            {
                int error = Network::getLastError();
                std::cerr << "Failed to close socket, error: " << error << std::endl;
            }
            else
            {
                std::cout << "Socket closed" << std::endl;
            }
        }
    }

    Socket::Socket(Socket&& other):
        network(other.network),
        socketFd(other.socketFd),
        ready(other.ready),
        blocking(other.blocking),
        ipAddress(other.ipAddress),
        port(other.port),
        readCallback(std::move(other.readCallback)),
        closeCallback(std::move(other.closeCallback))
    {
        network.addSocket(*this);

        other.socketFd = INVALID_SOCKET;
        other.ready = false;
        other.blocking = true;
        other.ipAddress = 0;
        other.port = 0;
    }

    Socket& Socket::operator=(Socket&& other)
    {
        socketFd = other.socketFd;
        ready = other.ready;
        blocking = other.blocking;
        ipAddress = other.ipAddress;
        port = other.port;
        readCallback = std::move(other.readCallback);
        closeCallback = std::move(other.closeCallback);

        other.socketFd = INVALID_SOCKET;
        other.ready = false;
        other.blocking = true;
        other.ipAddress = 0;
        other.port = 0;

        return *this;
    }

    bool Socket::close()
    {
        ready = false;

        if (socketFd >= 0)
        {
#ifdef _MSC_VER
            int result = closesocket(socketFd);
#else
            int result = ::close(socketFd);
#endif
            socketFd = INVALID_SOCKET;

            if (result < 0)
            {
                int error = Network::getLastError();
                std::cerr << "Failed to close socket, error: " << error << std::endl;
                return false;
            }
            else
            {
                std::cout << "Socket closed" << std::endl;
            }

        }

        return true;
    }

    void Socket::update(float)
    {
    }

    bool Socket::startRead()
    {
        if (socketFd == INVALID_SOCKET)
        {
            std::cerr << "Can not start reading, invalid socket" << std::endl;
            return false;
        }

        ready = true;

        return true;
    }

    void Socket::setReadCallback(const std::function<void(const std::vector<uint8_t>&)>& newReadCallback)
    {
        readCallback = newReadCallback;
    }

    void Socket::setCloseCallback(const std::function<void()>& newCloseCallback)
    {
        closeCallback = newCloseCallback;
    }

    bool Socket::setBlocking(bool newBlocking)
    {
#ifdef _MSC_VER
        unsigned long mode = newBlocking ? 0 : 1;
        if (ioctlsocket(socketFd, FIONBIO, &mode) != 0)
        {
            return false;
        }
#else
        int flags = fcntl(socketFd, F_GETFL, 0);
        if (flags < 0) return false;
        flags = newBlocking ? (flags&~O_NONBLOCK) : (flags|O_NONBLOCK);

        if (fcntl(socketFd, F_SETFL, flags) != 0)
        {
            return false;
        }
#endif

        blocking = newBlocking;

        return true;
    }

    bool Socket::send(std::vector<uint8_t> buffer)
    {
        if (!ready)
        {
            return false;
        }

        outData.insert(outData.end(), buffer.begin(), buffer.end());

        return true;
    }

    bool Socket::read()
    {
        int size = static_cast<int>(recv(socketFd, reinterpret_cast<char*>(TEMP_BUFFER), sizeof(TEMP_BUFFER), 0));

        if (size < 0)
        {
            int error = Network::getLastError();

            if (error == ECONNRESET)
            {
                std::cerr << "Connection reset by peer" << std::endl;
            }
            else
            {
                std::cerr << "Failed to read from socket, error: " << error << std::endl;
            }

            disconnected();

            return false;
        }
        else if (size == 0)
        {
            disconnected();

            return false;
        }

#ifdef DEBUG
        std::cout << "Socket received " << size << " bytes" << std::endl;
#endif

        inData.insert(inData.end(), TEMP_BUFFER, TEMP_BUFFER + size);

        if (!inData.empty())
        {
            if (readCallback)
            {
                readCallback(inData);
            }

            inData.clear();
        }

        return true;
    }

    bool Socket::write()
    {
        if (ready && !outData.empty())
        {
#ifdef _MSC_VER
            int dataSize = static_cast<int>(outData.size());
#else
            size_t dataSize = outData.size();
#endif
            int size = static_cast<int>(::send(socketFd, reinterpret_cast<const char*>(outData.data()), dataSize, 0));

            if (size < 0)
            {
                int error = Network::getLastError();
                if (error != EAGAIN && error != EWOULDBLOCK)
                {
                    std::cerr << "Failed to send data, error: " << error << std::endl;

                    outData.clear();

                    return false;
                }
            }
            else if (size != static_cast<int>(outData.size()))
            {
                std::cerr << "Failed to send all data" << std::endl;
            }

            if (size)
            {
                outData.erase(outData.begin(), outData.begin() + size);

#ifdef DEBUG
                std::cout << "Socket sent " << size << " bytes" << std::endl;
#endif
            }
        }

        return true;
    }

    bool Socket::disconnected()
    {
        if (ready)
        {
            std::cout << "Socket disconnected" << std::endl;

            ready = false;

            if (closeCallback)
            {
                closeCallback();
            }
        }

        return true;
    }
}
