$ErrorActionPreference = "SilentlyContinue"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$PidFile = Join-Path $Root "data\cluster_pids.json"

Write-Host "Stopping DLM-DB cluster processes..." -ForegroundColor Cyan

if (Test-Path $PidFile) {
    $pids = Get-Content $PidFile | ConvertFrom-Json
    if ($pids.frontendPid) {
        Stop-Process -Id $pids.frontendPid -Force
    }
    if ($pids.apiPid) {
        Stop-Process -Id $pids.apiPid -Force
    }
    Remove-Item $PidFile -Force
}

Get-Process -Name "dlm_api","dlm_node" -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Host "Cluster stopped. Persistent data under data/ preserved." -ForegroundColor Green
