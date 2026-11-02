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

## Etapa 3 — Instancias y grafo  (2026-11-01)
**Hecho:**
- Grafo: `AudioEngine` ahora tiene un **mezclador de salida** (nodo `GainProcessor` "Mezcla" a 0 dB con
  medidor de pico) entre las fuentes y el nodo de salida; el tono de prueba también entra por ahí.
  Por instancia: `MidiSourceProcessor` → plugin → `GainProcessor` (ganancia en dB −60…+12, mute,
  rampa de 20 ms, medidor de pico) → mezclador.
- `src/engine/InstanceGraph.{h,cpp}`: `addInstance(PluginDescription) -> InstanceId` (UUID) con
  `createPluginInstanceAsync` (la instancia aparece enseguida en estado "cargando"; si falla queda en
  estado "error" con un mensaje legible: nombre, ruta y el error de JUCE). `removeInstance`,
  `removeAllInstances`, `getInstances`, `findInstance`, `setName`, `setGainDb`, `setMute`,
  `setEditorWindowState`, `sendMidi`, `panic()`. Listeners: `instanceListChanged`,
  `instanceAboutToBeRemoved` (la UI cierra el editor **antes** de quitar los nodos),
  `instanceLoadFinished`. Todo en el hilo de mensajes; cambios agrupados con `UpdateKind::async` +
  `rebuild()` síncrono, y el plugin se destruye después del rebuild (el hilo de audio ya no lo usa).
  Una carga que termina después de quitar la instancia (o de destruir el grafo, vía `WeakReference`)
  descarta el plugin.
- **Plugins con más de 2 salidas**: se llama `disableNonMainBuses()` (apaga salidas extra y
  sidechains) y sólo los canales 0 y 1 del bus principal van al mezclador. Un plugin mono alimenta L y R.
- `src/engine/MidiSourceProcessor.h`: entrada MIDI por instancia con `MidiMessageCollector` (seguro
  desde cualquier hilo, convierte timestamps a offsets dentro del bloque; es el punto donde la etapa 4
  va a escribir desde los puertos MIDI). Pánico = bandera atómica: en el bloque siguiente descarta lo
  pendiente y emite CC64=0 + All Notes Off (CC123) + All Sound Off (CC120) en los 16 canales.
- `src/engine/Instance.h`: id, nombre editable, `PluginDescription`, estado (loading/ready/failed) y
  error, nodos (MIDI, plugin, ganancia), gain, mute, estado de ventana (abierta, posición).
- `src/ui/PluginEditorWindow.{h,cpp}`: `DocumentWindow` con el editor propio
  (`createEditorAndMakeActive`, el reemplazo no deprecado de `createEditorIfNeeded`) o
  `GenericAudioProcessorEditor` si no tiene. Una ventana por instancia: "Editor" con la ventana abierta
  la trae al frente. Cerrar la ventana guarda la posición en la instancia y destruye la ventana (y el
  editor), **no** la instancia; al reabrir vuelve a la misma posición (si sigue en alguna pantalla).
- `src/ui/InstancePanel.{h,cpp}` (UI provisoria, dentro de `MainContent`): "Agregar instancia…"
  (popup de la `KnownPluginList` agrupado por fabricante), "Pánico", una fila por instancia (nombre
  editable con doble clic, plugin/estado o error en rojo, slider de ganancia en dB, Mute, Editor,
  Quitar) y teclado virtual (`MidiKeyboardComponent`, canal 1) al pie que toca la instancia
  seleccionada (clic en la fila). El note-off va a la instancia que recibió el note-on aunque la
  selección haya cambiado (no quedan notas colgadas al cambiar de fila).
- Hooks de prueba nuevos (ver `src/app/InstanceSelfTest.h` y `src/app/CommandLine.h`):
  - `VstPlayer.exe --instance-selftest`: abre la GUI real y ejecuta sin intervención: carga Dexed y
    Surge XT (−30 dB para no aturdir), abre/cierra sus editores (uno por la API del panel y otro por
    su botón de cierre), reabre en la posición guardada, toca notas con el estado del teclado virtual
    y mide el pico en cada nodo de ganancia y en la mezcla, prueba mute y pánico, cambia el buffer con
    las instancias cargadas, carga sforzando y abre su editor, quita Surge XT con el editor abierto y
    sale con el editor de Dexed abierto. Exit code 0 sólo si todo pasó. En este modo no se guarda la
    configuración de audio ni la de la ventana.
  - `--instance-selftest --plugin "<nombre>"`: versión corta para un solo plugin.
  - `--debug-to-stdout` (cualquier modo): copia todo `OutputDebugString` del proceso a stdout como
    `[debug] ...` (handler vectorizado de excepciones sobre `DBG_PRINTEXCEPTION_C`, activo hasta el
    final del proceso, así captura también el detector de fugas).
  - `tools/run-gui-test.ps1 [-Config Debug|Release] [-AppArgs ...] [-AllowedLeaks ...]`: corre el exe
    con `--debug-to-stdout`, muestra la salida y falla si el exit code ≠ 0, si hubo `jassert` o fugas.
- **Corrección a lo dicho en las etapas 1 y 2**: sin depurador, un `jassert` en Debug **no** termina
  el proceso (JUCE sólo hace `OutputDebugString`), así que "exit 0 en Debug" no probaba ausencia de
  aserciones ni de fugas. Desde esta etapa se verifica con `--debug-to-stdout`. Primero se intentó
  escuchar el buffer DBWIN (como DebugView), pero en esta máquina otro proceso ya lo escucha y se
  perdían líneas; por eso la captura es dentro del proceso.

**Archivos:** nuevos `src/engine/Instance.h`, `src/engine/InstanceGraph.{h,cpp}`,
`src/engine/GainProcessor.h`, `src/engine/MidiSourceProcessor.h`, `src/ui/PluginEditorWindow.{h,cpp}`,
`src/ui/InstancePanel.{h,cpp}`, `src/app/InstanceSelfTest.{h,cpp}`, `tools/run-gui-test.ps1`;
cambiados `CMakeLists.txt`, `src/Main.cpp`, `src/app/CommandLine.{h,cpp}`, `src/app/MainWindow.{h,cpp}`
(tamaño por defecto 1024x700, mínimo 640x480), `src/engine/AudioEngine.{h,cpp}`,
`src/ui/MainContent.{h,cpp}`, `docs/progress.md`.

**Verificación:**
- `pwsh -NoProfile -File .\tools\build.ps1` y `-Config Debug` → OK, sin warnings en nuestro código
  (se reemplazaron dos APIs deprecadas de JUCE 8: `createEditorIfNeeded`, `Display::userArea`).
- `pwsh -NoProfile -File .\tools\run-gui-test.ps1 -Config Release` → `RESULTADO: OK`, exit 0, 23 s.
  Salida relevante (WASAPI compartido, Focusrite, 48 kHz / 480):
  - Dexed y Surge XT cargan (2 salidas, aceptan MIDI, editor propio). Editores 867x675 y 1370x854,
    visibles; cerrados → ventanas destruidas, instancias vivas, posición guardada; Dexed reabre en la
    misma posición (168, 74); "Editor" con la ventana abierta reutiliza la misma.
  - Notas por el teclado virtual (Dexed C4 y Surge XT G4 a la vez): pico Dexed −47.6 dBFS, Surge XT
    −43.9 dBFS, **mezcla (salida) −39.8 dBFS** (ganancias en −30 dB) → el ruteo
    teclado → colector → plugin → ganancia → mezcla → salida funciona. 2 eventos MIDI encolados y
    entregados por instancia.
  - Mute de Dexed: pico 0 exacto. Nota colgada en Surge XT (note-on sin note-off) suena; tras "Pánico":
    Surge XT, Dexed y mezcla en silencio.
  - Cambio de buffer con instancias cargadas: WASAPI compartido sólo ofrece 480, así que el test pasa a
    DirectSound con 128: dispositivo reabierto, Dexed/Surge/mezcla re-preparados con bloque 128 y
    siguen sonando con los mismos picos; se restaura WASAPI 480, re-preparados con 480, siguen sonando.
  - sforzando carga (2 salidas, MIDI), su editor abre (774x498, propio), se cierra y se quita.
  - Quitar Surge XT con el editor abierto: la ventana se cierra antes, queda sólo Dexed, que sigue
    sonando (pico en Dexed y en la mezcla). Salida de la app con el editor de Dexed abierto: exit 0.
- `run-gui-test.ps1 -Config Debug -AllowedLeaks VST3HostContextHeadless,AsyncUpdater` (2 corridas):
  mismos resultados, **0 aserciones de JUCE**; únicas fugas: `VST3HostContextHeadless` y su
  `AsyncUpdater` (ver Pendiente). Por plugin (`--plugin`): Dexed y Surge XT en Debug → 0 aserciones,
  0 fugas.
- GUI normal en Debug con `--debug-to-stdout`, cerrada con `CloseMainWindow()` → exit 0, sin debug output.
- `--audio-selftest` (etapa 2) sigue OK con el tono pasando por el mezclador.
- `settings.xml` conserva `Windows Audio` tras los tests (el modo self-test no persiste el audio).
- Captura de pantalla de la ventana durante el test: filas, sliders, botones y teclado bien ubicados
  a 900x559 (lógicos) con el escalado de Windows.

**Pendiente / problemas:**
- **Fuga en sforzando (lado del plugin)**: en Debug, cargar y quitar sforzando deja vivo el
  `VST3HostContextHeadless` de JUCE (objeto COM con conteo de referencias que se le pasa al plugin) y
  su `AsyncUpdater`. Ocurre aunque no se abra el editor y aunque no se llame a
  `disableNonMainBuses()` (ambas cosas probadas), y no ocurre con Dexed ni Surge XT por el mismo camino
  de código: sforzando 2.1.2.3 se queda con una referencia al host. Son unos cientos de bytes por
  instancia cargada; no afecta el funcionamiento. Por eso el test completo en Debug se corre con
  `-AllowedLeaks VST3HostContextHeadless,AsyncUpdater`.
- **Verificación manual pendiente (usuario)**, con `build\VstPlayer_artefacts\Release\VstPlayer.exe`:
  1. "Agregar instancia…" → Digital Suburban → Dexed; otra vez → Surge Synth Team → Surge XT.
  2. Clic en la fila de Dexed y tocar el teclado virtual con el mouse (o con el teclado de la PC tras
     hacer clic en el teclado virtual: A, W, S, E, D…): se oye Dexed. Clic en la fila de Surge XT y
     tocar: se oye Surge XT. Mantener una tecla con el mouse en una, cambiar de fila y tocar la otra:
     suenan las dos juntas, sin notas colgadas al soltar.
  3. Slider de ganancia y Mute de cada fila: el volumen cambia sin clicks. "Pánico" corta todo.
  4. "Editor" de cada una: abre su interfaz; moverla, cerrarla, volver a abrir → misma posición; el
     sonido sigue. Con el editor de Surge XT abierto, "Quitar" → se cierra sin colgarse.
  5. sforzando: agregarlo (Plogue → sforzando), "Editor", cargar un `.sfz` desde su interfaz
     (arrastrando el archivo o con su menú de instrumentos) y tocarlo con el teclado virtual.
  6. Con instancias sonando, Audio → Configuración de audio…: pasar a DirectSound con buffer 128 y
     volver a Windows Audio; la app no debe caerse (puede haber un corte breve de audio).
- La UI es provisoria (se reemplaza en la etapa 6): el estado de la ventana del editor se guarda en la
  instancia pero todavía no se persiste (etapa 5). Si la app se cierra mientras un plugin está
  cargando, la carga pendiente se descarta.
- Observación lateral (etapa 2): con `--show-plugins`, `CloseMainWindow()` apunta a la ventana de
  plugins y no a la principal, así que un script que sólo hace eso no cierra la app.

## Etapa 4 — Entrada MIDI y ruteo  (2026-11-01)
**Hecho:**
- `src/engine/MidiRouter.{h,cpp}`:
  - Modelo `MidiRoute { inputDevice, channelMask (bit n-1 = canal n), instanceId, remapChannel }`
    con `remapChannel` 0 = `keep` y 1..16 = `remapTo: n`. API: `getRoutes`, `setRoutes`,
    `getRoutesForInstance`, `setRoutesForInstance`, `panic`, `getPortStatus`, `getRecentMessages`,
    `getActivityCount`, `setMonitorAllPorts`, `refreshDevices`. Es un `ChangeBroadcaster` (la UI se
    refresca cuando cambian puertos o rutas). Al quitar una instancia se borran sus rutas.
  - Puertos por **nombre**. Se abren sólo los que usan las rutas (o todos, con el monitor de la ventana
    de diagnóstico). Detección de conexión/desconexión con `MidiDeviceListConnection` **y** poll cada 2 s
    de respaldo: un puerto que desaparece (o reaparece con otro identificador) se cierra y se reabre solo
    cuando vuelve; la ruta se conserva mientras tanto y la fila lo muestra "(desconectado)" en rojo.
    Un puerto que existe pero no se pudo abrir se reintenta en cada poll.
  - Entrega: el `MidiInputCallback` (hilo MIDI) busca la tabla de despacho del puerto bajo un
    `CriticalSection` y escribe en el `MidiSourceProcessor` (colector) de cada instancia ruteada, que lo
    entrega al plugin en el bloque siguiente con el offset derivado del timestamp de entrada (los
    timestamps de `MidiInput` y del colector usan la misma base, `getMillisecondCounterHiRes`).
    La tabla se reconstruye en el hilo de mensajes y se intercambia bajo el lock; una instancia que se
    quita sale de la tabla **antes** de que se destruya su nodo (`instanceAboutToBeRemoved`).
    Cerrar un puerto: se saca de la tabla bajo el lock y se hace `stop()` fuera del lock.
  - Sólo pasan mensajes de canal (0x80..0xEF); clock, active sensing, sysex, etc. se descartan (quedan
    en el log como "descartado").
  - **Notas colgadas**: el router lleva las notas sostenidas por instancia y canal de salida. Un
    note-on de una nota que ya está sostenida (retrigger sin note-off, o dos canales de origen
    remapeados al mismo canal) recibe un note-off justo antes. Al cambiar las rutas de una instancia o
    al desaparecer su puerto, se sueltan sus notas sostenidas. `panic()` olvida ese estado y llama al
    pánico del grafo (que además vacía los colectores: lo pendiente se descarta en el bloque siguiente).
- `MidiSourceProcessor`: contadores de note-on / note-off entregados al plugin por canal (diagnóstico
  y tests).
- UI (`InstancePanel`, provisoria): cada fila tiene una segunda línea "MIDI:" con LED de actividad
  (se enciende 120 ms con cada mensaje ruteado, timer de 30 Hz), selector de puerto ("(sin entrada
  MIDI)" + puertos del sistema), botón "Canales: …" que abre un call-out con 16 toggles + "Todos" /
  "Ninguno", y modo "Canal original (keep)" / "Remapear → canal n". Elegir un puerto crea la ruta
  con todos los canales y keep. La UI edita **una ruta por instancia** (el modelo admite varias).
  Pánico pasa por el router.
- `src/ui/MidiPortsWindow.{h,cpp}`: Audio → "Puertos MIDI…": estado de cada puerto (abierto / cerrado /
  DESCONECTADO / error, rutas, mensajes recibidos), log de los últimos 200 mensajes (hora, puerto,
  bytes, descripción, "→ N instancia(s)" / "sin ruta" / "descartado (sistema)"), "Limpiar log",
  "Actualizar lista" y "Escuchar todos los puertos mientras esta ventana esté abierta".
- Línea de comandos nueva: `--midi-ports` (lista entradas/salidas), `--midi-monitor [s]` (abre todas
  las entradas e imprime lo que llega), `--midi-send "<puerto de salida>" 90:3C:64 +50 80:3C:00 …`
  (emisor externo: bytes hex separados por `:`, `+N` = esperar N ms). Gancho de GUI `--show-midi-ports`.
