# CLAUDE.md — vst-player

Host de instrumentos VST3 para Windows (JUCE 8, C++17, MSVC). Recibe MIDI por puertos del sistema
(loopMIDI), toca instancias de plugins organizadas en proyectos JSON y expone una API local
HTTP + WebSocket. El plan original por etapas está en `PLAN.md`; la bitácora de cada etapa (qué se
hizo, cómo se verificó, problemas) en `docs/progress.md`. Usuario final: `README.md`.

## Decisiones fijas (resumen de PLAN.md §0)

- C++17, **JUCE 8.0.15** por CMake `FetchContent` (tag fijo en `cmake/juce.cmake`). cpp-httplib
  v0.64.0 (header-only) para la API (`cmake/httplib.cmake`).
- **Sólo VST3 de 64 bits** (nada de VST2/AU/LV2/CLAP).
- **MSVC** (VS 2022 Build Tools) + **Ninja**. Nada de MinGW/clang. Runtime de MSVC **estático**
  (`CMAKE_MSVC_RUNTIME_LIBRARY`, `/MT`): el exe no depende de los redistribuibles.
- Audio con `juce::AudioDeviceManager`, WASAPI por defecto; ASIO sólo si está el SDK en
  `third_party/asiosdk` (gitignored).
- MIDI in con `juce::MidiInput` (loopMIDI instalado en la máquina de desarrollo).
- Grafo `juce::AudioProcessorGraph`: por instancia `MidiSourceProcessor` → plugin → `GainProcessor`
  → mezclador (`GainProcessor` "Mezcla") → salida.
- Proyecto `*.vstproj.json` (esquema v1 en `docs/project-format.md`), estado del plugin en base64.
- Datos de usuario en `%APPDATA%\VstPlayer\` (`settings.xml`, `plugins.xml`, `recent.json`,
  `autosave.vstproj.json`, `plugin-scan.log`); `--data-dir <dir>` lo redirige todo.
- JUCE bajo AGPLv3, sin splash. Ejecutable `VstPlayer.exe`. Versión: `project(VstPlayer VERSION …)`
  en `CMakeLists.txt` (Ayuda → Acerca de, recurso de versión del exe, nombre del zip).
- **Código (identificadores y comentarios) en inglés; docs y UI en español.**

## Estructura

```
CMakeLists.txt        targets VstPlayer (gui app) y ProjectRoundTripTest (ctest)
cmake/                juce.cmake, httplib.cmake, msvc-flags.cmake (/W4 /permissive- /utf-8 /MP)
src/Main.cpp          JUCEApplication: crea y destruye todo en orden; modos de línea de comandos
src/app/              MainWindow (menús, archivo, atajos), AppSettings, CommandLine (modos sin GUI),
                      OfflineRender (--render),
                      Translations (textos de JUCE en español), ProcessTools, *SelfTest (tests de GUI)
src/engine/           AudioEngine, PluginManager (escaneo fuera de proceso), InstanceGraph, Instance,
                      MidiRouter, MidiSourceProcessor, GainProcessor
src/project/          Project (modelo, sólo juce_core), ProjectSerializer (JSON), ProjectController
                      (aplica/captura proyectos, sucio, autoguardado), RecentProjects
src/api/              ApiServer (red, cpp-httplib, sin JUCE), ApiController (comandos, eventos)
src/ui/               MainContent, InstancePanel (filas), AddInstanceDialog, PluginEditorWindow,
                      PluginListWindow, MidiPortsWindow, LevelMeter, Theme (look and feel oscuro)
resources/            ícono: icon.svg (48 px y más), icon-small.svg (16/32 px) y los PNG que pasa CMake
                      (ICON_BIG / ICON_SMALL; JUCE arma el .ico). Regenerar: node tools/render-icon.mjs
tests/                ProjectRoundTripTest.cpp; fixtures/ = proyectos de prueba fijos (prueba-etapa5 con sus
                      3 instancias, plugin-faltante) para ctest, api-test y render-test. examples/ lo
                      edita el usuario con la app: los tests no dependen de su contenido
tools/                build.ps1, dev-env.ps1, package.ps1, publish-carrilion.ps1 (+ VSTPLAYER.md.in),
                      register-filetype.ps1, render-icon.mjs, run-*-test.ps1, api-test.mjs, render-test.mjs
