#include "HttpParser.h"
#include <charconv>
#include <algorithm>
#include <cctype>
#include <sstream>

static long long getContentLength(const std::string &headers)
{
    std::string_view sv(headers);

    size_t idx = sv.find("Content-Length:");
    if (idx == std::string_view::npos)
    {
        idx = sv.find("content-length:");
        if (idx == std::string_view::npos)
        {
            return 0;
        }
    }

    idx += 15;

    while (idx < sv.length() && (sv[idx] == ' ' || sv[idx] == '\t'))
        idx++;

    long long length = 0;
    const char *start = sv.data() + idx;
    const char *end = sv.data() + sv.length();

    auto [ptr, ec] = std::from_chars(start, end, length);
    if (ec == std::errc{})
    {
        return length;
    }
    return 0;
}

static HttpParseResult makeComplete(const HttpRequest &request, size_t consumed)
{
    HttpParseResult parseResult;

    parseResult.status = HttpParseStatus::COMPLETE;
    parseResult.request = request;
    parseResult.consumed = consumed;

    return parseResult;
}

static HttpParseResult makeIncomplete()
{
    HttpParseResult parseResult;
    parseResult.status = HttpParseStatus::INCOMPLETE;
    return parseResult;
}

static HttpParseResult makeError(const std::string &error)
{
    HttpParseResult parseResult;
    parseResult.status = HttpParseStatus::ERROR;
    parseResult.error = error;
    return parseResult;
}

HttpParseResult parseHTTPRequest(const std::string &buffer)
{
    size_t headerEnd = buffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
    {
        return makeIncomplete();
    }

    size_t headerLength = headerEnd + 4;

    std::string headerSection = buffer.substr(0, headerEnd);

    size_t lineEnd = headerSection.find("\r\n");
    if (lineEnd == std::string::npos)
    {
        return makeError("Malformed HTTP Request Line");
    }

    std::string requestLine = headerSection.substr(0, lineEnd);
    size_t space1 = requestLine.find(' ');
    size_t space2 = requestLine.find(' ', space1 + 1);

    if (space1 == std::string::npos || space2 == std::string::npos)
    {
        return makeError("Invalid request lineStructure");
    }

    HttpRequest request;
    request.method = requestLine.substr(0, space1);
    request.path = requestLine.substr(space1 + 1, space2 - space1 - 1);
    request.version = requestLine.substr(space2 + 1);

    long long expectedContentLength = getContentLength(headerSection);
    if (expectedContentLength < 0)
    {
        return makeError("Invalid Content-Length header");
    }

    size_t totalRequiredLength = headerLength + static_cast<size_t>(expectedContentLength);

    if (buffer.length() < totalRequiredLength)
    {
        return makeIncomplete();
    }

    request.rawRequest = buffer.substr(0, totalRequiredLength);
    request.body = buffer.substr(headerLength, expectedContentLength);

    return makeComplete(request, totalRequiredLength);
}

HttpResponseParseResult parseHTTPResponse(const std::string &buffer)
{
    HttpResponseParseResult result;
    result.status = HttpParseStatus::INCOMPLETE;

    size_t headerEnd = buffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
    {
        return result;
    }

    std::string headerPart = buffer.substr(0, headerEnd);
    std::istringstream stream(headerPart);
    std::string line;

    if (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::istringstream statusLine(line);
        statusLine >> result.response.version >> result.response.statusCode;
        std::getline(statusLine, result.response.statusText);
        if (!result.response.statusText.empty() && result.response.statusText[0] == ' ')
        {
            result.response.statusText.erase(0, 1);
        }
    }

    size_t contentLength = 0;
    bool contentLengthFound = false;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            break;

        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos)
        {
            std::string headerName = line.substr(0, colonPos);
            std::string headerValue = line.substr(colonPos + 1);

            headerValue.erase(0, headerValue.find_first_not_of(" \t"));

            std::string lowerName = headerName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

            if (lowerName == "content-length")
            {
                try
                {
                    contentLength = std::stoul(headerValue);
                    contentLengthFound = true;
                }
                catch (...)
                {
                    contentLength = 0;
                }
            }
            result.response.headers[headerName] = headerValue;
        }
    }

    if (!contentLengthFound)
    {
        contentLength = buffer.size() - (headerEnd + 4);
    }

    size_t totalExpectedLength = headerEnd + 4 + contentLength;
    if (buffer.size() < totalExpectedLength)
    {
        return result;
    }

    result.response.body = buffer.substr(headerEnd + 4, contentLength);
    result.response.rawResponse = buffer.substr(0, totalExpectedLength);
    result.consumed = totalExpectedLength;
    result.status = HttpParseStatus::COMPLETE;
    return result;
}
