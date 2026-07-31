#include "LoadBalancer.h"
#include "Backend.h"
#include "ProxySession.h"
#include "RecoveryManager.h" // Include new recovery component
#include "HealthChecker.h"   // Include new health checker component
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <iostream>
#include <thread>

LoadBalancer::LoadBalancer(int port) : port_(port), serverSocket_(-1)
{
}

void LoadBalancer::start()
{
    createSocket();
    bindSocket();
    listenSocket();
    std::cout << "Load Balancer listening on port " << port_ << std::endl;
    acceptClients();
}

void LoadBalancer::createSocket()
{
    serverSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket_ == -1)
    {
        perror("SOCKET CREATION FAILED");
        exit(EXIT_FAILURE);
    }
}

void LoadBalancer::bindSocket()
{
    struct sockaddr_in serverAddress;
    std::memset(&serverAddress, 0, sizeof(serverAddress));

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port_);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    // Enable SO_REUSEADDR so you don't get "Address already in use" errors during quick restarts
    int opt = 1;
    setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int bfd = bind(serverSocket_, (sockaddr *)&serverAddress, sizeof(serverAddress));

    if (bfd < 0)
    {
        perror("BINDING");
        close(serverSocket_);
        exit(EXIT_FAILURE);
    }
}

void LoadBalancer::listenSocket()
{
    int lfd = listen(serverSocket_, 3);
    if (lfd < 0)
    {
        perror("LISTENING");
        close(serverSocket_);
        exit(EXIT_FAILURE);
    }
}

void LoadBalancer::acceptClients()
{
    // 1. Define initial backends. Start them as UP so they can receive initial client data.
    std::vector<Backend> backends = {
        {"127.0.0.1", 9001, BackendStatus::UP},
        {"127.0.0.1", 9002, BackendStatus::UP},
        {"127.0.0.1", 9003, BackendStatus::UP}};

    // 2. Instantiate core routing components in the correct reference order
    Router router(backends);
    RecoveryManager recoveryManager(router);
    HealthChecker healthChecker(router, recoveryManager);

    // 3. Fire up the health checking background thread loop
    healthChecker.start();
    std::cout << "[SYSTEM] HealthChecker background loop active (Interval: 3s)\n";

    while (true)
    {
        struct sockaddr_in clientAddress;
        socklen_t addrLen = sizeof(clientAddress);

        int clientSocket = accept(serverSocket_, (sockaddr *)&clientAddress, &addrLen);
        if (clientSocket < 0)
        {
            perror("ACCEPT");
            continue;
        }

        std::cout << "Client Connected\n";

        // Pass the long-lived router reference cleanly down into proxy session worker threads
        std::thread([clientSocket, &router]()
                    {
                        ProxySession session(router);
                        session.start(clientSocket);
                        close(clientSocket); })
            .detach();
    }

    // Optional safety step: Stop health check thread if server ever breaks out of the loop
    healthChecker.stop();
}
