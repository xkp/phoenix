param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake is not installed or not available on PATH."
}

if (-not $env:VCPKG_ROOT) {
    throw "VCPKG_ROOT is not set. Example: `$env:VCPKG_ROOT='C:\dev\vcpkg'"
}

Write-Host "Configuring with preset windows-default..."
cmake --preset windows-default

$buildPreset = if ($Configuration -eq "Debug") {
    "build-windows-debug"
} else {
    "build-windows-release"
}

Write-Host "Building with preset $buildPreset..."
cmake --build --preset $buildPreset

Write-Host "Windows build completed successfully."
