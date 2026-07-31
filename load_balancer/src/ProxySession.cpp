#include "ProxySession.h"
#include "HttpParser.h"
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>
#include <iostream>

ProxySession::ProxySession(Router &router) : router_(router), backendSocket_(-1) {}

void ProxySession::start(int clientSocket)
{
    forwardTraffic(clientSocket);
}

bool ProxySession::connectToBackend(const Backend &backend)
{
    // Re-use active connection if already connected to this backend
    if (backendSocket_ != -1)
        return true;

    backendSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (backendSocket_ < 0)
        return false;

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(backend.port);
    inet_pton(AF_INET, backend.ip.c_str(), &addr.sin_addr);

    if (connect(backendSocket_, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(backendSocket_);
        backendSocket_ = -1;
        return false;
    }
    return true;
}

HttpResponseParseResult ProxySession::readBackendResponse()
{
    char rawBuffer[4096];
    std::string responseBuffer;

    while (true)
    {
        HttpResponseParseResult parseResult = parseHTTPResponse(responseBuffer);

        if (parseResult.status == HttpParseStatus::COMPLETE)
        {
            return parseResult;
        }
        if (parseResult.status == HttpParseStatus::ERROR)
        {
            return parseResult;
        }

        std::cout << "Waiting backend recv.." << std::endl;

        // Status is INCOMPLETE: Read more bytes from backend socket
        ssize_t bytesRead = recv(backendSocket_, rawBuffer, sizeof(rawBuffer), 0);

        std::cout << "Received " << bytesRead << " bytes\n";
        if (bytesRead <= 0)
        {
            parseResult.status = HttpParseStatus::ERROR;
            parseResult.error = "Backend connection closed or error";
            return parseResult;
        }

        responseBuffer.append(rawBuffer, bytesRead);
        std::cout << responseBuffer << std::endl;
    }
}

void ProxySession::forwardTraffic(int clientSocket)
{
    char rawBuffer[4096];
    std::string clientBuffer;

    // Outer Loop: Persistent connection with client
    while (true)
    {
        ssize_t bytesRead = recv(clientSocket, rawBuffer, sizeof(rawBuffer), 0);

        if (bytesRead <= 0)
            break; // Client disconnected or timed out

        clientBuffer.append(rawBuffer, bytesRead);
        std::cout << "Received from client:\n";
        std::cout << clientBuffer << "\n";

        // Inner Loop: Parse and process all complete requests in clientBuffer
        while (!clientBuffer.empty())
        {
            HttpParseResult result = parseHTTPRequest(clientBuffer);
            std::cout << "Parse Status: " << static_cast<int>(result.status) << "\n";
            std::cout << "ClientBuffer size " << clientBuffer.size() << "\n";

            if (result.status == HttpParseStatus::INCOMPLETE)
                break;
            if (result.status == HttpParseStatus::ERROR)
                return;

            HttpRequest request = result.request;
            std::string key = request.getKey();

            bool isWriteRequest = (request.method == "POST" || request.method == "PUT" || request.method == "DELETE");
            std::vector<Backend> candidates;

            if (isWriteRequest)
            {
                candidates = router_.getWriteBackendsForKey(key, 2);
            }
            else
            {
                candidates = router_.getReadBackendsForKey(key, 2);
            }

            if (candidates.empty())
            {
                std::string errResp = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 21\r\n\r\n503 Service Unavailable";
                sendAll(clientSocket, errResp.c_str(), errResp.size());
                return;
            }

            if (isWriteRequest)
            {
                bool atLeastOneSuccess = false;
                std::string primaryResponse;

                std::string requestPayload = request.rawRequest;
                if (request.method == "PUT")
                {
                    size_t pos = requestPayload.find("\r\n");
                    if (pos != std::string::npos)
                    {
                        requestPayload.insert(pos + 2, "X-Internal-Replication: true\r\n");
                    }
                }

                for (const auto &backend : candidates)
                {
                    std::cout << "[Router] Replicating Write for key: " << key << " -> Backend Port " << backend.port
                              << "\n";
                    if (connectToBackend(backend))
                    {
                        sendAll(backendSocket_, requestPayload.c_str(), requestPayload.size());

                        HttpResponseParseResult respResult = readBackendResponse();
                        std::cout << "Returned from readBackendResponse()\n";

                        if (respResult.status == HttpParseStatus::COMPLETE)
                        {
                            int current_status = respResult.response.statusCode;
                            if (primaryResponse.empty() || current_status == 200 || current_status == 201)
                            {
                                primaryResponse = respResult.response.rawResponse;
                            }
                            atLeastOneSuccess = true;
                        }
                        closeBackend();
                    }
                    else
                    {
                        std::cerr << "[Failover] Backend port " << backend.port << " failed. Marking node DOWN.\n";
                        router_.markBackendDown(backend.port);
                    }
                }

                if (atLeastOneSuccess)
                {
                    std::cout << "Sending response back to the client\n";
                    std::cout << "ClientSocket = " << clientSocket << "\n";
                    std::cout << "Response : " << primaryResponse << "\n";
                    sendAll(clientSocket, primaryResponse.c_str(), primaryResponse.size());
                    std::cout << "Response sent to the client";
                }
                else
                {
                    std::string errResp = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 21\r\n\r\n503 Service Unavailable";
                    sendAll(clientSocket, errResp.c_str(), errResp.size());
                }
            }
            else
            {
                bool connected = false;

                for (const auto &backend : candidates)
                {

                    std::cout << "[Router] Attempting Key: " << key << " -> Backend Port " << backend.port << "\n";

                    if (connectToBackend(backend))
                    {
                        sendAll(backendSocket_, request.rawRequest.c_str(), request.rawRequest.size());
                        std::cout << "Request forwarded to backend\n";
                        HttpResponseParseResult respResult = readBackendResponse();

                        if (respResult.status == HttpParseStatus::COMPLETE)
                        {
                            connected = true;
                            sendAll(clientSocket, respResult.response.rawResponse.c_str(), respResult.response.rawResponse.size());
                            closeBackend();
                            break;
                        }
                        closeBackend();
                    }
                    std::cerr << "[Failover] failed on port " << backend.port << ". Marking node DOWN and trying successor...\n";
                    router_.markBackendDown(backend.port);
                }

                if (!connected)
                {
                    std::string errResp = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 21\r\n\r\n503 Service Unavailable";
                    sendAll(clientSocket, errResp.c_str(), errResp.size());
                    return;
                }
            }
            clientBuffer.erase(0, result.consumed);
        }
    }
}

bool ProxySession::sendAll(int socket, const char *data, size_t length)
{
    size_t totalSent = 0;
    while (totalSent < length)
    {
        ssize_t sent = send(socket, data + totalSent, length - totalSent, 0);

        if (sent <= 0)
            return false;

        totalSent += sent;
    }
    return true;
}

void ProxySession::closeBackend()
{
    if (backendSocket_ != -1)
    {
        close(backendSocket_);
        backendSocket_ = -1;
    }
}