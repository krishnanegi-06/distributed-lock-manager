# Chapter 3: Build & Execution Guide

This chapter walks through building and running the Distributed Lock Manager (DLM) from source code on Windows using Visual Studio and CMake, as well as on Linux platforms.

---

## 1. System Requirements & Prerequisites

Before building the system, ensure the following software tools are installed on your machine:

| Requirement | Supported Version | Purpose |
| :--- | :--- | :--- |
| **Operating System** | Windows 10/11 (or Linux / macOS) | Core OS for threading & network primitives |
| **C++ Compiler** | MSVC 19.30+ (VS 2022) / GCC 9+ / Clang 11+ | Must support C++17 (`std::shared_mutex`, `std::optional`, `<filesystem>`) |
| **Build System** | CMake 3.16 or higher | Cross-platform build script generator |
| **Platform SDK** | Windows SDK 10.0.19041+ (Windows) | Provides Winsock2 (`ws2_32.lib`) and socket APIs |

---

## 2. Directory Structure Before Build

Your project workspace has the following structure:

```
distribute_lock_manager/
├── CMakeLists.txt
├── README.md
├── include/
│   └── dlm/...
├── src/...
├── tests/
│   └── test_all.cpp
└── manual/...
```

---

## 3. Step-by-Step Build Instructions (Windows PowerShell)

### Step 3.1: Open Developer PowerShell or standard PowerShell
You can use standard PowerShell or Visual Studio Developer PowerShell. If using standard PowerShell, CMake automatically locates the MSVC compiler installed with Visual Studio 2022.

### Step 3.2: Create and Navigate to the `build` Directory
```powershell
# Navigate to the project root
cd c:\Users\yogyatanegi\Desktop\distribute_lock_manager

# Create build folder if it does not exist
if (-not (Test-Path build)) { New-Item -ItemType Directory -Name build }

# Switch into the build directory
cd build
```

### Step 3.3: Configure the Project with CMake
Run CMake to detect the compiler, configure includes, and generate the Visual Studio solution (`.sln`):

```powershell
cmake .. -G "Visual Studio 17 2022" -A x64
```

**Expected Terminal Output:**
```
-- Selecting Windows SDK version 10.0.22621.0 to target Windows 10.0.26200.
-- The CXX compiler identification is MSVC 19.43.34810.0
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: .../cl.exe - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Configuring done (1.2s)
-- Generating done (0.2s)
-- Build files have been written to: C:/Users/yogyatanegi/Desktop/distribute_lock_manager/build
```

### Step 3.4: Compile the Binaries
Compile the solution in **Debug** or **Release** configuration:

```powershell
cmake --build . --config Debug
```

**Expected Terminal Output:**
```
MSBuild version 17.13.19 for .NET Framework

  1>Checking Build System
  Logger.cpp
  LockManager.cpp
  DeadlockDetector.cpp
  Message.cpp
  NetworkManager.cpp
  HeartbeatManager.cpp
  TwoPCCoordinator.cpp
  Transaction.cpp
  TransactionLog.cpp
  TransactionManager.cpp
  Node.cpp
  dlm_core.vcxproj -> ...\build\Debug\dlm_core.lib
  node_main.cpp
  dlm_node.vcxproj -> ...\build\Debug\dlm_node.exe
  simulation_main.cpp
  dlm_simulation.vcxproj -> ...\build\Debug\dlm_simulation.exe
  test_all.cpp
  dlm_tests.vcxproj -> ...\build\Debug\dlm_tests.exe
```

Four distinct artifacts are generated in `build/Debug/`:
1. `dlm_core.lib`: The static core library containing all lock, network, transaction, and coordination logic.
2. `dlm_node.exe`: Standalone cluster node process.
3. `dlm_simulation.exe`: End-to-end multi-threaded and multi-node automated demonstration.
4. `dlm_tests.exe`: Automated test suite containing 17 unit and integration tests.

---

## 4. Running the Test Suite (`dlm_tests.exe`)

The test suite validates individual modules as well as multi-node integration.

```powershell
.\Debug\dlm_tests.exe
```

**Expected Output:**
```
==================================================
  DISTRIBUTED LOCK MANAGER - TEST SUITE
==================================================
[TEST 1] Shared Lock Acquisition ... PASS
[TEST 2] Exclusive Lock Acquisition ... PASS
[TEST 3] Multiple Shared Locks Allowed ... PASS
[TEST 4] Exclusive Blocks Shared ... PASS
[TEST 5] Release Lock Allows Blocked ... PASS
[TEST 6] Wait-For Edge Generation ... PASS
[TEST 7] Release All Locks ... PASS
[TEST 8] No Deadlock in DAG ... PASS
[TEST 9] Deadlock Cycle Detection ... PASS
[TEST 10] Youngest Victim Selected ... PASS
[TEST 11] Message Serialization & Deserialization ... PASS
[TEST 12] Network Loopback Communication ... PASS
[TEST 13] Transaction Lifecycle (Active/Commit/Abort) ... PASS
[TEST 14] Transaction WAL Append & Recovery ... PASS
[TEST 15] 2PC All-Commit Scenario ... PASS
[TEST 16] 2PC Abort When One Participant Fails ... PASS
[TEST 17] Full Node Integration Smoke Test ... PASS
==================================================
  SUMMARY: 17/17 TESTS PASSED (0 FAILED)
==================================================
```

---

## 5. Running the Simulation (`dlm_simulation.exe`)

The simulation runs 5 real-world distributed database scenarios demonstrating:
1. Shared vs Exclusive lock contention
2. Local deadlock detection and victim abort
3. Distributed transaction commit with WAL verification
4. Multi-node network message dispatching
5. Wait-for graph edge tracking

```powershell
.\Debug\dlm_simulation.exe
```

---

## 6. Running a Standalone Node (`dlm_node.exe`)

You can launch an independent cluster node process over TCP:

```powershell
# Syntax: dlm_node.exe <node_id> <listen_port> [peer1_ip:port peer2_ip:port ...]

# Terminal 1 (Node 1 on port 9001):
.\Debug\dlm_node.exe 1 9001 127.0.0.1:9002 127.0.0.1:9003

# Terminal 2 (Node 2 on port 9002):
.\Debug\dlm_node.exe 2 9002 127.0.0.1:9001 127.0.0.1:9003

# Terminal 3 (Node 3 on port 9003):
.\Debug\dlm_node.exe 3 9003 127.0.0.1:9001 127.0.0.1:9002
```

---

## 7. Troubleshooting Common Build Errors

### Error: `winsock2.h` redefinition conflicts
- **Cause**: Windows header `<windows.h>` included before `<winsock2.h>`.
- **Solution**: Always define `#define WIN32_LEAN_AND_MEAN` and `#define NOMINMAX` before `#include <winsock2.h>`.

### Error: `bind` ambiguous symbol
- **Cause**: `std::bind` in `<functional>` clashes with socket function `::bind`.
- **Solution**: Use global scope resolution `::bind(listenSocket, ...)` in `NetworkManager.cpp`.

### Error: `ERROR` macro collision
- **Cause**: Windows `wingdi.h` defines `#define ERROR 0`.
- **Solution**: Use `dlm::LogLevel::ERR` instead of `ERROR` in `Logger.h`.
