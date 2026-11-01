# vst-player — Plan de implementación por etapas

Host de instrumentos VST3 para Windows que recibe MIDI por un puerto de entrada
(normalmente loopMIDI) y toca instancias de plugins organizadas en **proyectos**.
Pensado para que proyectos web (carrilion y otros) que emiten MIDI por Web MIDI
disparen sintes nativos sin cambiar nada de su lado.

## 0. Decisiones fijas (no se discuten en las etapas)

| Tema | Decisión |
|---|---|
| Lenguaje / framework | C++17, JUCE 8.0.x por CMake `FetchContent`, pin a un tag concreto |
| Formatos de plugin | **Sólo VST3**, 64 bits. Nada de VST2, AU, CLAP (CLAP se evalúa después) |
| Compilador | MSVC (Visual Studio 2022 Build Tools) + Ninja. **No** MinGW/clang de msys2 |
| Audio | `juce::AudioDeviceManager`: WASAPI por defecto; ASIO opcional si aparece el SDK en `third_party/asiosdk` |
| MIDI in | Puertos MIDI del sistema vía `juce::MidiInput`. loopMIDI ya está instalado en esta máquina |
| Grafo | `juce::AudioProcessorGraph`: nodo MIDI in → instancias → ganancia por instancia → mezcla → salida |
| Proyecto | JSON (`*.vstproj.json`), estado de cada plugin como base64 de `getStateInformation` |
| Configuración de usuario | `%APPDATA%\VstPlayer\` : `settings.xml`, `plugins.xml` (caché del escaneo), `recent.json` |
| Licencia | JUCE bajo AGPLv3 (uso personal / open source). Sin splash screen |
| Nombre del ejecutable | `VstPlayer.exe` |
| Idioma del código | Identificadores y comentarios en inglés; docs y UI en español |

Plugins de prueba presentes en la máquina (`C:\Program Files\Common Files\VST3`):
`Dexed.vst3` (simple, open source), `Surge Synth Team\Surge XT.vst3` (muchos parámetros,
pesado), `sforzando.vst3` (sampler, estado grande, buen sustituto de Kontakt hasta que se instale).

Referencia de código: el ejemplo `extras/AudioPluginHost` de JUCE hace casi todo esto.
Se usa como referencia de API, **no** se copia su código tal cual.

Fuente MIDI de prueba: carrilion (`C:\Users\jntac\Documents\prj\jape\carrilion`), que emite
por WebMidi.js al puerto que se elige en su selector; usa varios canales MIDI (uno por canal
de secuenciador) y un modo piano que mantiene teclas por canal x nota.

## 1. Protocolo de orquestación

- Cada etapa la ejecuta **un subagente nuevo**, secuencialmente, con `model: opus`
  (Opus 5.5) y `effort: medium`. No se lanza la etapa N+1 hasta que la N cumpla su
  criterio de aceptación.
- El subagente recibe este archivo completo, el número de etapa, y `docs/progress.md`.
- Antes de empezar, lee `docs/progress.md` y el código existente. No rehace lo anterior.
- Al terminar, **agrega** (no reescribe) una sección en `docs/progress.md` con: qué hizo,
  archivos creados o cambiados, cómo se verificó (comando y resultado), problemas
  abiertos. Si una verificación falla, lo dice tal cual.
- Hace commit al final de la etapa con mensaje `etapa N: <resumen>`.
- Si algo del plan no se puede cumplir, lo implementa hasta donde llega, lo documenta en
  `progress.md` bajo "Pendiente" y **no** amplía el alcance a etapas siguientes.
- Las verificaciones que requieren oír audio o tocar desde carrilion las hace el usuario;
  el subagente deja preparado el escenario (proyecto de prueba, pasos exactos) y lo marca
  como "verificación manual pendiente" en `progress.md`. Todo lo que se pueda verificar
  por programa (compilación, tests, escaneo, round-trip JSON, API con curl) lo verifica él.
- Compilación de referencia siempre en Release:
  ```
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build build
  ```
  (desde un shell con el entorno de MSVC cargado, ver etapa 0).

Plantilla de prompt para cada subagente:

```
Sos el implementador de la etapa N del proyecto vst-player.
Leé C:\Users\jntac\Documents\prj\jape\vst-player\PLAN.md completo y docs\progress.md.
Implementá SOLO la etapa N siguiendo sus tareas y criterio de aceptación.
Respetá las decisiones fijas de la sección 0 y el protocolo de la sección 1.
Al terminar, agregá tu sección en docs\progress.md y hacé commit.
Reportá: qué quedó funcionando, qué verificaste y cómo, y qué quedó pendiente.
```

## 2. Estructura de carpetas objetivo

```
vst-player/
  CMakeLists.txt
  cmake/              # helpers (FetchContent de JUCE, flags MSVC)
  src/
    Main.cpp          # JUCEApplication
    app/              # ventana principal, menús, settings
    engine/           # AudioEngine, PluginManager, InstanceGraph, MidiRouter
    project/          # Project (modelo), ProjectSerializer (JSON)
    api/              # servidor local (etapa 7)
    ui/               # componentes de la UI
  tests/              # tests de round-trip y modelo (ctest)
  tools/
    build.ps1         # carga vcvars y compila Release
    dev-env.ps1       # abre shell con entorno MSVC
  docs/
    progress.md       # bitácora por etapa
    project-format.md # esquema JSON del proyecto
    api.md            # API local (etapa 7)
  examples/
    carrilion-client.js  # cliente JS de ejemplo (etapa 7)
  third_party/        # vacío salvo asiosdk opcional (gitignored)
