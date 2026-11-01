<#
.SYNOPSIS
    Loads the MSVC x64 build environment (vcvars64.bat) into the current PowerShell session.
.DESCRIPTION
    Locates Visual Studio / Build Tools with the VC toolset through vswhere, runs
    vcvars64.bat in cmd and copies the resulting environment variables into this
    process. Environment variables are process-wide, so running the script
    (`.\tools\dev-env.ps1`) is enough; dot-sourcing is not required.
    Safe to call more than once: it does nothing if cl.exe is already on PATH
    with the x64 environment loaded (unless -Force).
#>
[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

if (-not $Force -and $env:VSCMD_ARG_TGT_ARCH -eq 'x64' -and (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Write-Verbose 'MSVC x64 environment already loaded.'
    return
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found at '$vswhere'. Install Visual Studio 2022 Build Tools with the C++ workload (see PLAN.md, stage 0)."
}

$installPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $installPath) {
    throw 'No Visual Studio installation with the VC x64 toolset was found (vswhere returned nothing).'
}

$vcvars = Join-Path $installPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) {
    throw "vcvars64.bat not found at '$vcvars'."
}

Write-Host "Loading MSVC environment from $vcvars"
$output = & cmd.exe /d /s /c "`"$vcvars`" >nul 2>&1 && set"
if ($LASTEXITCODE -ne 0) {
    throw "vcvars64.bat failed with exit code $LASTEXITCODE."
}

foreach ($line in $output) {
    $idx = $line.IndexOf('=')
    if ($idx -gt 0) {
        $name = $line.Substring(0, $idx)
        $value = $line.Substring($idx + 1)
        Set-Item -Path "Env:$name" -Value $value
    }
}

$cl = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $cl) {
    throw 'cl.exe is still not on PATH after loading vcvars64.bat.'
}
Write-Host "MSVC environment loaded: $($cl.Source)"
