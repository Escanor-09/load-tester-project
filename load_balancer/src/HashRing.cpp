#include "HashRing.h"
#include <set>

HashRing::HashRing(size_t num_vnodes) : num_vnodes_(num_vnodes)
{
}

uint32_t HashRing::hash_string(const std::string &key) const
{
    std::hash<std::string> hasher;
    return static_cast<uint32_t>(hasher(key));
}

bool HashRing::empty() const
{
    std::lock_guard<std::mutex> lock(ring_mutex_);
    return ring_.empty();
}

void HashRing::add_node(const Backend &node)
{
    std::lock_guard<std::mutex> lock(ring_mutex_);

    std::string node_id = node.ip + ":" + std::to_string(node.port);

    for (size_t i = 0; i < num_vnodes_; i++)
    {
        std::string vnode_key = node_id + "#vnode_" + std::to_string(i);
        uint32_t hash = hash_string(vnode_key);
        ring_[hash] = node;
    }
}

void HashRing::remove_node(const Backend &node)
{
    std::lock_guard<std::mutex> lock(ring_mutex_);

    std::string node_id = node.ip + ":" + std::to_string(node.port);

    for (size_t i = 0; i < num_vnodes_; i++)
    {
        std::string vnode_key = node_id + "#vnode_" + std::to_string(i);
        uint32_t hash = hash_string(vnode_key);
        ring_.erase(hash);
    }
}

std::vector<Backend> HashRing::getNodes(const std::string &key, size_t replication_factor) const
{
    std::lock_guard<std::mutex> lock(ring_mutex_);
    std::vector<Backend> result;

    if (ring_.empty())
    {
        return result;
    }

    uint32_t key_hash = hash_string(key);

    auto it = ring_.lower_bound(key_hash);

    std::set<std::string> seen_nodes;
    std::set<std::string> unique_backends;

    for (const auto &[hash, backend] : ring_)
    {
        unique_backends.insert(backend.ip + " :" + std::to_string(backend.port));
    }

    while (result.size() < replication_factor && seen_nodes.size() < unique_backends.size())
    {
        if (it == ring_.end())
        {
            it = ring_.begin();
        }

        const Backend &current_backend = it->second;
        std::string backend_id = current_backend.ip + ":" + std::to_string(current_backend.port);

        if (seen_nodes.find(backend_id) == seen_nodes.end())
        {
            seen_nodes.insert(backend_id);
            result.push_back(current_backend);
        }
        ++it;
    }
    return result;
}