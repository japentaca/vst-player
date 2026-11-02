# VstPlayer

Host de instrumentos **VST3** para Windows. Recibe MIDI por un puerto de entrada (normalmente
**loopMIDI**) y toca instancias de plugins organizadas en **proyectos** (`*.vstproj.json`). Está
pensado para que páginas web que emiten MIDI con Web MIDI (por ejemplo **carrilion**) disparen
sintes nativos sin cambiar nada de su lado. Además expone una **API local** (HTTP + WebSocket en
`127.0.0.1:9471`) para controlarlo desde el navegador o desde scripts.

- Varias instancias de plugins VST3 (64 bits), cada una con ganancia, mute, vúmetro y su editor.
- Ruteo MIDI por puerto y canal (`keep` = canal original, o `remapTo` = todo a un canal).
- Proyectos JSON con el estado completo de cada plugin, rutas, ganancias, ventanas y audio.
- Escaneo de plugins fuera de proceso (un plugin que se cae al escanear no tumba la app).
- Teclado virtual, botón **PÁNICO** (Esc), diagnóstico de puertos MIDI.

## Requisitos

- Windows 10 u 11 de 64 bits. No hace falta instalar nada más para el programa (el runtime de
  Visual C++ va enlazado dentro del ejecutable).
- Opcional: [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html) (gratis) para recibir MIDI
  desde el navegador u otros programas por un puerto MIDI (por ejemplo el que propone, `loopMIDI Port`),
  o un teclado MIDI. **No hace falta para carrilion**: su salida "VstPlayer" toca por la API local con
  rutas "Sin puerto" (ver "carrilion → VstPlayer sin dispositivos MIDI").
- Plugins **VST3** de 64 bits instalados (normalmente en `C:\Program Files\Common Files\VST3`).
  Probado con Dexed, Surge XT y sforzando.
- Una salida de audio. Por defecto usa *Windows Audio* (WASAPI compartido); en muchas placas eso
  significa un buffer de 10 ms (480 muestras a 48 kHz). Ver "Latencia".

## Instalación

1. Descomprimir `VstPlayer-<versión>-win64.zip` en cualquier carpeta (por ejemplo
   `C:\Users\<vos>\Apps\VstPlayer`). No hace falta instalador ni permisos de administrador.
2. Ejecutar `VstPlayer.exe`. La primera vez Windows SmartScreen puede avisar que el programa no está
   firmado: "Más información" → "Ejecutar de todas formas".
3. **Escanear los plugins** (la primera vez y cada vez que instales plugins nuevos): botón
   **Plugins…** (arriba a la derecha) → **Escanear**. La barra de estado dice "Escaneando plugins…"; al
   terminar, la lista muestra nombre, fabricante y versión. Sólo se prueban los archivos nuevos o
   cambiados. Mientras no haya plugins escaneados, la barra de estado lo recuerda.
   Alternativa desde una consola: `VstPlayer.exe --scan`.
   - **Carpetas de búsqueda**: arriba en la misma ventana. Siempre se buscan las dos carpetas estándar
     de VST3 (`C:\Program Files\Common Files\VST3` y `%LOCALAPPDATA%\Programs\Common\VST3`), con sus
     subcarpetas. Si tenés plugins en otro lado (otro disco, una carpeta propia), **Agregar carpeta…**
     la suma a la lista y la escanea en el momento. **Quitar carpeta** la saca y quita de la lista los
     plugins que estaban sólo ahí (un proyecto que los use los va a mostrar como faltantes).
   - Una carpeta agregada que no existe en ese momento (un disco externo desconectado) se conserva y
     se marca "no existe ahora"; se vuelve a usar cuando aparece.
   - No se aceptan la raíz de un disco, `C:\Windows`, `Program Files` ni la carpeta del usuario (se
     recorrería el disco entero), ni una carpeta que ya esté cubierta por otra de la lista.
