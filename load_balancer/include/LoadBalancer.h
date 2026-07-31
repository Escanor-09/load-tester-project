#pragma once

class LoadBalancer
{
public:
    explicit LoadBalancer(int port);

    void start();

private:
    int port_;
    int serverSocket_;

    void createSocket();
    void bindSocket();
    void listenSocket();
    void acceptClients();
};