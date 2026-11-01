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

## Etapa 2 — Motor de audio y escáner de plugins  (2026-11-01)
**Hecho:**
- `src/engine/AudioEngine.{h,cpp}`: posee `AudioDeviceManager`, `AudioProcessorGraph` y
  `AudioProcessorPlayer` (player → grafo, grafo con nodo de salida de audio + nodo de tono).
  Primera ejecución: fuerza `Windows Audio` (WASAPI compartido) y pide 48 kHz / 256; luego guarda
  `createStateXml()` en `settings.xml` (clave `audioDeviceState`) en cada cambio del dispositivo y al
  salir, y lo restaura al arrancar. Expone `getDeviceManager()`, `getSampleRate()`, `getBufferSize()`,
  latencia de salida y `describeDevice()`. Los cambios de sample rate / buffer llegan al grafo porque
  `AudioProcessorPlayer::audioDeviceAboutToStart` vuelve a llamar a `prepareToPlay` (verificado abajo).
- `src/engine/ToneGenerator.h`: `ToneGeneratorProcessor`, seno 440 Hz estéreo a -14 dBFS con rampa de
  20 ms (sin clicks al prender/apagar) y contadores atómicos de diagnóstico. Botón
  "Tono de prueba 440 Hz" en la ventana principal. Se quita en la etapa 6.
- `src/engine/PluginManager.{h,cpp}`: `AudioPluginFormatManager` sólo con `VST3PluginFormat`,
  `KnownPluginList` persistida en `%APPDATA%\VstPlayer\plugins.xml` (se guarda en cada cambio y al
  salir). **Escaneo fuera de proceso** con `KnownPluginList::setCustomScanner`: cada archivo candidato
  se prueba en un hijo `VstPlayer.exe --scan-one <archivo> <resultado.xml>` (`juce::ChildProcess`,
  timeout 120 s). Si el hijo crashea, se cuelga o no encuentra plugins, el archivo queda en la
  blacklist (se ve en rojo en la lista) y el host sigue vivo. El hijo desactiva los diálogos de error
  de Windows y termina con código `0xC0DE0001` ante una excepción no manejada. Log por sonda en
  `%APPDATA%\VstPlayer\plugin-scan.log`. Rutas: carpetas VST3 por defecto de JUCE
  (`C:\Program Files\Common Files\VST3` y `%LOCALAPPDATA%\Programs\Common\VST3`) + carpetas extra en
  `settings.xml`, clave `pluginExtraFolders` (separadas por `;`). Las carpetas que el usuario agrega en
  el diálogo de escaneo se guardan ahí al cerrar la ventana de plugins.
- `src/ui/PluginListWindow.{h,cpp}`: Archivo → "Plugins…" abre `PluginListComponent` con un modelo de
  tabla propio (Nombre, Fabricante, Versión, Instrumento Sí/No, Categoría, Ruta), botón "Escanear",
  menú Opciones (quitar, vaciar, quitar faltantes) y clic derecho por fila. Escaneo en un hilo de
  fondo (UI responsiva).
- Menú Audio → "Configuración de audio…": `AudioDeviceSelectorComponent` (0 entradas, 2 salidas) en un
  diálogo. `src/ui/MainContent.{h,cpp}`: UI provisoria con estado del audio, cantidad de plugins,
  error de audio (no modal) y botones Tono / Configuración de audio / Plugins.
- `src/app/Translations.{h,cpp}`: traducción al español de los textos de los componentes de JUCE
  usados (selector de audio, lista de plugins, diálogo de carpetas).
- `src/app/CommandLine.{h,cpp}`, modos sin GUI (permiten varias instancias):
  - `VstPlayer.exe --scan [--force]`: escanea (fuera de proceso), escribe `plugins.xml`, imprime la
    lista y sale. Sin `--force` no vuelve a probar lo que ya está al día ni lo que está en blacklist.
  - `VstPlayer.exe --scan-one <archivo> <out.xml>`: hijo del escáner (uso interno).
  - `VstPlayer.exe --audio-selftest [--type "<tipo>"]`: abre el audio con la config guardada, toca el
    tono a -40 dBFS, mide bloques procesados, cambia el buffer a 128 (o al más cercano disponible) y
    verifica que el grafo se re-preparó. No modifica `settings.xml`.
  - `VstPlayer.exe --audio-devices`: lista tipos de dispositivo, salida por defecto, sample rates y
    buffers disponibles.
  - Ganchos de prueba de GUI: `--show-plugins`, `--gui-scan`, `--show-audio-settings`.
  La salida va a stdout aunque el exe sea de subsistema GUI (redirigida o a la consola padre).