4. Opcional: asociar los proyectos con VstPlayer (sólo para tu usuario, sin administrador):
   `powershell -ExecutionPolicy Bypass -File .\tools\register-filetype.ps1`
   Agrega "Abrir en VstPlayer" al menú contextual de los `*.vstproj.json` (en Windows 11, dentro de
   "Mostrar más opciones") y VstPlayer a "Abrir con". Windows no permite asociar una extensión doble
   (`.vstproj.json` es un `.json` para el Explorador), así que para abrir con doble clic hay que elegir
   una vez "Abrir con → Elegir otra aplicación → VstPlayer → Siempre" (eso vale para todos los
   `.json`). Para deshacerlo: el mismo script con `-Unregister`.

Archivos del usuario: `%APPDATA%\VstPlayer\` (`settings.xml`, `plugins.xml`, `recent.json`,
`autosave.vstproj.json`). Para desinstalar: borrar la carpeta del programa y esa carpeta (y correr
`register-filetype.ps1 -Unregister` si se usó).

## Uso rápido

1. **+ Agregar instancia…**: buscar el plugin (por nombre o fabricante) y doble clic.
2. En la fila de la instancia, segunda línea: la **entrada MIDI**, los **Canales** (botón con los 16
   canales) y el modo: *Canal original (keep)* o *Remapear → canal n*. Una instancia nueva ya viene con
   **Sin puerto (sólo API / carrilion)** en el primer canal libre (la primera en el canal 1, la
   segunda en el 2…): toca lo que manda carrilion por ese canal sin ningún dispositivo MIDI. Para tocar
   también desde loopMIDI o un teclado MIDI, elegir el puerto (`loopMIDI Port`); *(no recibe MIDI)*
   quita la ruta.
3. **Editor** abre la interfaz del plugin (elegir preset, patch, cargar un SFZ…). Cerrar la ventana no
   borra la instancia.
4. Hacer clic en una fila y tocar el **teclado virtual** (abajo) para probar sin MIDI externo.
5. **Guardar** (Ctrl+S), **Abrir…** (Ctrl+O), **Recientes ▾**, **Nuevo** (Ctrl+N). **PÁNICO** o Esc
   corta todas las notas.
6. También se abre un proyecto desde la consola: `VstPlayer.exe "C:\ruta\mi.vstproj.json"` (si la app
   ya está abierta, lo abre esa misma ventana).
7. **● Grabar** (Ctrl+R) graba lo que suena; ver "Grabar" abajo.

En `examples\` hay proyectos de ejemplo: `prueba-etapa5.vstproj.json` (Dexed en el canal 1, Surge XT en
los canales 2 y 3 remapeados al 1, sforzando en el canal 4, todo desde `loopMIDI Port`) y
`plugin-faltante.vstproj.json` (muestra cómo se ve un plugin no instalado). Un plugin que no esté
instalado o escaneado aparece en rojo como "FALTA" y el resto del proyecto funciona igual.

## Grabar

**● Grabar** (barra superior, Ctrl+R o Audio → Grabar) graba **lo que suena por la salida de audio**
(la mezcla de todas las instancias, después del volumen) hasta que se vuelve a apretar. El botón se
pone rojo con el tiempo transcurrido.

- **Archivo**: `<proyecto> AAAA-MM-DD HH-MM-SS.mp3`, p. ej. `Piano y pads 2026-11-02 21-35-07.mp3`
  (si ya existe, ` (2)`). Por defecto en la **carpeta del proyecto** (la del `.vstproj.json`); un
  proyecto sin guardar graba en `Documentos\VstPlayer\Grabaciones`.
- **Calidad**: el MP3 se codifica con **LAME** (incluido en el exe) en su modo de máxima calidad
  (`-q 0`, joint stereo), a la frecuencia del dispositivo (48 kHz normalmente; más de 48 kHz se
  convierte a 48 / 44,1 kHz, el máximo del MP3). Se codifica mientras se graba: al detener, el archivo
  ya está listo.
- **Audio → Opciones de grabación…**: formato (**MP3 256 kbps CBR** por defecto, 320, 192, VBR V0
  ~245 kbps, VBR V2 ~190 kbps, o *Sólo WAV*), **Guardar también el WAV** (32 bits float, sin pérdida,
  al lado del MP3) y la carpeta (la del proyecto o una fija). Se guarda en `settings.xml`
  (`recordingFormat`, `recordingKeepWav`, `recordingFolder`). **Abrir carpeta de grabaciones** en el
  mismo menú.
- Si la mezcla pasa de 0 dBFS el botón se pone naranja y el aviso final lo dice (en el MP3 eso queda
  saturado; el WAV float lo conserva). Bajar la ganancia de las instancias.
- Si cambia el dispositivo de audio, su sample rate o el buffer mientras se graba, la grabación se
  corta (los archivos quedan bien hasta ese momento) y se avisa. Salir mientras se graba pregunta
  primero.
- Desde la API: `startRecording` / `stopRecording` (ver [`docs/api.md`](docs/api.md)).

## carrilion → VstPlayer sin dispositivos MIDI (lo más simple)

No hace falta loopMIDI ni un teclado MIDI:

1. Abrir **VstPlayer** y agregar las instancias (**+ Agregar instancia…**). Cada una queda en "Sin puerto
   (sólo API / carrilion)" en su canal (1, 2, 3…); cambiar los **Canales** si carrilion usa otros.
   Guardar el proyecto (Ctrl+S) para la próxima vez.
2. En carrilion, en el selector **Midi Out**, elegir **VstPlayer**. La línea de estado muestra las
   rutas leídas (p. ej. `VstPlayer conectado: 1→Dexed, 2→Pads`).
3. Reproducir: cada canal suena en su instancia; el LED MIDI de la fila no se enciende (carrilion
   manda directo a la instancia), pero el vúmetro sí se mueve.

Una ruta "Sin puerto" no escucha ningún puerto MIDI: si después se conecta un teclado, no suena en esa
instancia hasta que se elija su puerto en la fila (la ruta con puerto sigue valiendo para carrilion).

## Flujo carrilion → loopMIDI → VstPlayer (opcional)

1. Abrir **loopMIDI** y verificar que existe el puerto (`loopMIDI Port`). loopMIDI tiene que estar
   corriendo (queda en la bandeja del sistema).
2. Abrir **VstPlayer** → **Abrir…** → `examples\prueba-etapa5.vstproj.json` (o un proyecto propio con
   rutas a `loopMIDI Port`). Esperar a que las filas dejen de decir "cargando…". En **Puertos MIDI…** el
   puerto debe figurar "abierto".
3. Abrir **carrilion** en Chrome (o Edge) y, en su selector de salida MIDI, elegir **loopMIDI Port**.
   Si el navegador pide permiso para usar dispositivos MIDI, aceptar.
4. Reproducir o tocar en carrilion: cada canal MIDI suena en la instancia ruteada (con el ejemplo:
   canal 1 → Dexed, 2 y 3 → Surge XT "Pads", 4 → sforzando "Sampler"). El LED MIDI de cada fila parpadea
   y **Puertos MIDI…** muestra el log de lo que llega.
5. Si queda una nota colgada: **PÁNICO** (o Esc).

Si se borra y se vuelve a crear el puerto en loopMIDI con la app abierta, la fila muestra
"(desconectado)" y se reconecta sola a los ~2 s (Chrome puede necesitar que se vuelva a elegir el
puerto).

**Sin loopMIDI:** en el selector Midi Out de carrilion, la opción **VstPlayer** toca por la API local
(WebSocket) con las mismas rutas del proyecto (las de cualquier puerto y las "Sin puerto"; ver la
sección anterior). Se reconecta sola si VstPlayer se abre después.

**Volumen y pan:** carrilion manda el volumen y el pan de cada canal como CC7 / CC10 por su canal MIDI
(por cualquiera de los dos caminos) y VstPlayer los aplica en la instancia que recibe ese canal, encima
de la ganancia de la fila: el control de la fila queda como ajuste fino. Detalle en
[`docs/api.md`](docs/api.md) ("Volumen y pan por MIDI").

## Formato de proyecto

JSON versionado (`"version": 1`) con el audio, las instancias (plugin, nombre, ganancia, mute, estado
del plugin en base64, overrides de parámetros por nombre, ventana del editor) y las rutas MIDI. Los
campos desconocidos se conservan al volver a guardar. Especificación completa:
[`docs/project-format.md`](docs/project-format.md).

## API local

Servidor en **`127.0.0.1:9471`** (sólo loopback): `POST /api` con un comando JSON, WebSocket en `/ws`
con respuestas y eventos (`paramChanged`, `instanceAdded`, `projectLoaded`, `midiActivity`…). Comandos:
`status`, `loadProject`, `listProjects`, `saveProject`, `listParams`, `getParam`, `setParam`, `setGain`,
`setMute`, `panic`, `listPrograms`, `setProgram`, `midi` (a una instancia, o sin `instanceId` por las
rutas del proyecto), `startRecording`, `stopRecording`. La barra de estado muestra
`API: 127.0.0.1:9471, N clientes`.

Desde la consola del navegador (F12) en cualquier página:

```js
const { connect } = await import('http://127.0.0.1:9471/client.js')
const vp = await connect()
const st = await vp.status()                      // instancias, rutas, audio
await vp.setParam(st.instances[0].id, 0, 0.5)     // parámetro 0 al 50 %
await vp.panic()
```

Con curl: `curl -d "{\"cmd\":\"status\"}" http://127.0.0.1:9471/api`.