- Self-test nuevo `--midi-selftest` (en `src/app/MidiSelfTest.cpp`, misma infraestructura de pasos que
  `--instance-selftest`, que sigue igual): el MIDI lo manda **otro proceso** (`VstPlayer.exe
  --midi-send` lanzado con `ChildProcess`) a la salida de "loopMIDI Port", y la app escucha su entrada.
  Con `--restart-loopmidi` además termina `loopMIDI.exe` y lo vuelve a lanzar minimizado (prueba de
  borrar y recrear el puerto). `src/app/ProcessTools.{h,cpp}`: helpers Win32 para eso.

**Archivos:** nuevos `src/engine/MidiRouter.{h,cpp}`, `src/ui/MidiPortsWindow.{h,cpp}`,
`src/app/MidiSelfTest.cpp`, `src/app/ProcessTools.{h,cpp}`; cambiados `CMakeLists.txt`, `src/Main.cpp`,
`src/app/CommandLine.{h,cpp}`, `src/app/InstanceSelfTest.{h,cpp}`, `src/app/MainWindow.{h,cpp}`,
`src/engine/MidiSourceProcessor.h`, `src/ui/InstancePanel.{h,cpp}`, `src/ui/MainContent.{h,cpp}`,
`docs/progress.md`.

**Verificación:**
- `pwsh -NoProfile -File .\tools\build.ps1` y `-Config Debug` → OK, sin warnings en nuestro código.
- Puertos en esta máquina (`VstPlayer.exe --midi-ports`): entrada y salida **"loopMIDI Port"** (único
  puerto configurado en loopMIDI, `HKCU\Software\Tobias Erichsen\loopMIDI\Ports`); además la salida
  "Microsoft GS Wavetable Synth". `--midi-send "loopMIDI Port" 90:3C:64 +50 80:3C:00` con
  `--midi-monitor 4` escuchando → el monitor imprime el note-on y el note-off (loopback confirmado).
- `.\tools\run-gui-test.ps1 -Config Release -AppArgs '--midi-selftest','--restart-loopmidi'` (desde
  PowerShell con `&`, no con `pwsh -File`, que no separa el array) → **RESULTADO: OK**, 225 pasos, 61 s,
  exit 0. Instancias A = Dexed, B = Surge XT, C = Dexed (−30 dB). Rutas puestas por los mismos
  métodos que usa la UI: A ← canal 1 keep; B ← canales 2,3 remapTo 1; C ← canal 4 keep. Por cada caso se
  compara cuántos note-on recibió cada plugin en cada canal (contadores del `MidiSourceProcessor`) y el
  pico en el nodo de ganancia:
  - canal 1 → sólo A en canal 1 (−47.6 dBFS); canal 2 y 3 → sólo B **en canal 1** (remap, −43.5 dBFS);
    canal 4 → sólo C en canal 4; canal 5 → nadie, y el log lo registra "sin ruta".
  - B cambiado a keep por la UI: canal 2 → B en canal 2; canal 3 → B en canal 3.
  - C con canales 1 y 4: canal 1 → A y C a la vez.
  - Las filas muestran puerto, "Canales: 2, 3", "Remapear → canal 1"; los tres LED se encendieron.
  - Retrigger estilo carrilion (on, off, +20 ms, on, +300 ms, off): A recibe 2 on / 2 off y a los 2.5 s
    está en silencio. Note-on repetido sin note-off: el router agrega el note-off (2 on / 2 off) y A
    queda en silencio. Dos canales remapeados a uno con la misma nota y un solo note-off: B en silencio.
  - Nota sostenida en A y cambio de canal de la ruta con la tecla apretada: el router manda el
    note-off, A en silencio sin el note-off de la fuente.
  - Pánico con notas sostenidas por el puerto en A y C → silencio; la misma nota después del pánico no
    genera un note-off de retrigger extra (el estado se limpió).
  - Clock, active sensing y un sysex: ninguna instancia recibe eventos; 4 entradas "descartado" en el log.
  - Ventana Audio → Puertos MIDI: "loopMIDI Port  abierto  rutas: 3  mensajes: 40" y las últimas líneas
    del log, p. ej. `93 48 64  Note on C4 Velocity 100 Channel 4  → 1 instancia(s)`.
  - **Borrar y recrear el puerto con la app abierta** (`--restart-loopmidi`): con una nota sostenida en
    A se terminó `loopMIDI.exe` → el router detectó la desconexión, soltó la nota (A en silencio), la
    ruta siguió apuntando a "loopMIDI Port" y la fila mostró "loopMIDI Port (desconectado)". Se relanzó
    loopMIDI minimizado → el router reabrió el puerto solo y canal 1 → A, canal 2 → B (remap), canal 4 → C
    volvieron a llegar y sonar. loopMIDI quedó corriendo otra vez con su puerto (verificado con
    `--midi-ports`).
  - Quitar C borra sus rutas (canal 4 → nadie); sacar la entrada de A y B cierra el puerto; se sale con el
    puerto abierto.
- Lo mismo en Debug (`-Config Debug -AppArgs '--midi-selftest'`, sin reiniciar loopMIDI): RESULTADO OK,
  **0 aserciones de JUCE, 0 fugas**.
- Test de la etapa 3 sin cambios: `run-gui-test.ps1 -Config Release` → OK (59 pasos, exit 0);
  `-Config Debug -AllowedLeaks VST3HostContextHeadless,AsyncUpdater` → OK, 0 aserciones (sólo la fuga
  conocida de sforzando).
- GUI normal con `--show-midi-ports`: ventana "Puertos MIDI" visible (captura revisada), cerrada por
  `WM_CLOSE` junto con la principal → exit 0. Captura durante el self-test: filas con la línea MIDI
  bien ubicadas a 1024 de ancho. `settings.xml` sigue en "Windows Audio".
- **Latencia que reporta `AudioDeviceManager`** (Focusrite, 48 kHz; WASAPI compartido sólo ofrece 480, así
  que 256/128 son con DirectSound, como en la etapa 3):

  | Tipo | Buffer | `getOutputLatencyInSamples` | `getInputLatencyInSamples` |
  |---|---|---|---|
  | DirectSound | 256 (5.33 ms) | 384 (8.00 ms) | 384 |
  | DirectSound | 128 (2.67 ms) | 192 (4.00 ms) | 192 |
  | Windows Audio (compartido) | 480 (10 ms) | 480 (10.0 ms) | — |

  Con DirectSound 128 el ruteo canal 1 → A también se verificó (señal en A). A esto se suma hasta un
  bloque de espera en el colector MIDI (el mensaje entra en el bloque siguiente a su llegada).

**Pendiente / problemas:**
- **Verificación manual pendiente (usuario), con carrilion en Chrome:**
  1. loopMIDI corriendo con "loopMIDI Port". Abrir `build\VstPlayer_artefacts\Release\VstPlayer.exe`.
  2. Agregar instancia → Dexed; otra → Surge XT. En la fila de Dexed: MIDI → "loopMIDI Port",
     "Canales: …" → "Ninguno" y marcar 1. En Surge XT: "loopMIDI Port", canales 2 y 3,
     "Remapear → canal 1".
  3. Audio → "Puertos MIDI…": debe decir "loopMIDI Port  abierto  rutas: 2".
  4. En carrilion (Chrome) elegir "loopMIDI Port" en su selector de salida MIDI y reproducir algo que use
     los canales 1, 2 y 3: el canal 1 debe sonar con Dexed y el 2 y 3 con Surge XT; los LED parpadean y
     el log de "Puertos MIDI" muestra "→ 1 instancia(s)". Cambiar Surge XT a "Canal original (keep)" y
     verificar que sigue sonando.
  5. Activar el modo piano de carrilion (política retrigger) y tocar notas sostenidas y repetidas; al
     parar no debe quedar ninguna nota sonando. Si queda alguna, "Pánico" la corta.
  6. Con la app abierta, en la ventana de loopMIDI borrar "loopMIDI Port" (botón "-") y volver a crearlo
     con el mismo nombre ("+"): la fila pasa a "(desconectado)" en rojo y a los ~2 s vuelve sola; Chrome
     puede necesitar volver a elegir el puerto (eso es del lado de Web MIDI).
- El "borrar y recrear" automatizado se hizo terminando y relanzando `loopMIDI.exe` (mismo efecto para
  el driver; no se tocó la configuración de loopMIDI). loopMIDI quedó relanzado **minimizado** con su
  ventana en la barra de tareas en lugar de sólo en la bandeja, como al iniciar sesión. Ese paso es
  opcional (`--restart-loopmidi`) para no cortar el puerto si el usuario lo está usando.
- Se intentó un puerto virtual propio con `teVirtualMIDI64.dll` (instalado por loopMIDI) para no tocar
  loopMIDI: el puerto aparece en el sistema pero no pasan datos en ninguna dirección (probado con
  varias combinaciones de flags desde PowerShell y desde la app); se descartó.
- La UI edita una sola ruta por instancia; varias rutas por instancia (o rutas desde varios puertos) ya
  las soporta el router y llegarán por el proyecto (etapa 5) o la API (etapa 7).
- Las rutas no se persisten todavía (etapa 5): al reabrir la app hay que volver a elegir puerto y canales.
- Si dos puertos del sistema tienen el mismo nombre, se usa el primero.
- Con remap, dos canales de origen que tocan la misma nota se fusionan: el segundo note-on corta al
  primero (note-off agregado) y el primer note-off que llegue apaga la nota (no queda colgada, pero
  puede cortarse antes de lo que la fuente pretendía).

## Etapa 5 — Proyectos (modelo y JSON)  (2026-11-01)
**Hecho:**
- `src/project/Project.{h,cpp}`: modelo en memoria sólo con `juce_core` (`Project`, `AudioSetup`,
  `InstanceData`, `PluginRef`, `WindowState`, `ParamOverride`, `MidiRouteData`). Cada objeto guarda en
  `extra` (`NamedValueSet`) las propiedades JSON que no conoce. Esquema v1 documentado en
  **`docs/project-format.md`** (igual al del plan, con `plugin.format` agregado y `channelMode` =
  `"keep"` o `{ "remapTo": n }`).
- `src/project/ProjectSerializer.{h,cpp}`: `toVar/toJson`, `fromVar/fromJson` (tolerante: tipos
  equivocados → valor por defecto + aviso; JSON inválido o raíz no-objeto → error), `loadFile`,
  `saveFile` con **escritura atómica** (`TemporaryFile` en la misma carpeta + rename), estado en
  **base64 estándar** (`juce::Base64`, no el formato propio de `MemoryBlock`). Campos desconocidos se
  reescriben después de los conocidos en su orden original, en todos los niveles. Un archivo con
  `version` mayor a 1 se abre con aviso y se vuelve a guardar con esa versión.
- `src/project/ProjectController.{h,cpp}` (integración):
  - `apply()`: audio primero (`AudioEngine::applyDeviceSetup`, no reabre si ya coincide; avisa si el
    tipo/dispositivo no existe o si el dispositivo redondea el pedido), después instancias con el mismo
    `id` del archivo (carga asíncrona; el `state` y luego los `params` por nombre se aplican en un hook
    `prepare` **antes** de que el plugin entre al grafo), rutas MIDI, y reapertura de los editores que
    estaban abiertos, en su posición, cuando el plugin está listo. Progreso "Cargando proyecto: N de M
    instancias…" en la línea de proyecto.
  - Resolución del plugin: `uniqueId` → ruta → nombre (mismo fabricante primero). Si no se encuentra:
    instancia **faltante** (`Instance::State::missing`, fila en rojo "FALTA: …"), aviso al terminar de
    abrir, y al guardar se reescribe tal cual (plugin, estado, params, ventana, campos desconocidos). Lo
    mismo para un plugin que existe pero falla al cargar.
  - `capture()`: `getStateInformation` de cada plugin, ganancia, mute, nombre, ventanas (posición actual
    de los editores abiertos), rutas, audio actual; arrastra `extra`, `params` (actualizados al valor
    actual) y el `extra` de cada ruta.
  - "Cambios sin guardar": contadores de edición de `InstanceGraph` (agregar, quitar, renombrar,
    ganancia, mute) y `MidiRouter` (rutas) + `AudioProcessorListener` en cada plugin (cambios de
    parámetro, de programa o de estado no-parámetro). Se ignoran las notificaciones de los 2.5 s
    posteriores a una carga (los plugins las emiten solos al restaurar estado). Abrir/cerrar/mover un
    editor no marca cambios (la posición se toma al guardar).
  - `recent.json` (máx. 10, el más reciente primero) y **autoguardado** cada 2 min si hay cambios en
    `autosave.vstproj.json` (no cambia el archivo ni el `*`; se borra al guardar).
- `InstanceGraph`: `addInstance (desc, AddOptions)` (id, nombre, ganancia, mute, ventana, `prepare`),
  `addMissingInstance`, `getEditCounter()`. `MidiRouter::getEditCounter()`. `AudioEngine::applyDeviceSetup`.
  `InstancePanel::syncEditorWindowStates`, `closeAllEditors`, fila faltante en rojo, `RowInfo` con el
  texto de estado. `MainContent::setProjectInfo` (línea superior "Proyecto: nombre *  (ruta) — progreso").
- `MainWindow`: Archivo → **Nuevo, Abrir…, Guardar, Guardar como…, Recientes ▸** (+ "Limpiar la lista"),
  Plugins…, Salir. Título "VstPlayer — nombre" con " *" si hay cambios. Nuevo / Abrir / Recientes /
  Salir / botón de cierre preguntan "Cambios sin guardar" (Guardar / No guardar / Cancelar, async; Guardar
  en un proyecto sin archivo abre "Guardar como"). Errores y avisos de carga en `AlertWindow` async.
  Recuerda la última carpeta de proyectos en settings (no en modo prueba).
- `Main.cpp`: **`VstPlayer.exe ruta\proyecto.vstproj.json`** abre ese proyecto (primer argumento que no
  es opción y termina en `.json`); si la app ya está abierta, la segunda invocación se lo pasa a la
  primera (`anotherInstanceStarted`). `systemRequestedQuit` pasa por la pregunta de cambios.
  `--project-data-dir <dir>`: carpeta alternativa para `recent.json` y el autoguardado. En los
  self-tests sin esa opción no se escribe ninguno de los dos, no hay diálogos modales (los mensajes van
  a stdout) y la pregunta de cambios se responde "No guardar" sola.
- Tests:
  - `tests/ProjectRoundTripTest.cpp` → ejecutable `ProjectRoundTripTest` (consola, sólo `juce_core`)
    registrado en **ctest** (`project_roundtrip`): round-trip de un proyecto sintético (texto idéntico,
    estado binario de 100 KB byte a byte, unicode, comillas y barras), campos desconocidos de una
    "versión 3" en todos los niveles, lectura tolerante y errores, archivos (carpeta nueva, sobrescritura
    sin temporales sobrantes, LF) y los dos proyectos de `examples/`.
  - Self-test `--project-selftest <fase>` (`src/app/ProjectSelfTest.cpp`) y **`tools/run-project-test.ps1`**,
    que corre 4 procesos seguidos (cada uno por `run-gui-test.ps1`, o sea con control de aserciones y
    fugas) en una carpeta temporal con su propio `--project-data-dir`:
    1. `save`: carga Dexed, Surge XT y sforzando por la ruta de la UI; Dexed → programa 5 ("Lavitar Y") y
       3 parámetros; Surge XT → 3 parámetros (macros M1–M3); sforzando → estado nuevo con
       `globalTuning=7` y **un instrumento SFZ cargado** (`seno-de-prueba.sfz`, `sample=*sine`): el estado
       de sforzando es un XML `AriaSave` comprimido con zlib dentro del chunk VST3, y agregándole
       `<Slot id="0" name="ruta.sfz"/>` sforzando carga el SFZ (verificado: suena, entre −11 y −15 dBFS según la corrida). Ganancias
       −30 / −31.5 / −33 dB, nombre "Pads Surge", rutas (Dexed ← loopMIDI canal 1 keep y canal 10;
       Surge ← canales 2,3 remapTo 1 por los métodos de la fila; sforzando ← "Puerto MIDI Fantasma"),
       nivel de una nota A4 en cada una, mute de sforzando, editor de Dexed abierto en (140,160), el de
       Surge movido a (260,210) y cerrado. "Guardar como" `prueba` → `prueba.vstproj.json` (105 KB) y una
       huella con todo lo observable.
    2. `verify-cli`: `VstPlayer.exe <dir>\prueba.vstproj.json …` (camino de la línea de comandos).
    3. `verify-recent`: arranca vacío y abre Recientes[0] (leído de `recent.json`) con el mismo método
       que el menú.
    4. `extras`: proyecto derivado con un plugin inexistente, campos desconocidos en todos los niveles,
       `version: 2`, un override `params` y un parámetro inexistente.
