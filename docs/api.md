# API local de VstPlayer

VstPlayer expone una API JSON para que páginas web (carrilion u otras) o scripts controlen el host:
cargar proyectos, listar instancias y parámetros, mover parámetros, cambiar programas, mandar MIDI,
pánico. Implementación: `src/api/ApiServer.{h,cpp}` (red, cpp-httplib) y
`src/api/ApiController.{h,cpp}` (comandos). Cliente de ejemplo: `examples/carrilion-client.js`.

## Conexión

| | |
|---|---|
| Dirección | **sólo `127.0.0.1`** (loopback; no se abre a la red y Windows no pide permiso de firewall) |
| Puerto | **9471** por defecto (un solo puerto para HTTP y WebSocket) |
| HTTP | `POST http://127.0.0.1:9471/api` con el comando como cuerpo JSON; `GET /api/status` = `status`; `GET /` = info |
| WebSocket | `ws://127.0.0.1:9471/ws` (también `/`): un comando JSON por mensaje de texto; respuestas y eventos |
| Cliente JS | `GET /client.js`: `examples/carrilion-client.js` (embebido al compilar; sin token), para `await import('http://127.0.0.1:9471/client.js')` desde cualquier página |
| Token | opcional; si está configurado, `?token=<token>` en la URL (o cabecera `X-Api-Token`); `/client.js` no lo pide |

La barra de estado de la ventana muestra `API: 127.0.0.1:9471, N clientes` (N = WebSockets abiertos),
`API: desactivada` o el error en rojo (p. ej. puerto ocupado por otro programa u otra instancia).

### Configuración (`%APPDATA%\VstPlayer\settings.xml`)

Nada que configurar para usarla: sin estas claves vale el default.

| Clave | Default | |
|---|---|---|
| `apiEnabled` | `true` | `false` apaga el servidor |
| `apiPort` | `9471` | |
| `apiToken` | vacío | vacío = sin token |

Para editarlas, con la app cerrada, agregar dentro de `<PROPERTIES>`:
`<VALUE name="apiToken" val="mi-secreto"/>`. Línea de comandos (pisa a settings):
`--api-port <n>`, `--api-token <t>`, `--no-api`. Los self-tests (`--*-selftest`) corren sin API salvo
que se pase `--api-port`; `--api-test` es el modo que usa `tools/run-api-test.ps1`.

### CORS y seguridad (riesgo aceptado)

Todas las respuestas HTTP llevan `Access-Control-Allow-Origin: *` (y la respuesta al preflight
`OPTIONS` agrega `Access-Control-Allow-Private-Network: true` para Chrome). Eso significa que
**cualquier página abierta en el navegador de esta máquina puede controlar VstPlayer** (cargar o
guardar proyectos en cualquier ruta, tocar notas). Se acepta porque el servidor sólo escucha en
loopback (nada de afuera de la máquina llega) y es una herramienta personal. Para cerrarlo, poner un
`apiToken`: sin el token, HTTP responde 401 y el WebSocket recibe un error y se cierra con código 1008.
El tráfico no está cifrado (no hay TLS; en loopback no sale de la máquina).

## Formato

Pedido: objeto JSON con `cmd` y sus campos. `id` (cualquier valor JSON) es opcional y se devuelve
igual en la respuesta (útil en el WebSocket para emparejar respuestas).

Respuesta: `{"ok": true, ..., "cmd": "...", "id": ...}` o
`{"ok": false, "code": "...", "error": "mensaje en español", "cmd": "...", "id": ...}`.

Por HTTP el código de estado acompaña: 200 ok; 400 `badJson`, `unknownCommand`, `badRequest`,
`badMidi`, `failed` (archivo ilegible); 401 `unauthorized`; 404 `notFound`, `paramNotFound`,
`programNotFound`; 409 `notReady`, `unsavedChanges`, `noFile`; 500 `failed` (no se pudo escribir);
503 `stopping`; 504 `timeout`. Por WebSocket llega el mismo JSON (sin código HTTP).
JSON inválido, un mensaje que no es objeto o un comando desconocido devuelven error y la conexión
sigue abierta.

