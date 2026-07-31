#pragma once
#include "Router.h"
#include "HttpParser.h"
#include <string>

class ProxySession
{
private:
    Router &router_;
    int backendSocket_;

    bool connectToBackend(const Backend &backend);
    void closeBackend();
    HttpResponseParseResult readBackendResponse();
    bool sendAll(int socket, const char *data, size_t length);

public:
    ProxySession(Router &router);
    void start(int clientSocket);
    void forwardTraffic(int clientSocket);
};