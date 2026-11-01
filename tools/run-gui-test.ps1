<#
.SYNOPSIS
  Runs VstPlayer.exe with a self-test hook (default: --instance-selftest) plus
  --debug-to-stdout, shows its output and fails if the exit code is not 0 or if any JUCE
  assertion or leaked object was reported.

.NOTES
  Without a debugger attached, a jassert in a Debug build does NOT stop the process: JUCE only
  logs "JUCE Assertion failure in <file>:<line>" with OutputDebugString, and the leak detector
  does the same at exit ("*** Leaked objects detected"). --debug-to-stdout makes the app copy
  every OutputDebugString to stdout as "[debug] ..." (in-process, see cli::installDebugOutputCapture),
  which is what this script inspects. In Release builds there are no assertions to catch.

.EXAMPLE
  pwsh -NoProfile -File .\tools\run-gui-test.ps1 -Config Debug
  pwsh -NoProfile -File .\tools\run-gui-test.ps1 -Config Debug -AppArgs '--instance-selftest','--plugin','sforzando'
#>
param(
    [ValidateSet('Release', 'Debug')] [string]$Config = 'Release',
    [string[]]$AppArgs = @('--instance-selftest'),
    [int]$TimeoutSeconds = 300,
    # Leaks of these classes are reported but do not fail the test (known plugin-side leaks).
    [string[]]$AllowedLeaks = @(),
    [switch]$ShowAllDebugOutput
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$buildDir = if ($Config -eq 'Debug') { 'build-debug' } else { 'build' }
$exe = Join-Path $root "$buildDir\VstPlayer_artefacts\$Config\VstPlayer.exe"
if (-not (Test-Path $exe)) { throw "No existe $exe (compilar con tools\build.ps1 -Config $Config)" }

$tmp = [System.IO.Path]::GetTempPath()
$outFile = Join-Path $tmp ("vstplayer-test-{0}.out.txt" -f [guid]::NewGuid())
$errFile = "$outFile.err"
$allArgs = @($AppArgs) + '--debug-to-stdout'

"Ejecutando: $exe $($allArgs -join ' ')"
$t0 = Get-Date
$p = Start-Process -FilePath $exe -ArgumentList $allArgs -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile

if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
    Write-Warning "Timeout de $TimeoutSeconds s: cerrando la ventana principal"
    [void]$p.CloseMainWindow()
    if (-not $p.WaitForExit(15000)) { Stop-Process -Id $p.Id -Force; Write-Warning 'Proceso terminado a la fuerza' }
}
$p.WaitForExit()
$exitCode = $p.ExitCode
$seconds = ((Get-Date) - $t0).TotalSeconds

$lines = @(Get-Content -Path $outFile -Encoding utf8)
Remove-Item $outFile, $errFile -ErrorAction SilentlyContinue

$debugLines = @($lines | Where-Object { $_.StartsWith('[debug] ') })
$assertions = @($debugLines | Where-Object { $_ -match 'JUCE Assertion failure' -and $_ -notmatch 'LeakedObjectDetector' })
$leaks = @($debugLines | Where-Object { $_ -match 'Leaked objects detected' })
$allowed = @($leaks | Where-Object { $l = $_; @($AllowedLeaks | Where-Object { $l -match "class $_\b" }).Count -gt 0 })
$badLeaks = @($leaks | Where-Object { $allowed -notcontains $_ })

"---- salida ----"
if ($ShowAllDebugOutput) { $lines } else { $lines | Where-Object { -not $_.StartsWith('[debug] ') -or $_ -match 'Assertion|Leak' } }
"---- resumen ----"
"exit code: $exitCode  ({0:N1} s)" -f $seconds
"lineas de debug: $($debugLines.Count)"
"aserciones JUCE (sin contar las del detector de fugas): $($assertions.Count)"
"clases con fuga: $($leaks.Count) (permitidas: $($allowed.Count))"

if ($exitCode -ne 0 -or $assertions.Count -gt 0 -or $badLeaks.Count -gt 0) { "TEST: FALLO"; exit 1 }
"TEST: OK"
exit 0