Documentación completa (comandos, eventos, errores, token opcional, CORS): [`docs/api.md`](docs/api.md).
Cliente de ejemplo sin dependencias: [`examples/carrilion-client.js`](examples/carrilion-client.js).

## Latencia

La latencia de salida la pone el dispositivo: con WASAPI compartido suele ser 10 ms y no se puede
bajar desde la app. Audio → **Configuración de audio…** (o el botón del dispositivo en la barra
superior) permite elegir *Windows Audio (Exclusive Mode)* o *DirectSound* con buffers más chicos; el
modo exclusivo puede no rendir con algunos drivers. Para menos de ~5 ms hace falta ASIO, que requiere
compilar con el SDK de Steinberg (ver "Cómo compilar").

## Línea de comandos

| | |
|---|---|
| `VstPlayer.exe [proyecto.vstproj.json]` | abre la app (y el proyecto) |
| `--scan [--force]` | escanea los plugins, escribe `plugins.xml` y sale |
| `--plugin-folders` | lista las carpetas de búsqueda de VST3 (estándar y agregadas) |
| `--add-plugin-folder <carpeta>` | agrega una carpeta de búsqueda y escanea lo nuevo |
| `--remove-plugin-folder <carpeta>` | quita una carpeta agregada y sus plugins de la lista |
| `--data-dir <carpeta>` | usa esa carpeta en lugar de `%APPDATA%\VstPlayer` (settings, plugins, recientes, autoguardado); corre como una instancia independiente |
| `--api-port <n>`, `--api-token <t>`, `--no-api` | configuración de la API para esta ejecución |
| `--midi-ports`, `--midi-monitor [s]`, `--midi-send "<puerto>" 90:3C:64 +100 80:3C:00` | diagnóstico MIDI (listar, escuchar, enviar) |
| `--audio-devices` | lista los dispositivos de audio y sus buffers |
| `--render <proyecto> --midi <archivo.mid> --out <archivo.wav> [opciones]` | render offline a WAV, sin dispositivo de audio (ver abajo) |

