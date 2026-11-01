<#
.SYNOPSIS
  Stage 5 end-to-end project test: runs VstPlayer.exe four times (one process per phase,
  through run-gui-test.ps1, so JUCE assertions and leaks are checked too):
    1. save           build a setup with Dexed, Surge XT and sforzando, save it, write the
                      expected fingerprint, quit.
    2. verify-cli     VstPlayer.exe <dir>\prueba.vstproj.json  -> compare with the fingerprint.
    3. verify-recent  VstPlayer.exe (no project) -> Archivo > Recientes[0] -> compare.
    4. extras         missing plugin, unknown fields, params override, unsaved-changes question,
                      autosave, re-save.
  Everything happens in a temporary folder (--project-test-dir) with its own recent.json and
  autosave (--project-data-dir): %APPDATA%\VstPlayer is not modified (the self-test mode does
  not persist the audio setup, window state or last folder either).

.EXAMPLE
  pwsh -NoProfile -File .\tools\run-project-test.ps1
  pwsh -NoProfile -File .\tools\run-project-test.ps1 -Config Debug -AllowedLeaks VST3HostContextHeadless,AsyncUpdater
#>
param(
    [ValidateSet('Release', 'Debug')] [string]$Config = 'Release',
    [string[]]$AllowedLeaks = @(),
    [switch]$KeepFiles
)
$ErrorActionPreference = 'Stop'

$runner = Join-Path $PSScriptRoot 'run-gui-test.ps1'
$dir = Join-Path ([System.IO.Path]::GetTempPath()) ('vstplayer-project-test-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
if ($dir -match '\s') { throw "La carpeta temporal tiene espacios: $dir" }
$dataDir = Join-Path $dir 'userdata'
New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
$project = Join-Path $dir 'prueba.vstproj.json'
$common = @('--project-test-dir', $dir, '--project-data-dir', $dataDir)

$phases = @(
    @{ Name = 'save';          Args = @('--project-selftest', 'save') + $common },
    @{ Name = 'verify-cli';    Args = @($project, '--project-selftest', 'verify-cli') + $common },
    @{ Name = 'verify-recent'; Args = @('--project-selftest', 'verify-recent') + $common },
    @{ Name = 'extras';        Args = @('--project-selftest', 'extras') + $common }
)

$t0 = Get-Date
$failed = @()
foreach ($phase in $phases) {
    ""
    "==================== fase $($phase.Name) ===================="
    & $runner -Config $Config -AppArgs $phase.Args -AllowedLeaks $AllowedLeaks
    if ($LASTEXITCODE -ne 0) {
        $failed += $phase.Name
        if ($phase.Name -eq 'save') { break }
    }
}

""
"Carpeta de prueba: $dir"
if (Test-Path $project) { "prueba.vstproj.json: {0:N0} KB" -f ((Get-Item $project).Length / 1KB) }
"Tiempo total: {0:N0} s" -f ((Get-Date) - $t0).TotalSeconds

if ($failed.Count -gt 0) {
    "PRUEBA DE PROYECTOS: FALLO en $($failed -join ', ') (archivos conservados)"
    exit 1
}

if (-not $KeepFiles) { Remove-Item -Recurse -Force $dir }
"PRUEBA DE PROYECTOS: OK"
exit 0
