<#
.SYNOPSIS
    Configures and builds vst-player with MSVC + Ninja.
.PARAMETER Config
    Debug or Release (default Release). Release builds into .\build, Debug into .\build-debug.
.PARAMETER Clean
    Deletes the build directory before configuring.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$buildDir = if ($Config -eq 'Release') { Join-Path $root 'build' } else { Join-Path $root 'build-debug' }

& (Join-Path $PSScriptRoot 'dev-env.ps1')

if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    throw 'ninja not found on PATH.'
}

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Removing $buildDir"
    Remove-Item -Recurse -Force $buildDir
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()

cmake -S $root -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config" -DCMAKE_CXX_COMPILER=cl
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)." }

cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)." }

$sw.Stop()
Write-Host ("Build {0} OK in {1:N1} s -> {2}" -f $Config, $sw.Elapsed.TotalSeconds, $buildDir)
