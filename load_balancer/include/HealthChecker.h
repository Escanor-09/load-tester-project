#include "Router.h"
#include "RecoveryManager.h"
#include <thread>
#include <atomic>
class HealthChecker
{
public:
    HealthChecker(Router &router, RecoveryManager &recovery_manager);

    void start();
    void stop();

private:
    void run();
    bool pingBackend(const Backend &backend);
    RecoveryManager &recovery_manager_;
    Router &router_;
    std::thread worker_;
    std::atomic<bool> running_;
};