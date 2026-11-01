<#
.SYNOPSIS
  Builds Release and packs dist\VstPlayer-<version>-win64.zip with VstPlayer.exe, README.md,
  examples\, docs\ and tools\register-filetype.ps1. The version comes from CMakeLists.txt
  (project(VstPlayer VERSION x.y.z)), the same one shown in Ayuda > Acerca de.

.PARAMETER SkipBuild
  Pack the existing build\ without running tools\build.ps1 first.
.PARAMETER OutDir
  Where the zip goes (default: <repo>\dist, ignored by git).

.EXAMPLE
  pwsh -NoProfile -File .\tools\package.ps1
#>
param(
    [switch]$SkipBuild,
    [string]$OutDir = ''
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $root 'dist' }

$cmake = Get-Content -Raw -LiteralPath (Join-Path $root 'CMakeLists.txt')
if ($cmake -notmatch 'project\(\s*VstPlayer\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') { throw 'No se encontro project(VstPlayer VERSION x.y.z) en CMakeLists.txt' }
$version = $Matches[1]
$name = "VstPlayer-$version-win64"
"Version $version -> $name.zip"

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Config Release
}

$exe = Join-Path $root 'build\VstPlayer_artefacts\Release\VstPlayer.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "No existe $exe" }

# The exe's version resource must match CMakeLists.txt (a stale build would not).
$exeVersion = (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion
if (-not $exeVersion -or -not $exeVersion.StartsWith($version)) { throw "VstPlayer.exe tiene version '$exeVersion', CMakeLists.txt dice $version (recompilar)" }

$stage = Join-Path $OutDir $name
$zip = Join-Path $OutDir "$name.zip"
if (Test-Path -LiteralPath $stage) { Remove-Item -Recurse -Force -LiteralPath $stage }
if (Test-Path -LiteralPath $zip) { Remove-Item -Force -LiteralPath $zip }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Copy-Item -LiteralPath $exe -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'examples') -Destination $stage -Recurse
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'docs') | Out-Null
Get-ChildItem -LiteralPath (Join-Path $root 'docs') -File -Filter '*.md' | Copy-Item -Destination (Join-Path $stage 'docs')
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'tools') | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'register-filetype.ps1') -Destination (Join-Path $stage 'tools')

Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
Remove-Item -Recurse -Force -LiteralPath $stage

$size = (Get-Item -LiteralPath $zip).Length / 1MB
"Contenido:"
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
try { $archive.Entries | Where-Object { $_.Name } | ForEach-Object { "  {0,10:N0}  {1}" -f $_.Length, $_.FullName } }
finally { $archive.Dispose() }
"Paquete: $zip ({0:N1} MB)" -f $size