La salida de texto se ve al redirigirla (`VstPlayer.exe --midi-ports > puertos.txt`) o desde una
consola.

## Render offline (`--render`)

Toca un proyecto con un archivo MIDI estándar y escribe el resultado en un WAV, sin abrir el
dispositivo de audio ni los puertos MIDI, más rápido que en tiempo real y con cada mensaje en su
muestra exacta. Lo usa carrilion para que los canales que mandan MIDI a VstPlayer suenen en sus
renders.

```
VstPlayer.exe --render proyecto.vstproj.json --midi eventos.mid --out salida.wav
              [--sample-rate 44100] [--block 512] [--warmup 2] [--tail-max 60] [--data-dir <carpeta>]
```

- El proyecto se carga como en la app (plugins de `plugins.xml`, estado, parámetros, ganancia, mute y
  rutas MIDI); el bloque `audio` del proyecto se ignora. Si una instancia no carga (plugin faltante o
  que falla) el render **falla** (exit 1). No escribe nada en la carpeta de datos.
- MIDI de tipo 0 o 1, con su mapa de tempo; el tiempo 0 del archivo es la primera muestra del WAV. Cada
  mensaje pasa por las rutas MIDI del proyecto **de todos los puertos y las "Sin puerto"** (canales de
  origen, `keep` / `remapTo`), como si llegara por el puerto; CC7 / CC10 se aplican como volumen / pan de la instancia.
  Mensajes de sistema, sysex y meta se ignoran.
