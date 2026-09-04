cmake_minimum_required(VERSION 3.16)
project(DistributedLockManager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core library: every reusable component (logging now, lock manager /
# transaction / network / deadlock / coordinator later) gets compiled
# into this once and linked into every executable.
add_library(dlm_core
    src/common/Logger.cpp
    src/lock_manager/LockManager.cpp
)
target_include_directories(dlm_core PUBLIC include)

add_executable(dlm_node src/node_main.cpp)
target_link_libraries(dlm_node PRIVATE dlm_core)