<#
.SYNOPSIS
  Stage 6 UI test: runs VstPlayer.exe --ui-selftest through run-gui-test.ps1 (so JUCE
  assertions and leaks are checked too) in a temporary folder with its own recent.json and
  autosave (--project-data-dir): %APPDATA%\VstPlayer is not modified (except plugins.xml, which
  the scan step rewrites with exactly the same content it had).

  The screenshots (PNG) are copied to -OutDir (default: build\ui-test or build-debug\ui-test).

.EXAMPLE
  pwsh -NoProfile -File .\tools\run-ui-test.ps1
  pwsh -NoProfile -File .\tools\run-ui-test.ps1 -Config Debug -AllowedLeaks VST3HostContextHeadless,AsyncUpdater
#>
param(
    [ValidateSet('Release', 'Debug')] [string]$Config = 'Release',
    [string[]]$AllowedLeaks = @(),
    [string]$OutDir = '',
    [switch]$KeepFiles
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$runner = Join-Path $PSScriptRoot 'run-gui-test.ps1'
if (-not $OutDir) { $OutDir = Join-Path $root ($(if ($Config -eq 'Debug') { 'build-debug' } else { 'build' }) + '\ui-test') }

$dir = Join-Path ([System.IO.Path]::GetTempPath()) ('vstplayer-ui-test-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
if ($dir -match '\s') { throw "La carpeta temporal tiene espacios: $dir" }
$dataDir = Join-Path $dir 'userdata'
New-Item -ItemType Directory -Force -Path $dataDir | Out-Null

$pluginsXml = Join-Path $env:APPDATA 'VstPlayer\plugins.xml'
$pluginsHashBefore = if (Test-Path $pluginsXml) { (Get-FileHash $pluginsXml).Hash } else { '' }
# The test re-scans (plugins.xml) and adds / removes a search folder (settings.xml): keep
# byte-exact copies of both and put them back afterwards.
$userFiles = @('plugins.xml', 'settings.xml') | ForEach-Object { Join-Path $env:APPDATA "VstPlayer\$_" }
$backups = @{}
foreach ($f in $userFiles) { if (Test-Path $f) { $backups[$f] = [System.IO.File]::ReadAllBytes($f) } }
# JUCE's scanner leaves this (empty) "dead man's pedal" file after a GUI scan.
$pedal = Join-Path $env:APPDATA 'VstPlayer\plugin-scan-in-progress.txt'
$pedalExisted = Test-Path $pedal

& $runner -Config $Config -AppArgs @('--ui-selftest', '--project-test-dir', $dir, '--project-data-dir', $dataDir) -AllowedLeaks $AllowedLeaks
$code = $LASTEXITCODE

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Get-ChildItem -Path $dir -Filter '*.png' | Copy-Item -Destination $OutDir -Force
""
"Capturas en: $OutDir"
Get-ChildItem -Path $OutDir -Filter '*.png' | ForEach-Object { "  $($_.Name)  ($([math]::Round($_.Length / 1KB)) KB)" }

if (Test-Path $pluginsXml) {
    $same = ((Get-FileHash $pluginsXml).Hash -eq $pluginsHashBefore)
    "plugins.xml igual que antes de la prueba: $same"
}
foreach ($f in $userFiles) {
    if ($backups.ContainsKey($f)) { [System.IO.File]::WriteAllBytes($f, $backups[$f]) }
}
"plugins.xml y settings.xml restaurados a su contenido previo"
if (-not $pedalExisted -and (Test-Path $pedal)) { Remove-Item $pedal }
$leftovers = @('recent.json', 'autosave.vstproj.json') | Where-Object { Test-Path (Join-Path $env:APPDATA "VstPlayer\$_") }
if ($leftovers.Count -gt 0) { "AVISO: hay archivos en %APPDATA%\VstPlayer: $($leftovers -join ', ')" }

if ($code -ne 0) {
    "PRUEBA DE UI: FALLO (archivos conservados en $dir)"
    exit 1
}
if (-not $KeepFiles) { Remove-Item -Recurse -Force $dir }
"PRUEBA DE UI: OK"
exit 0
