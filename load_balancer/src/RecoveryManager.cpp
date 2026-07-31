#include "RecoveryManager.h"
#include "httplib.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <set>

RecoveryManager::RecoveryManager(Router &router) : router_(router) {}

std::vector<std::string> RecoveryManager::fetchAllKeysFromNode(const Backend &backend)
{
    std::vector<std::string> keys;
    httplib::Client cli(backend.ip, backend.port);
    cli.set_connection_timeout(0, 500000); // 500ms timeout

    auto res = cli.Get("/kvstore/allkeys");

    if (!res)
    {
        // std::cout << "[FETCH] Request to port "
        //           << backend.port
        //           << " failed\n";
    }
    else
    {
        // std::cout << "[FETCH] Port "
        //           << backend.port
        //           << " Status: "
        //           << res->status
        //           << "\n";

        // std::cout << "[FETCH] Body:\n"
        //           << res->body
        //           << "\n";

        if (res->status == 200)
        {
            std::istringstream stream(res->body);
            std::string key;
            while (std::getline(stream, key))
            {
                if (!key.empty())
                {
                    keys.push_back(key);
                }
            }
        }
    }

    // std::cout << "[FETCH] Parsed " << keys.size()
    //           << " keys from port "
    //           << backend.port << "\n";
    return keys;
}

std::string RecoveryManager::fetchValueFromPeer(const Backend &peer, const std::string &key)
{
    httplib::Client cli(peer.ip, peer.port);
    cli.set_connection_timeout(0, 500000);

    if (auto res = cli.Get(("/kvstore/" + key).c_str()))
    {
        if (res->status == 200)
        {
            return res->body;
        }
    }
    return "";
}

bool RecoveryManager::writeValueToRecoveredNode(const Backend &target, const std::string &key, const std::string &value)
{
    httplib::Client cli(target.ip, target.port);
    cli.set_connection_timeout(0, 500000);

    // Call the safe, non-destructive /kvstore/sync route
    if (auto res = cli.Post(("/kvstore/sync/" + key).c_str(), value, "text/plain"))
    {
        return (res->status == 200 || res->status == 201);
    }
    return false;
}

void RecoveryManager::recover(const Backend &recovered_backend)
{
    // std::cout << "\n========================================\n";
    // std::cout << "Backend port " << recovered_backend.port << " is UP\n";
    // std::cout << "Recovery started\n";

    // FIX: Scan all online nodes to build a comprehensive deduplicated keyspace map
    std::vector<Backend> active_backends = router_.getOnlineBackends();
    std::set<std::string> global_keyspace;

    // std::cout << "Recovered node: " << recovered_backend.port << "\n";
    // std::cout << "Online nodes:\n";

    for (const auto &node : active_backends)
    {
        std::cout << "  Port " << node.port << "\n";

        if (node.port == recovered_backend.port)
            continue;

        // std::cout << "Fetching keys from node " << node.port << "\n";

        std::vector<std::string> node_keys = fetchAllKeysFromNode(node);

        // std::cout << "Received " << node_keys.size() << " keys\n";

        for (const auto &k : node_keys)
        {
            // std::cout << "    " << k << "\n";
            global_keyspace.insert(k);
        }
    }

    // std::cout << "Global keyspace size = " << global_keyspace.size() << "\n";

    if (global_keyspace.empty())
    {
        // std::cout << "No keys found across active online nodes. Recovery skipped.\n";
        // std::cout << "Recovery finished\n";
        // std::cout << "========================================\n\n";
        return;
    }

    for (const auto &key : global_keyspace)
    {
        std::vector<Backend> target_replicas = router_.getBackendsForKey(key, 2);

        bool should_own = false;
        for (const auto &replica : target_replicas)
        {
            if (replica.port == recovered_backend.port)
            {
                should_own = true;
                break;
            }
        }

        if (!should_own)
        {
            continue;
        }

        // std::cout << "Copying " << key << "\n";

        std::string source_value = "";
        for (const auto &replica : target_replicas)
        {
            if (replica.port != recovered_backend.port)
            {
                source_value = fetchValueFromPeer(replica, key);
                if (!source_value.empty())
                {
                    break;
                }
            }
        }

        if (!source_value.empty())
        {
            writeValueToRecoveredNode(recovered_backend, key, source_value);
        }
        else
        {
            std::cerr << "Warning: Failed to fetch source value for key: " << key << "\n";
        }
    }

    // std::cout << "Recovery finished\n";
    // std::cout << "========================================\n\n";
}
