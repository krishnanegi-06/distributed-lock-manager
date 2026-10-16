cmake_minimum_required(VERSION 3.16)
project(DistributedLockManager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------- Core library: every reusable component ----------
add_library(dlm_core
    # Common
    src/common/Logger.cpp

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

    # Node
    src/node/Node.cpp
)
target_include_directories(dlm_core PUBLIC include)

# Link Winsock on Windows
if(WIN32)
    target_link_libraries(dlm_core PUBLIC ws2_32)
endif()

# ---------- Executables ----------

# Single-node launcher (CLI)
add_executable(dlm_node src/node_main.cpp)
target_link_libraries(dlm_node PRIVATE dlm_core)

# Multi-node simulation
add_executable(dlm_simulation src/simulation_main.cpp)
target_link_libraries(dlm_simulation PRIVATE dlm_core)

# ---------- Tests ----------

add_executable(dlm_tests tests/test_all.cpp)
target_link_libraries(dlm_tests PRIVATE dlm_core)

# Enable CTest
enable_testing()
add_test(NAME unit_tests COMMAND dlm_tests)