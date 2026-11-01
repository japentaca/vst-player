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
