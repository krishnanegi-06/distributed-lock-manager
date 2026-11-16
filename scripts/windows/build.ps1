param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Push-Location $Root
try {
    Write-Host "[1/3] Configuring CMake..." -ForegroundColor Cyan
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64

    Write-Host "[2/3] Building C++ targets ($Config)..." -ForegroundColor Cyan
    cmake --build build --config $Config

    Write-Host "[3/3] Building React + TypeScript Frontend..." -ForegroundColor Cyan
    Push-Location frontend
    if (-not (Test-Path node_modules)) {
        npm install
    }
    npm run typecheck
    npm run build
    Pop-Location

    Write-Host "Build completed successfully." -ForegroundColor Green
} finally {
    Pop-Location
}
