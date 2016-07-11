//
//  rtmp_relay
//

#pragma once

#include <vector>
#include <memory>
#include "Socket.h"

class Network
{
    friend Socket;
public:
    static std::string ipToString(uint32_t ip);

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
};
