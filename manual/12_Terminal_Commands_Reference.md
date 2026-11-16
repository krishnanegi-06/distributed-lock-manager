# Chapter 12: Terminal Commands Reference

This reference chapter provides all command-line recipes required to configure, build, execute, test, and debug the Distributed Lock Manager on both **Windows (PowerShell / CMD)** and **Linux (Bash)**.

---

## 1. Build System Commands (CMake)

### Initial Configuration
```powershell
# Windows (Visual Studio 2022, 64-bit)
cmake -B build -G "Visual Studio 17 2022" -A x64

# Linux / macOS (Makefiles)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Linux / macOS (Ninja - Faster)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

### Compiling Binaries
```powershell
# Compile all targets (Windows Debug)
cmake --build build --config Debug

# Compile all targets (Windows Release)
cmake --build build --config Release

# Compile a specific target only
cmake --build build --config Debug --target dlm_simulation
cmake --build build --config Debug --target dlm_tests
cmake --build build --config Debug --target dlm_node
cmake --build build --config Debug --target dlm_core

# Linux / macOS compilation with parallel cores
cmake --build build -j$(nproc)
```

### Clean Rebuild
```powershell
# Windows PowerShell
Remove-Item -Recurse -Force build
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug

# Linux / macOS
rm -rf build && mkdir build && cd build
cmake .. && make -j$(nproc)
```

---

## 2. Execution Commands

### Running Unit & Integration Tests
```powershell
# Direct execution (Windows)
.\build\Debug\dlm_tests.exe

# Direct execution (Linux)
./build/dlm_tests

# Execution via CTest
cd build
ctest -C Debug --output-on-failure --verbose
```

### Running the Multi-Node Simulation
```powershell
# Windows
.\build\Debug\dlm_simulation.exe

# Linux
./build/dlm_simulation

# Saving simulation log to a file while viewing in terminal
.\build\Debug\dlm_simulation.exe | Tee-Object -FilePath simulation_output.txt
```

### Running Standalone Nodes (Multi-Process TCP Cluster)
To simulate a real distributed cluster across separate terminal windows:

```powershell
# Syntax: dlm_node.exe <node_id> <port> [peer_id:host:port ...]

# In Terminal 1 (Node 1):
.\build\Debug\dlm_node.exe 1 9001 2:127.0.0.1:9002 3:127.0.0.1:9003

# In Terminal 2 (Node 2):
.\build\Debug\dlm_node.exe 2 9002 1:127.0.0.1:9001 3:127.0.0.1:9003

# In Terminal 3 (Node 3):
.\build\Debug\dlm_node.exe 3 9003 1:127.0.0.1:9001 2:127.0.0.1:9002
```

---

## 3. Log Inspection Commands (WAL & Debug Logs)

```powershell
# Inspect the runtime Write-Ahead Log for Node 1
Get-Content logs\node_1_wal.log

# Stream WAL changes in real time (similar to tail -f)
Get-Content logs\node_1_wal.log -Wait

# Search for all ABORT operations across all node logs
Select-String -Path "logs\*.log" -Pattern "ABORT"

# View the last 20 log entries of Node 2
Get-Content logs\node_2_wal.log -Tail 20
```

---

## 4. Git Version Control Recipes

```powershell
# Check current branch and modified files
git status

# View compact commit history of the last 5 commits
git log --oneline -n 5

# Compare your current working branch against main
git diff origin/main..HEAD

# Inspect differences between Krishna's branch and Niharika's branch
git diff origin/niharika-deadlock-detector..origin/krishna --stat

# Stash current temporary changes safely
git stash push -m "Work in progress"

# Pop stashed changes back
git stash pop
```

---

## 5. Network & Port Diagnostics

If a previous run did not terminate cleanly and left a port bound:

```powershell
# Find process holding TCP port 9001 on Windows
Get-NetTCPConnection -LocalPort 9001 -ErrorAction SilentlyContinue | Format-Table LocalAddress, LocalPort, OwningProcess

# Forcibly terminate a stuck process by PID
Stop-Process -Id <PID> -Force

# Linux equivalent:
lsof -i :9001
kill -9 <PID>
```