- `--warmup`: segundos de silencio procesados a ritmo real y descartados antes de empezar (plugins que
  cargan samples en segundo plano). `--block`: tamaño de bloque del render.
- Cola: después del último evento sigue hasta que la mezcla queda por debajo de −70 dBFS (2 s
  seguidos), y el archivo termina 0,5 s después de la última muestra audible (nunca antes del último
  evento, como mucho `--tail-max` segundos después).
- Salida: WAV estéreo float de 32 bits de la mezcla, escrito en un temporal y renombrado al final.
- stdout: **una línea JSON** al terminar, `{"ok":true,"frames":…,"sampleRate":…,"seconds":…,
  "lastEventSeconds":…,"tailSeconds":…,"peakDb":…,"instances":[…],"warnings":[…],…}` o
  `{"ok":false,"error":"…"}`; el progreso va a stderr. Exit 0 = bien, 1 = error del render (proyecto,
  plugins, escritura), 2 = argumentos o archivos de entrada inválidos.

## Cómo compilar

Requisitos: Visual Studio 2022 Build Tools con el workload de C++ (MSVC 19.3x o superior), CMake
3.22+, Ninja y git. JUCE 8.0.15 y cpp-httplib se descargan solos (CMake `FetchContent`).

```powershell
pwsh -NoProfile -File .\tools\build.ps1                 # Release -> build\VstPlayer_artefacts\Release\VstPlayer.exe
pwsh -NoProfile -File .\tools\build.ps1 -Config Debug   # Debug   -> build-debug\
pwsh -NoProfile -File .\tools\package.ps1               # Release + dist\VstPlayer-<versión>-win64.zip
pwsh -NoProfile -File .\tools\publish-carrilion.ps1     # Release + copia el exe a ..\carrilion\bin\win64
```

La primera compilación tarda varios minutos (JUCE). Pruebas: `ctest --test-dir build` y los scripts
`tools\run-*-test.ps1` (ver `CLAUDE.md`). ASIO: copiar el SDK de Steinberg en `third_party\asiosdk`
(con `common\iasiodrv.h`) y recompilar; CMake lo detecta solo. Próximos pasos: `docs/roadmap.md`.

**VstPlayer dentro de carrilion:** el repositorio de carrilion trae una copia del exe en
`bin\win64\VstPlayer.exe` (con `VSTPLAYER.md`: versión, commit, fecha, licencia), así quien usa
carrilion en Windows ya lo tiene. Para actualizarla: `tools\publish-carrilion.ps1` (compila Release,
copia el exe y escribe `VSTPLAYER.md`; `-Target <carpeta>` si carrilion está en otro lado) y después
commitear `bin/win64` en carrilion.

**Ícono:** las fuentes son `resources\icon.svg` (48 px y más) y `resources\icon-small.svg` (16 y
32 px); `node tools\render-icon.mjs` regenera los PNG que usa CMake (con el Playwright de carrilion).

## Licencia

VstPlayer es software libre bajo la **GNU Affero General Public License v3.0** (ver `LICENSE`).
Código fuente: <https://github.com/japentaca/vst-player>.

Usa [JUCE](https://juce.com) bajo AGPLv3, [cpp-httplib](https://github.com/yhirose/cpp-httplib)
(MIT) y [LAME](https://lame.sourceforge.io) 3.100 (encoder MP3, LGPL 2.0 o posterior, compilado sin
cambios dentro del exe; su código se baja al compilar, ver `cmake/lame.cmake`). VST es una marca de Steinberg Media Technologies GmbH.
