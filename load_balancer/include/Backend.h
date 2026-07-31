#pragma once

#include <string>
enum class BackendStatus
{
    UP,
    RECOVERING,
    DOWN
};

struct Backend
{
    std::string ip;
    int port;

    BackendStatus status = BackendStatus::UP;
};