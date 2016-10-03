//
//  cppsocket
//

#pragma once

#include <errno.h>
#include <vector>
#include <memory>
#include <string>
#include "Socket.h"

namespace cppsocket
{
    class Network
    {
        friend Socket;
    public:
        static std::string ipToString(uint32_t ip);
        static int getLastError()
        {
#ifdef _MSC_VER
            return WSAGetLastError();
#else
            return errno;
#endif
        }

        Network();

        Network(const Network&) = delete;
        Network& operator=(const Network&) = delete;

        Network(Network&&) = delete;
        Network& operator=(Network&&) = delete;

        bool update();

    protected:
        void addSocket(Socket& socket);
        void removeSocket(Socket& socket);

        std::vector<std::reference_wrapper<Socket>> sockets;

        std::chrono::steady_clock::time_point previousTime;
    };
}
