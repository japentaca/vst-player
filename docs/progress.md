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

## Etapa 1 — Esqueleto JUCE  (2026-11-01)
**Hecho:**
- `cmake/juce.cmake`: `FetchContent` de JUCE con **tag `8.0.15`** (el 8.0.x más reciente según
  `git ls-remote --tags` el 2026-11-01), `GIT_SHALLOW TRUE`. Tag en la caché `VSTPLAYER_JUCE_TAG`.
- `cmake/msvc-flags.cmake`: `vstplayer_set_msvc_flags()` → `/W4 /permissive- /utf-8 /MP` (sin `/WX`).
- `CMakeLists.txt`: `juce_add_gui_app(VstPlayer PRODUCT_NAME "VstPlayer" COMPANY_NAME "Jape")`,
  módulos `juce_gui_extra`, `juce_audio_utils`, `juce_audio_processors`, `juce_audio_devices`,
  definiciones `JUCE_PLUGINHOST_VST3=1`, `VST=0`, `AU=0`, `LADSPA=0` (y además `LV2=0`),
  `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`, `JUCE_DISPLAY_SPLASH_SCREEN=0`, `JUCE_REPORT_APP_USAGE=0`.
  ASIO: si existe `third_party/asiosdk/common/iasiodrv.h` define `JUCE_ASIO=1` y agrega el include;
  hoy no está, el configure informa "ASIO disabled (WASAPI only)". Se agregó `C` a `LANGUAGES`
  (JUCE compila fuentes `.c`).
- `src/Main.cpp`: `VstPlayerApplication` (`JUCEApplication`), instancia única; en `shutdown()`
  guarda el estado de la ventana y hace `flush` de settings (cubre todas las vías de cierre).
- `src/app/MainWindow.{h,cpp}`: `DocumentWindow` con barra de título nativa, redimensionable
  (mín. 640x400, default 1024x640 centrada) y `MenuBarModel` con menús **Archivo** (Salir),
  **Audio** (ítem deshabilitado "(sin opciones todavía)") y **Ayuda** (Acerca de, con versión).
  Contenido vacío con el texto "Sin proyecto cargado".
- `src/app/AppSettings.{h,cpp}`: `juce::PropertiesFile` en `%APPDATA%\VstPlayer\settings.xml`
  (XML), clave `mainWindowState` con `getWindowStateAsString()` / `restoreWindowStateFromString()`.
  `AppSettings::getUserDataDirectory()` para los archivos de etapas siguientes.

**Archivos:** `CMakeLists.txt`, `cmake/juce.cmake`, `cmake/msvc-flags.cmake`, `src/Main.cpp`,
`src/app/MainWindow.h`, `src/app/MainWindow.cpp`, `src/app/AppSettings.h`, `src/app/AppSettings.cpp`,
`docs/progress.md`.

**Verificación:**
- `pwsh -NoProfile -File .\tools\build.ps1` (primera vez, incluye clonar JUCE y compilar todos los
  módulos): **165,4 s** → `build\VstPlayer_artefacts\Release\VstPlayer.exe`. Recompilación
  incremental tras cambiar un .cpp: 61 s (dominada por el link con LTCG).
- `.\tools\build.ps1 -Config Debug`: 123,5 s → `build-debug\VstPlayer_artefacts\Debug\VstPlayer.exe`.
- Sin warnings en nuestro código (se corrigió un C4390 por `DBG` vacío en Release). Único aviso:
  JUCE 8 informa que `JUCE_DISPLAY_SPLASH_SCREEN` se ignora (ya no tiene splash); se deja la
  definición porque la pide el plan.
- GUI por programa (script PowerShell con `Start-Process`, Win32 `GetWindowRect`/`SetWindowPos`,
  `CloseMainWindow()` + `WaitForExit`):
  - Antes no existía `%APPDATA%\VstPlayer`. Run 1: ventana con título "VstPlayer" aparece
    (1038x678 exterior, centrada); se movió/redimensionó a 150,120 900x560; `CloseMainWindow()` →
    el proceso termina solo, **exit code 0**. `settings.xml` queda con
    `<VALUE name="mainWindowState" val="158 150 886 522"/>`.
  - Runs 2–4: la ventana reabre en **151,120 900x559**: el tamaño se recuerda exacto y la
    posición es estable (sin deriva entre ejecuciones); hay 1 px de diferencia en X respecto de lo
    pedido, atribuible al redondeo de escalado DPI de JUCE (el monitor principal está escalado).
    Todas las salidas con exit code 0.
  - Lo mismo con el build Debug: exit code 0 en todos los cierres, es decir, ningún `jassert` ni
    el detector de fugas de JUCE se disparó (sin depurador, eso habría terminado el proceso con
    una excepción de breakpoint).

**Pendiente / problemas:**
- Verificación manual pendiente: abrir los menús Archivo / Audio / Ayuda con el mouse, usar
  "Acerca de" y "Salir" (no se automatizó la interacción con menús).
- Desfase de 1 px en X al restaurar la posición (cosmético, no acumulativo).
