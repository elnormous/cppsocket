//
//  cppsocket
//

#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>
#include "Network.h"
#include "Acceptor.h"
#include "Connector.h"

static void printUsage(const std::string& executable)
{
    std::cout << "Usage: " << executable << " [server|client] [port|address]";
}

int main(int argc, const char* argv[])
{
    if (argc < 3)
    {
        printUsage(argc ? argv[0] : "test");
        return 0;
    }

    std::string type = argv[1];
    std::string address = argv[2];

    cppsocket::Network network;
    cppsocket::Acceptor server(network);
    cppsocket::Connector client(network);

    if (type == "server")
    {
        std::istringstream buffer(address);
        uint16_t port;
        buffer >> port;

        server.setBlocking(false);
        server.startAccept(port);
    }
    else if (type == "client")
    {
        client.setBlocking(false);
        client.setConnectTimeout(2.0f);
        client.connect(address);
    }

    const std::chrono::microseconds sleepTime(10000);

    for (;;)
    {
        network.update();

        std::this_thread::sleep_for(sleepTime);
    }

    return 0;
}