docs/                 progress.md, project-format.md, api.md, roadmap.md
examples/             proyectos de ejemplo, sfz/, carrilion-client.js (embebido en el exe: GET /client.js)
```

## Compilar

```powershell
pwsh -NoProfile -File .\tools\build.ps1                    # Release -> build\VstPlayer_artefacts\Release\VstPlayer.exe
pwsh -NoProfile -File .\tools\build.ps1 -Config Debug      # Debug -> build-debug\
pwsh -NoProfile -File .\tools\build.ps1 -BuildDir <dir>    # otro directorio (p. ej. compilar de cero)
pwsh -NoProfile -File .\tools\package.ps1                  # Release + dist\VstPlayer-<versión>-win64.zip
pwsh -NoProfile -File .\tools\publish-carrilion.ps1        # Release + ..\carrilion\bin\win64\VstPlayer.exe
```

`publish-carrilion.ps1` copia el exe al repo de carrilion (`-Target <carpeta>` para otro lugar,
`-SkipBuild` para no compilar) y escribe ahí `VSTPLAYER.md` (versión, commit de HEAD, fecha, tamaño,
SHA-256, licencia) desde `tools/VSTPLAYER.md.in`. Publicar desde un estado commiteado (avisa si hay
cambios en lo que entra al exe) y después commitear `bin/win64` en carrilion (el script no commitea).

`build.ps1` carga el entorno de MSVC (`dev-env.ps1`, vía vswhere + vcvars64) y hace configure + build
con Ninja. **No borrar `build\` ni `build-debug\`** entre cambios: JUCE desde cero tarda varios
minutos. Para usar `cl`, `dumpbin` o `ctest` a mano: `& .\tools\dev-env.ps1` en la sesión.
Los scripts de `tools/` son ASCII (funcionan también en Windows PowerShell 5.1).

## Probar

Toda la suite debe pasar en Release y en Debug antes de cerrar un cambio:

```powershell
ctest --test-dir build ; ctest --test-dir build-debug                       # round-trip JSON
& .\tools\run-gui-test.ps1                                                  # --instance-selftest
& .\tools\run-gui-test.ps1 -AppArgs '--midi-selftest'                       # MIDI por loopMIDI
& .\tools\run-project-test.ps1                                              # guardar / reabrir proyectos
& .\tools\run-ui-test.ps1                                                   # layout, atajos, responsividad
& .\tools\run-api-test.ps1                                                  # API (requiere Node 22+)
& .\tools\run-render-test.ps1                                               # --render offline (Node 22+)
# Debug: agregar -Config Debug y, donde se carga sforzando, -AllowedLeaks VST3HostContextHeadless,AsyncUpdater
```

- Los self-tests abren la GUI real, corren sin intervención y salen con exit 0 si todo pasó.
  `--debug-to-stdout` copia `OutputDebugString` a stdout; los scripts fallan ante una aserción de
  JUCE o una fuga. Pasar arrays a los scripts con `&` (con `pwsh -File` llegan como un solo texto).
- Los tests **no** deben dejar nada en `%APPDATA%\VstPlayer` (sin recientes, autoguardado ni cambios
  de settings): los modos self-test no persisten; usar `--project-data-dir` / `--data-dir` con una
  carpeta temporal.
- Necesitan loopMIDI corriendo con "loopMIDI Port", los plugins Dexed, Surge XT y sforzando, y una
  salida de audio. Lo que requiere oír o tocar desde carrilion queda como verificación manual.
- Herramientas de diagnóstico: `--midi-ports`, `--midi-monitor`, `--midi-send`, `--audio-devices`,
  `--audio-selftest`, `--scan`, `--plugin-folders`, `--add-plugin-folder`, `--remove-plugin-folder`.
- Render offline: `--render <proyecto> --midi <archivo.mid> --out <archivo.wav>` (`src/app/OfflineRender`):
  sin dispositivo, puertos, API ni ventana; settings y `plugins.xml` de sólo lectura; el grafo corre
  `setNonRealtime` en un hilo propio y el MIDI entra por `MidiSourceProcessor::addOfflineMessage` (no
  por el collector). stdout = una línea JSON; progreso en stderr. Contrato en `README.md`.
- Carpetas de búsqueda de plugins: las estándar de VST3 (fijas) + `pluginExtraFolders` en
  `settings.xml`, manejadas por `PluginManager` (agregar / quitar / validar). El escaneo desde la UI
  pasa la lista de archivos a `PluginListComponent::scanFor`, sin el diálogo de carpetas de JUCE.

## Convenciones y reglas de hilos

- El grafo, el router MIDI y el proyecto se modifican **sólo en el hilo de mensajes**. Parámetros
  (`setValueNotifyingHost`) y MIDI (`MidiMessageCollector`) pueden venir de cualquier hilo.
- Rutas MIDI: `inputDevice` vacío = ruta **sin puerto** (ningún puerto llega por ella; la usan
  carrilion, `midi` sin `instanceId` → `MidiRouter::routeMidi` y `--render`, que cuentan todas las
  rutas). Las instancias nuevas de la UI reciben una en el primer canal libre; abrir un proyecto no
  agrega rutas.
- Quitar una instancia: `instanceAboutToBeRemoved` avisa primero (la UI cierra el editor, el router y
  la API sacan sus punteros bajo lock) y recién después se quitan los nodos.
- Los VST3 se crean en el hilo de mensajes (restricción de JUCE/VST3): cola de a uno, DLL precargado en
  otro hilo.
- La API despacha al hilo de mensajes con `MessageManager::callAsync` y espera con timeout; se detiene
  antes que todo lo demás al cerrar (`Main.cpp::shutdown`).
- Estilo JUCE (llaves en línea propia, espacio antes de paréntesis de llamada), 4 espacios, LF, UTF-8.
- Commits por etapa/cambio: `etapa N: …` durante el plan; mensajes en español.

## Pendientes conocidos

Ver `docs/roadmap.md` (detalle) y la sección "Pendiente" de cada etapa en `docs/progress.md`. Los
principales: valores viejos de parámetros en la caché del host VST3 (`listParams` con `refresh`),
crear un VST3 bloquea la UI 0.1–0.5 s, fuga de `VST3HostContextHeadless` con sforzando, WASAPI
compartido sólo ofrece buffer 480 en la máquina de desarrollo (ASIO pendiente).
