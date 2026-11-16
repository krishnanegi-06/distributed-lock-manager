# Native Windows Installation & Execution Guide

## 1. Prerequisites

- **Windows 10 / 11 (x64)**
- **Visual Studio 2022** (or Visual Studio 2022 Build Tools with C++ Desktop workload / MSVC v143)
- **CMake 3.16+**
- **Node.js 20+** and **npm**

No Docker, Boost, or vcpkg installation is required for native Windows execution.

## 2. Build Everything

```powershell
.\scripts\windows\build.ps1 -Config Release
```

This configures CMake for Visual Studio 17 2022 (`x64`), builds `dlm_core`, `dlm_node.exe`, `dlm_api.exe`, `dlm_simulation.exe`, `dlm_tests.exe`, `dlm_gtest.exe`, and `dlm_benchmark.exe`, then typechecks and builds the React frontend (`frontend/dist/`).

## 3. Run the 3-Node Cluster & Dashboard

```powershell
.\scripts\windows\start-cluster.ps1 -Config Release
```

- Launches `dlm_api.exe` with `--supervise`, spawning 3 independent `dlm_node.exe` processes on TCP ports `9001`, `9002`, and `9003` with persistent storage under `data/node1/`, `data/node2/`, `data/node3/`.
- Starts the Vite frontend at `http://localhost:5173`.
- On first visit, complete the **First-Run Admin Setup** form in the browser to create your local administrator password.

## 4. Stop Cluster (Preserving Data)

```powershell
.\scripts\windows\stop-cluster.ps1
```

## 5. Run Automated Tests & Reset Demo Data

```powershell
# Run CTest + Frontend typecheck/build
.\scripts\windows\run-tests.ps1 -Config Debug

# Explicitly wipe demo data/ and logs/
.\scripts\windows\reset-demo.ps1
```
