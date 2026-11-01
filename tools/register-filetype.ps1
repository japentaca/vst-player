<#
.SYNOPSIS
  Optional association of *.vstproj.json with VstPlayer.exe for the current user (HKCU only,
  no administrator rights). -Unregister removes everything this script wrote.

.DESCRIPTION
  Windows decides the file type by the LAST extension, so a "*.vstproj.json" file is a ".json"
  file for Explorer: a double extension cannot get its own default program. This script does not
  take over .json; it adds, under HKCU\Software\Classes:
    - VstPlayer.Project                     ProgID ("Proyecto de VstPlayer", icon, open command)
    - .json\OpenWithProgids\VstPlayer.Project
                                            VstPlayer appears in "Abrir con" for .json files
    - Applications\VstPlayer.exe            friendly name + supported type .json
    - SystemFileAssociations\.json\shell\VstPlayer.Open
                                            context menu entry "Abrir en VstPlayer", shown only
                                            for files named *.vstproj.json (AppliesTo)
  To open projects with a double click, choose "Abrir con > Elegir otra aplicacion > VstPlayer >
  Siempre" once (Windows does not let scripts set the default program).

.PARAMETER ExePath
  VstPlayer.exe to register. Default: VstPlayer.exe next to this script's folder (zip layout:
  <carpeta>\VstPlayer.exe + <carpeta>\tools\register-filetype.ps1), or the Release build of the repo.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File .\tools\register-filetype.ps1
  powershell -ExecutionPolicy Bypass -File .\tools\register-filetype.ps1 -Unregister
#>
param(
    [string]$ExePath = '',
    [switch]$Unregister
)
$ErrorActionPreference = 'Stop'

$progId = 'VstPlayer.Project'
$classes = 'HKCU:\Software\Classes'
$progKey = "$classes\$progId"
$openWithKey = "$classes\.json\OpenWithProgids"
$appKey = "$classes\Applications\VstPlayer.exe"
$verbKey = "$classes\SystemFileAssociations\.json\shell\VstPlayer.Open"

function Notify-Shell {
    # SHCNE_ASSOCCHANGED so Explorer reloads the associations without logging off.
    if (-not ('VstPlayerShell' -as [type])) {
        Add-Type -Namespace '' -Name 'VstPlayerShell' -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("shell32.dll")]
public static extern void SHChangeNotify(int wEventId, uint uFlags, System.IntPtr dwItem1, System.IntPtr dwItem2);
'@
    }
    [VstPlayerShell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
}

# New-Item -Force on an EXISTING registry key recreates it empty (its values are lost), and
# .json\OpenWithProgids usually holds other programs' entries: only create what is missing.
function Ensure-Key([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { New-Item -Path $path -Force | Out-Null }
}

function Remove-KeyIfPresent([string]$path) {
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force
        "  borrado   $path"
    }
}

if ($Unregister) {
    "Quitando la asociacion de VstPlayer (HKCU)..."
    Remove-KeyIfPresent $progKey
    Remove-KeyIfPresent $appKey
    Remove-KeyIfPresent $verbKey
    if (Test-Path -LiteralPath $openWithKey) {
        $item = Get-Item -LiteralPath $openWithKey
        if ($item.GetValueNames() -contains $progId) {
            Remove-ItemProperty -LiteralPath $openWithKey -Name $progId
            "  borrado   $openWithKey\$progId"
        }
        # Only delete keys that are left empty (other programs may use them).
        $item = Get-Item -LiteralPath $openWithKey
        if ($item.ValueCount -eq 0 -and $item.SubKeyCount -eq 0) { Remove-Item -LiteralPath $openWithKey -Force }
    }
    foreach ($k in @("$classes\SystemFileAssociations\.json\shell", "$classes\SystemFileAssociations\.json", "$classes\.json")) {
        if (Test-Path -LiteralPath $k) {
            $item = Get-Item -LiteralPath $k
            if ($item.ValueCount -eq 0 -and $item.SubKeyCount -eq 0) { Remove-Item -LiteralPath $k -Force }
        }
    }
    Notify-Shell
    "Listo."
    exit 0
}

if (-not $ExePath) {
    $root = Split-Path -Parent $PSScriptRoot
    $candidates = @((Join-Path $root 'VstPlayer.exe'), (Join-Path $root 'build\VstPlayer_artefacts\Release\VstPlayer.exe'))
    $ExePath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $ExePath) { throw "No se encontro VstPlayer.exe (probado: $($candidates -join ', ')). Usar -ExePath." }
}
if (-not (Test-Path -LiteralPath $ExePath)) { throw "No existe $ExePath" }
$exe = (Resolve-Path -LiteralPath $ExePath).Path
$command = "`"$exe`" `"%1`""
$icon = "`"$exe`",0"

"Registrando $exe para *.vstproj.json (HKCU)..."

Ensure-Key $progKey
Set-ItemProperty -LiteralPath $progKey -Name '(default)' -Value 'Proyecto de VstPlayer'
Set-ItemProperty -LiteralPath $progKey -Name 'FriendlyTypeName' -Value 'Proyecto de VstPlayer'
Ensure-Key "$progKey\DefaultIcon"
Set-ItemProperty -LiteralPath "$progKey\DefaultIcon" -Name '(default)' -Value $icon
Ensure-Key "$progKey\shell\open\command"
Set-ItemProperty -LiteralPath "$progKey\shell\open\command" -Name '(default)' -Value $command
"  escrito   $progKey"

Ensure-Key $openWithKey
New-ItemProperty -LiteralPath $openWithKey -Name $progId -PropertyType String -Value '' -Force | Out-Null
"  escrito   $openWithKey\$progId"

Ensure-Key "$appKey\shell\open\command"
Set-ItemProperty -LiteralPath $appKey -Name 'FriendlyAppName' -Value 'VstPlayer'
Set-ItemProperty -LiteralPath "$appKey\shell\open\command" -Name '(default)' -Value $command
Ensure-Key "$appKey\SupportedTypes"
New-ItemProperty -LiteralPath "$appKey\SupportedTypes" -Name '.json' -PropertyType String -Value '' -Force | Out-Null
"  escrito   $appKey"

Ensure-Key "$verbKey\command"
Set-ItemProperty -LiteralPath $verbKey -Name '(default)' -Value 'Abrir en VstPlayer'
Set-ItemProperty -LiteralPath $verbKey -Name 'Icon' -Value $icon
Set-ItemProperty -LiteralPath $verbKey -Name 'AppliesTo' -Value 'System.FileName:"*.vstproj.json"'
Set-ItemProperty -LiteralPath "$verbKey\command" -Name '(default)' -Value $command
"  escrito   $verbKey"

Notify-Shell
"Listo. Clic derecho en un .vstproj.json > 'Abrir en VstPlayer' (en Windows 11: 'Mostrar mas opciones')."
"Para abrir con doble clic: 'Abrir con > Elegir otra aplicacion > VstPlayer' y marcar 'Siempre'."
"Para quitarlo: register-filetype.ps1 -Unregister"
