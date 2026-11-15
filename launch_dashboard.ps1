$ErrorActionPreference = "Stop"
& (Join-Path $PSScriptRoot "scripts\windows\start-cluster.ps1") -Config Release
Start-Process "http://localhost:5173"
