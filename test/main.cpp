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
    cppsocket::Socket socket(network);

    if (type == "server")
    {
        cppsocket::Acceptor server(network);

        std::istringstream buffer(address);
        uint16_t port;

        server.startAccept(port);
        socket = std::move(server);
    }
    else if (type == "client")
    {
        cppsocket::Connector client(network);
        client.connect(address);
        socket = std::move(client);
    }

    const std::chrono::microseconds sleepTime(10000);

    for (;;)
    {
        network.update();

        std::this_thread::sleep_for(sleepTime);
    }

    return 0;
}
