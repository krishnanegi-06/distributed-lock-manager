param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Push-Location $Root
try {
    Write-Host "Running CTest ($Config)..." -ForegroundColor Cyan
    ctest --test-dir build -C $Config --output-on-failure

    Write-Host "Running Frontend TypeCheck & Build..." -ForegroundColor Cyan
    Push-Location frontend
    npm run typecheck
    npm run build
    Pop-Location

    Write-Host "All checks passed." -ForegroundColor Green
} finally {
    Pop-Location
}
