# Bitácora de etapas

Cada subagente agrega una sección al final con este formato:

```
## Etapa N — <título>  (fecha)
**Hecho:** ...
**Archivos:** ...
**Verificación:** comando(s) y resultado real
**Pendiente / problemas:** ...
```

## Etapa 0 — Entorno y repositorio  (2026-11-01)
**Hecho:**
- Instalado Visual Studio 2022 Build Tools 17.14.44 con el workload VCTools (+ recomendados) vía
  `winget install --id Microsoft.VisualStudio.2022.BuildTools --accept-package-agreements --accept-source-agreements --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"`.
  Terminó con exit 0, sin intervención. Ruta: `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`.
- **Compilador: MSVC `cl.exe` 19.44.35229 para x64** (toolset 14.44.35207). Cumple "19.3x o superior".
- `tools/dev-env.ps1`: busca la instalación con `vswhere` (requiere `VC.Tools.x86.x64`), ejecuta
  `vcvars64.bat` en cmd y copia las variables al proceso de PowerShell actual. Idempotente
  (no recarga si ya está el entorno x64; `-Force` para forzar). Como las variables de entorno
  son de proceso, alcanza con ejecutarlo; no hace falta dot-source.
- `tools/build.ps1 [-Config Debug|Release] [-Clean]`: carga el entorno, configura con Ninja y compila.
  Release (default) en `build\`, Debug en `build-debug\` (para no reconfigurar el mismo
  directorio y recompilar JUCE al alternar). `build-*/` agregado a `.gitignore`.
- `git init`, `.gitignore`, `.gitattributes` (`* text=auto`), `.editorconfig` (4 espacios, UTF-8, LF).
- `CMakeLists.txt` mínimo (C++17, falla si el compilador no es MSVC, `/W4 /permissive- /utf-8`,
  `project(VstPlayer VERSION 0.1.0)`) y `src/Main.cpp` hola mundo que imprime `_MSC_FULL_VER`.
  El ejecutable ya se llama `VstPlayer.exe`; en la etapa 1 se reemplaza por `juce_add_gui_app`.

**Archivos:** `.gitignore`, `.gitattributes`, `.editorconfig`, `CMakeLists.txt`, `src/Main.cpp`,
`tools/dev-env.ps1`, `tools/build.ps1`, `docs/progress.md`.

**Verificación:**
- `pwsh -NoProfile -File .\tools\build.ps1` → `The CXX compiler identification is MSVC 19.44.35229.0`,
  `[2/2] Linking CXX executable VstPlayer.exe`, `Build Release OK in 3,6 s`.
- `.\build\VstPlayer.exe` → `Hola desde VstPlayer (MSVC 194435229)`, exit 0.
- `.\tools\build.ps1 -Config Debug` → OK; `.\build-debug\VstPlayer.exe` imprime lo mismo.
- `cl` tras `dev-env.ps1` → "Compilador de optimización de C/C++ de Microsoft (R) versión 19.44.35229 para x64".
- Ninja usado: `C:\Users\jntac\AppData\Local\Microsoft\WinGet\Links\ninja.exe` (1.13.2); CMake 4.0.1.

**Pendiente / problemas:** ninguno. Nota: los scripts de `tools/` están en ASCII para que
también funcionen en Windows PowerShell 5.1, aunque se probaron con pwsh 7.
