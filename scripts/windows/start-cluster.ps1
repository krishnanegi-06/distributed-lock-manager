param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [int]$ApiPort = 8080,
    [int]$FrontendPort = 5173
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$BinDir = Join-Path $Root "build\$Config"
$NodeExe = Join-Path $BinDir "dlm_node.exe"
$ApiExe  = Join-Path $BinDir "dlm_api.exe"
$PidFile = Join-Path $Root "data\cluster_pids.json"

if (-not (Test-Path $NodeExe) -or -not (Test-Path $ApiExe)) {
    throw "Binaries not found in $BinDir. Run scripts/windows/build.ps1 -Config $Config first."
}

New-Item -ItemType Directory -Force -Path (Join-Path $Root "data") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $Root "logs") | Out-Null

# Ensure config/nodes.json exists
$NodesCfg = Join-Path $Root "config\nodes.json"
if (-not (Test-Path $NodesCfg)) {
    Copy-Item (Join-Path $Root "config\nodes.example.json") $NodesCfg
}

Write-Host "Starting DLM-DB API + Supervised 3-Node Cluster..." -ForegroundColor Cyan
$apiProc = Start-Process -FilePath $ApiExe `
    -ArgumentList "--port", "$ApiPort", "--config", "$NodesCfg", "--supervise", "--node-bin", "$NodeExe", "--data-dir", (Join-Path $Root "data"), "--log-dir", (Join-Path $Root "logs") `
    -WorkingDirectory $Root `
    -PassThru -WindowStyle Hidden

Start-Sleep -Milliseconds 1200

Write-Host "Starting Frontend Dev Server on http://localhost:$FrontendPort ..." -ForegroundColor Cyan
$feProc = Start-Process -FilePath "cmd.exe" `
    -ArgumentList "/c", "npm run dev -- --port $FrontendPort" `
    -WorkingDirectory (Join-Path $Root "frontend") `
    -PassThru -WindowStyle Hidden

@{
    apiPid = $apiProc.Id
    frontendPid = $feProc.Id
} | ConvertTo-Json | Set-Content -Path $PidFile

Write-Host "Cluster started!" -ForegroundColor Green
Write-Host "  Dashboard : http://localhost:$FrontendPort"
Write-Host "  API       : http://127.0.0.1:$ApiPort/api/health"
Write-Host "  Nodes     : 9001, 9002, 9003 (supervised by dlm_api)"
