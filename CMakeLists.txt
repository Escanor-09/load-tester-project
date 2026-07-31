cmake_minimum_required(VERSION 3.16)

project(KVStoreLoadBalancer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED libpqxx)

include_directories(
    ${PQXX_INCLUDE_DIRS}
    .
    load_balancer/include
)

add_executable(kvserver
    kvserver.cpp
)

target_link_libraries(kvserver
    ${PQXX_LIBRARIES}
    pthread
)

add_executable(kvclient
    kvclient.cpp
)

target_link_libraries(kvclient
    pthread
)

add_Executable(load_balancer
    load_balancer/src/main.cpp
    load_balancer/src/LoadBalancer.cpp
    load_balancer/src/ProxySession.cpp
    load_balancer/src/HttpHelper.cpp
    load_balancer/src/Router.cpp
    load_balancer/src/HashRing.cpp
    load_balancer/src/HealthChecker.cpp
    load_balancer/src/RecoveryManager.cpp
)

target_link_libraries(load_balancer
    pthread
)