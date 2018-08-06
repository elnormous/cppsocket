//
//  cppsocket
//

#include <algorithm>
#include <chrono>
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#endif
#include <stdexcept>
#include "Network.hpp"
#include "Socket.hpp"
#include "Log.hpp"

namespace cppsocket
{
    Network::Network()
    {
        previousTime = std::chrono::steady_clock::now();
    }

    void Network::update()
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
            if (socket->socketFd != INVALID_SOCKET)
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
#else
            if (poll(pollFds.data(), static_cast<nfds_t>(pollFds.size()), 0) < 0)
#endif
            {
                int error = getLastError();
                throw std::runtime_error("Poll failed, error: " + std::to_string(error));
            }

            for (pollfd& pollFd : pollFds)
            {
                for (Socket* deleteSocket : socketDeleteSet)
                {
                    auto i = std::find(sockets.begin(), sockets.end(), deleteSocket);

                    if (i != sockets.end())
                    {
                        sockets.erase(i);
                    }
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

    void Network::addSocket(Socket& socket)
    {
        socketAddSet.insert(&socket);

        auto setIterator = socketDeleteSet.find(&socket);

        if (setIterator != socketDeleteSet.end())
        {
            socketDeleteSet.erase(setIterator);
        }
    }

    void Network::removeSocket(Socket& socket)
    {
        socketDeleteSet.insert(&socket);

        auto setIterator = socketAddSet.find(&socket);

        if (setIterator != socketAddSet.end())
        {
            socketAddSet.erase(setIterator);
        }
    }
}
