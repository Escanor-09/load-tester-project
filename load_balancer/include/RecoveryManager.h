#pragma once
#include <string>
#include <vector>
#include "Router.h"

class RecoveryManager
{
public:
    RecoveryManager(Router &router);

    // Triggered by the HealthChecker when a node wakes up
    void recover(const Backend &recovered_backend);

private:
    Router &router_;

    // Internal network helpers
    std::vector<std::string> fetchAllKeysFromNode(const Backend &backend);
    std::string fetchValueFromPeer(const Backend &peer, const std::string &key);
    bool writeValueToRecoveredNode(const Backend &target, const std::string &key, const std::string &value);
};
