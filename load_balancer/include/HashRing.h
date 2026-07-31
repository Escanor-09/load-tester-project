#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <map>
#include "Backend.h"

struct HashRing
{
public:
    // Constructor: number of virtual nodes
    HashRing(size_t num_vnodes = 50);

    // add a backend server to the hash ring
    void add_node(const Backend &node);

    // remove a backend server from hash ring
    void remove_node(const Backend &node);

    // get the primary server and the successor of that server
    std::vector<Backend> getNodes(const std::string &key, size_t replication_factor = 1) const;

    // helper to check if the ring is empty
    bool empty() const;

private:
    size_t num_vnodes_;

    std::map<uint32_t, Backend> ring_;

    mutable std::mutex ring_mutex_;

    uint32_t hash_string(const std::string &key) const;
};