`instanceId` acepta el id (UUID) de la instancia **o su nombre** visible (el primero que coincida).

## Comandos

### `status`
Proyecto, audio, instancias y rutas.
```json
{"cmd":"status"}
→ {"ok":true,
   "project":{"name":"Prueba etapa 5","path":"C:\\...\\x.vstproj.json","dirty":false,"loading":false,"pendingLoads":0},
   "audio":{"deviceType":"Windows Audio","device":"Altavoces (...)","sampleRate":48000.0,"bufferSize":480,"outputLatencyMs":10.0,"cpu":2.1},
   "instances":[{"id":"5a1e...","name":"Pads","state":"ready",
                 "plugin":{"name":"Surge XT","manufacturer":"Surge Synth Team","version":"1.3.4","format":"VST3","fileOrIdentifier":"C:\\...","uniqueId":"190e4fbd","isInstrument":true},
                 "gainDb":-12.0,"mute":false,"editorOpen":false,"numParams":2855,"numPrograms":0,
                 "midiRoutes":[{"inputDevice":"loopMIDI Port","channels":[2,3],"instanceId":"5a1e...","channelMode":{"remapTo":1}}]}],
   "midiRoutes":[...],
   "api":{"host":"127.0.0.1","port":9471,"clients":1,"version":1,"app":"0.1.0"}}
```
`state`: `loading`, `ready`, `failed` (con `error`), `missing` (plugin no instalado). `currentProgram`
y `programName` aparecen si el plugin tiene programas.

### `loadProject`
```json
{"cmd":"loadProject","path":"C:\\Users\\yo\\Documents\\pads.vstproj.json","discardChanges":false}
```
Abre el proyecto (mismo camino que Archivo → Abrir) y **responde cuando todas las instancias
terminaron de cargar** (o fallaron), con `project`, `warnings` (plugins faltantes, puertos MIDI
inexistentes, …) e `instances`. Rutas relativas: respecto de la última carpeta de proyectos (o
Documentos). Si el proyecto actual tiene cambios sin guardar responde 409 `unsavedChanges`, salvo que
se mande `"discardChanges": true` (no hay diálogos: la API nunca pregunta). Si hay advertencias, la
ventana además las muestra como al abrir desde el menú. Timeout del pedido: 120 s.

### `listProjects`
```json
{"cmd":"listProjects","dir":"C:\\Users\\yo\\Documents","recursive":false}
→ {"ok":true,"dir":"...","projects":[{"name":"pads","path":"...","size":105123,"modified":"2026-11-01T18:00:00.000-03:00"}],
   "truncated":false,"recent":["..."],"current":"..."}
```
Archivos `*.vstproj.json` (máx. 500). Sin `dir`: la última carpeta de proyectos o Documentos.
`recent` = lista de Recientes.

### `saveProject`
```json
{"cmd":"saveProject"}                                  // al archivo actual (409 noFile si no tiene)
{"cmd":"saveProject","path":"C:\\...\\otro.vstproj.json"} // "Guardar como"
→ {"ok":true,"path":"...","name":"otro"}
```
Escritura atómica como desde el menú; el archivo pasa a ser el proyecto actual y entra en Recientes.

### `listParams`
```json
{"cmd":"listParams","instanceId":"Pads","filter":"cutoff","offset":0,"limit":50,"refresh":false}
→ {"ok":true,"instanceId":"...","total":2855,"matched":3,
   "params":[{"index":12,"name":"Filter 1 Cutoff","id":"1234","value":0.5,"text":"440.00 Hz","label":"",
              "default":0.5,"steps":12,"automatable":true}]}
```
`value` siempre normalizado 0..1; `text` es el texto que da el plugin para ese valor; `steps` sólo en
parámetros discretos; `id` es el id VST3 del parámetro. `filter` (subcadena del nombre, sin
mayúsculas), `offset` y `limit` son opcionales. La instancia tiene que estar `ready` (si no, 409).