- Problemas encontrados y resueltos durante la verificación:
  - JUCE informa la DLL interna (`X.vst3\Contents\x86_64-win\X.vst3`) para bundles sin
    `moduleinfo.json` (Surge XT, sforzando), pero el escáner enumera la carpeta del bundle, así que
    cada escaneo los volvía a probar. Se guarda la ruta del bundle y el re-escaneo ya no los prueba.
  - Nombre del archivo temporal de resultado único por sonda (UUID); con `getNonexistentChildFile`
    dos hilos podían recibir el mismo nombre.
  - El escaneo de la GUI usa **1 hilo**: en JUCE 8.0.15 el `Scanner` de `PluginListComponent` da el
    escaneo por terminado cuando *un* hilo toma el último archivo y cancela a los otros, que perdían
    el plugin que estaban probando (se reprodujo con 2 hilos: FB-3300 desaparecía ~1 de cada 4 veces).

**Archivos:** `CMakeLists.txt`, `src/Main.cpp`, `src/app/MainWindow.{h,cpp}`,
`src/app/CommandLine.{h,cpp}`, `src/app/Translations.{h,cpp}`, `src/engine/AudioEngine.{h,cpp}`,
`src/engine/ToneGenerator.h`, `src/engine/PluginManager.{h,cpp}`, `src/ui/MainContent.{h,cpp}`,
`src/ui/PluginListWindow.{h,cpp}`, `docs/progress.md`.

**Verificación:**
- `pwsh -NoProfile -File .\tools\build.ps1` → `Build Release OK` (45–60 s incremental, por LTCG);
  `-Config Debug` → OK. Sin warnings en nuestro código.
- Escaneo por línea de comandos (`build\VstPlayer_artefacts\Release\VstPlayer.exe`):
  - Sin `plugins.xml`: `--scan` → "Archivos candidatos: 7, procesos hijos lanzados: 7, tiempo: 3.9 s",
    7 plugins: **Dexed** (Digital Suburban 1.0.0, instrumento), **Surge XT** (1.3.4, instrumento),
    Surge XT Effects, **sforzando** (Plogue 2.1.2.3, instrumento), FB-3300, The Legend HZ,
    Legend HZ FX. `plugins.xml` escrito, exit 0.
  - Segunda y tercera ejecución: "procesos hijos lanzados: **0**, tiempo: 0.5 s" (no re-escanea).
  - Plugins rotos en carpeta extra (`<scratchpad>\extra-vst3`, configurada en `pluginExtraFolders`):
    `Empty.vst3` (archivo de 0 bytes) y `Crash.vst3` (DLL compilada con `cl /LD` cuyo
    `GetPluginFactory` desreferencia un puntero nulo). Antes se comprobó que el crash es real:
    `--scan-one Crash.vst3` sale con `0xC0DE0001` sin escribir resultado. `--scan` → candidatos 9,
    hijos 2, los 7 plugins buenos intactos, "Desactivados (blacklist): 2" (Empty y Crash, `[nuevo]`),
    exit 0. Siguiente `--scan`: hijos 0 (la blacklist persiste en `plugins.xml`).
- Escaneo desde la GUI (script `verify-gui2.ps1` en el scratchpad: lanza el exe con `--gui-scan`, manda
  Enter al diálogo "Elegí las carpetas a escanear...", espera "Escaneo terminado", cierra ese aviso y
  manda `WM_CLOSE` a la ventana principal): sin `plugins.xml` y con la carpeta extra rota, 5 corridas
  Release + 5 Debug: siempre 7 plugins + 2 en blacklist, escaneo en 2–3 s, **exit code 0** (en Debug
  eso además implica que no saltó ningún `jassert` ni el detector de fugas). Sin temporales sobrantes
  en `%TEMP%`.
- Reabrir la GUI después del escaneo: `plugin-scan.log` no crece (90/90 líneas) y `plugins.xml` queda
  con el mismo hash: no re-escanea.