- `examples/prueba-etapa5.vstproj.json` (Dexed canal 1, Surge XT canales 2-3 → 1, sforzando canal 4,
  sin estado), `examples/plugin-faltante.vstproj.json` (Dexed + "Pianoteq 8" que no está instalado +
  campos desconocidos) y `examples/sfz/seno.sfz` (SFZ mínimo con ondas generadas por sforzando).

**Archivos:** nuevos `src/project/Project.{h,cpp}`, `src/project/ProjectSerializer.{h,cpp}`,
`src/project/ProjectController.{h,cpp}`, `src/project/RecentProjects.{h,cpp}`, `src/app/ProjectSelfTest.cpp`,
`tests/ProjectRoundTripTest.cpp`, `tools/run-project-test.ps1`, `docs/project-format.md`,
`examples/prueba-etapa5.vstproj.json`, `examples/plugin-faltante.vstproj.json`, `examples/sfz/seno.sfz`;
cambiados `CMakeLists.txt`, `src/Main.cpp`, `src/app/MainWindow.{h,cpp}`, `src/app/InstanceSelfTest.{h,cpp}`,
`src/engine/Instance.h`, `src/engine/InstanceGraph.{h,cpp}`, `src/engine/MidiRouter.{h,cpp}`,
`src/engine/AudioEngine.{h,cpp}`, `src/ui/InstancePanel.{h,cpp}`, `src/ui/MainContent.{h,cpp}`, `docs/progress.md`.

**Verificación:**
- `pwsh -NoProfile -File .\tools\build.ps1` y `-Config Debug` → OK, sin warnings en nuestro código.
- `ctest --test-dir build` y `ctest --test-dir build-debug` (con el entorno de `tools\dev-env.ps1`) →
  `100% tests passed` (`ProjectRoundTripTest`: **106/106 comprobaciones**).
- `pwsh -NoProfile -File .\tools\run-project-test.ps1` (Release) → **PRUEBA DE PROYECTOS: OK**, 37 s, las
  4 fases con exit 0. Resultados al reabrir (igual por línea de comandos y por Recientes):
  - mismos ids, nombres, ganancias, mute, audio (Windows Audio / Altavoces (Focusrite USB Audio) /
    48000 / 480) y rutas (las 4, con keep / remapTo y el puerto inexistente);
  - **getStateInformation idéntico byte a byte** en Dexed (8344 bytes) y sforzando (758 bytes, con el
    SFZ cargado y `globalTuning=7`); Surge XT: 69508 vs 69509 bytes (Surge reescribe su XML) pero sus
    **2855 parámetros** iguales (tolerancia 1e-4); Dexed: 2238 parámetros iguales y programa 5
    "Lavitar  Y"; los 6 parámetros cambiados conservan el valor guardado;
  - **mismo nivel de audio** con una nota A4 en cada instancia: Dexed −52.1, Surge XT −45.4, sforzando
    −44.0 dBFS, idénticos a los medidos antes de guardar;
  - editor de Dexed reabierto solo en (140,160); Surge cerrado con su posición (260,210) guardada;
  - título "VstPlayer — prueba" sin `*` y sin cambios tras abrir; única advertencia: el puerto
    "Puerto MIDI Fantasma" que no existe; mover un parámetro de Surge (misma notificación que su editor)
    pone el `*`.
  - fase `extras`: abre con 4 instancias, la faltante "Instrumento desinstalado" en la lista con
    `State::missing`, fila en rojo "PluginQueNoExiste — FALTA: …", aviso mostrado (en modo prueba va a
    stdout); el override `params` de Dexed ("Cutoff" = 0.25) se aplica sobre el estado; cambio de
    ganancia → `*`; autoguardado (intervalo bajado a 1 s) escrito en la carpeta de datos alternativa sin
    tocar el archivo ni el `*`; "Cambios sin guardar" → Cancelar no hace nada; "Guardar como" borra el
    autoguardado; en el archivo re-guardado siguen idénticos el bloque `plugin` de la faltante, su
    `state`, `params`, `window` (con `"monitor": 2`), `effects`, los campos `futureField`,
    `audio.clockSource`, `color`, `plugin.sha256`, `velocityCurve` de la ruta y `version: 2`; "Cambios sin
    guardar" → Guardar escribe el archivo (Dexed −12.25 dB) antes de "Nuevo"; un archivo JSON roto da
    error y deja el proyecto anterior.
- Lo mismo en Debug (`run-project-test.ps1 -Config Debug -AllowedLeaks VST3HostContextHeadless,AsyncUpdater`)
  → OK, **0 aserciones de JUCE**, sólo la fuga conocida de sforzando (etapa 3).
- Proyectos de ejemplo abiertos en la app (`VstPlayer.exe examples\X.vstproj.json --project-selftest open
  --project-test-dir <tmp>`): `prueba-etapa5` → 3 instancias cargadas, 3 rutas, sin avisos;
  `plugin-faltante` → Dexed cargado + "Piano que no esta instalado (Pianoteq 8): FALTA", aviso.
- Tests de etapas anteriores, sin cambios: `run-gui-test.ps1` (instancias) Release OK y Debug OK (fuga
  conocida); `-AppArgs '--midi-selftest'` Release OK (49.5 s) y Debug OK, 0 fugas.
- `%APPDATA%\VstPlayer` después de todo: sólo `plugin-scan.log`, `plugins.xml` y `settings.xml` (este
  último sin modificar); ni `recent.json` ni autoguardado de prueba. Carpetas temporales borradas.

**Pendiente / problemas:**
- **Verificación manual pendiente (usuario)**, con `build\VstPlayer_artefacts\Release\VstPlayer.exe` y
  loopMIDI corriendo:
  1. Archivo → Abrir… → `examples\prueba-etapa5.vstproj.json`. Se cargan Dexed, "Pads" (Surge XT) y
     "Sampler" (sforzando); el título dice "VstPlayer — Prueba etapa 5".
  2. "Editor" de Dexed: elegir otro preset (otro cartucho/programa). "Editor" de Pads: cargar otro patch
     de Surge XT. "Editor" de Sampler: cargar un `.sfz` (cualquiera, o arrastrar
     `examples\sfz\seno.sfz`). Dejar abierto, por ejemplo, el editor de Dexed en algún lugar de la
     pantalla. Tocar cada uno con el teclado virtual (o desde carrilion: canal 1 Dexed, 2-3 Pads, 4 Sampler)
     y recordar cómo suenan. El título muestra `*`.
  3. Archivo → Guardar como… → por ejemplo `Documentos\mi-prueba.vstproj.json`. El `*` desaparece.
  4. Cerrar la app (botón de cierre). Volver a abrirla: arranca vacía ("Sin título").
  5. Archivo → Recientes → 1. `…\mi-prueba.vstproj.json`. Sin tocar nada: mismos presets/patch/SFZ,
     mismas ganancias y rutas (fila MIDI de cada instancia), editor de Dexed abierto donde estaba, y
     suena igual. El título no tiene `*`.
  6. Cambiar una ganancia → aparece `*`; cerrar → pregunta "Cambios sin guardar"; "Cancelar" deja la app
     abierta.
  7. Archivo → Abrir… → `examples\plugin-faltante.vstproj.json` → aviso "Falta el plugin Pianoteq 8…" y
     esa fila en rojo; Guardar como… otro archivo y comprobar con un editor de texto que la instancia de
     Pianoteq, su `state` y `"color"` / `"comentario"` siguen ahí.
  8. Opcional: `build\VstPlayer_artefacts\Release\VstPlayer.exe "C:\…\mi-prueba.vstproj.json"` desde una
     consola abre ese proyecto directamente.
- **Caché de parámetros del host (JUCE VST3)**: cuando un plugin cambia sus propios parámetros sin
  avisarle al host (Dexed al cambiar de programa, sforzando al cargar un instrumento), los valores que
  devuelve `AudioProcessorParameter::getValue()` del lado del host quedan viejos hasta el siguiente
  `setStateInformation`. No afecta lo que se guarda ni cómo suena (el estado del plugin es la fuente), pero
  el test sincroniza esa caché (reaplicando a cada plugin su propio estado) antes de tomar la huella, y
  un override `params` de un parámetro así se guardaría con el valor viejo. Relevante para la etapa 7
  (`listParams`). sforzando además reporta a veces valores fuera de 0..1 en un macro (p. ej. 120); el test
  los informa y no los compara.
- sforzando sin instrumento cargado no guarda nada de sus 512 macros (dependen del instrumento). El SFZ
  cargado desde su propia interfaz queda como verificación manual (paso 2); por programa se cargó con un
  estado armado a mano (ver fase `save`).
- La ruta del SFZ dentro del estado de sforzando es absoluta: un proyecto con sforzando apunta a esa ruta
  en disco (comportamiento del plugin, no del formato).
- Surge XT devuelve un estado que difiere en 1 byte después de restaurarlo (los parámetros y el nivel son
  iguales); por eso la comparación byte a byte es informativa para Surge.
- No hay recuperación automática del autoguardado al arrancar: se abre a mano desde
  `%APPDATA%\VstPlayer\autosave.vstproj.json` (documentado en `docs/project-format.md`).
- "Cambios sin guardar" se basa en contadores: cambiar algo y volver al valor anterior también deja el `*`.
- La invocación `VstPlayer.exe proyecto.json` con la app ya abierta (`anotherInstanceStarted`) no tiene
  test automático. Asociar la extensión queda para la etapa 8. Atajos de teclado (Ctrl+S, etc.) quedan para
  la etapa 6.
- Observación lateral: `plugins.xml` se reescribe (mismo contenido) en cada salida de la app, también en
  los self-tests (comportamiento de `PluginManager` desde la etapa 2).

## Etapa 6 — Interfaz definitiva  (2026-11-01)
**Hecho:**
- **Layout** (`src/ui/MainContent.{h,cpp}`, reescrito):
  - Barra superior, línea 1: nombre del proyecto (con `*`; ruta en el tooltip), botones **Nuevo, Abrir…,
    Recientes ▾** (popup con la lista de `recent.json` + "Limpiar la lista"), **Guardar, Guardar como…** y
    **PÁNICO** (rojo). Línea 2: dispositivo de audio (botón: abre la configuración de audio; el nombre se
    acorta con "…" si no entra, tipo + nombre completo en el tooltip), sample rate · buffer · latencia,
    **CPU %** (`AudioDeviceManager::getCpuUsage`, ~4 veces por segundo, naranja > 50 %, rojo > 80 %),
    vúmetro estéreo de la mezcla, **Puertos MIDI…** y **Plugins…**.
  - Centro (`src/ui/InstancePanel.{h,cpp}`, reescrito sobre la base de las etapas 3–5): encabezado
    "Instancias (N)" + "+ Agregar instancia…", lista desplazable de filas, **teclado virtual plegable**
    (botón "▾/▸ Teclado" con la instancia destino; el estado plegado se guarda en `settings.xml`, clave
    `keyboardVisible`, salvo en los self-tests).
  - Barra de estado: último mensaje (errores en rojo: carga fallida, error de audio, no se pudo abrir/guardar;
    "Guardado: …", "Abierto: …", "X cargado (N ms)", "Pánico…"), **indicador de progreso** (barra animada +
    "Cargando N plugins…" / "Escaneando plugins…") y recordatorio de atajos.
- **Fila de instancia** (2 líneas, 64 px): nombre editable (doble clic), plugin — fabricante (o "cargando…" /
  "ERROR" / "FALTA" en rojo, con la fila en rojo), **vúmetro estéreo** de la salida de la instancia (pico por
  canal medido en su `GainProcessor`, un `std::atomic<float>` por canal con retención hasta que la UI lo lee,
  escala −60…+6 dBFS, caída ~45 dB/s), slider de ganancia en dB, **M** (mute), **Editor**, **Duplicar**,
  **Quitar**; línea 2: LED MIDI, puerto, "Canales: …" (call-out con 16 toggles), keep / remapear. Mientras
  carga muestra una barra de progreso animada en lugar del vúmetro.
- **Duplicar** (`InstanceGraph::duplicateInstance` + `InstancePanel::duplicateInstance`): nueva instancia del
  mismo plugin con el `getStateInformation` del original aplicado antes de entrar al grafo, misma ganancia y
  mute, nombre "X (copia)" y las mismas rutas MIDI.
- **Agregar instancia** (`src/ui/AddInstanceDialog.{h,cpp}`): ventana no modal con búsqueda (nombre,
  fabricante, categoría; varias palabras = todas), "Sólo instrumentos" (activo por defecto), lista agrupada por
  fabricante (encabezados no seleccionables), versión e instrumento/efecto por fila, contador "N de M plugins",
  "Plugins…" para escanear; doble clic / Enter / "Agregar" agrega y cierra; Esc / "Cancelar" cierra. Se
  actualiza sola si la lista de plugins cambia (escaneo con el diálogo abierto).
- **Look and feel** (`src/ui/Theme.{h,cpp}`): `DarkLookAndFeel` (LookAndFeel_V4 con esquema oscuro propio,
  tipografía por defecto, botones planos, sin iconos externos), instalado como look and feel por defecto en
  `Main.cpp` (lo usan también el diálogo de audio, la lista de plugins y "Puertos MIDI").
- **Atajos** con `ApplicationCommandManager` (se muestran en los menús): **Ctrl+N, Ctrl+O, Ctrl+S,
  Ctrl+Shift+S, Esc = pánico** (también Audio → Pánico en el menú). Escuchan a nivel de ventana, así que
  funcionan con el foco en cualquier control de la ventana principal; Esc mientras se renombra cancela la
  edición (lo consume el editor de texto).
- **Carga de plugins sin congelar la UI** (`InstanceGraph`): JUCE crea los VST3 en el hilo de mensajes
  (incluso con `createPluginInstanceAsync`), así que ahora las creaciones van a una **cola**: una por vez,
  cada una 50 ms después de la anterior (la UI pinta la fila "cargando", cierra el diálogo y procesa eventos
  entre una y otra), y antes de cada una el **DLL del plugin se carga en un hilo de fondo** (`LoadLibraryEx`:
  la carga del binario y su inicialización estática no ocurren en el hilo de mensajes; JUCE después lo
  encuentra ya cargado). La creación es sincrónica dentro de su turno para poder medirla
  (`getLastCreationMs`, `getMaxCreationMs`, `getLastPreloadMs`).
  El escaneo ya corría en hilos de fondo con procesos hijos (etapa 2); ahora además la barra de estado muestra
  "Escaneando plugins…" mientras dura (`PluginListWindow::isScanning`).