**Valores viejos (importante).** JUCE guarda del lado del host una copia de cada valor. Cuando el
plugin cambia sus propios parámetros **sin avisar al host**, esa copia queda vieja: pasa con Dexed al
cambiar de programa (desde su editor o con `setProgram`: los 2238 valores siguen siendo los del
programa anterior; medido: 0 cambian sin refresh, 86 con refresh) y con sforzando al cargar un
instrumento. `listParams` devuelve esa copia y en esos casos **no hay eventos `paramChanged`**.
`"refresh": true` la pone al día releyendo el estado del propio plugin (`getStateInformation` +
`setStateInformation`, como el self-test de la etapa 5) unos 100 ms después del pedido (para que los
cambios pendientes ya hayan llegado al plugin) y recién ahí responde. Costo y riesgos: es una
restauración completa de estado (sforzando recarga su instrumento; puede oírse un corte), los plugins
pueden emitir notificaciones que marquen el proyecto como modificado, y con Dexed se observó que un
`setProgram` mandado inmediatamente después de un refresh a veces no se aplica (el siguiente sí). Usarlo
sólo para leer, no en cada pedido. `setParam` y `getParam` usan la misma copia (lo que la API escribió
se lee bien enseguida).

### `getParam`
```json
{"cmd":"getParam","instanceId":"Pads","index":12}       // o "name" / "paramId"
→ {"ok":true,"instanceId":"...","param":{"index":12,"name":"...","value":0.4,"text":"...",...}}
```

### `setParam`
```json
{"cmd":"setParam","instanceId":"Pads","index":12,"value":0.4}
{"cmd":"setParam","instanceId":"Pads","name":"Filter 1 Cutoff","value":0.4}
{"cmd":"setParam","instanceId":"Pads","paramId":"1234","value":0.4}
→ {"ok":true,"instanceId":"...","index":12,"name":"Filter 1 Cutoff","value":0.4}
```
`value` normalizado (fuera de 0..1 se limita). Por nombre, si hay nombres repetidos gana el primero
(usar `index`). Se ejecuta directo en el hilo de red con `setValueNotifyingHost` (no pasa por el hilo
de la UI): el plugin lo recibe en el bloque de audio siguiente y su editor se actualiza (JUCE lo
manda al controlador a 60 Hz). La respuesta no trae `text` (pedirlo al hilo de mensajes desde el de
red podría bloquear); llega en el evento `paramChanged` o con `getParam`. Mueve el `*` de "cambios sin
guardar" como si se hubiera tocado en el editor.

### `setGain` / `setMute`
```json
{"cmd":"setGain","instanceId":"Pads","gainDb":-6}     → {"ok":true,"instanceId":"...","gainDb":-6.0}
{"cmd":"setMute","instanceId":"Pads","mute":true}     → {"ok":true,"instanceId":"...","mute":true}
```
Ganancia en dB, limitada a −60…+12 (≤ −60 = silencio). Con rampa de 20 ms (sin clicks). La fila de la
UI se actualiza.

### `panic`
```json
{"cmd":"panic"}  → {"ok":true}
```
Igual que el botón PÁNICO / Esc: suelta las teclas del teclado virtual y las notas sostenidas por los
puertos, y manda sustain off + all notes off + all sound off en los 16 canales de cada instancia.

### `listPrograms` / `setProgram`
```json
{"cmd":"listPrograms","instanceId":"Dexed"}
→ {"ok":true,"instanceId":"...","current":0,"programs":[{"index":0,"name":"Say Again."}, ...]}
{"cmd":"setProgram","instanceId":"Dexed","index":5}
→ {"ok":true,"instanceId":"...","current":5,"name":"Lavitar  Y"}
```
Los programas VST3 del plugin (Dexed: 32 del cartucho actual; Surge XT y sforzando no exponen
programas: lista vacía). Ver "Valores viejos" en `listParams`.

### `midi`
```json
{"cmd":"midi","instanceId":"Dexed","bytes":[144,60,100]}
{"cmd":"midi","instanceId":"Dexed","bytes":[128,60,0, 144,64,90]}   // varios mensajes seguidos
→ {"ok":true,"instanceId":"...","messages":2}
```
MIDI directo a una instancia, alternativa a loopMIDI para pruebas y control. Sólo mensajes de canal
(status 0x80–0xEF, sin running status); sysex y mensajes de sistema → 400 `badMidi`. Sin timestamps:
entra en el bloque de audio siguiente. No pasa por las rutas MIDI (el canal va tal cual) ni por el
seguimiento de notas sostenidas del router; `panic` igual lo corta. Cuenta para `midiActivity` pero no
enciende el LED de la fila (que muestra lo que llega por los puertos).

