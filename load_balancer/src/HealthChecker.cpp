#include "HealthChecker.h"
#include "httplib.h"
#include "RecoveryManager.h"
#include <iostream>

HealthChecker::HealthChecker(Router &router, RecoveryManager &recovery_manager)
    : router_(router), recovery_manager_(recovery_manager), running_(false) {}

void HealthChecker::start()
{
    if (running_)
        return;
    running_ = true;
    worker_ = std::thread(&HealthChecker::run, this);
}

void HealthChecker::stop()
{
    running_ = false;
    if (worker_.joinable())
        worker_.join();
}

bool HealthChecker::pingBackend(const Backend &backend)
{
    httplib::Client client(backend.ip, backend.port);
    // Set a short timeout so a dead/lagging server doesn't freeze the health check loop
    client.set_connection_timeout(0, 500000); // 500ms
    auto res = client.Get("/health");
    return res && res->status == 200;
}

void HealthChecker::run()
{
    while (running_)
    {
        // Fetch real-time states from the router map
        auto backends = router_.getAllBackends();

        for (const auto &backend : backends)
        {
            bool alive = pingBackend(backend);

            if (alive)
            {
                // Handle transition from DOWN to ALIVE
                if (backend.status == BackendStatus::DOWN)
                {
                    // 1. Move to RECOVERING first (adds to HashRing for live client writes)
                    router_.markBackendRecovering(backend.port);

                    // 2. Synchronize missed keys in the background path
                    recovery_manager_.recover(backend);

                    // 3. Promote to UP to safely open up client reads (GET)
                    router_.markBackendUp(backend.port);
                }
            }
            else
            {
                // Handle transition from ONLINE/RECOVERING to DEAD
                if (backend.status == BackendStatus::UP || backend.status == BackendStatus::RECOVERING)
                {
                    std::cout << "[WARN] Backend port " << backend.port << " is dead. Marking DOWN.\n";
                    router_.markBackendDown(backend.port);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}
