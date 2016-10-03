//
//  cppsocket
//

#pragma once

#include <vector>
#include <functional>
#include <cstdint>

#ifdef _MSC_VER
#define NOMINMAX
#include <winsock2.h>
typedef SOCKET socket_t;
#else
typedef int socket_t;
#define INVALID_SOCKET -1
#endif

namespace cppsocket
{
    const uint32_t ANY_ADDRESS = 0;

    inline std::string ipToString(uint32_t ip)
    {
        uint8_t* ptr = reinterpret_cast<uint8_t*>(&ip);

        return std::to_string(static_cast<uint32_t>(ptr[0])) + "." +
        std::to_string(static_cast<uint32_t>(ptr[1])) + "." +
        std::to_string(static_cast<uint32_t>(ptr[2])) + "." +
        std::to_string(static_cast<uint32_t>(ptr[3]));
    }

    inline int getLastError()
    {
#ifdef _MSC_VER
        return WSAGetLastError();
#else
        return errno;
#endif
    }

    class Network;

    class Socket
    {
        friend Network;
    public:
        Socket(Network& pNetwork, socket_t pSocketFd = INVALID_SOCKET);
        virtual ~Socket();

        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;

        Socket(Socket&& other);
        Socket& operator=(Socket&& other);

        bool close();
        virtual void update(float delta);

        bool startRead();
        void setReadCallback(const std::function<void(const std::vector<uint8_t>&)>& newReadCallback);
        void setCloseCallback(const std::function<void()>& newCloseCallback);

        bool send(std::vector<uint8_t> buffer);

        uint32_t getIPAddress() const { return ipAddress; }
        uint16_t getPort() const { return port; }

        bool isBlocking() const { return blocking; }
        bool setBlocking(bool newBlocking);

        bool isReady() const { return ready; }

        bool hasOutData() const { return !outData.empty(); }

    protected:
        virtual bool read();
        virtual bool write();
        virtual bool disconnected();

        bool createSocketFd();
        bool setFdBlocking(bool block);

        Network& network;

        socket_t socketFd = INVALID_SOCKET;

        bool ready = false;
        bool blocking = true;

        uint32_t ipAddress = 0;
        uint16_t port = 0;

        std::function<void(const std::vector<uint8_t>&)> readCallback;
        std::function<void()> closeCallback;

        std::vector<uint8_t> inData;
        std::vector<uint8_t> outData;
    };
}
