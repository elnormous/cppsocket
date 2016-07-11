//
//  rtmp_relay
//

#pragma once

#include <vector>
#include <functional>

#ifdef _MSC_VER
#include <winsock2.h>
typedef SOCKET socket_t;
#else
typedef int socket_t;
#define INVALID_SOCKET -1
#endif

class Network;

class Socket
{
    friend Network;
public:
    Socket(Network& pNetwork, socket_t pSocketFd = -1);
    virtual ~Socket();
    
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    
    Socket(Socket&& other);
    Socket& operator=(Socket&& other);

    bool close();
    
    bool connect(const std::string& address, uint16_t newPort = 0);
    bool connect(uint32_t address, uint16_t newPort);
    bool disconnect();
    
    bool startRead();
    void setConnectCallback(const std::function<void()>& newConnectCallback);
    void setReadCallback(const std::function<void(const std::vector<uint8_t>&)>& newReadCallback);
    void setCloseCallback(const std::function<void()>& newCloseCallback);
    
    bool send(std::vector<uint8_t> buffer);
    
    uint32_t getIPAddress() const { return ipAddress; }
    uint16_t getPort() const { return port; }
    
    bool isBlocking() const { return blocking; }
    bool setBlocking(bool newBlocking);
    
    bool isConnecting() const { return connecting; }
    bool isReady() const { return ready; }

    bool hasOutData() const { return !outData.empty(); }
    
protected:
    virtual bool read();
    virtual bool write();
    virtual bool disconnected();
    
    Network& network;
    
    socket_t socketFd = INVALID_SOCKET;
    
    bool connecting = false;
    bool ready = false;
    bool blocking = true;

    uint32_t ipAddress = 0;
    uint16_t port = 0;
    
    std::function<void()> connectCallback;
    std::function<void(const std::vector<uint8_t>&)> readCallback;
    std::function<void()> closeCallback;

    std::vector<uint8_t> inData;
    std::vector<uint8_t> outData;
};
