//
//  cppsocket
//

#pragma once

#include <errno.h>
#include <vector>
#include <map>
#include <memory>
#include <string>
#include <set>
#include <chrono>
#include "Socket.hpp"

namespace cppsocket
{
    class Network
    {
        friend Socket;
    public:
        Network();

        Network(const Network&) = delete;
        Network& operator=(const Network&) = delete;

        Network(Network&&) = delete;
        Network& operator=(Network&&) = delete;

        bool update();

    protected:
        void addSocket(Socket& socket);
        void removeSocket(Socket& socket);

        std::vector<Socket*> sockets;
        std::set<Socket*> socketAddSet;
        std::set<Socket*> socketDeleteSet;

        std::chrono::steady_clock::time_point previousTime;
    };
}
