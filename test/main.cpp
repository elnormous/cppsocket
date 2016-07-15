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
    std::cout << "Usage: " << executable << " [server|client] [port|address]" << std::endl;
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

        server.setAcceptCallback([](cppsocket::Socket& clientSocket) {
            std::cout << "Client connected" << std::endl;
            clientSocket.send({'t', 'e', 's', 't'});
        });
    }
    else if (type == "client")
    {
        client.setBlocking(false);
        client.setConnectTimeout(2.0f);
        client.connect(address);

        client.setReadCallback([](const std::vector<uint8_t>& data) {
            std::cout << "Got data: " << data.data() << std::endl;
        });
    }

    const std::chrono::microseconds sleepTime(10000);

    for (;;)
    {
        network.update();

        std::this_thread::sleep_for(sleepTime);
    }

    return 0;
}