- GUI con `--show-audio-settings --show-plugins` (Release y Debug): aparecen "Configuración de audio" y
  "Plugins VST3"; se cierra el diálogo de audio (es modal) y luego la principal → exit 0.
- Persistencia de audio: tras el primer arranque `settings.xml` tiene
  `<DEVICESETUP deviceType="Windows Audio" audioOutputDeviceName="Altavoces (Focusrite USB Audio)"
  audioDeviceRate="48000.0" audioDeviceBufferSize="480"/>`. Editándolo a
  `Windows Audio (Exclusive Mode)` / 256, tanto `--audio-selftest` como la GUI abren ese tipo con
  buffer 256 (restauración OK); después se volvió a dejar WASAPI compartido.
- `--audio-devices` en esta máquina (Focusrite USB Audio, 48 kHz):
  - Windows Audio (compartido): buffer **sólo 480** (10 ms). Low Latency Mode: sólo 480.
  - Exclusive Mode: 144…2048 (no hay 128). DirectSound: 64…6912.
- `--audio-selftest` (tono a -40 dBFS, 2 s por paso):
  - Windows Audio: sr 48000, buffer 480, latencia de salida 480 muestras (10.0 ms), grafo preparado
    con 48000/480, 200 bloques en 2 s = 47 968 muestras/s (tiempo real), pico 0.0100 → OK. Cambio de
    buffer omitido (el tipo sólo ofrece 480).
  - Exclusive Mode: 256 → 144; el grafo se re-prepara (bloque=144, `prepareToPlay` x4); latencia
    10.67 ms → 6.00 ms. **Pero sólo renderiza ~27–29 k muestras/s de 48 k**: el modo exclusivo de JUCE
    con este driver Focusrite no da abasto o está mal sincronizado; casi seguro se oye entrecortado.
  - DirectSound: 256 → 128 OK (grafo re-preparado con 128, latencia 4 ms); ritmo de callbacks
    irregular (típico de DirectSound).
- Limpieza: se quitó de `settings.xml` la carpeta extra de prueba y se re-escaneó con
  `--scan --force` (7 plugins, 0 en blacklist). Nada se escribió en `C:\Program Files`.

**Pendiente / problemas:**
- **Buffer 128 con WASAPI no es posible en esta máquina**: el modo compartido sólo ofrece 480 muestras
  (el default pedido de 256 se redondea a 480, unos 10 ms) y el exclusivo empieza en 144 y además rinde
  por debajo de tiempo real (ver arriba). Para 128 o menos hace falta ASIO (el Focusrite tiene driver
  ASIO): poner el SDK en `third_party/asiosdk` y recompilar. El criterio "cambiar buffer a 128 no
  corta" sólo se pudo verificar por programa con DirectSound (cambio y re-preparación del grafo OK).
- **Verificación manual pendiente (usuario):**
  1. Abrir `build\VstPlayer_artefacts\Release\VstPlayer.exe`. Debe decir
     "Audio: Windows Audio / Altavoces (Focusrite USB Audio) - 48000 Hz, buffer 480…" y
     "Plugins VST3 conocidos: 7 (instrumentos: 5, desactivados: 0)".
  2. Apretar "Tono de prueba 440 Hz": debe oírse un La continuo, limpio, sin clicks al prender ni al
     apagar.
  3. Con el tono sonando, Audio → Configuración de audio…: cambiar "Tipo de dispositivo" a
     "Windows Audio (Exclusive Mode)" y buffer 256, luego 144; anotar si se entrecorta (la prueba por
     programa predice que sí). Probar también DirectSound con 128. Volver a "Windows Audio".
  4. Archivo → Plugins…: ver las columnas (nombre, fabricante, versión, instrumento, ruta), apretar
     "Escanear" → "Escanear" y confirmar que termina sin cerrar la app.
  5. Menús Archivo / Audio / Ayuda y "Acerca de" (pendiente desde la etapa 1).
- El timeout de 120 s para un plugin que se cuelga al escanear no se probó (sólo crash y archivo vacío).
- Con el diálogo modal de audio abierto, `WM_CLOSE` a la ventana principal se ignora (comportamiento de
  JUCE con ventanas modales); hay que cerrar el diálogo primero.
- El orden de la lista que imprime `--scan` se invierte entre ejecuciones (cosmético; la GUI ordena
  por columna).
