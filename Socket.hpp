//
//  cppsocket
//

#pragma once

#include <vector>
#include <functional>
#include <cstdint>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
typedef SOCKET socket_t;
#else
#include <errno.h>
typedef int socket_t;
#define INVALID_SOCKET -1
#endif

namespace cppsocket
{
    const uint32_t ANY_ADDRESS = 0;
    const uint16_t ANY_PORT = 0;

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
#ifdef _WIN32
        return WSAGetLastError();
#else
        return errno;
#endif
    }

    class Network;

    class Socket final
    {
        friend Network;
    public:
        static void getAddress(const std::string& address, std::pair<uint32_t, uint16_t>& result);

        Socket(Network& aNetwork);
        ~Socket();

        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;

        Socket(Socket&& other);
        Socket& operator=(Socket&& other);

        void close();
        void update(float delta);

        void startRead();

        void startAccept(const std::string& address);
        void startAccept(uint32_t address, uint16_t newPort);

        void connect(const std::string& address);
        void connect(uint32_t address, uint16_t newPort);

        bool isConnecting() const { return connecting; }
        void setConnectTimeout(float timeout);

        void setReadCallback(const std::function<void(Socket&, const std::vector<uint8_t>&)>& newReadCallback);
        void setCloseCallback(const std::function<void(Socket&)>& newCloseCallback);
        void setAcceptCallback(const std::function<void(Socket&, Socket&)>& newAcceptCallback);
        void setConnectCallback(const std::function<void(Socket&)>& newConnectCallback);
        void setConnectErrorCallback(const std::function<void(Socket&)>& newConnectErrorCallback);

        void send(std::vector<uint8_t> buffer);

        uint32_t getLocalIPAddress() const { return localIPAddress; }
        uint16_t getLocalPort() const { return localPort; }

        uint32_t getRemoteIPAddress() const { return remoteIPAddress; }
        uint16_t getRemotePort() const { return remotePort; }

        bool isBlocking() const { return blocking; }
        void setBlocking(bool newBlocking);

        bool isReady() const { return ready; }

        bool hasOutData() const { return !outData.empty(); }

    protected:
        Socket(Network& aNetwork, socket_t aSocketFd, bool aReady,
               uint32_t aLocalIPAddress, uint16_t aLocalPort,
               uint32_t aRemoteIPAddress, uint16_t aRemotePort);

        void read();
        void write();

        void readData();
        void writeData();

        void disconnected();

        void createSocketFd();
        void closeSocketFd();
        void setFdBlocking(bool block);

        Network& network;

        socket_t socketFd = INVALID_SOCKET;

        bool ready = false;
        bool blocking = true;

        uint32_t localIPAddress = 0;
        uint16_t localPort = 0;

        uint32_t remoteIPAddress = 0;
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
    };
}