```

---

## Etapa 0 — Entorno y repositorio

**Objetivo:** que `cmake --build` compile un hola-mundo C++ con MSVC en esta máquina.

Estado detectado el 2026-11-01: CMake 4.0.1, Ninja, git y winget presentes; **no** hay
Visual Studio ni Build Tools; loopMIDI instalado; carpeta VST3 con Dexed, Surge XT, sforzando.

**Tareas**
1. Instalar Visual Studio 2022 Build Tools con el workload de C++ (sin IDE):
   ```
   winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
   ```
   Si winget falla, documentar el comando manual para el usuario y detenerse ahí.
2. Crear `tools/dev-env.ps1` que localice `vcvars64.bat` con `vswhere` y cargue sus
   variables en la sesión PowerShell actual, y `tools/build.ps1` que haga configure + build
   (parámetro `-Config Debug|Release`, default Release).
3. `git init`, `.gitignore` (build/, third_party/asiosdk/, *.user, .vs/), `.gitattributes`
   (`* text=auto`), `.editorconfig` (4 espacios, UTF-8, LF).
4. `CMakeLists.txt` mínimo + `src/Main.cpp` hola mundo. Compilar.
5. Completar `docs/progress.md` con la sección de esta etapa.

**Criterio de aceptación**
- `.\tools\build.ps1` produce un ejecutable hola-mundo y se ejecuta.
- `cl.exe` es MSVC 19.3x o superior. Documentar versión en progress.md.

---

## Etapa 1 — Esqueleto JUCE

**Objetivo:** aplicación JUCE que abre una ventana vacía con menú, compila en Release y
tiene activado el hosting VST3.

**Tareas**
1. `cmake/juce.cmake`: `FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG <tag 8.0.x más reciente> GIT_SHALLOW TRUE)`.
2. `juce_add_gui_app(VstPlayer PRODUCT_NAME "VstPlayer" COMPANY_NAME "Jape")`.
   Módulos: `juce_gui_extra`, `juce_audio_utils`, `juce_audio_processors`, `juce_audio_devices`.
   Definiciones: `JUCE_PLUGINHOST_VST3=1`, `JUCE_PLUGINHOST_VST=0`, `JUCE_PLUGINHOST_AU=0`,
   `JUCE_PLUGINHOST_LADSPA=0`, `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`,
   `JUCE_DISPLAY_SPLASH_SCREEN=0`, `JUCE_REPORT_APP_USAGE=0`.
   Si el SDK de ASIO está en `third_party/asiosdk`, definir `JUCE_ASIO=1` e incluirlo.
3. `src/Main.cpp` con `juce::JUCEApplication`, `src/app/MainWindow.{h,cpp}` con
   `DocumentWindow` + `MenuBarModel` (menús Archivo, Audio, Ayuda vacíos por ahora).
4. `src/app/AppSettings.{h,cpp}`: `juce::PropertiesFile` en `%APPDATA%\VstPlayer\settings.xml`,
   con tamaño y posición de ventana persistidos.
5. Flags MSVC: `/W4`, `/permissive-`, `/utf-8`, `/MP`. Sin warnings-as-errors.

**Criterio de aceptación**
- `.\tools\build.ps1` genera `build\VstPlayer_artefacts\Release\VstPlayer.exe`.
- La app abre, muestra la ventana, recuerda su tamaño al reabrir, cierra limpio.
- La primera compilación de JUCE tarda varios minutos; anotar el tiempo en progress.md.

---

## Etapa 2 — Motor de audio y escáner de plugins

**Objetivo:** el host enumera los VST3 instalados y suena (tono de prueba) por el dispositivo
de audio elegido.

**Tareas**
1. `src/engine/AudioEngine.{h,cpp}`: posee `AudioDeviceManager`, inicializa con WASAPI,
   48 kHz, buffer 256 por defecto; guarda y restaura la configuración (XML de
   `createStateXml`) en settings. Expone `getDeviceManager()`, sample rate, buffer, y un
   `AudioProcessorPlayer` conectado a un `AudioProcessorGraph` (todavía con un solo nodo
   de salida).
2. Menú Audio → "Configuración de audio…" abre `AudioDeviceSelectorComponent` en un diálogo.
   Cambios de sample rate o buffer deben propagarse al grafo (`prepareToPlay`).
3. `src/engine/PluginManager.{h,cpp}`: `AudioPluginFormatManager` con sólo `VST3PluginFormat`,
   `KnownPluginList` persistida en `%APPDATA%\VstPlayer\plugins.xml`.
   Escaneo con `PluginDirectoryScanner` **fuera de proceso** (`juce::ChildProcess` o el
   mecanismo de `PluginListComponent` con `setCustomScanner`) para que un plugin que
   crashea no tumbe el host. Rutas: `C:\Program Files\Common Files\VST3` + carpetas extra
   configurables en settings.
4. Menú Archivo → "Plugins…" abre un diálogo con `PluginListComponent` (escanear, quitar,
   ver lista con nombre, fabricante, versión, ruta, si es instrumento).
5. Un nodo generador de tono de prueba (seno 440 Hz, botón en la UI) para validar audio
   sin plugins. Se quita en la etapa 6.

**Criterio de aceptación**
- Escaneo encuentra al menos Dexed, Surge XT y sforzando; `plugins.xml` queda escrito y
  al reabrir no re-escanea.
- El tono de prueba se oye por el dispositivo elegido; cambiar buffer a 128 no corta
  (verificación manual del usuario).
- Un VST3 roto (simular con un `.vst3` vacío en una carpeta extra) no cierra la app.

---

## Etapa 3 — Instancias y grafo

**Objetivo:** crear instancias de plugins, abrir sus editores, oírlas mezcladas.

**Tareas**
1. `src/engine/InstanceGraph.{h,cpp}` sobre `AudioProcessorGraph`:
   - `addInstance(PluginDescription) -> InstanceId` (UUID string). Carga asíncrona con
     `createPluginInstanceAsync`; errores reportados con mensaje legible.
   - Por instancia: nodo del plugin + nodo de ganancia (`GainProcessor` propio con
     `gainDb`, `mute`) conectado al mezclador de salida estéreo. Plugins con más de 2
     salidas: mezclar sólo las dos primeras en esta etapa, documentar.
   - `removeInstance`, `getInstances()`, `setGainDb`, `setMute`, `panic()` (all notes off
     + all sound off a cada instancia, en los 16 canales).
   - Toda modificación del grafo desde el hilo de mensajes; el grafo de JUCE ya es seguro
     respecto del hilo de audio.
2. `src/engine/Instance.h`: struct con id, nombre editable, `PluginDescription`,
   puntero al nodo, gain, mute, estado de ventana.
3. `src/ui/PluginEditorWindow.{h,cpp}`: `DocumentWindow` que aloja
   `createEditorIfNeeded()`; si el plugin no tiene editor, `GenericAudioProcessorEditor`.
   Una ventana por instancia, reutilizada; cerrar la ventana no destruye la instancia.
   Guardar posición de la ventana en la instancia.
4. UI provisoria en la ventana principal: botón "Agregar instancia" (elige de la
   `KnownPluginList` con un popup menu por fabricante), lista de instancias con botones
   "Editor", "Quitar", slider de ganancia, mute. Botón "Pánico".
5. Teclado virtual (`MidiKeyboardComponent`) al pie de la ventana, que envía notas a la
   instancia seleccionada en el canal 1. Sirve para probar sin MIDI externo.

**Criterio de aceptación**
- Dos instancias (Dexed + Surge XT) suenan a la vez con el teclado virtual; sus editores
  abren y cierran sin fugas ni crashes; quitar una instancia con el editor abierto es seguro.
- sforzando carga, abre editor y permite cargar un SFZ.
- Cambiar buffer de audio con instancias cargadas no crashea.

---

## Etapa 4 — Entrada MIDI y ruteo

**Objetivo:** MIDI de loopMIDI (desde carrilion) llega a las instancias correctas.

**Tareas**
1. `src/engine/MidiRouter.{h,cpp}`:
   - Enumera `MidiInput::getAvailableDevices()`, abre los que el proyecto use, detecta
     conexión y desconexión (`MidiDeviceListConnection` o poll cada 2 s).
   - Modelo de ruta: `{ inputDeviceName, channels: set<1..16>, instanceId, channelMode }`
     con `channelMode` = `keep` (pasa el canal original) o `remapTo: n`.
   - Implementación: `MidiInputCallback` escribe a un `MidiMessageCollector` por
     instancia; el nodo de la instancia recibe su bloque en `processBlock`. Preservar
     timestamps relativos dentro del bloque.
   - Mensajes de sistema (clock, sysex) descartados en esta etapa.
2. UI: en cada instancia, selector de puerto de entrada y de canales (16 toggles o
   "todos"). Indicador de actividad MIDI por instancia (LED que parpadea).
3. Panic también limpia los colectores.
4. Menú Audio → "Puertos MIDI": lista con estado de cada puerto y log de los últimos
   mensajes (ventana de diagnóstico).

**Criterio de aceptación**
- Con carrilion en Chrome seleccionando el puerto loopMIDI, un proyecto con varios canales
  MIDI toca cada canal en su instancia según las rutas. Probar `keep` y `remapTo`.
- Modo piano de carrilion (notas sostenidas, retrigger) no deja notas colgadas; si quedan,
  Pánico las corta.
- Borrar y volver a crear el puerto loopMIDI con la app abierta se recupera solo.
- Anotar en progress.md la latencia que reporta `AudioDeviceManager` con buffer 128 y 256.

---

## Etapa 5 — Proyectos (modelo y JSON)

**Objetivo:** guardar y reabrir exactamente el mismo setup: instancias, estados, rutas,
ganancias, ventanas, audio.

**Tareas**
1. `src/project/Project.{h,cpp}`: modelo en memoria (sin JUCE UI). `docs/project-format.md`
   con el esquema versionado. Esquema v1:
   ```json
   {
     "version": 1,
     "name": "Piano y pads",
     "audio": { "deviceType": "Windows Audio", "outputDevice": "...", "sampleRate": 48000, "bufferSize": 256 },
     "instances": [
       {
         "id": "uuid",
         "name": "Piano",
         "plugin": { "name": "sforzando", "manufacturer": "Plogue", "fileOrIdentifier": "C:\\...\\sforzando.vst3", "uniqueId": "...", "version": "..." },
         "gainDb": 0.0, "mute": false,
         "state": "<base64 de getStateInformation>",
         "params": { "Cutoff": 0.5 },
         "window": { "x": 100, "y": 100, "open": true }
       }
     ],
     "midiRoutes": [
       { "inputDevice": "loopMIDI Port", "channels": [1, 2], "instanceId": "uuid", "channelMode": "keep" }
     ]
   }
   ```
   - `params` es opcional: overrides por **nombre** de parámetro aplicados después de
     `setStateInformation`. Valores normalizados 0..1.
   - `plugin` se resuelve primero por `uniqueId` en la `KnownPluginList`, luego por ruta,
     luego por nombre; si no se encuentra, la instancia queda como "faltante" (se muestra
     en rojo, se conserva en el JSON al volver a guardar, no se pierde el estado).
2. `src/project/ProjectSerializer.{h,cpp}`: `toJson` / `fromJson` con `juce::var` /
   `JSON`, escritura atómica (temp + rename). Tests en `tests/` como ejecutable CMake
   registrado en `ctest` que hace round-trip de un proyecto sintético.
3. Integración: `ProjectController` que aplica un `Project` al `InstanceGraph` +
   `MidiRouter` + `AudioEngine` (carga asíncrona con progreso; estados aplicados cuando
   la instancia está lista) y que extrae el `Project` actual para guardar.
4. Menú Archivo: Nuevo, Abrir…, Guardar, Guardar como…, Recientes (máx. 10, en
   `recent.json`), con pregunta de "cambios sin guardar" al cerrar. Título de ventana con
   nombre del proyecto y `*` si está sucio.
5. Línea de comandos: `VstPlayer.exe ruta\proyecto.vstproj.json` abre ese proyecto.
   Asociar extensión `.vstproj.json` queda para la etapa 8.
6. Autoguardado de respaldo cada 2 minutos en `%APPDATA%\VstPlayer\autosave.vstproj.json`
   si hay cambios.

**Criterio de aceptación**
- Proyecto con Dexed (preset elegido en su editor), Surge XT (patch cambiado) y sforzando
  (SFZ cargado) + rutas MIDI: guardar, cerrar la app, abrir desde recientes, y suena
  idéntico sin tocar nada. Round-trip de JSON no pierde campos desconocidos de versiones
  futuras (conservar `extra`).
- Test de round-trip pasa con `ctest`.
- Un proyecto que referencia un plugin desinstalado abre igual y avisa.

---

## Etapa 6 — Interfaz definitiva (mínima pero cuidada)

**Objetivo:** reemplazar la UI provisoria por una que se pueda usar todos los días.

**Tareas**
1. Layout: barra superior (proyecto, dispositivo de audio, CPU %, sample rate/buffer,
   botón Pánico), lista central de instancias como filas (`ListBox` o `Viewport` de
   componentes), teclado virtual plegable abajo, barra de estado con mensajes.
2. Fila de instancia: nombre editable, nombre del plugin, puerto MIDI y canales, LED MIDI,
   slider de ganancia en dB, mute, vúmetro estéreo de salida (medición en el nodo de
   ganancia, `std::atomic<float>` por canal), botones Editor, Duplicar, Quitar.
3. Agregar instancia: diálogo con búsqueda por nombre, filtro "sólo instrumentos",
   agrupado por fabricante.
4. Medidor de CPU del audio (`AudioDeviceManager::getCpuUsage`).
5. Look and feel: `LookAndFeel_V4` con tema oscuro, tipografía por defecto, sin iconos
   externos. Atajos: Ctrl+S, Ctrl+O, Ctrl+N, Esc = pánico.
6. Quitar el generador de tono y la UI provisoria.

**Criterio de aceptación**
- Todas las operaciones de las etapas 3 a 5 se hacen desde la UI sin menús ocultos.
- La ventana es usable a 1024x600 y escala con DPI de Windows.
- Ningún trabajo pesado (escaneo, carga de plugin) bloquea la UI: hay indicador de progreso.

---

## Etapa 7 — API local para los proyectos web

**Objetivo:** que carrilion u otra página en el navegador pueda controlar el host: cargar
proyecto, listar instancias y parámetros, mover parámetros, pánico.

**Tareas**
1. Servidor WebSocket + HTTP mínimo en `127.0.0.1`, puerto configurable (default 9471),
   en un hilo propio. Implementación preferida: `cpp-httplib` + `IXWebSocket` vía
   FetchContent (header-only / sin dependencias pesadas). Sólo escucha en loopback.
   Opcional: token en settings, enviado como query `?token=`.
2. CORS: `Access-Control-Allow-Origin: *` en HTTP (sólo loopback, riesgo aceptable;
   documentarlo).
3. Mensajes JSON (documentar en `docs/api.md`):
   - `{"cmd":"status"}` → proyecto actual, audio, instancias con id, nombre, plugin,
     rutas MIDI, gain, mute.
   - `{"cmd":"loadProject","path":"..."}` / `{"cmd":"listProjects","dir":"..."}` /
     `{"cmd":"saveProject"}`.
   - `{"cmd":"listParams","instanceId":"..."}` → `[ {index, name, value, text, label} ]`.
   - `{"cmd":"setParam","instanceId":"...","name":"Cutoff","value":0.4}` (también por `index`).
   - `{"cmd":"setGain"}`, `{"cmd":"setMute"}`, `{"cmd":"panic"}`.
   - `{"cmd":"listPrograms","instanceId"}` / `{"cmd":"setProgram","instanceId","index"}`.
   - `{"cmd":"midi","instanceId":"...","bytes":[144,60,100]}`: MIDI directo por WebSocket
     como alternativa a loopMIDI (sin timestamps, para pruebas y control).
   - Eventos push por WS: `instanceAdded`, `instanceRemoved`, `paramChanged`
     (throttled a 20 Hz), `projectLoaded`, `midiActivity`.
4. Todo comando que toque el grafo se despacha al hilo de mensajes con
   `MessageManager::callAsync` y responde cuando terminó; `setParam` va directo a
   `AudioProcessorParameter::setValueNotifyingHost` (es seguro desde otro hilo).
5. `examples/carrilion-client.js`: módulo ES sin dependencias, `connect(url)`,
   `status()`, `setParam(...)`, `loadProject(...)`, con un ejemplo de uso en comentarios
   pensado para integrarse en `apps/web/lib/engine/midi-out.js` de carrilion (sin
   modificar carrilion en esta etapa).
6. Indicador en la barra de estado: "API: 127.0.0.1:9471, N clientes".

**Criterio de aceptación**
- Verificable por programa: con `curl` o un script Node contra el servidor, `status`
  devuelve las instancias, `setParam` cambia el valor leído después con `listParams`,
  `loadProject` cambia el proyecto, JSON inválido devuelve error sin caer.
- Manual: desde la consola de Chrome en carrilion, con el cliente de ejemplo, `setParam`
  mueve un parámetro visible en el editor de Surge XT y `panic` corta.
- Con 50 `setParam` por segundo no hay clicks ni drops de audio.

---

## Etapa 8 — Empaquetado, documentación y cierre

**Objetivo:** que el ejecutable se pueda instalar y usar sin el repositorio.

**Tareas**
1. `tools/package.ps1`: build Release + zip `VstPlayer-<version>-win64.zip` con el exe,
   `README.md`, `examples/`, `docs/`. Versión desde `CMakeLists.txt` (`project(... VERSION)`)
   mostrada en Ayuda → Acerca de.
2. Asociación opcional de `.vstproj.json` (script `tools/register-filetype.ps1` que
   escribe en HKCU, con `-Unregister`).
3. `README.md`: qué es, requisitos (Windows 10/11 x64, loopMIDI), instalación, flujo
   carrilion → loopMIDI → VstPlayer, formato de proyecto, API, cómo compilar.
4. `CLAUDE.md` del repo: estructura, convenciones, cómo compilar y probar, decisiones
   fijas (resumen de la sección 0), pendientes conocidos.
5. Revisión final: revisar el repo buscando errores de correctness (hilos, ciclo de vida
   de instancias y editores, manejo de errores de carga) y corregirlos. Verificar que
   `build/` borrado compila de cero con `tools/build.ps1`.
6. Lista de siguientes pasos fuera de alcance, en `docs/roadmap.md`: CLAP, multisalida
   por instancia, efectos en cadena por instancia, grabación a WAV, ASIO con SDK, MIDI
   clock, Windows MIDI Services como alternativa a loopMIDI.

**Criterio de aceptación**
- El zip descomprimido en otra carpeta arranca, abre un proyecto guardado y toca desde
  carrilion siguiendo sólo el README.
- `git status` limpio, tag `v0.1.0`.

---

## 3. Riesgos conocidos y cómo tratarlos

- **Primera compilación de JUCE lenta** (5–15 min). Usar Ninja y `/MP`; no limpiar `build/`
  entre etapas.
- **Plugins que crashean al escanear o instanciar.** Escaneo fuera de proceso (etapa 2);
  instanciación dentro del proceso es inevitable, se documenta.
- **Estado de plugin grande** (samplers): base64 en JSON puede ser de varios MB; aceptable.
  Si molesta, en el futuro el estado va a un archivo `.state` al lado del proyecto.
- **Nombres de parámetro duplicados** dentro de un plugin: `params` por nombre toma el
  primero; la API también acepta `index`.
- **Cambios de nombre de puerto MIDI** (loopMIDI permite renombrar): el proyecto guarda
  el nombre; si no existe al abrir, la ruta queda inactiva y se muestra para reasignar.
- **Hilos:** regla única, el grafo se modifica sólo en el hilo de mensajes; parámetros y
  MIDI pueden venir de cualquier hilo por las vías que JUCE marca como seguras.
