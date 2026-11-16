param(
    [switch]$Force
)

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if (-not $Force) {
    $ans = Read-Host "This will stop any running nodes and DELETE all data/ and logs/. Type YES to continue"
    if ($ans -ne "YES") {
        Write-Host "Aborted."
        exit 0
    }
}

& (Join-Path $PSScriptRoot "stop-cluster.ps1")

$DataDir = Join-Path $Root "data"
$LogsDir = Join-Path $Root "logs"
if (Test-Path $DataDir) { Remove-Item -Recurse -Force $DataDir }
if (Test-Path $LogsDir) { Get-ChildItem $LogsDir -Filter "*.log" | Remove-Item -Force }

New-Item -ItemType Directory -Force -Path $DataDir | Out-Null
Write-Host "Demo data and logs reset cleanly." -ForegroundColor Green
