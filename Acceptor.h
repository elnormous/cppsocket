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
        Acceptor(Network& aNetwork);
        virtual ~Acceptor();

        Acceptor(Acceptor&& other);
        Acceptor& operator=(Acceptor&& other);

        bool startAccept(const std::string& address, uint16_t newPort = 0);
        bool startAccept(uint32_t address, uint16_t newPort);
        void setAcceptCallback(const std::function<void(Socket&)>& newAcceptCallback);

    protected:
        virtual bool read();

        std::function<void(Socket&)> acceptCallback;
    };
}
