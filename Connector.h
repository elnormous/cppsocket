//
//  cppsocket
//

#pragma once

#include <functional>
#include "Socket.h"

namespace cppsocket
{
    class Connector: public Socket
    {
    public:
        Connector(Network& network);
        virtual ~Connector();

        Connector(Connector&& other);
        Connector& operator=(Connector&& other);

        virtual void update(float delta) override;

        bool connect(const std::string& address, uint16_t newPort = 0);
        bool connect(uint32_t address, uint16_t newPort);
        bool disconnect();

        bool isConnecting() const { return connecting; }
        void setConnectTimeout(float timeout);

        void setConnectCallback(const std::function<void()>& newConnectCallback);
        void setConnectErrorCallback(const std::function<void()>& newConnectErrorCallback);

    protected:
        virtual bool write() override;
        virtual bool disconnected() override;

        float connectTimeout = 10.0f;
        float timeSinceConnect = 0.0f;
        bool connecting = false;

        std::function<void()> connectCallback;
        std::function<void()> connectErrorCallback;
    };
}
