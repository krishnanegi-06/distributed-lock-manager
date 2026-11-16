cmake_minimum_required(VERSION 3.16)
project(DistributedLockManager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------- Core library: every reusable component ----------
add_library(dlm_core
    # Common
    src/common/Logger.cpp
    src/common/Hash.cpp
    src/common/Platform.cpp

    # Lock manager
    src/lock_manager/LockManager.cpp

    # Deadlock detection
    src/deadlock/DeadlockDetector.cpp

    # Network
    src/network/Message.cpp
    src/network/NetworkManager.cpp
    src/network/HeartbeatManager.cpp

    # Coordinator (2PC)
    src/coordinator/TwoPCCoordinator.cpp

    # Transaction
    src/transaction/Transaction.cpp
    src/transaction/TransactionLog.cpp
    src/transaction/TransactionManager.cpp

    # Storage
    src/storage/StorageEngine.cpp

    # SQL-like parser
    src/sql/CommandParser.cpp

    # API & Security
    src/api/AuthManager.cpp
    src/api/ClusterClient.cpp
    src/api/HttpWsServer.cpp
    src/api/ApiServer.cpp

    # Node
    src/node/Node.cpp
)
target_include_directories(dlm_core PUBLIC
    include
    third_party
)

# Link Winsock & BCrypt on Windows, pthread on POSIX
if(WIN32)
    target_link_libraries(dlm_core PUBLIC ws2_32 bcrypt)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(dlm_core PUBLIC Threads::Threads)
endif()

# ---------- Executables ----------

# Single-node launcher (CLI)
add_executable(dlm_node src/node_main.cpp)
target_link_libraries(dlm_node PRIVATE dlm_core)

# Application API & WebSocket Gateway
add_executable(dlm_api src/api_main.cpp)
target_link_libraries(dlm_api PRIVATE dlm_core)

# Multi-node simulation
add_executable(dlm_simulation src/simulation_main.cpp)
target_link_libraries(dlm_simulation PRIVATE dlm_core)

# ---------- Tests ----------

add_executable(dlm_tests tests/test_all.cpp)
target_link_libraries(dlm_tests PRIVATE dlm_core)

# GoogleTest (vendored offline in third_party/googletest)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)

add_executable(dlm_gtest tests/gtest_suite.cpp)
target_link_libraries(dlm_gtest PRIVATE dlm_core gtest_main)

# Benchmarks executable
add_executable(dlm_benchmark src/benchmark_main.cpp)
target_link_libraries(dlm_benchmark PRIVATE dlm_core)

enable_testing()
add_test(NAME unit_tests COMMAND dlm_tests)
add_test(NAME comprehensive_gtest COMMAND dlm_gtest)
