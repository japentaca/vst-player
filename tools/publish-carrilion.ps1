<#
.SYNOPSIS
  Builds Release and publishes VstPlayer.exe into the carrilion repository: copies
  build\VstPlayer_artefacts\Release\VstPlayer.exe to <carrilion>\bin\win64\VstPlayer.exe and writes
  bin\win64\VSTPLAYER.md (version, vst-player commit, build date, size, SHA-256, license note)
  from the template tools\VSTPLAYER.md.in. Commit in carrilion afterwards (this script does not).

.PARAMETER Target
  Destination folder (default: <repo>\..\carrilion\bin\win64). Created if missing.
.PARAMETER SkipBuild
  Publish the existing Release build without running tools\build.ps1 first.

.NOTES
  Publish from a committed state: VSTPLAYER.md names HEAD. If files that go into the exe
  (CMakeLists.txt, cmake\, src\, resources\, examples\carrilion-client.js) have uncommitted
  changes, the script warns and says so in VSTPLAYER.md.

.EXAMPLE
  pwsh -NoProfile -File .\tools\publish-carrilion.ps1
  pwsh -NoProfile -File .\tools\publish-carrilion.ps1 -Target D:\otro\carrilion\bin\win64
#>
param(
    [string]$Target = '',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
if (-not $Target) { $Target = Join-Path (Split-Path -Parent $root) 'carrilion\bin\win64' }

$cmake = Get-Content -Raw -LiteralPath (Join-Path $root 'CMakeLists.txt')
if ($cmake -notmatch 'project\(\s*VstPlayer\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') { throw 'No se encontro project(VstPlayer VERSION x.y.z) en CMakeLists.txt' }
$version = $Matches[1]

Push-Location $root
try {
    $commit = (git rev-parse HEAD).Trim()
    $commitShort = (git rev-parse --short HEAD).Trim()
    $commitDate = (git log -1 --format=%cI HEAD).Trim()
    $dirty = @(git status --porcelain -- CMakeLists.txt cmake src resources examples/carrilion-client.js)
    $remote = ''
    $remoteName = @(git remote) | Select-Object -First 1
    if ($remoteName) { $remote = (git remote get-url $remoteName).Trim() }
}
finally { Pop-Location }
if ($dirty.Count -gt 0) { Write-Warning "Hay cambios sin commitear en archivos que entran al exe:`n$($dirty -join "`n")" }

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Config Release
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw "build.ps1 fallo (exit $LASTEXITCODE)" }
}

$exe = Join-Path $root 'build\VstPlayer_artefacts\Release\VstPlayer.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "No existe $exe" }
$exeVersion = (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion
if (-not $exeVersion -or -not $exeVersion.StartsWith($version)) { throw "VstPlayer.exe tiene version '$exeVersion', CMakeLists.txt dice $version (recompilar)" }

New-Item -ItemType Directory -Force -Path $Target | Out-Null
$dest = Join-Path $Target 'VstPlayer.exe'
Copy-Item -LiteralPath $exe -Destination $dest -Force
$size = (Get-Item -LiteralPath $dest).Length
$sha = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()

$commitText = "$commit ($commitDate)"
if ($dirty.Count -gt 0) { $commitText += ' + cambios locales sin commitear' }
# No remote yet: placeholder (Spanish, accents as char codes to keep this file ASCII).
$source = if ($remote) { $remote } else { "sin remoto publicado todav$([char]0x00ED)a: pedir el c$([char]0x00F3)digo al autor de carrilion" }
$sizeText = '{0:N1} MB ({1} bytes)' -f ($size / 1MB), $size

# The template is UTF-8 (Spanish text); this script stays ASCII.
$template = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot 'VSTPLAYER.md.in'), [System.Text.Encoding]::UTF8)
$md = $template.Replace('{{VERSION}}', $version).Replace('{{COMMIT}}', $commitText).Replace('{{COMMIT_SHORT}}', $commitShort)
$md = $md.Replace('{{BUILD_DATE}}', (Get-Date).ToString('yyyy-MM-dd HH:mm zzz')).Replace('{{SIZE}}', $sizeText)
$md = $md.Replace('{{SHA256}}', $sha).Replace('{{SOURCE}}', $source).Replace("`r`n", "`n")
$mdPath = Join-Path $Target 'VSTPLAYER.md'
[System.IO.File]::WriteAllText($mdPath, $md, (New-Object System.Text.UTF8Encoding $false))

"VstPlayer $version (commit $commitShort) publicado:"
"  $dest  ($sizeText)"
"  SHA-256 $sha"
"  $mdPath"
"Falta commitear en carrilion (git add bin/win64)."
