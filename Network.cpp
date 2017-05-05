//
//  cppsocket
//

#include <algorithm>
#include <chrono>
#ifdef _MSC_VER
#define NOMINMAX
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#endif
#include "Log.h"
#include "Network.h"
#include "Socket.h"

namespace cppsocket
{
    Network::Network()
    {
#ifdef _MSC_VER
        static bool ready = false;
        if (!ready)
        {
            WORD sockVersion = MAKEWORD(2, 2);
            WSADATA wsaData;
            int error = WSAStartup(sockVersion, &wsaData);
            if (error != 0)
            {
                Log(Log::Level::ERR) << "WSAStartup failed, error: " << error;
                return;
            }

            if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
            {
                Log(Log::Level::ERR) << "Incorrect Winsock version";
                WSACleanup();
                return;
            }

            ready = true;
        }
#endif

        previousTime = std::chrono::steady_clock::now();
    }

    bool Network::update()
    {
        for (Socket* socket : socketDeleteSet)
        {
            auto i = std::find(sockets.begin(), sockets.end(), socket);

            if (i != sockets.end())
            {
                sockets.erase(i);
            }
        }

        socketDeleteSet.clear();

        for (Socket* socket : socketAddSet)
        {
            auto i = std::find(sockets.begin(), sockets.end(), socket);

            if (i == sockets.end())
            {
                sockets.push_back(socket);
            }
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
#ifdef _MSC_VER
            if (WSAPoll(pollFds.data(), static_cast<ULONG>(pollFds.size()), 0) < 0)
#else
            if (poll(pollFds.data(), static_cast<nfds_t>(pollFds.size()), 0) < 0)
#endif
            {
                int error = getLastError();
                Log(Log::Level::ERR) << "Poll failed, error: " << error;
                return false;
            }

            for (pollfd& pollFd : pollFds)
            {
                auto iter = std::find_if(sockets.begin(), sockets.end(), [&pollFd](Socket* socket) {
                    return socket->socketFd == pollFd.fd;
                });

                if (iter != sockets.end())
                {
                    Socket* socket = *iter;

                    if (pollFd.revents & POLLIN)
                    {
                        socket->read();
                    }

                    if (pollFd.revents & POLLOUT)
                    {
                        socket->write();
                    }

                    socket->update(delta);
                }
            }
        }

        return true;
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
