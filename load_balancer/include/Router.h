#pragma once
#include <vector>
#include <unordered_map>
#include "Backend.h"
#include "HashRing.h"
class Router
{
private:
    HashRing hash_ring_;
    std::unordered_map<int, Backend> backends_;

public:
    Router(const std::vector<Backend> &backends_, size_t vnodes_per_backend = 50);
    void removeBackend(const Backend &backend);

    Backend getBackendForKey(const std::string &key) const;
    std::vector<Backend> getBackendsForKey(const std::string &key, size_t replication_factor) const;
    std::vector<Backend> getReadBackendsForKey(const std::string &key, size_t replication_factor) const;
    std::vector<Backend> getWriteBackendsForKey(const std::string &key, size_t replicattion_factor) const;

    std::vector<Backend> getAllBackends() const;
    std::vector<Backend> getOnlineBackends() const;

    void markBackendRecovering(int port);
    void markBackendUp(int port);
    void markBackendDown(int port);
};