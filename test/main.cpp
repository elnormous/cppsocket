//
//  cppsocket
//

#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>
#include "Socket.hpp"

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
    cppsocket::Socket server(network);
    cppsocket::Socket client(network);
    std::vector<cppsocket::Socket> clientSockets;

    if (type == "server")
    {
        std::istringstream buffer(address);
        uint16_t port;
        buffer >> port;

        server.setBlocking(false);
        server.startAccept(cppsocket::ANY_ADDRESS, port);

        server.setAcceptCallback([&clientSockets](cppsocket::Socket&, cppsocket::Socket& c) {
            std::cout << "Client connected" << std::endl;
            c.startRead();
            c.send({'t', 'e', 's', 't', '\0'});
            c.setCloseCallback([&clientSockets](cppsocket::Socket& socket) {
                std::cout << "Client at " << cppsocket::ipToString(socket.getRemoteIPAddress()) << " disconnected" << std::endl;

                for (auto i = clientSockets.begin(); i != clientSockets.end();)
                {
                    if (&(*i) == &socket)
                    {
                        clientSockets.erase(i);
                        break;
                    }
                    else
                        ++i;
                }

            });
            clientSockets.push_back(std::move(c));
        });
    }
    else if (type == "client")
    {
        client.setBlocking(false);
        client.setConnectTimeout(2.0f);
        client.connect(address);

        client.setReadCallback([](cppsocket::Socket& socket, const std::vector<uint8_t>& data) {
            std::cout << "Got data: " << data.data() << " from " << cppsocket::ipToString(socket.getRemoteIPAddress()) << std::endl;
        });

        client.setConnectCallback([](cppsocket::Socket& socket) {
            std::cout << "Connected to " << cppsocket::ipToString(socket.getRemoteIPAddress()) << std::endl;

            socket.send({'t', 'e', 's', 't', '\0'});
        });

        client.setConnectErrorCallback([&client, address](cppsocket::Socket& socket) {
            std::cout << "Failed to connected to " << cppsocket::ipToString(socket.getRemoteIPAddress()) << std::endl;

            client.connect(address);
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
