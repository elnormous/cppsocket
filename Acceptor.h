//
//  cppsocket
//

#pragma once

#include <vector>
#include <functional>
#include "Socket.h"

namespace cppsocket
{
    class Acceptor: public Socket
    {
    public:
        Acceptor(Network& network);
        virtual ~Acceptor();

        Acceptor(Acceptor&& other);
        Acceptor& operator=(Acceptor&& other);

        bool startAccept(uint16_t newPort);
        void setAcceptCallback(const std::function<void(Socket&)>& newAcceptCallback);

    protected:
        virtual bool read();

        std::function<void(Socket&)> acceptCallback;
    };
}
