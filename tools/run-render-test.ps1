<#
.SYNOPSIS
  Offline render test (VstPlayer.exe --render): runs tools\render-test.mjs (Node), which writes
  small MIDI files and a test project with two Dexed instances, renders them to WAV with the exe
  (spawned from Node with piped stdio, as carrilion does) and checks the JSON summary, the WAV,
  sample-accurate onsets, tempo map, routing (keep / remapTo), CC7, tail, determinism, errors and
  JUCE assertions / leaks (--debug-to-stdout).

.NOTES
  Uses a temporary --data-dir with a copy of plugins.xml and settings.xml from %APPDATA%\VstPlayer
  and checks that the render left it exactly as it was (same files, same contents, same dates).
  %APPDATA%\VstPlayer is not used. Needs Node 22+ and Dexed scanned in plugins.xml.

.EXAMPLE
  pwsh -NoProfile -File .\tools\run-render-test.ps1
  pwsh -NoProfile -File .\tools\run-render-test.ps1 -Config Debug
#>
param(
    [ValidateSet('Release', 'Debug')] [string]$Config = 'Release',
    [string[]]$AllowedLeaks = @(),
    [switch]$KeepFiles
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$buildDir = if ($Config -eq 'Debug') { 'build-debug' } else { 'build' }
$exe = Join-Path $root "$buildDir\VstPlayer_artefacts\$Config\VstPlayer.exe"
if (-not (Test-Path $exe)) { throw "No existe $exe (compilar con tools\build.ps1 -Config $Config)" }
if (-not (Get-Command node -ErrorAction SilentlyContinue)) { throw 'Hace falta Node.js 22+.' }

$appData = Join-Path $env:APPDATA 'VstPlayer'
if (-not (Test-Path (Join-Path $appData 'plugins.xml'))) { throw "No hay $appData\plugins.xml (escanear los plugins primero)" }

$dir = Join-Path ([System.IO.Path]::GetTempPath()) ('vstplayer-render-test-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$dataDir = Join-Path $dir 'userdata'
$workDir = Join-Path $dir 'work'
New-Item -ItemType Directory -Force -Path $dataDir, $workDir | Out-Null
foreach ($name in 'plugins.xml', 'settings.xml') {
    $src = Join-Path $appData $name
    if (Test-Path $src) { Copy-Item $src $dataDir }
}

function Get-Snapshot([string]$path) {
    @(Get-ChildItem -Path $path -Recurse -Force -File | Sort-Object FullName | ForEach-Object {
        '{0}|{1}|{2}|{3}' -f $_.FullName.Substring($path.Length), $_.Length, $_.LastWriteTimeUtc.Ticks, (Get-FileHash $_.FullName -Algorithm SHA256).Hash
    })
}
$before = Get-Snapshot $dataDir
$appDataBefore = @{}
foreach ($name in 'recent.json', 'autosave.vstproj.json') { $appDataBefore[$name] = Test-Path (Join-Path $appData $name) }

$failed = @()
$t0 = Get-Date
$nodeArgs = @((Join-Path $PSScriptRoot 'render-test.mjs'), '--exe', $exe, '--data-dir', $dataDir, '--examples', (Join-Path $root 'examples'), '--tmp', $workDir)
if ($AllowedLeaks.Count -gt 0) { $nodeArgs += @('--allowed-leaks', ($AllowedLeaks -join ',')) }
"Ejecutando: node $($nodeArgs -join ' ')"
& node @nodeArgs
if ($LASTEXITCODE -ne 0) { $failed += 'render-test.mjs' }

"---- carpeta de datos ----"
$after = Get-Snapshot $dataDir
$diff = @(Compare-Object -ReferenceObject $before -DifferenceObject $after)
if ($diff.Count -eq 0) { "  OK    $dataDir sin cambios ($($after.Count) archivos)" }
else { "  FALLO la carpeta de datos cambio:"; $diff | ForEach-Object { "        $($_.SideIndicator) $($_.InputObject)" }; $failed += 'carpeta de datos' }
foreach ($name in 'recent.json', 'autosave.vstproj.json') {
    if (-not $appDataBefore[$name] -and (Test-Path (Join-Path $appData $name))) { "  FALLO $name aparecio en $appData"; $failed += 'appdata' }
}

"---- resumen ----"
"tiempo total: {0:N1} s" -f ((Get-Date) - $t0).TotalSeconds
if ($failed.Count -gt 0) {
    "PRUEBA DE RENDER: FALLO ($($failed -join ', ')); archivos en $dir"
    exit 1
}
if (-not $KeepFiles) { Remove-Item -Recurse -Force $dir }
"PRUEBA DE RENDER: OK"
exit 0
