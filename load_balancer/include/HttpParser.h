#pragma once
#include <string>
#include <unordered_map>

enum class HttpParseStatus
{
    COMPLETE,
    INCOMPLETE,
    ERROR
};

struct HttpRequest
{
    std::string method; // GET, POST, PUT, DELETE
    std::string path;
    std::string version;

    std::unordered_map<std::string, std::string> headers;

    std::string body;
    std::string rawRequest;

    std::string getKey() const
    {
        if (path == "/kvstore/create" && !body.empty())
        {
            size_t spacePos = body.find(' ');
            if (spacePos != std::string::npos)
            {
                return body.substr(0, spacePos);
            }
            return body;
        }

        size_t lastSlash = path.find_last_of('/');
        if (lastSlash != std::string::npos && lastSlash + 1 < path.length())
        {
            return path.substr(lastSlash + 1);
        }
        return path;
    }
};

struct HttpParseResult
{
    HttpParseStatus status;
    HttpRequest request;
    std::string error;
    size_t consumed = 0;
};

struct HttpResponse
{
    std::string version;
    int statusCode = 0;
    std::string statusText;

    std::unordered_map<std::string, std::string> headers;

    std::string body;
    std::string rawResponse;
};

struct HttpResponseParseResult
{
    HttpParseStatus status;
    HttpResponse response;
    std::string error;
    size_t consumed = 0;
};

HttpParseResult parseHTTPRequest(const std::string &buffer);

HttpResponseParseResult parseHTTPResponse(const std::string &buffer);