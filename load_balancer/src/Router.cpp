#include "Router.h"
#include <functional>
#include <stdexcept>
#include <algorithm>

Router::Router(const std::vector<Backend> &backends, size_t vnodes_per_backend) : hash_ring_(vnodes_per_backend)
{
    if (backends.empty())
    {
        throw std::invalid_argument("Router requires at least one backend node!");
    }

    for (const auto &backend : backends)
    {
        backends_[backend.port] = backend;
        hash_ring_.add_node(backend);
    }
}

// Keep this only for permanent administrative removals
void Router::removeBackend(const Backend &backend)
{
    hash_ring_.remove_node(backend);
    backends_.erase(backend.port);
}

std::vector<Backend> Router::getReadBackendsForKey(const std::string &key, size_t replication_factor) const
{
    if (hash_ring_.empty())
    {
        throw std::runtime_error("No available backend servers in HashRing!");
    }

    std::vector<Backend> prospective_nodes = hash_ring_.getNodes(key, replication_factor);
    std::vector<Backend> read_targets;

    for (const auto &node : prospective_nodes)
    {
        auto it = backends_.find(node.port);
        // Only serve reads if the backend status is explicitly UP
        if (it != backends_.end() && it->second.status == BackendStatus::UP)
        {
            read_targets.push_back(it->second);
        }
    }
    return read_targets;
}

std::vector<Backend> Router::getWriteBackendsForKey(const std::string &key, size_t replication_factor) const
{
    if (hash_ring_.empty())
    {
        throw std::runtime_error("No available backend servers in HashRing!");
    }

    std::vector<Backend> prospective_nodes = hash_ring_.getNodes(key, replication_factor);
    std::vector<Backend> write_targets;

    for (const auto &node : prospective_nodes)
    {
        auto it = backends_.find(node.port);
        // Serve writes if UP or RECOVERING
        if (it != backends_.end() &&
            (it->second.status == BackendStatus::UP || it->second.status == BackendStatus::RECOVERING))
        {
            write_targets.push_back(it->second);
        }
    }
    return write_targets;
}

std::vector<Backend> Router::getBackendsForKey(const std::string &key, size_t replication_factor) const
{
    if (hash_ring_.empty())
    {
        throw std::runtime_error("No available backend servers in HashRing!");
    }
    return hash_ring_.getNodes(key, replication_factor);
}

void Router::markBackendRecovering(int port)
{
    auto it = backends_.find(port);
    if (it == backends_.end())
        return;

    it->second.status = BackendStatus::RECOVERING;
    // Add back to the hash ring so it receives live writes and ownership lookups succeed
    hash_ring_.add_node(it->second);
}

void Router::markBackendUp(int port)
{
    auto it = backends_.find(port);
    if (it == backends_.end())
        return;

    it->second.status = BackendStatus::UP;
    // Ensure it exists on the ring (idempotent call)
    hash_ring_.add_node(it->second);
}

void Router::markBackendDown(int port)
{
    auto it = backends_.find(port);
    if (it == backends_.end())
        return;

    it->second.status = BackendStatus::DOWN;
    // Remove from ring so live traffic bypasses it immediately
    hash_ring_.remove_node(it->second);
}

std::vector<Backend> Router::getOnlineBackends() const
{
    std::vector<Backend> result;
    for (const auto &[port, backend] : backends_)
    {
        if (backend.status == BackendStatus::UP)
        {
            result.push_back(backend);
        }
    }
    return result;
}

std::vector<Backend> Router::getAllBackends() const
{
    std::vector<Backend> result;
    for (const auto &[port, backend] : backends_)
    {
        result.push_back(backend);
    }
    return result;
}