- **Generador de tono quitado**: borrado `src/engine/ToneGenerator.h`, su nodo y la API de `AudioEngine`, y el
  botón de la UI provisoria. `--audio-selftest` **se conserva** pero ahora es silencioso: mide sobre el nodo de
  mezcla (siempre presente) los bloques procesados por segundo, los argumentos de `prepareToPlay`, el tamaño de
  bloque y la CPU, y sigue verificando que cambiar el buffer re-prepara el grafo. Ya no verifica un pico de
  señal (no hay fuente); el sonido real lo cubren los self-tests con plugins. `GainProcessor` ganó
  `getAndResetMeterPeak(ch)`, `getPrepareCount()` y `getLastBlockSize()`.
- `MainWindow`: botones de la barra conectados a las mismas acciones que el menú, menú "Recientes" reutilizado
  por el botón, mensajes también en la barra de estado, `fileChooserOverride` (los self-tests reemplazan el
  diálogo nativo de archivos), tamaño mínimo 860x540 (lógicos, sin el marco).
- **Self-test nuevo `--ui-selftest`** (`src/app/UiSelfTest.cpp`) y **`tools/run-ui-test.ps1`** (carpeta temporal
  con `--project-test-dir` y `--project-data-dir`; capturas copiadas a `build\ui-test\` o `build-debug\ui-test\`;
  verifica que `plugins.xml` quede idéntico y que no queden `recent.json` / autoguardado en `%APPDATA%`).
  80 pasos, sin diálogos ni intervención:
  - **Layout por programa**: recorre todos los componentes visibles de la ventana y falla si un hijo se sale
    de su padre, si dos hermanos visibles se superponen, si un control mide menos de 24x18 o si el texto de un
    botón no entra sin comprimirse (misma cuenta que `LookAndFeel_V2::drawButtonText`). Se corre a 1024x600
    (vacía, con 3 instancias, con el teclado plegado), 1600x1000, en el mínimo y con
    `Desktop::setGlobalScaleFactor (1.5)`.
  - **Capturas** con `PrintWindow` (lo que dibuja Windows, en píxeles físicos) de la ventana y del diálogo.
  - Diálogo de agregar: filtro de instrumentos, agrupado y ordenado, búsqueda por nombre y por fabricante, Esc
    (con el foco real en el cuadro de búsqueda); agrega Surge XT, sforzando y Dexed por el diálogo.
  - **Responsividad**: un timer de 5 ms en el hilo de mensajes mide el hueco máximo durante cada carga, la
    carga de dos plugins a la vez, la apertura de un proyecto de 4 plugins y un escaneo real.
  - Duplicar (estado byte a byte, programa, ganancia, rutas), vúmetros (instancia, copia en silencio, mezcla),
    CPU y audio en la barra, indicador de progreso al abrir un proyecto.
  - Atajos enviados como pulsaciones a la ventana (`ComponentPeer::handleKeyPress`, el mismo camino que una
    tecla real: van al componente con foco y suben por los padres): Esc con el foco en el teclado virtual y en un
    combo; Esc además como **entrada real del sistema con `SendInput`** (la ventana estaba en primer plano);
    Ctrl+S sin archivo (Guardar como), Ctrl+S con cambios (mismo archivo), Ctrl+N, Ctrl+O (reabre el proyecto).
  - Escaneo desde la UI: vacía la `KnownPluginList`, re-escanea los mismos 7 archivos (procesos hijos) con la
    ventana de plugins y al final restaura el XML original exacto.
- Self-tests anteriores: no hizo falta cambiarlos (la API pública de `InstancePanel` se mantuvo);
  `--ui-selftest` agrega `Kind::ui` a `InstanceSelfTest`.

**Archivos:** nuevos `src/ui/Theme.{h,cpp}`, `src/ui/LevelMeter.h`, `src/ui/AddInstanceDialog.{h,cpp}`,
`src/app/UiSelfTest.cpp`, `tools/run-ui-test.ps1`; reescritos `src/ui/MainContent.{h,cpp}`,
`src/ui/InstancePanel.{h,cpp}`; cambiados `CMakeLists.txt`, `src/Main.cpp`, `src/app/MainWindow.{h,cpp}`,
`src/app/CommandLine.{h,cpp}`, `src/app/InstanceSelfTest.{h,cpp}`, `src/engine/AudioEngine.{h,cpp}`,
`src/engine/GainProcessor.h`, `src/engine/InstanceGraph.{h,cpp}`, `src/ui/PluginListWindow.{h,cpp}`,
`docs/progress.md`; borrado `src/engine/ToneGenerator.h`.

**Verificación:**
- `pwsh -NoProfile -File .\tools\build.ps1` y `-Config Debug` → OK, sin warnings en nuestro código.
- `pwsh -NoProfile -File .\tools\run-ui-test.ps1` (Release) → **PRUEBA DE UI: OK**, 80 pasos, 97 comprobaciones,
  14 s, 0 aserciones. `-Config Debug -AllowedLeaks VST3HostContextHeadless,AsyncUpdater` → OK, **0 aserciones**,
  sólo la fuga conocida de sforzando (etapa 3). `plugins.xml` idéntico (hash) después de las dos corridas.
  - Pantalla principal de esta máquina: 1638x1024 lógicos a escala **1.25** (2048x1280 físicos); la ventana de
    1024x600 lógicos mide 1280x750 px físicos. **0 problemas de layout** a 1024x600 (vacía, 3 y 4 instancias,
    teclado plegado), 1600x1000, mínimo 860x540 y 1024x600 con escala global 1.5 (1911x1102 px físicos).
  - Capturas revisadas a ojo (`build\ui-test\*.png`): 01 vacía, 02 diálogo agregar, 03 tres instancias,
    04 vúmetro de Dexed encendido + copia, 05 teclado plegado, 06 1600x1000, 07 mínimo (4 filas con scroll),
    08 escala 1.5 (todo proporcional, texto nítido). Nada cortado ni superpuesto; en el mínimo el texto del
    plugin se abrevia con "…".
  - **Hilo de mensajes durante cargas** (Release; Debug entre paréntesis): Surge XT: DLL precargada en otro
    hilo en 32 ms, creación en el hilo de mensajes **~320 ms** (411), hueco máximo = la creación; sforzando
    **~90 ms** (203); Dexed **~16 ms** (92). Entre "Agregar" y el comienzo de la creación la UI corrió 20–50 ms
    (≥ 2 ticks: fila "cargando" visible y diálogo cerrado). Dos cargas a la vez: hueco máximo 51 ms (la UI corre
    entre una y otra). Abrir el proyecto de 4 plugins: hueco máximo 360 ms = la creación de Surge XT; el
    indicador "Cargando 3 plugins…" y las barras de las filas se vieron durante la carga.
  - **Escaneo** de 7 archivos desde la UI: hueco máximo 27 ms (Debug 161 ms), 231 ticks del timer de 5 ms,
    "Escaneando plugins…" en la barra de estado, mismos 7 plugins.
  - Duplicar: estado de Dexed idéntico (8284 bytes), programa 5, −30 dB, rutas "loopMIDI Port / Canales: 1, 3 /
    Remapear → canal 2". Vúmetro: Dexed L/R −41.4 dB con una nota (ganancia −30 dB), copia −60 (silencio),
    mezcla −41.4 dB.
  - Atajos: Esc → "Pánico…" y la tecla del teclado virtual se suelta (foco en el teclado y en un combo); Esc por
    `SendInput` → pánico; Ctrl+S → `ui-prueba.vstproj.json` escrito, título sin `*`; cambio → `*` → Ctrl+S
    reescribe el mismo archivo; Ctrl+N → vacío; Ctrl+O → reabre las 4 instancias.
- Tests anteriores (Release y Debug): `ctest --test-dir build` y `--test-dir build-debug` → 100 % (1/1);
  `run-gui-test.ps1` (`--instance-selftest`) Release OK, Debug OK (fuga conocida); `-AppArgs '--midi-selftest'`
  Release OK (49.7 s) y Debug OK, 0 fugas; `run-project-test.ps1` Release OK y Debug OK (fuga conocida).
- `--audio-selftest` (sin tono): WASAPI 48000/480, 47 754 muestras/s, grafo preparado con 480 → OK (cambio de
  buffer omitido: WASAPI compartido sólo ofrece 480); `--type DirectSound`: 256 → 128, grafo re-preparado con
  128 → OK (aviso de ritmo irregular de DirectSound, como en la etapa 2). Debug → OK.
- GUI normal (sin self-test) Release y Debug con `--debug-to-stdout`: abre "VstPlayer — Sin título",
  `CloseMainWindow()` → exit 0, 0 líneas de debug.
- `%APPDATA%\VstPlayer` al final: `plugin-scan.log`, `plugins.xml` (mismo contenido), `settings.xml` (restaurado
  del respaldo tras la prueba de GUI normal). El escaneo de JUCE deja un `plugin-scan-in-progress.txt` vacío;
  `run-ui-test.ps1` lo borra si no existía antes. Sin `recent.json` ni autoguardado.

**Pendiente / problemas:**
- **La creación de un VST3 sigue bloqueando el hilo de mensajes lo que tarde el plugin** (Surge XT ~0.3 s,
  ~0.4–0.5 s en Debug o al abrir un proyecto; sforzando ~0.1 s; Dexed ~20 ms). Es una restricción de VST3 /
  JUCE (el componente se crea en el hilo de la UI). Lo que se hizo: DLL en otro hilo, una creación por vez con
  pausas para que la UI pinte y responda, filas e indicador de progreso visibles antes de cada bloqueo. Hostear
  plugins en otro proceso queda fuera de alcance. Restaurar un estado grande (`setStateInformation`) también
  ocurre en ese turno.
- La precarga del DLL ejecuta su `DllMain` / inicialización estática en un hilo de fondo; con los 7 plugins
  instalados no hubo problemas, pero un plugin que asuma el hilo de la UI en su inicialización estática podría
  fallar (habría que agregar una lista de exclusión).
- "Escala con DPI de Windows": verificado a 125 % (la escala real de esta pantalla) y simulando 150 % con
  `setGlobalScaleFactor`; no se probó cambiar la escala de Windows en caliente ni mover la ventana al segundo
  monitor (100 %) por programa.
- **Verificación manual pendiente (usuario)**, con `build\VstPlayer_artefacts\Release\VstPlayer.exe`:
  1. Mirar la ventana: tema oscuro, barra superior con proyecto/botones/PÁNICO y audio/CPU/vúmetro, lista,
     teclado abajo, barra de estado. Pasar el mouse por los botones (tooltips).
  2. "+ Agregar instancia…": escribir "surge" (sólo Surge XT), destildar "Sólo instrumentos" (aparece Surge XT
     Effects), Enter o doble clic agrega. Agregar Dexed y sforzando también. Mientras Surge carga, la ventana
     puede quedar quieta ~0.3 s (ver arriba); el resto del tiempo responde.
  3. Clic en una fila y tocar el teclado virtual: el vúmetro de la fila y el de la mezcla se mueven.
     "Duplicar" en Dexed: aparece "Dexed (copia)" con el mismo sonido. Probar "M", el slider, "Editor", "Quitar".
  4. Plegar / desplegar el teclado ("▾ Teclado"); cerrar y reabrir la app: queda como se dejó.
  5. Esc con una nota colgada (o desde carrilion) → corta todo. Ctrl+S / Ctrl+O / Ctrl+N y "Recientes ▾".
  6. Achicar la ventana al mínimo y agrandarla: nada se superpone; con muchas instancias aparece el scroll.
  7. Botón del dispositivo de audio → configuración de audio; "Puertos MIDI…"; "Plugins…" → Escanear: la barra
     de estado dice "Escaneando plugins…" y la ventana sigue respondiendo.
- `Translations.cpp` conserva la traducción de "Plays a test tone" (texto de JUCE, inofensivo).

## Etapa 7 — API local para los proyectos web  (2026-11-01)
**Hecho:**
- **Librería: sólo cpp-httplib `v0.64.0`** (`cmake/httplib.cmake`, FetchContent con tag fijo, `SYSTEM`,
  header-only, sin OpenSSL/zlib/brotli/zstd). **No se usó IXWebSocket**: cpp-httplib trae servidor
  WebSocket desde hace varias versiones, así que HTTP y WebSocket comparten **un solo puerto** (el plan
  pide "puerto configurable (default 9471)") con una sola dependencia. IXWebSocket habría obligado a un
  segundo puerto (su servidor HTTP y su servidor WS son sockets distintos) y a compilar una librería más.
  `httplib.h` se incluye sólo en `src/api/ApiServer.cpp`, que no incluye JUCE (Winsock y `windows.h`
  de JUCE no se cruzan).
- `src/api/ApiServer.{h,cpp}` (red, sin JUCE): escucha **sólo en `127.0.0.1`**, `POST /api`,
  `GET /api/status`, `GET /`, `OPTIONS` (preflight), WebSocket en `/ws` y `/`. CORS
  `Access-Control-Allow-Origin: *` (+ `Allow-Private-Network` para Chrome) en todas las respuestas.
  Token opcional (`?token=` o cabecera `X-Api-Token`): 401 por HTTP; por WebSocket manda el error y
  cierra con 1008. Los eventos salen por una cola + hilo emisor (un cliente lento no frena al hilo que
  los produce). Al detenerse, los bucles de WebSocket (lectura con timeout de 250 ms) terminan solos y
  se espera a todos los hilos.
- `src/api/ApiController.{h,cpp}` (comandos, documentados en **`docs/api.md`**): `status`,
  `loadProject` (responde cuando terminaron de cargar todas las instancias; 409 `unsavedChanges` si hay
  cambios sin guardar salvo `discardChanges: true`), `listProjects`, `saveProject` (con o sin `path`),
  `listParams` (`index, name, id, value, text, label, default, steps, automatable`; `filter`, `offset`,
  `limit`, `refresh`), `getParam`, `setParam` (por `index`, `name` o `paramId`), `setGain`, `setMute`,
  `panic` (el mismo del botón), `listPrograms`, `setProgram`, `midi` (uno o varios mensajes de canal),
  y de diagnóstico `levels` (pico por instancia y mezcla) y `audioStats` (callbacks, tiempo del
  callback, xruns, CPU). `instanceId` acepta también el nombre de la instancia. Respuestas
  `{ok, ..., cmd, id}` / `{ok:false, code, error}` con códigos HTTP 400/401/404/409/500/503/504.
  - Hilos: los comandos que tocan grafo/router/proyecto van al hilo de mensajes con
    `MessageManager::callAsync` y el hilo de red espera la respuesta (con timeout y corte si la app
    se cierra). `setParam` y `midi` corren en el hilo de red (`setValueNotifyingHost` /
    `MidiMessageCollector`), con punteros de una tabla que el hilo de mensajes actualiza bajo lock
    **antes** de quitar los nodos de una instancia. En `setParam` no se pide `getText` (en VST3 toma un
    `MessageManagerLock` y podría trabarse contra el hilo de mensajes).
  - Eventos por WebSocket: `instanceAdded`, `instanceLoaded` (extra), `instanceRemoved`, `paramChanged`
    (banderas atómicas por parámetro marcadas desde cualquier hilo, agrupadas por un timer de **20 Hz**
    por instancia, máx. 256 por evento), `projectLoaded`, `midiActivity` (≤ 20 Hz por instancia).
- **Caché de parámetros (aviso de la etapa 5)**: confirmado y documentado en `docs/api.md` → "Valores
  viejos". Con Dexed, después de `setProgram` (igual que desde su editor) `listParams` devuelve los
  valores del programa anterior y no hay `paramChanged` (medido: 0 de 2238 cambian; con refresh 86).
  `listParams` con `"refresh": true` relee el estado del propio plugin (`getStateInformation` +
  `setStateInformation`) ~100 ms después del pedido (antes, el estado todavía no tiene los cambios
  pendientes y los deshacía) y recién ahí responde; es opt-in porque es una restauración completa
  (sforzando recarga su instrumento) y porque con Dexed se vio que un `setProgram` justo después de un
  refresh a veces no se aplica (el siguiente sí, medido por nivel de audio). Lo que escribe la API
  (`setParam`) se lee bien enseguida.
- `AudioEngine`: el callback del dispositivo pasa por `TimedCallback` (reenvía al
  `AudioProcessorPlayer` y mide la duración de cada callback: media, máximo, cuántos superan la duración
  del bloque) → `getAndResetCallbackStats()`, `getXRunCount()`.
- `ProjectController::getLoadSerial()` (se incrementa al terminar cada carga) para `projectLoaded` y
  para que `loadProject` responda al final. `InstancePanel::refreshRows()` (la fila refleja gain/mute
  cambiados por la API).
- UI: indicador en la barra de estado **"API: 127.0.0.1:9471, N clientes"** (N = WebSockets abiertos;
  "API: desactivada"; el error en rojo si el puerto está ocupado). Mensajes "API: abierto …",
  "API: pánico…", "API: guardado …" en la barra de estado.
- Configuración: `settings.xml` `apiEnabled` (true), `apiPort` (9471), `apiToken` (vacío). Nada se
  escribe en settings: sin las claves vale el default. Línea de comandos `--api-port`, `--api-token`,
  `--no-api`. Los self-tests corren **sin** API (salvo `--api-port`), así no chocan con una VstPlayer
  abierta. Modo nuevo **`--api-test`**: GUI sin pasos de self-test, sin persistir nada, sin diálogos
  (permite varias instancias, como los self-tests).
- `examples/carrilion-client.js`: módulo ES sin dependencias (navegador o Node 22+): `connect(url,
  {token})`, `status()`, `listParams`, `setParam(id, index|name|{paramId}, value)`, `getParam`,
  `setGain`, `setMute`, `panic`, `listPrograms`, `setProgram`, `loadProject(path, {discardChanges})`,
  `listProjects`, `saveProject`, `midi`, `levels`, `audioStats`, `on(event, fn)`, `httpRequest()` (sin
  WebSocket) y **`vstPlayerPort(client, instanceId)`**: un "port" con la interfaz que usa `MidiOut` de
  carrilion (`now, on, off, all_off`), con el ejemplo de integración en `apps/web/lib/engine/midi-out.js`
  en comentarios. **carrilion no se modificó.**
- Tests: `tools/api-test.mjs` (Node, sin dependencias) y `tools/run-api-test.ps1` (arranca la app con
  `--api-test --api-port 9479 --project-data-dir <tmp> --debug-to-stdout`, chequea netstat, corre el
  test, cierra la ventana y controla exit code, aserciones, fugas y que `%APPDATA%` no reciba
  `recent.json` / autoguardado).
- `GET /client.js` sirve `examples/carrilion-client.js` (embebido al compilar: CMake lo convierte en
  `build*/generated/ClientScript.h` y lo regenera si cambia), sin token y con CORS, para poder hacer
  `await import('http://127.0.0.1:9471/client.js')` desde la consola de cualquier página.

**Archivos:** nuevos `cmake/httplib.cmake`, `src/api/ApiServer.{h,cpp}`, `src/api/ApiController.{h,cpp}`,
`docs/api.md`, `examples/carrilion-client.js`, `tools/api-test.mjs`, `tools/run-api-test.ps1`; cambiados
`CMakeLists.txt`, `src/Main.cpp`, `src/engine/AudioEngine.{h,cpp}`, `src/project/ProjectController.{h,cpp}`,
`src/ui/InstancePanel.h`, `src/ui/MainContent.{h,cpp}`, `docs/progress.md`.

**Verificación:**
- `pwsh -NoProfile -File .\tools\build.ps1` y `-Config Debug` → OK, sin warnings en nuestro código
  (cpp-httplib entra como include `SYSTEM` / `/external:W0`).
- **`& .\tools\run-api-test.ps1`** (Release, sin token) → **PRUEBA DE API: OK, 83 comprobaciones, 0
  fallos**, exit 0, 0 aserciones, 0 fugas, ~30 s. `-Token s3cr3t` → OK, 86 comprobaciones.
  `-Config Debug -Token dbg -AllowedLeaks VST3HostContextHeadless,AsyncUpdater` → OK, 86, **0 aserciones**,
  sólo la fuga conocida de sforzando (etapa 3; 2 instancias porque el test lo carga dos veces).
  (Como `run-gui-test.ps1`: con `pwsh -File` el array de `-AllowedLeaks` llega como un solo texto; usar `&`.)
  Lo que comprueba (resultados reales de la última corrida Release):
  - netstat: `TCP 127.0.0.1:9479 0.0.0.0:0 LISTENING` (única dirección; `Get-NetTCPConnection` igual).
    No apareció aviso del firewall.
  - token: sin token / token equivocado → 401; WebSocket sin token → mensaje `unauthorized` y cierre
    1008. `OPTIONS` → 204 con `Allow-Origin: *`; todas las respuestas con `Allow-Origin: *`.
  - `status` por HTTP, `GET /api/status` y por WebSocket (con `id` devuelto y `clients: 1`).
  - `listProjects examples` → `plugin-faltante, prueba-etapa5`; carpeta inexistente → 404.
  - `loadProject examples\prueba-etapa5.vstproj.json` por WebSocket: responde con las 3 instancias
    `ready` (~600 ms Release, ~1 s Debug); eventos `projectLoaded`, `instanceAdded` x3,
    `instanceLoaded` x3. `status`: Dexed (2238 parámetros, 32 programas), Pads = Surge XT (2855),
    Sampler = sforzando (2593), 3 rutas con `remapTo`.
  - `setGain` −30 dB a las tres, `setMute` por id y por nombre; `status` lo refleja y `dirty: true`.
  - `listParams` Surge XT (2855, con index/name/id/value/text/label); `setParam` por index 0.4 →
    `listParams` lee 0.4; evento `paramChanged` con ese parámetro; `setParam` por nombre (WS) y por
    `paramId`; `getParam` lee 0.7; errores: nombre inexistente 404, sin value 400, instancia
    inexistente 404, valor 7 → se limita a 1.
  - `listPrograms` Dexed (32); `setProgram` 20 y 5 → la misma nota mide −45.6 vs −41.0 dBFS (el programa
    cambia de verdad); valores viejos: 0 parámetros cambian sin refresh, 86 con `refresh: true`.
  - **MIDI directo**: note-on por WS a Dexed → pico en su nodo de ganancia de −100 (silencio) a
    **−40.9 dBFS**, mezcla −40.9; evento `midiActivity`; array con 3 mensajes por HTTP; **panic** →
    Dexed y mezcla −100 dBFS. MIDI inválido (sysex, sin status, incompleto, dato > 127) → 400 `badMidi`.
  - JSON inválido, cuerpo vacío, array en vez de objeto → 400 `badJson`; comando desconocido → 400
    `unknownCommand`; ruta desconocida → 404; por WebSocket JSON roto + comando desconocido → errores y la
    conexión sigue abierta; el servidor sigue respondiendo.
  - `loadProject` con cambios sin guardar → 409 `unsavedChanges`; `saveProject` a
    `%TEMP%\vstplayer-api-test-*\api-guardado.vstproj.json` → ok, `status` sin cambios, `saveProject` sin
    ruta → ok, aparece en `listProjects` y en recientes (los de la carpeta temporal);
    `loadProject plugin-faltante` → instancia `missing` + aviso, `instanceRemoved` x3, `listParams` de la
    faltante → 409; archivo inexistente → 404; reabrir el guardado → Pads −30 dB, Dexed programa 5 y el
    parámetro movido por la API en 0.4.
  - **50 setParam/s** (WS, 10 s, sobre Surge XT mientras suena un acorde en Pads) comparado con 5 s sin
    tráfico y con un estrés de 500/s durante 3 s. WASAPI compartido 48 kHz / 480 (bloque 10 ms):

    | | setParam/s | latencia p50 / p99 | callbacks (esperados) | callback medio / máx | sobre presupuesto | xruns | CPU |
    |---|---|---|---|---|---|---|---|
    | sin tráfico | 0 | — | 503 (500) | 0.22 / 1.16 ms | 0 | 0 | 2.6 % |
    | 50/s | 50.0 | 0.54 / 0.99 ms | 1001 (1000) | 0.22 / 1.03 ms | 0 | 0 | 2.4 % |
    | 500/s | 500.3 | 0.23 / 0.49 ms | 301 (300) | 0.20 / 0.70 ms | 0 | 0 | 1.9 % |

    Debug, 50/s: callbacks 1001/1000, máx 0.64 ms, 0 sobre presupuesto, 0 xruns. "Callback" = todo el
    grafo (medido alrededor de `AudioProcessorPlayer`). La nota sigue sonando durante la ráfaga.
    `paramChanged`: como máximo 20 eventos en cualquier ventana de 1 s. (En una corrida anterior el p99
    de latencia de 50/s fue 10.6 ms: un pedido esperó un ciclo; sin efecto en el audio.)
  - Dos clientes WS → `clients: 2`; al cerrarlos vuelve a 0.
  - `GET /client.js` = el archivo de `examples` byte a byte; el módulo `carrilion-client.js` importado en
    Node: `connect`, `status`, `setParam` por nombre + `listParams`, `on('paramChanged')`, error como
    `VstPlayerError` con `code`, `vstPlayerPort` (interfaz de puerto de `MidiOut`) toca Dexed y su note-off
    programado + `all_off` dejan silencio, `httpRequest`.
  - Cierre con `CloseMainWindow()` → exit 0. `%APPDATA%\VstPlayer` sin `recent.json` ni autoguardado;
    `settings.xml` sin modificar.
- Tests anteriores (Release y Debug): `ctest --test-dir build` y `build-debug` → 100 %;
  `run-gui-test.ps1` (`--instance-selftest`) OK / OK (fuga conocida); `-AppArgs '--midi-selftest'` OK / OK
  (0 fugas); `run-project-test.ps1` OK / OK; `run-ui-test.ps1` OK / OK (0 problemas de layout con el
  indicador de API en la barra de estado, también en el mínimo 860x540 y a escala 1.5). Una corrida de
  `run-ui-test.ps1` Release informó "plugins.xml igual que antes de la prueba: False" (la prueba igual da
  OK; las demás corridas True); `plugins.xml` sigue con los mismos 7 plugins y sin blacklist (parece el
  orden en que el escaneo de esa prueba lo reescribe, no algo de esta etapa).

**Pendiente / problemas:**
- **Verificación manual pendiente (usuario): Chrome + carrilion + Surge XT.**
  1. Abrir `build\VstPlayer_artefacts\Release\VstPlayer.exe`. La barra de estado (abajo a la derecha)
     debe decir `API: 127.0.0.1:9471, 0 clientes`.
  2. Archivo → Abrir… → `examples\prueba-etapa5.vstproj.json` (o cualquier proyecto con Surge XT). En la
     fila "Pads" (Surge XT) apretar **Editor** y dejar visibles los macros (abajo a la izquierda del
     editor de Surge: M1…M8).
  3. Abrir carrilion en Chrome como siempre, F12 → Consola, y pegar de a una línea:
     ```js
     const { connect } = await import('http://127.0.0.1:9471/client.js')
     const vp = await connect()
     const st = await vp.status(); st.instances.map(i => i.name + ' = ' + i.plugin.name)
     const pads = st.instances.find(i => i.plugin.name === 'Surge XT')
     await vp.listParams(pads.id, { filter: 'M1' })
     await vp.setParam(pads.id, 0, 1.0)   // índice 0 = macro M1: la perilla M1 del editor sube al máximo
     await vp.setParam(pads.id, 0, 0.0)   // y vuelve a cero
     for (let i = 0; i <= 50; i++) setTimeout(() => vp.setParam(pads.id, 0, i / 50), i * 20)  // barrido de 1 s a 50/s
     vp.on('paramChanged', e => console.log(e.params))  // mover M1 con el mouse en el editor: aparecen eventos
     ```
     La barra de estado de VstPlayer pasa a `1 cliente`.
  4. Tocar algo desde carrilion (canal 2 o 3 → Pads por loopMIDI), o desde la consola
     `await vp.midi(pads.id, [0x90, 60, 100])`, y con la nota sonando: `await vp.panic()` → se corta todo.
  5. Para oír "50 setParam/s sin clicks": buscar un parámetro audible
     (`await vp.listParams(pads.id, { filter: 'Cutoff' })`), y con una nota sostenida correr el barrido del
     paso 3 con ese `index` varias veces seguidas; no debería haber clicks ni cortes (por programa: 0
     callbacks perdidos ni lentos, ver arriba).
  6. Si Chrome bloquea el `import` o el `connect` (p. ej. carrilion servido por https), anotar el mensaje de
     la consola (no debería: 127.0.0.1 cuenta como origen seguro y la API manda CORS y
     `Allow-Private-Network`).
- **Valores viejos de parámetros** (Dexed tras cambiar de programa, sforzando tras cargar un instrumento):
  `listParams` devuelve la copia del host y no hay `paramChanged`; `refresh: true` es la salida, con los
  costos documentados en `docs/api.md`. JUCE 8.0.15 no ofrece una forma pública de releer los valores del
  controlador VST3 sin restaurar el estado (`resetParameters` es privado de su host VST3).
- Con Dexed, un `setProgram` mandado inmediatamente después de un `refresh` a veces no se aplica
  (observado midiendo el nivel de una nota; el siguiente `setProgram` sí). Causa no investigada (lado
  plugin/JUCE); documentado.
- `setParam` responde sin `text` (ver docs/api.md); el texto llega en `paramChanged` o con `getParam`. El
  texto que Surge XT da para sus macros no siempre coincide con el valor (0.4 → "0.38"): es del plugin.
- MIDI por la API no enciende el LED de la fila (el LED muestra los puertos) ni pasa por las rutas.
- `loadProject` / `saveProject` aceptan cualquier ruta del disco: con `Allow-Origin: *`, cualquier página
  abierta en el navegador de esta máquina puede usarlos (riesgo aceptado y documentado; `apiToken` lo
  cierra).
- No hay UI para cambiar puerto o token (se editan en `settings.xml`, documentado). Un puerto ocupado se
  muestra en rojo en la barra de estado y la app sigue funcionando sin API.

## Etapa 8 — Empaquetado, documentación y cierre  (2026-11-01)
**Hecho:**
- **Runtime de MSVC estático** (`CMAKE_MSVC_RUNTIME_LIBRARY = MultiThreaded$<$<CONFIG:Debug>:Debug>` en
  `CMakeLists.txt`, para todos los targets, JUCE incluido): `VstPlayer.exe` ya no depende de
  `MSVCP140.dll` / `VCRUNTIME140*.dll` / `api-ms-win-crt-*` (antes sí). Recompilación completa de `build\` y
  `build-debug\` (una vez).
- **`tools/package.ps1 [-SkipBuild] [-OutDir]`**: lee la versión de `project(VstPlayer VERSION x.y.z)`,
  compila Release (con `build.ps1`), verifica que el recurso de versión del exe coincida (un build viejo
  no pasa), y arma `dist\VstPlayer-<versión>-win64.zip` con carpeta raíz `VstPlayer-<versión>-win64\`:
  `VstPlayer.exe`, `README.md`, `examples\` (con `sfz\`), `docs\*.md`, `tools\register-filetype.ps1`.
  `dist/` en `.gitignore`. La versión ya se mostraba en Ayuda → Acerca de (`JUCE_APPLICATION_VERSION_STRING`
  = versión del proyecto CMake desde la etapa 1).
- **`tools/register-filetype.ps1 [-ExePath] [-Unregister]`** (sólo HKCU, sin administrador). Windows
  decide el tipo por la **última** extensión, así que `*.vstproj.json` es un `.json` para el Explorador y
  no puede tener programa por defecto propio. El script no se apropia de `.json`; escribe en
  `HKCU\Software\Classes`: ProgID `VstPlayer.Project` (nombre, icono, `"exe" "%1"`), el valor
  `VstPlayer.Project` en `.json\OpenWithProgids` (aparece en "Abrir con"), `Applications\VstPlayer.exe`
  (nombre + `SupportedTypes .json`) y el verbo de menú contextual
  `SystemFileAssociations\.json\shell\VstPlayer.Open` "Abrir en VstPlayer" con
  `AppliesTo = System.FileName:"*.vstproj.json"` (sólo para esos archivos). Avisa al shell
  (`SHChangeNotify`). `-Unregister` borra lo propio y sólo las claves que quedan vacías. El exe por
  defecto: `..\VstPlayer.exe` (zip) o el Release del repo.
- **`--data-dir <carpeta>`** (nuevo, cualquier modo): todo lo del usuario (`settings.xml`, `plugins.xml`,
  `recent.json`, autoguardado, log del escaneo) va a esa carpeta en lugar de `%APPDATA%\VstPlayer`
  (`AppSettings::setUserDataDirectoryOverride`; `AppSettings` abre el `PropertiesFile` por ruta). Con
  `--data-dir` se permite otra instancia de la GUI (es otra configuración). Sirve para probar el zip
  sin tocar los datos del usuario y para tener configuraciones separadas.
- Arranque sin plugins escaneados (primera vez o `--data-dir` nuevo): la barra de estado dice
  "No hay plugins escaneados: apretá "Plugins…" → Escanear." (el README lo explica).
- `tools/build.ps1 -BuildDir <dir>`: compilar en otro directorio (para la prueba "de cero").
- **`README.md`** (español): qué es, requisitos, instalación desde el zip, escaneo, asociación opcional,
  uso rápido, flujo carrilion → loopMIDI → VstPlayer, formato de proyecto, API (con ejemplo de consola),
  latencia, línea de comandos, cómo compilar, licencia. **`CLAUDE.md`**: decisiones fijas, estructura,
  compilar, suite de pruebas (Release/Debug), reglas de hilos, convenciones, pendientes.
  **`docs/roadmap.md`**: pendientes conocidos (valores viejos de parámetros, creación de VST3 en el hilo de
  UI, fuga de sforzando, buffer 480 de WASAPI compartido / ASIO, etc.) y siguientes pasos (ASIO con SDK,
  CLAP, multisalida, efectos por instancia, grabación a WAV, MIDI clock, Windows MIDI Services, plugins
  fuera de proceso…). `docs/api.md` y `docs/project-format.md`: notas de `--data-dir`, límite de
  anidamiento y `graphNodes`.
- **Revisión final (tarea 5)**: tres revisiones de lectura en paralelo (API/red, motor/hilos, UI/ciclo de
  vida/proyecto) y verificación propia de cada hallazgo antes de tocar código. Corregido (bugs reales):
  1. **Reabrir un proyecto mientras todavía carga dejaba plugins huérfanos en el grafo**
     (`InstanceGraph`). Las instancias de un proyecto usan los `id` del archivo; `removeAllInstances` no
     sacaba de la cola las creaciones pendientes, y al volver a crear instancias con los mismos `id` las
     entradas viejas coincidían: el plugin se creaba dos veces y `pluginCreated` pisaba los nodos de la
     instancia, dejando el primer juego (MIDI + plugin + ganancia) conectado a la mezcla para siempre, con
     el editor que se reabría atado al huérfano. **Evidencia** (build con el código anterior + script por la
     API): `graphNodes` 11 tras una carga normal → **14** tras `loadProject` A y enseguida B (mismos ids).
     Corrección: `removeInstance` borra de la cola las creaciones de ese id, y `pluginCreated` descarta un
     plugin si la instancia no está en estado `loading`. Con el arreglo: 11 → 11.
  2. **Un JSON muy anidado tumbaba la app** (`ApiController`). `juce::JSON::parse` es recursivo sin
     límite; cualquier página del navegador puede mandar `POST /api` (CORS `*`, sin preflight con
     text/plain). **Evidencia**: `[` × 300 000 → la app murió con **0xC00000FD** (stack overflow) y el
     puerto dejó de responder. Corrección: se rechaza sin parsear todo JSON con más de 64 niveles de
     anidamiento (`badJson`, 400). Con el arreglo: 400 y el servidor sigue.
  3. **Varios `loadProject` esperando la misma carga compartían el objeto de respuesta**: cada hilo de red
     le escribía su `id`/`cmd` mientras otro lo serializaba (carrera de datos; `id` cruzado). Ahora cada
     espera recibe su propio objeto.
  4. **`broadcast()` (hilo de mensajes) podía quedar esperando a un cliente WebSocket lento**: tomaba el
     mismo mutex que el hilo emisor mantiene durante `send()` (hasta 5 s por socket trabado), congelando
     la UI cada 50 ms con el timer de `paramChanged`. Ahora mira un contador atómico. De paso: el conteo
     de clientes se publica dentro del lock (antes dos conexiones/desconexiones simultáneas podían dejar
     `numClients` en 0 con un cliente conectado y se dejaban de mandar eventos), y una excepción dentro del
     manejador de un mensaje WebSocket se responde como error en lugar de terminar la app (cpp-httplib
     no las atrapa en WebSocket).
  5. **Nota colgada al cambiar rutas MIDI** (`MidiRouter::setRoutes` / `setRoutesForInstance`): las notas
     sostenidas se soltaban *antes* de abrir puertos nuevos e instalar la nueva tabla de despacho; un
     note-on que llegaba en ese intervalo (abrir un puerto tarda decenas de ms) quedaba marcado por la ruta
     vieja y su note-off ya no tenía ruta. Ahora se sueltan después del cambio de tabla (como ya hacía
     `closePort`). Ventana de carrera chica: sin reproducción determinística; cubierto por el
     `--midi-selftest` como regresión.
  - Revisado y **sin cambios** (con motivo):
    - Orden de cierre (`Main.cpp::shutdown`): API primero (los pedidos pendientes responden 503, se esperan
      los hilos), ventana y editores, router, grafo, motor: correcto.
    - Quitar instancias con MIDI/API en vuelo: router y API sacan sus punteros bajo lock en
      `instanceAboutToBeRemoved`, antes de quitar nodos; el hilo de audio mantiene los nodos vivos vía el
      render sequence de JUCE: correcto.
    - Editores: se cierran antes de quitar la instancia; callbacks async con `SafePointer`/`WeakReference`:
      correcto. Guardado atómico (temporal + `ReplaceFileW`) y errores de carga (JSON roto, archivo
      inexistente, plugin faltante o que falla): estado consistente.
    - Un comando que vence su timeout (504) igual se ejecuta después en el hilo de mensajes: un reintento
      del cliente puede ejecutarlo dos veces. Se deja: sólo pasa si la UI estuvo bloqueada más que el
      timeout (10–120 s) y los comandos son idempotentes salvo `saveProject`.
    - `finishLoading` diferido / `openEditor` diferido pueden aplicarse a un proyecto más nuevo con los
      mismos ids si se reabre en el mismo instante: efecto cosmético (aviso duplicado, editor abierto); se
      deja documentado.
    - `anotherInstanceStarted` resuelve una ruta relativa contra la carpeta de la instancia abierta (el
      Explorador siempre pasa rutas absolutas); no restaura una ventana minimizada.
    - Estadísticas de callback (`AudioEngine`): carrera benigna entre el reset y el hilo de audio (sólo
      diagnóstico). Plugin quitado sin dispositivo de audio corriendo: JUCE lo destruye recién cuando el
      grafo vuelve a renderizar (seguro). Precarga de DLL que tarde > 30 s al cerrar: muy improbable.
    - Carrera de JUCE al destruir un listener de parámetros mientras el hilo de audio notifica (ventana de
      microsegundos al cerrar; limitación de `AudioProcessorParameter`).
- `tools/api-test.mjs`: casos nuevos para 1–3 (JSON anidado por HTTP y WebSocket, dos `loadProject`
  seguidos con mismos ids: cada respuesta con su id, estado del segundo proyecto, mismo `graphNodes`).
  `audioStats` devuelve `graphNodes` (diagnóstico).

**Archivos:** nuevos `README.md`, `CLAUDE.md`, `docs/roadmap.md`, `tools/package.ps1`,
`tools/register-filetype.ps1`; cambiados `.gitignore` (`dist/`), `CMakeLists.txt` (runtime estático),
`tools/build.ps1` (`-BuildDir`), `src/Main.cpp`, `src/app/AppSettings.{h,cpp}`, `src/api/ApiServer.cpp`,
`src/api/ApiController.cpp`, `src/engine/InstanceGraph.cpp`, `src/engine/MidiRouter.cpp`,
`tools/api-test.mjs`, `docs/api.md`, `docs/project-format.md`, `docs/progress.md`.

**Verificación:**
- `pwsh -NoProfile -File .\tools\build.ps1` y `-Config Debug` → OK, sin warnings en nuestro código
  (recompilación completa por el cambio de runtime: Release ~104 s).
- **Compila de cero** sin tocar `build\`: `tools\build.ps1 -BuildDir %TEMP%\vstp-clean-build` (carpeta
  nueva y vacía; FetchContent clonó JUCE 8.0.15 y cpp-httplib de nuevo) → **Build Release OK, 205 s en
  total** (configure + clon + compilación, 60 pasos de Ninja, 0 warnings nuestros); `ctest` en esa carpeta
  → 100 %. Carpeta borrada al final (fuera del repo; `build-*/` y `dist/` igual están en `.gitignore`).
- **Dependencias del exe** (`dumpbin /dependents`): sólo DLLs del sistema (`KERNEL32`, `USER32`, `GDI32`,
  `SHELL32`, `ole32`, `OLEAUT32`, `COMDLG32`, `SHLWAPI`, `WINMM`, `IMM32`, `COMCTL32`, `WS2_32`, `dwmapi`,
  `d2d1`, `dxgi`, `d3d11`, `dcomp`, `api-ms-win-shcore-scaling-l1-1-1`), todas presentes en Windows 10/11.
  Antes de esta etapa además `MSVCP140`, `VCRUNTIME140(_1)` y 12 `api-ms-win-crt-*` (redistribuible).
  Recurso de versión del exe: `ProductVersion 0.1.0`.
- `pwsh -NoProfile -File .\tools\package.ps1 -SkipBuild` → `dist\VstPlayer-0.1.0-win64.zip` (3,0 MB): exe
  6,8 MB, README.md, docs (api, progress, project-format, roadmap), examples (2 proyectos, cliente JS,
  sfz), tools\register-filetype.ps1.
- **Zip en una carpeta nueva fuera del repo** (`%TEMP%\vstp-zip-test-*`, script en el scratchpad), con
  `--data-dir` en una carpeta temporal:
  1. `VstPlayer.exe --data-dir <tmp> --scan` (lo que dice el README para la primera vez) → 7 archivos, 7
     procesos hijos, 2 s, 7 plugins (Dexed, Surge XT, sforzando, …), `plugins.xml` en la carpeta temporal.
  2. `VstPlayer.exe --data-dir <tmp> --api-port 9476 examples\prueba-etapa5.vstproj.json` (desde la carpeta
     descomprimida) → por la API: proyecto "Prueba etapa 5", Dexed / Pads (Surge XT) / Sampler (sforzando)
     `ready`, 3 rutas desde "loopMIDI Port" (1 keep; 2,3 → remapTo 1; 4 keep), WASAPI 48000/480, `dirty: false`.
  3. MIDI por loopMIDI con `VstPlayer.exe --midi-send "loopMIDI Port" …` (otro proceso): canal 1 → sólo
     Dexed (−29.6 dBFS); canal 2 → sólo Pads (−25.5 dBFS); canal 5 → nadie (−100); `panic` → mezcla −100.
  4. Cierre con `CloseMainWindow()` → exit 0. La carpeta temporal quedó con `settings.xml`, `plugins.xml`,
     `recent.json`, `plugin-scan.log`; **`%APPDATA%\VstPlayer` idéntico (hash de cada archivo)**.
  Carpeta borrada al final.
- Arranque con `--data-dir` vacío y `--no-api`: leído por UI Automation, la barra de estado dice
  "No hay plugins escaneados: apretá "Plugins…" → Escanear." y "API: desactivada"; cierre exit 0.
- **`register-filetype.ps1` en HKCU**: registrar → `VstPlayer.Project` (nombre, icono y comando
  `"…\VstPlayer.exe" "%1"`), `.json\OpenWithProgids` = `VSCode.json, VstPlayer.Project`, verbo
  "Abrir en VstPlayer" con `AppliesTo System.FileName:"*.vstproj.json"`, `Applications\VstPlayer.exe` con
  `SupportedTypes .json`, `.json` sigue con su default `json_auto_file`. `-Unregister` → las tres claves
  propias no existen y `reg export HKCU\Software\Classes\.json` es **idéntico (hash) al de antes**. No
  quedó nada registrado. **Problema encontrado y corregido en el camino**: la primera versión usaba
  `New-Item -Force` sobre `.json\OpenWithProgids`, que en el registro recrea la clave vacía y borró el
  valor `VSCode.json` del usuario; se restauró enseguida desde el `reg export` previo (verificado por hash)
  y el script ahora sólo crea claves que faltan.
- Evidencia de los bugs 1 y 2 (antes / después): build Release con `InstanceGraph.cpp` de `HEAD` y el
  chequeo de anidamiento desactivado, script Node contra `--api-test --api-port 9478`: `graphNodes` 11 → 14
  y `[`×300000 → `ECONNRESET`, proceso terminado con `0xC00000FD`. Con el código final: 11 → 11, 400
  `badJson`, `status` sigue respondiendo, cierre exit 0.
- **Suite completa después de las correcciones** (Release y Debug, en ese orden, sin intervención):

  | Prueba | Release | Debug |
  |---|---|---|
  | `ctest` (round-trip) | OK | OK |
  | `run-gui-test.ps1` (`--instance-selftest`) | OK, 24 s | OK, 0 aserciones, fuga conocida de sforzando |
  | `run-gui-test.ps1 -AppArgs '--midi-selftest'` | OK, 50 s | OK, 0 aserciones, 0 fugas |
  | `run-project-test.ps1` | OK, 37 s | OK, 0 aserciones, fuga conocida |
  | `run-ui-test.ps1` | OK, 15 s | OK, 0 aserciones, fuga conocida |
  | `run-api-test.ps1` | OK, **90 comprobaciones**, 0 fallos | OK, 90, 0 aserciones, fuga conocida |

  (Debug con `-AllowedLeaks VST3HostContextHeadless,AsyncUpdater` donde se carga sforzando.)
  `run-ui-test.ps1` informó "plugins.xml igual que antes: False" en las dos corridas (ya observado en la
  etapa 7: el escaneo de esa prueba reescribe el archivo en otro orden; mismos 7 plugins, 0 en blacklist).
- `%APPDATA%\VstPlayer` al final: `plugin-scan.log`, `plugins.xml`, `settings.xml` (hash de `settings.xml`
  igual al del comienzo de la etapa); sin `recent.json` ni autoguardado.

**Pendiente / problemas:**
- **Verificación manual pendiente (usuario)**: con el zip descomprimido en otra carpeta y siguiendo sólo
  el `README.md`: escanear plugins ("Plugins…" → Escanear), abrir `examples\prueba-etapa5.vstproj.json`,
  elegir "loopMIDI Port" en carrilion (Chrome) y tocar: canal 1 → Dexed, 2 y 3 → Pads, 4 → Sampler
  (sforzando sin instrumento cargado no suena: cargar un SFZ en su editor, p. ej. `examples\sfz\seno.sfz`).
  Opcional: `tools\register-filetype.ps1`, clic derecho en un `.vstproj.json` → "Abrir en VstPlayer"
  (no se probó el clic en el Explorador por programa; sí las claves) y luego `-Unregister`.
- Asociación: un `*.vstproj.json` no puede abrirse con doble clic en VstPlayer sin cambiar el programa de
  todos los `.json` (limitación de Windows; documentado en README y roadmap).
- El ejecutable no está firmado (SmartScreen avisa la primera vez).
- Pendientes de etapas anteriores sin cambios: ver `docs/roadmap.md` (valores viejos de parámetros, VST3
  creado en el hilo de UI, fuga de sforzando, buffer 480 de WASAPI compartido / ASIO).

## Después de v0.1.0 — Carpetas de búsqueda de plugins  (2026-11-01)

**Hecho:** pedido del usuario: poder agregar más carpetas donde buscar VST3.
- Antes ya existían carpetas extra (`pluginExtraFolders` en `settings.xml`), pero sólo se editaban en el
  diálogo de JUCE que aparecía al tocar "Escanear". Además había un bug: una carpeta extra que no
  existía al abrir la ventana (disco externo desconectado) se borraba de la configuración al cerrarla,
  porque el diálogo se sembraba sólo con carpetas existentes y al cerrar se guardaba esa lista.
- Ventana **Plugins VST3**: panel "Carpetas de búsqueda" arriba de la tabla. Muestra las dos carpetas
  estándar de VST3 (fijas, "predeterminada") y las agregadas; marca "no existe ahora" en naranja.
  - **Agregar carpeta…**: selector de carpetas; la carpeta se guarda y se escanea en el momento.
  - **Quitar carpeta** (sólo agregadas): la saca y quita de la lista de plugins las entradas (plugins y
    desactivados) que estaban dentro y que ninguna otra carpeta cubre.
  - Se rechazan: carpeta inexistente, raíz de disco, `C:\Windows` y subcarpetas, `Program Files`,
    `Program Files (x86)`, `Common Files`, la carpeta del usuario y `C:\Users`, y carpetas ya cubiertas
    (igual, dentro de otra o que contiene a otra; la búsqueda es recursiva).
  - **Escanear** ya no abre el diálogo de carpetas de JUCE: busca los `.vst3` de las carpetas
    configuradas y le pasa la lista a `PluginListComponent::scanFor`. Sólo se prueban los nuevos o
    cambiados. La clave vieja `lastPluginScanPath_VST3` se borra de `settings.xml` al arrancar.
- Línea de comandos: `--plugin-folders`, `--add-plugin-folder <dir>` (agrega y escanea lo nuevo),
  `--remove-plugin-folder <dir>`.
- Corregido de paso:
  - En la tabla de plugins, "Desactivado (falló al escanear)" se veía como "fallÃ³" (literal sin UTF-8).
  - Si al cerrar la app quedaba abierto el aviso "Scan complete" de JUCE (aparece cuando un archivo
    falla al escanear), la ventana del aviso se perdía al salir (fugas `VBlankDispatcher`, `DirectX`,
    `SharedResourcePointer` y aserciones de `DeletedAtShutdown` en Debug): JUCE la borra de forma
    asíncrona y el bucle de mensajes ya había terminado. Ahora `MainWindow::requestQuit` destruye la
    ventana de plugins antes de `quit()`.
- Tests: el verificador de layout del test de UI no entra en `ListBox`/`TableListBox` (JUCE estaciona
  filas recicladas fuera del área visible: 24 falsos positivos). `run-ui-test.ps1` ahora guarda y
  restaura byte a byte `plugins.xml` y `settings.xml` del usuario.

**Archivos:** `src/engine/PluginManager.{h,cpp}`, `src/ui/PluginListWindow.{h,cpp}`,
`src/app/CommandLine.{h,cpp}`, `src/Main.cpp`, `src/app/MainWindow.cpp`, `src/app/UiSelfTest.cpp`,
`tools/run-ui-test.ps1`, `README.md`, `CLAUDE.md`, `docs/progress.md`.

**Verificación:**
- Build Release y Debug sin warnings de nuestro código.
- Línea de comandos con `--data-dir` temporal y una carpeta "Mis VST3 extra" con una copia de Dexed en
  una subcarpeta y un `Roto.vst3` vacío:
  - `--add-plugin-folder`: "2 archivos nuevos probados, 1 plugins nuevos, 1 desactivados" (8 plugins).
  - `C:\`, `C:\Program Files` y una carpeta inexistente: rechazadas con exit 1. La subcarpeta: "Ya se
    busca…", exit 0, sin cambios.
  - Carpeta renombrada (simula disco desconectado): `--plugin-folders` la lista "(no existe)"; abrir y
    cerrar la GUI (`--show-plugins`, WM_CLOSE, exit 0) la conserva en `settings.xml`.
  - `--remove-plugin-folder`: "2 entradas quitadas", vuelve a 7 plugins y 0 desactivados.
- `run-ui-test.ps1` (Release): 85 pasos OK, incluidos los nuevos (agregar carpeta → escaneo automático →
  `Roto.vst3` desactivado; repetida y `C:\` rechazadas; captura `09-plugins-carpetas.png` revisada;
  layout 0 problemas; quitar → desactivados y carpetas como antes). `plugins.xml` igual que antes.
- Release: `ctest`, `--instance-selftest`, `--midi-selftest`, `run-project-test.ps1`, `run-api-test.ps1`: OK.
- Debug `run-ui-test.ps1`: 0 aserciones y sólo la fuga conocida de sforzando (antes del arreglo de
  `requestQuit`: 2 aserciones y 3 clases con fuga más). Falla un único paso **previo**: "la UI siguió
  respondiendo durante el escaneo" con hueco máximo ~296 ms (límite 250). La versión commiteada
  (v0.1.0), compilada en Debug en esta misma máquina, da lo mismo (296 y 308 ms en dos corridas), así
  que no lo causa este cambio; en la etapa 6 daba 161 ms. En Release el hueco es 28 ms (igual que antes).

**Pendiente / problemas:**
- Verificación manual: abrir "Plugins…", **Agregar carpeta…** con el selector real de Windows y ver que
  escanea; **Quitar carpeta**.
- El test de responsividad del escaneo en Debug supera el límite de 250 ms en esta máquina (previo).
- Plugins con el mismo nombre en dos carpetas (p. ej. una copia de Dexed) aparecen dos veces en la
  lista, cada uno con su ruta.
- El zip de `dist\` es de v0.1.0 y no incluye este cambio (regenerar con `tools\package.ps1`).

## Después de v0.1.0 — Volumen y pan por MIDI (CC7 / CC10) y salida "VstPlayer" de carrilion  (2026-11-01)

**Qué se hizo:**
- Cada instancia aplica **CC7 (volumen)** y **CC10 (pan)** en su nodo de ganancia, por cualquier canal y
  camino (puertos o API), y esos dos controladores **no llegan al plugin**. `MidiMixControls` (nuevo,
  `src/engine/MidiMixControls.h`) los comparten `MidiSourceProcessor` (los saca del bloque MIDI en el
  hilo de audio, sin asignar memoria: buffer reservado en `prepareToPlay`) y `GainProcessor` (ganancia de
  la fila × CC7/127; pan como el `StereoPannerNode` de Web Audio; suavizado de 20 ms). Sin CC: 0 dB y
  centro. No se guardan en el proyecto; pánico no los toca. Una instancia tiene un solo volumen/pan: si
  le llegan dos canales, gana el último CC.
- API: `status` informa `midiMix` (`volume` / `pan`, `null` sin CC) por instancia; `levels` agrega
  `peakDbL` / `peakDbR`. Doc: `docs/api.md` "Volumen y pan por MIDI"; README.
- carrilion (rama `feature/vstplayer-out`, en su repo): opción **VstPlayer** en Midi Out
  (`apps/web/lib/core/vstplayer-port.js`: WebSocket, aplica las rutas MIDI de VstPlayer, reconecta) y
  envío de CC7 / CC10 de cada canal (al armar la voz, al mover volumen/pan, al cambiar de canal MIDI, al
  cambiar de puerto y cuando VstPlayer se reconecta o abre un proyecto). Sirve también por loopMIDI.
- `examples/carrilion-client.js`: el comentario ya no dice que carrilion no está integrado.

**Verificación:**
- `run-api-test.ps1`: 6 comprobaciones nuevas (CC7 = 0 silencia; CC10 = 0 / 127 deja un solo lado en
  −100 dBFS; CC7 = 64 baja −5,95 dB exactos; `midiMix` en `status`). 96/96 en Release y Debug.
- Suite completa en Release y Debug (ctest, `--instance-selftest`, `--midi-selftest`, proyectos, UI,
  API): OK, sin warnings, aserciones ni fugas fuera de las permitidas. El paso de responsividad del
  escaneo en Debug pasó esta vez.
- carrilion: 697 tests (`npm run test:web`), con nuevos de `MidiOut` (CC), de la voz y del puerto.

**Pendiente:**
- Verificación manual de oído con carrilion tocando (volumen y pan de los canales en Dexed y Surge XT).
- La fila no muestra el volumen/pan MIDI recibido (sólo `status` de la API y los vúmetros L/R).

## Después de v0.1.0 — Render offline (`--render`)  (2026-11-01)

**Qué se hizo:** modo de línea de comandos que toca un proyecto con un archivo MIDI estándar y escribe
un WAV, sin dispositivo de audio, más rápido que en tiempo real y con cada mensaje en su muestra exacta.
Es el contrato con carrilion (etapa 4 de su plan: su render offline captura el MIDI que mandaría a
VstPlayer, lo escribe como SMF, llama a `--render` y usa el WAV).

```
VstPlayer.exe --render <proyecto.vstproj.json> --midi <archivo.mid> --out <archivo.wav>
              [--sample-rate 44100] [--block 512] [--warmup 2] [--tail-max 60] [--data-dir <carpeta>]
```

- `src/app/OfflineRender.{h,cpp}` (nuevo). Usa los mismos objetos que la app (`AudioEngine` sin
  `initialise()`: no se abre el `AudioDeviceManager`; `InstanceGraph`, `MidiRouter`,
  `ProjectController`), dentro del bucle de mensajes de JUCE (los VST3 se crean en el hilo de mensajes
  con la cola de carga de siempre) pero sin ventana ni API. `Main.cpp` lo crea en `initialise()` y sale
  con su código cuando termina.
- Sin escribir nada en la carpeta de datos: `PluginManager (settings, false)` de sólo lectura (no guarda
  `plugins.xml` ni toca `settings.xml` ni el log de escaneo), `ProjectController` sin carpeta (recientes
  en memoria, sin autoguardado) y con `offlineRender = true` (ignora el bloque `audio` del proyecto y
  no avisa por puertos MIDI no conectados), `MidiRouter (graph, false)` (no lista ni abre puertos).
  Falla si no hay `plugins.xml`.
- Carga: espera a que todas las instancias terminen (timeout 120 s). **Una instancia faltante o que
  falla es error** (exit 1, `{"ok":false,...}`): para la radio de carrilion es mejor no publicar un
  render al que le falta un instrumento que publicarlo incompleto. Los demás avisos de carga (estado
  base64 inválido, parámetros desconocidos) van a `warnings`.
- Formato: `AudioEngine::setOfflineFormat` hace que los plugins se creen ya a la frecuencia del render;
  `graph.setNonRealtime (true)` y `prepareToPlay (sr, block)` en el hilo de mensajes (JUCE lo exige
  para VST3; el plugin recibe `processMode = kOffline`). Latencia del grafo compensada (se descartan
  las primeras muestras; con Dexed / Surge XT es 0).
- Render en un hilo propio (el grafo no se toca desde el hilo de mensajes mientras tanto): primero
  `--warmup` segundos de silencio **a ritmo real** que se descartan (plugins que cargan samples en
  segundo plano), después el MIDI en bloques de `--block` muestras.
- MIDI: `juce::MidiFile` (tipo 0 y 1), `convertTimestampTicksToSeconds` (mapa de tempo); sin note-offs
  agregados; meta, sysex y mensajes de sistema ignorados. Tiempo 0 del archivo = primera muestra del
  WAV. Ruteo como la entrada por puerto pero con **las rutas de todos los puertos** (la misma tabla que
  arma `vstplayer-port.js` de carrilion): un mensaje del canal c va una vez a cada (instancia, canal
  de salida) cuya ruta toma c, con `keep` / `remapTo`; seguimiento de notas sostenidas como el router
  en vivo (un re-ataque sin note-off recibe un note-off antes). Entrega exacta:
  `MidiSourceProcessor::addOfflineMessage (mensaje, posición)` (buffer propio, consumido en el bloque
  siguiente, antes de sacar CC7 / CC10: volumen y pan se aplican igual que en vivo). El camino en vivo
  (`MidiMessageCollector` con timestamps de reloj) no cambia.
- Cola (la regla de `tools/render/page.js` de carrilion): después del último evento sigue hasta que la
  mezcla pasa 2 s seguidos por debajo de −70 dBFS (0.000316), como mucho `--tail-max` s; el archivo
  termina 0,5 s después de la última muestra audible, nunca antes del último evento.
- Salida: WAV estéreo float 32 de la mezcla (`WavAudioFormat`, `TemporaryFile` al lado del destino y
  renombrado al final; ante un error no queda nada). stdout: **una línea JSON**; progreso en stderr
  (`cli::printError`, nuevo: usa el stderr heredado y, si no hay, la consola del padre; nunca un stdout
  redirigido). Con `--debug-to-stdout` también salen líneas `[debug] …`: el resumen es la última línea
  que no empieza con `[debug]`.
  - Éxito: `{"ok":true,"out","frames","sampleRate","blockSize","seconds","lastEventSeconds",
    "tailSeconds","peakDb","midiFileType","midiEvents","midiRouted","midiUnrouted","deliveries",
    "warmupSeconds","latencySamples","renderMs","totalMs","instances":[{"id","name","plugin",
    "midiEvents","mixControllers","noteOns":[16],"peakDb"}],"warnings":[…]}`.
  - Error: `{"ok":false,"error":"…","warnings":[…]}`. Exit 0 = bien, 1 = error del render (proyecto,
    plugins, escritura, timeout), 2 = argumentos o archivos de entrada inválidos.

**Archivos:** `src/app/OfflineRender.{h,cpp}` (nuevo), `src/Main.cpp`, `src/app/CommandLine.{h,cpp}`,
`src/engine/{MidiSourceProcessor.h,MidiRouter.{h,cpp},AudioEngine.{h,cpp},PluginManager.{h,cpp}}`,
`src/project/ProjectController.{h,cpp}`, `CMakeLists.txt`, `tools/run-render-test.ps1` y
`tools/render-test.mjs` (nuevos), `README.md`, `CLAUDE.md`, `docs/roadmap.md`.

**Verificación:**
- `tools/run-render-test.ps1` (nuevo; Node lanza el exe con stdio por pipes, como lo hará carrilion):
  proyecto de prueba con dos Dexed (ruta canal 1 → DexA `keep` por "loopMIDI Port"; canal 3 → DexB
  `remapTo 2` por un puerto que no existe) y un bloque `audio` con un dispositivo inexistente. 75
  comprobaciones: exit 0, stdout = una línea JSON, progreso en stderr, WAV estéreo float 32 a 44100,
  frames del resumen = WAV; silencio exacto antes de la nota y ataque a +1,07 ms de 0,5 s (Dexed calcula
  en sub-bloques de 64 muestras); mapa de tempo (tipo 1, 120 → 60 bpm: ataque a 1,500 s y no a 1,25 s,
  con `--block 480`); remap (DexB recibe el note-on por el canal 2, DexA nada); CC7 = 0 deja DexA en
  −100 dBFS y no llega al plugin; canal sin ruta → silencio, aviso y el WAV termina en el último evento;
  fin del archivo 0,5 s después del último sonido > −70 dBFS; reemplazo de un archivo existente; errores
  (MIDI inexistente o inválido y `--sample-rate` inválido → exit 2; plugin faltante → exit 1, sin WAV);
  sin temporales sobrantes; la carpeta `--data-dir` (copia de `plugins.xml` y `settings.xml`) queda igual
  byte a byte y con las mismas fechas. Un render de 1,5 s de MIDI tarda ~2,4 s en total (carga ~0,9 s +
  calentamiento 2 s; el render en sí ~0,1 s).
- Determinismo: dos renders del mismo MIDI tienen los mismos frames y difieren en a lo sumo 3,05e-5
  (1 LSB de 16 bits) en ~1700 muestras desde el ataque: es Dexed (igual en Release y Debug).
- Build Release y Debug sin warnings. Suite completa, Release y Debug: `ctest`, `--instance-selftest`,
  `--midi-selftest`, `run-project-test.ps1`, `run-ui-test.ps1`, `run-api-test.ps1` (96/96) y
  `run-render-test.ps1` (75/75): OK, 0 aserciones; en Debug sólo las fugas conocidas de sforzando
  (`VST3HostContextHeadless`, `AsyncUpdater`; el render test no carga sforzando y no tiene ninguna).

**Pendiente:**
- carrilion (etapa 4 de su plan, otro agente): capturar el MIDI, escribir el SMF, llamar a `--render`.
- No hay `AudioPlayHead` (tempo / posición) en el render: plugins con LFOs o arpegiadores sincronizados
  usan su tempo interno. Tampoco en vivo.
- No se probó con sforzando con un instrumento de samples (para eso está el calentamiento a ritmo real).
- `examples/prueba-etapa5.vstproj.json`: el commit anterior (1e4ae93) lo guardó con 2 instancias (sin
  "Sampler"), y `run-api-test.ps1` espera las 3 de la etapa 5 (falla con 4 errores en HEAD sin este
  cambio). La suite de esta sección se corrió con la versión de la etapa 5 (32988de) en el árbol de
  trabajo; ese archivo no se toca en este commit (decide el usuario).

## Después de v0.1.0 — Ícono y VstPlayer dentro de carrilion  (2026-11-01)

VstPlayer pasa a ser parte de carrilion: el exe de Windows vive en el repositorio de carrilion
(`bin/win64/VstPlayer.exe`), así quien usa carrilion en Windows ya lo tiene (etapa 2 del plan; la
etapa 4 hará que el render de carrilion llame a ese exe).

**Qué se hizo:**
- **Ícono**: campana de carillón azul (acento del tema, `theme::accent`) con una forma de onda calada,
  dos arcos de sonido, badajo y labio claros, sobre el cuadrado redondeado oscuro del tema
  (`background` / `bar` / `outline`). Dos dibujos: `resources/icon.svg` (48 y 256 px) y
  `resources/icon-small.svg` (16 y 32 px: sin degradés, formas en la grilla de 2 px, onda más simple).
  `tools/render-icon.mjs` los rasteriza a `resources/icon-256.png` e `icon-32.png` con el Playwright +
  Chromium de `carrilion/tools/render` (vst-player sigue sin dependencias de Node; `--preview` arma una
  hoja con 256 / 48 / 32 / 16 px). `CMakeLists.txt`: `ICON_BIG` / `ICON_SMALL` en `juce_add_gui_app`;
  JUCE genera el `.ico` (16 y 32 del chico, 48 y 256 del grande) y lo embebe en el exe.
- Ventanas: JUCE registra su clase de ventana con `ExtractAssociatedIcon` (el ícono del exe), así que
  la ventana principal, los diálogos, los editores de plugins y la barra de tareas lo toman solos. Pero
  usa el tamaño grande también para el ícono chico, y la barra de título mostraba el dibujo grande
  achicado: `MainWindow` cambia el `GCLP_HICONSM` de la clase (compartida por todas las ventanas de
  JUCE) por el ícono del recurso `IDI_ICON1` al tamaño `SM_CXSMICON`, que sale del dibujo chico.
- **Fixture propio para los tests**: `examples/prueba-etapa5.vstproj.json` lo edita el usuario con la
  app (1e4ae93 lo guardó con 2 instancias y el árbol de trabajo tiene otra versión), y
  `run-api-test.ps1` esperaba el contenido de la etapa 5. Ahora `tests/fixtures/` tiene
  `prueba-etapa5.vstproj.json` (el de 32988de: Dexed, Pads = Surge XT, Sampler = sforzando, 3 rutas) y
  `plugin-faltante.vstproj.json` (copia). `run-api-test.ps1` / `api-test.mjs` y `run-render-test.ps1` /
  `render-test.mjs` reciben `--fixtures tests\fixtures` (antes `--examples examples`).
  `ProjectRoundTripTest` comprueba los fixtures en modo estricto (sin advertencias, con instancias y
  rutas) y todos los `examples/*.vstproj.json` sólo como "carga y round-trip estable".
- **Publicación en carrilion**: `tools/publish-carrilion.ps1` (ASCII, PowerShell 5.1) compila Release
  (`-SkipBuild` para no hacerlo), comprueba que la versión del exe coincida con `CMakeLists.txt`, copia el
  exe a `..\carrilion\bin\win64\` (`-Target <carpeta>` para otro lugar) y escribe ahí `VSTPLAYER.md` desde
  `tools/VSTPLAYER.md.in` (UTF-8): qué es, versión, commit de HEAD con su fecha, fecha de compilación,
  tamaño, SHA-256, que no necesita instalador (runtime estático), cómo actualizarlo y la nota de
  licencia (JUCE AGPLv3; el código es el repositorio vst-player: el remoto de git si hay, si no un
  texto provisional; hoy no hay remoto). Avisa (y lo anota en `VSTPLAYER.md`) si hay cambios sin
  commitear en lo que entra al exe. No commitea en carrilion.

**Archivos:** `resources/` (nuevo: `icon.svg`, `icon-small.svg`, `icon-256.png`, `icon-32.png`),
`tools/render-icon.mjs`, `tools/publish-carrilion.ps1`, `tools/VSTPLAYER.md.in` (nuevos),
`tests/fixtures/` (nuevo), `CMakeLists.txt`, `src/app/MainWindow.cpp`, `tests/ProjectRoundTripTest.cpp`,
`tools/{run-api-test.ps1,api-test.mjs,run-render-test.ps1,render-test.mjs}`, `README.md`, `CLAUDE.md`.

**Verificación:**
- Íconos de las ventanas leídos con Win32 mientras corre `--instance-selftest` (script aparte con
  `EnumWindows` + `GetClassLongPtr`): la ventana principal y los editores de Dexed, Surge XT y sforzando
  tienen el ícono grande (40 px a 125 %) y el chico (20 px, del dibujo chico); el ícono del archivo
  (`ExtractAssociatedIcon` del exe) es el nuevo. La barra de tareas usa el de la clase.
- El exe Release pesa ~6,9 MB (7,2 millones de bytes).
- Build Release y Debug sin warnings. Suite completa, Release y Debug: `ctest` (110/110),
  `--instance-selftest`, `--midi-selftest`, `run-project-test.ps1`, `run-ui-test.ps1`,
  `run-api-test.ps1` (96/96, con `tests/fixtures`) y `run-render-test.ps1` (75/75): OK, 0 aserciones; en
  Debug sólo las fugas conocidas de sforzando. `examples/prueba-etapa5.vstproj.json` no se tocó.
- `publish-carrilion.ps1` probado a una carpeta temporal y después al repo de carrilion desde el commit
  de esta sección.

**Pendiente:**
- Remoto público de vst-player: `VSTPLAYER.md` dice "sin remoto publicado todavía"; al crear el remoto,
  volver a publicar para que la nota de licencia apunte al código.
- carrilion (etapa 4): que su herramienta de render llame a `bin/win64/VstPlayer.exe --render`.
- Cada publicación agrega ~7 MB al historial de git de carrilion; si se publica seguido, considerar
  Git LFS o releases.

## Después de v0.1.0 — Rutas sin puerto MIDI  (2026-11-02)

Problema: la salida "VstPlayer" de carrilion (y `--render`) aplica las rutas de `status.midiRoutes`,
pero VstPlayer sólo creaba una ruta al elegir un puerto MIDI de entrada en la fila. Quien baja el exe
desde carrilion sin teclado MIDI ni loopMIDI no podía crear ninguna ruta: carrilion decía "sin rutas"
y no sonaba nada.

**Qué se hizo:**
- **Semántica elegida: "sin puerto"** (no "cualquier puerto"). Una ruta con `inputDevice` vacío dice
  "esta instancia toma el canal n" sin escuchar ningún puerto: la usan carrilion, `--render` y el
  nuevo `midi` sin `instanceId`; el MIDI de los puertos no llega por ella. Se descartó "cualquier
  puerto" porque haría que una instancia empiece a recibir MIDI de un teclado o de loopMIDI que el
  usuario nunca eligió (notas dobles con carrilion por loopMIDI + API, sorpresas al enchufar algo);
  "sin puerto" no depende de qué hay conectado y es lo que pide el caso real. Queda anotado en el
  roadmap por si hace falta.
- **Formato** (`docs/project-format.md`): `"inputDevice": ""` (también ausente o `null` al leer, se
  escribe `""`). Sin cambio de versión: el texto vacío ya se leía sin avisos y el router lo ignoraba,
  así que un archivo nuevo en una versión vieja funciona igual (inerte para los puertos, publicado en
  `status`, respetado por `--render`); sólo la UI vieja lo muestra como "(sin entrada MIDI)". Abrir un
  proyecto no agrega ni cambia rutas.
- **Router**: `MidiRoute::hasPort()`; las rutas sin puerto no abren puertos ni entran en la tabla de
  los puertos. Nueva tabla de despacho con **todas** las rutas y `MidiRouter::routeMidi` (cualquier
  hilo): entrega por canal como carrilion / `--render` (una copia por instancia y canal de salida, aun
  con dos rutas al mismo canal), con seguimiento de notas sostenidas, LED de la fila y log `(API)`.
- **API**: `midi` sin `instanceId` rutea dentro de VstPlayer (`{"messages":n,"routed":m}`);
  `"instanceId": ""` → 400. `status.midiRoutes` (global y por instancia) incluye las rutas sin puerto
  con `"inputDevice": ""` (mismo formato: el `route_table` de carrilion no mira el dispositivo) y ya no
  lista las rutas sin canales (`route_table` lee `"channels": []` como "todos").
- **UI**: el selector de la fila tiene "(no recibe MIDI)" (sin ruta), "Sin puerto (sólo API /
  carrilion)" y los puertos (tooltip con la explicación). Una ruta nueva (instancia nueva, o elegir
  algo en una fila sin ruta) toma el **primer canal libre** (antes: todos los canales). **Las
  instancias nuevas** ("Agregar instancia…") vienen con "Sin puerto" en el primer canal libre: la
  primera en el 1, la segunda en el 2… "Duplicar" copia las rutas como antes. `InstancePanel`:
  `setInstanceMidiPort (id, "")` = sin puerto, `removeInstanceMidiRoute`. **Puertos MIDI…**: una línea
  "(sin puerto: sólo API / carrilion) rutas: N" y el MIDI ruteado por la API en el log como `(API)`; el
  aviso "no hay puertos" ya no manda a instalar loopMIDI.
- `ProjectController`: el aviso de ruta a una instancia inexistente nombra "La ruta MIDI sin puerto".
- **Ítem del roadmap "MIDI de la API no pasa por las rutas"**: resuelto para `midi` sin `instanceId`
  (chico: la tabla ya existía para los puertos). carrilion no se tocó: sigue aplicando las rutas él
  mismo (funciona igual); pasar a `midi` sin `instanceId` queda como mejora posible.
- Tests: fixture nuevo `tests/fixtures/sin-puerto.vstproj.json` (dos Dexed: Piano ← canal 1 keep, Bajo
  ← 2 y 3 remapTo 1, ambos sin puerto). `ProjectRoundTripTest`: ruta sin puerto en el proyecto
  sintético (`"inputDevice": ""` escrito como texto), lectura de `""` / ausente / `null` sin avisos,
  fixture en modo estricto. `--midi-selftest`: rutas por defecto (A 1, B 2, C 3 sin puerto, puerto
  cerrado), `routeMidi` a una ruta sin puerto y por un canal sin ruta, C "sin puerto": el MIDI de
  loopMIDI por su canal no le llega y el de la API sí, log `(API)`, dos rutas al mismo canal = una
  entrega. `run-ui-test`: filas nuevas en "Sin puerto" canales 1/2/3, "(no recibe MIDI)" y vuelta al
  primer canal libre. `run-project-test`: una ruta sin puerto (remapTo 1) se guarda y se reabre igual,
  sin avisos. `run-api-test`: `status` con las rutas sin puerto, el **puerto de carrilion real**
  (`../carrilion/apps/web/lib/core/vstplayer-port.js`, si el repo está al lado; si no, una copia de
  `route_table`) lee "1→Piano, 2→Bajo:1, 3→Bajo:1" y hace sonar cada instancia por su canal; `midi` sin
  `instanceId` (WebSocket y HTTP), canal sin ruta (`routed` 0), errores. `run-render-test`: el fixture
  rinde con canal 1 → Piano, canal 3 → Bajo por el 1, canal 5 sin ruta.

**Archivos:** `src/engine/MidiRouter.{h,cpp}`, `src/api/ApiController.cpp`,
`src/ui/InstancePanel.{h,cpp}`, `src/ui/MidiPortsWindow.{h,cpp}`, `src/project/{Project.h,
ProjectController.cpp}`, `src/app/{MidiSelfTest,UiSelfTest,ProjectSelfTest}.cpp` (en `UiSelfTest`
además el chequeo de layout ignora los tooltips), `tests/ProjectRoundTripTest.cpp`, `tests/fixtures/sin-puerto.vstproj.json` (nuevo),
`tools/{api-test,render-test}.mjs`, `README.md`, `CLAUDE.md`, `docs/{api,project-format,roadmap}.md`.

**Verificación:** builds Release y Debug sin warnings. Suite completa en Release y en Debug (Debug con
`-AllowedLeaks VST3HostContextHeadless,AsyncUpdater` donde se carga sforzando): `ctest` (121/121
comprobaciones), `--instance-selftest`, `--midi-selftest`, `run-project-test.ps1`, `run-ui-test.ps1`,
`run-api-test.ps1` (111/111) y `run-render-test.ps1` (88/88): OK, 0 aserciones, sólo las fugas
conocidas de sforzando en Debug. En `%APPDATA%\VstPlayer` sólo lo que ya tocaba el re-escaneo de
`run-ui-test.ps1` (`plugins.xml` con el mismo contenido, `plugin-scan.log`; ver su encabezado).
`run-ui-test.ps1` falló dos veces por un chequeo de layout: "se superponen InstancePanel y tooltip":
el puntero real quedó sobre un control y JUCE mostró su tooltip, que el chequeo tomaba como un
componente más. `checkLayout` ahora ignora los `TooltipWindow`; después pasó en Release y Debug. En una
corrida Debug también falló una vez "la UI siguió respondiendo durante el escaneo" (hueco 252 ms,
límite 250): timing de Debug, no relacionado.

**Pendiente:**
- carrilion: podría usar `midi` sin `instanceId` (sin copia por instancia ni sondeo de rutas cada 5 s)
  y leer `"channels": []` como "ningún canal".
- Semántica "cualquier puerto" sin implementar (ver roadmap).
- El límite de 250 ms del hueco durante el escaneo en `run-ui-test.ps1` queda justo en Debug.