### `levels` / `audioStats` (diagnóstico)
```json
{"cmd":"levels"}
→ {"ok":true,"instances":[{"instanceId":"...","name":"Dexed","peakDb":-40.9,"blocks":12345}],"masterPeakDb":-40.9}
{"cmd":"audioStats"}
→ {"ok":true,"sampleRate":48000.0,"bufferSize":480,"cpu":2.1,"xruns":0,"callbacks":1002,"totalCallbacks":53000,
   "callbackAvgMs":0.21,"callbackMaxMs":1.1,"blockMs":10.0,"overBudget":0,"mixerBlocks":53000}
```
`levels`: pico (dBFS, −100 = silencio) en el nodo de ganancia de cada instancia y en la mezcla **desde
la consulta anterior**. `audioStats`: callbacks de audio desde la consulta anterior, tiempo medio y
máximo del callback completo (todo el grafo), cuántos superaron la duración del bloque, xruns que
reporta el driver (`AudioDeviceManager::getXRunCount`, −1 si no los cuenta) y CPU.

## Eventos (sólo WebSocket)

Llegan a todos los WebSockets abiertos como `{"event": "<nombre>", ...}` (sin `ok`):

| Evento | Campos | Cuándo |
|---|---|---|
| `instanceAdded` | `instance` (como en `status`) | se agregó una instancia (UI, proyecto, duplicar) |
| `instanceLoaded` | `instance` | terminó de cargar (`state` ready o failed) |
| `instanceRemoved` | `instanceId` | se quitó |
| `paramChanged` | `instanceId`, `params: [{index, name, value, text}]`, `truncated?` | cambió algún parámetro (API, editor del plugin, automatización); **agrupado a 20 Hz por instancia**: como mucho 20 eventos por segundo y por instancia, con todos los parámetros que cambiaron en ese intervalo (máx. 256; `truncated: true` si hubo más: usar `listParams`) |
| `projectLoaded` | `project`, `warnings`, `numInstances` | terminó de abrirse un proyecto (o "Nuevo") |
| `midiActivity` | `instanceId`, `count` | llegó MIDI a la instancia (puertos o API); como mucho 20 por segundo por instancia; `count` = total acumulado |

Ver "Valores viejos": los cambios internos de un plugin que no avisa al host no generan `paramChanged`.

## Hilos (para quien toque el código)

- `ApiServer` escucha en su hilo; cada pedido HTTP y cada WebSocket corre en un hilo del pool de
  cpp-httplib. Los eventos salen por una cola y un hilo emisor (un cliente lento no frena la UI).
- Todo comando que toca el grafo, el router o el proyecto se manda al hilo de mensajes con
  `MessageManager::callAsync` y el hilo de red espera la respuesta (10 s; 30 s `listParams`, 60 s
  `saveProject`, 120 s `loadProject`).
- `setParam` y `midi` van directo desde el hilo de red: `setValueNotifyingHost` y el
  `MidiMessageCollector` de la instancia son seguros desde cualquier hilo. Los punteros salen de una
  tabla que el hilo de mensajes actualiza bajo un lock **antes** de quitar los nodos de una instancia.
- `paramChanged`: el listener marca banderas atómicas por parámetro (también desde el hilo de audio,
  bajo un `SpinLock` breve, y sólo si hay clientes) y un timer de 20 Hz en el hilo de mensajes arma los
  eventos.
- Al cerrar la app, la API se detiene primero: los pedidos pendientes responden 503 y se esperan los
  hilos de red antes de destruir el motor.

## Pruebas

`tools/run-api-test.ps1 [-Config Debug] [-Token x] [-Port 9479]` arranca la app con `--api-test` y una
carpeta de datos temporal, corre `tools/api-test.mjs` (Node 22+) contra HTTP y WebSocket, verifica con
netstat que sólo escucha en 127.0.0.1 y que la app cierra con exit 0, sin aserciones ni fugas.
