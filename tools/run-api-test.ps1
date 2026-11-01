<#
.SYNOPSIS
  Stage 7 API test: starts VstPlayer.exe --api-test (GUI, nothing persisted, no dialogs) on a
  test port with a temporary data folder, runs tools\api-test.mjs (Node) against it over HTTP and
  WebSocket, checks with netstat that the port listens only on 127.0.0.1, closes the app and fails
  if its exit code is not 0 or if JUCE reported assertions / leaks (--debug-to-stdout, as
  run-gui-test.ps1 does).

.NOTES
  %APPDATA%\VstPlayer is not modified: recent.json and the autosave go to the temporary folder
  (--project-data-dir); --api-test does not persist audio, window or settings.

.EXAMPLE
  pwsh -NoProfile -File .\tools\run-api-test.ps1
  pwsh -NoProfile -File .\tools\run-api-test.ps1 -Token secreto
  pwsh -NoProfile -File .\tools\run-api-test.ps1 -Config Debug -AllowedLeaks VST3HostContextHeadless,AsyncUpdater
#>
param(
    [ValidateSet('Release', 'Debug')] [string]$Config = 'Release',
    [int]$Port = 9479,
    [string]$Token = '',
    [string[]]$AllowedLeaks = @(),
    [switch]$KeepFiles
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$buildDir = if ($Config -eq 'Debug') { 'build-debug' } else { 'build' }
$exe = Join-Path $root "$buildDir\VstPlayer_artefacts\$Config\VstPlayer.exe"
if (-not (Test-Path $exe)) { throw "No existe $exe (compilar con tools\build.ps1 -Config $Config)" }
if (-not (Get-Command node -ErrorAction SilentlyContinue)) { throw 'Hace falta Node.js 22+ (fetch y WebSocket globales).' }

$appData = Join-Path $env:APPDATA 'VstPlayer'
$recentBefore = Test-Path (Join-Path $appData 'recent.json')
$autosaveBefore = Test-Path (Join-Path $appData 'autosave.vstproj.json')

$dir = Join-Path ([System.IO.Path]::GetTempPath()) ('vstplayer-api-test-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$dataDir = Join-Path $dir 'userdata'
New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
$outFile = Join-Path $dir 'app.out.txt'

$appArgs = @('--api-test', '--api-port', "$Port", '--project-data-dir', $dataDir, '--debug-to-stdout')
if ($Token) { $appArgs += @('--api-token', $Token) }

if (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue) { throw "El puerto $Port ya esta en uso" }

"Ejecutando: $exe $($appArgs -join ' ')"
$t0 = Get-Date
$p = Start-Process -FilePath $exe -ArgumentList $appArgs -PassThru -RedirectStandardOutput $outFile -RedirectStandardError "$outFile.err"

$failed = @()
try {
    # Wait until it listens, then check the bound address.
    $listen = $null
    for ($i = 0; $i -lt 150 -and -not $listen; $i++) {
        Start-Sleep -Milliseconds 200
        $listen = @(Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue | Where-Object { $_.OwningProcess -eq $p.Id })
        if ($listen.Count -eq 0) { $listen = $null }
        if ($p.HasExited) { break }
    }
    "---- netstat ----"
    $netstat = @(netstat -ano -p tcp | Select-String -Pattern ":$Port\s" | Where-Object { $_ -match 'LISTENING' })
    $netstat | ForEach-Object { $_.Line.Trim() }
    $addresses = @($listen | ForEach-Object { $_.LocalAddress } | Sort-Object -Unique)
    if ($addresses.Count -eq 1 -and $addresses[0] -eq '127.0.0.1') { "  OK    solo escucha en 127.0.0.1:$Port" }
    else { "  FALLO escucha en: $($addresses -join ', ')"; $failed += 'netstat' }

    "---- api-test.mjs ----"
    $nodeArgs = @((Join-Path $PSScriptRoot 'api-test.mjs'), '--port', "$Port", '--examples', (Join-Path $root 'examples'), '--tmp', $dir)
    if ($Token) { $nodeArgs += @('--token', $Token) }
    & node @nodeArgs
    if ($LASTEXITCODE -ne 0) { $failed += 'api-test.mjs' }
}
finally {
    if (-not $p.HasExited) {
        [void]$p.CloseMainWindow()
        if (-not $p.WaitForExit(30000)) { Stop-Process -Id $p.Id -Force; Write-Warning 'Proceso terminado a la fuerza'; $failed += 'cierre' }
    }
    $p.WaitForExit()
}

$exitCode = $p.ExitCode
$lines = @(Get-Content -Path $outFile -Encoding utf8)
$debugLines = @($lines | Where-Object { $_.StartsWith('[debug] ') })
$assertions = @($debugLines | Where-Object { $_ -match 'JUCE Assertion failure' -and $_ -notmatch 'LeakedObjectDetector' })
$leaks = @($debugLines | Where-Object { $_ -match 'Leaked objects detected' })
$allowed = @($leaks | Where-Object { $l = $_; @($AllowedLeaks | Where-Object { $l -match "class $_\b" }).Count -gt 0 })
$badLeaks = @($leaks | Where-Object { $allowed -notcontains $_ })

"---- salida de la app ----"
$lines | Where-Object { -not $_.StartsWith('[debug] ') -or $_ -match 'Assertion|Leak' }
"---- resumen ----"
"exit code: $exitCode  ({0:N1} s)" -f ((Get-Date) - $t0).TotalSeconds
"aserciones JUCE: $($assertions.Count)"
"clases con fuga: $($leaks.Count) (permitidas: $($allowed.Count))"
if ($exitCode -ne 0) { $failed += 'exit code' }
if ($assertions.Count -gt 0) { $failed += 'aserciones' }
if ($badLeaks.Count -gt 0) { $failed += 'fugas' }

if (-not $recentBefore -and (Test-Path (Join-Path $appData 'recent.json'))) { "recent.json aparecio en $appData"; $failed += 'appdata' }
if (-not $autosaveBefore -and (Test-Path (Join-Path $appData 'autosave.vstproj.json'))) { "autosave aparecio en $appData"; $failed += 'appdata' }

if ($failed.Count -gt 0) {
    "PRUEBA DE API: FALLO ($($failed -join ', ')); archivos en $dir"
    exit 1
}
if (-not $KeepFiles) { Remove-Item -Recurse -Force $dir }
"PRUEBA DE API: OK"
exit 0
