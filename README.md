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
- [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html) (gratis) para recibir MIDI desde
  el navegador u otros programas, con un puerto creado (por ejemplo el que propone, `loopMIDI Port`).
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
   **Plugins…** (arriba a la derecha) → **Escanear** → confirmar las carpetas → **Escanear**. La barra de
   estado dice "Escaneando plugins…"; al terminar, la lista muestra nombre, fabricante y versión.
   Mientras no haya plugins escaneados, la barra de estado lo recuerda.
   Alternativa desde una consola: `VstPlayer.exe --scan`.
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
2. En la fila de la instancia, segunda línea: elegir el **puerto MIDI** (`loopMIDI Port`), los
   **Canales** (botón con los 16 canales) y el modo: *Canal original (keep)* o *Remapear → canal n*.
3. **Editor** abre la interfaz del plugin (elegir preset, patch, cargar un SFZ…). Cerrar la ventana no
   borra la instancia.
4. Hacer clic en una fila y tocar el **teclado virtual** (abajo) para probar sin MIDI externo.
5. **Guardar** (Ctrl+S), **Abrir…** (Ctrl+O), **Recientes ▾**, **Nuevo** (Ctrl+N). **PÁNICO** o Esc
   corta todas las notas.
6. También se abre un proyecto desde la consola: `VstPlayer.exe "C:\ruta\mi.vstproj.json"` (si la app
   ya está abierta, lo abre esa misma ventana).

En `examples\` hay proyectos de ejemplo: `prueba-etapa5.vstproj.json` (Dexed en el canal 1, Surge XT en
los canales 2 y 3 remapeados al 1, sforzando en el canal 4, todo desde `loopMIDI Port`) y
`plugin-faltante.vstproj.json` (muestra cómo se ve un plugin no instalado). Un plugin que no esté
instalado o escaneado aparece en rojo como "FALTA" y el resto del proyecto funciona igual.

## Flujo carrilion → loopMIDI → VstPlayer

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

## Formato de proyecto

JSON versionado (`"version": 1`) con el audio, las instancias (plugin, nombre, ganancia, mute, estado
del plugin en base64, overrides de parámetros por nombre, ventana del editor) y las rutas MIDI. Los
campos desconocidos se conservan al volver a guardar. Especificación completa:
[`docs/project-format.md`](docs/project-format.md).

## API local

Servidor en **`127.0.0.1:9471`** (sólo loopback): `POST /api` con un comando JSON, WebSocket en `/ws`
con respuestas y eventos (`paramChanged`, `instanceAdded`, `projectLoaded`, `midiActivity`…). Comandos:
`status`, `loadProject`, `listProjects`, `saveProject`, `listParams`, `getParam`, `setParam`, `setGain`,
`setMute`, `panic`, `listPrograms`, `setProgram`, `midi`. La barra de estado muestra
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
| `--data-dir <carpeta>` | usa esa carpeta en lugar de `%APPDATA%\VstPlayer` (settings, plugins, recientes, autoguardado); corre como una instancia independiente |
| `--api-port <n>`, `--api-token <t>`, `--no-api` | configuración de la API para esta ejecución |
| `--midi-ports`, `--midi-monitor [s]`, `--midi-send "<puerto>" 90:3C:64 +100 80:3C:00` | diagnóstico MIDI (listar, escuchar, enviar) |
| `--audio-devices` | lista los dispositivos de audio y sus buffers |

La salida de texto se ve al redirigirla (`VstPlayer.exe --midi-ports > puertos.txt`) o desde una
consola.

## Cómo compilar

Requisitos: Visual Studio 2022 Build Tools con el workload de C++ (MSVC 19.3x o superior), CMake
3.22+, Ninja y git. JUCE 8.0.15 y cpp-httplib se descargan solos (CMake `FetchContent`).

```powershell
pwsh -NoProfile -File .\tools\build.ps1                 # Release -> build\VstPlayer_artefacts\Release\VstPlayer.exe
pwsh -NoProfile -File .\tools\build.ps1 -Config Debug   # Debug   -> build-debug\
pwsh -NoProfile -File .\tools\package.ps1               # Release + dist\VstPlayer-<versión>-win64.zip
```

La primera compilación tarda varios minutos (JUCE). Pruebas: `ctest --test-dir build` y los scripts
`tools\run-*-test.ps1` (ver `CLAUDE.md`). ASIO: copiar el SDK de Steinberg en `third_party\asiosdk`
(con `common\iasiodrv.h`) y recompilar; CMake lo detecta solo. Próximos pasos: `docs/roadmap.md`.

## Licencia

VstPlayer usa [JUCE](https://juce.com) bajo **AGPLv3** (uso personal / open source) y
[cpp-httplib](https://github.com/yhirose/cpp-httplib) (MIT). VST es una marca de Steinberg Media
Technologies GmbH.
