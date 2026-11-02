# Formato de proyecto `*.vstproj.json` (versión 1)

Un proyecto de VstPlayer es un archivo JSON en UTF-8 (fin de línea LF) con extensión
`.vstproj.json`. Guarda todo lo necesario para volver a armar el mismo setup: dispositivo de
audio, instancias de plugins con su estado, ganancias, mute, ventanas de los editores y rutas MIDI.

Código: modelo en `src/project/Project.h`, lectura/escritura en `src/project/ProjectSerializer.cpp`,
aplicación al motor en `src/project/ProjectController.cpp`.

## Ejemplo

```json
{
  "version": 1,
  "name": "Piano y pads",
  "audio": { "deviceType": "Windows Audio", "outputDevice": "Altavoces (Focusrite USB Audio)", "sampleRate": 48000.0, "bufferSize": 480 },
  "instances": [
    {
      "id": "1b4e28ba-2fa1-11d2-883f-0016d3cca427",
      "name": "Piano",
      "plugin": {
        "name": "sforzando",
        "manufacturer": "Plogue Art et Technologie, Inc",
        "format": "VST3",
        "fileOrIdentifier": "C:\\Program Files\\Common Files\\VST3\\sforzando.vst3",
        "uniqueId": "1026018b",
        "version": "2.1.2.3"
      },
      "gainDb": -6.0,
      "mute": false,
      "state": "VkMyIQAAAAAAAAAA...",
      "params": { "Cutoff": 0.5 },
      "window": { "x": 140, "y": 160, "open": true }
    }
  ],
  "midiRoutes": [
    { "inputDevice": "loopMIDI Port", "channels": [1, 2], "instanceId": "1b4e28ba-2fa1-11d2-883f-0016d3cca427", "channelMode": "keep" },
    { "inputDevice": "loopMIDI Port", "channels": [3], "instanceId": "1b4e28ba-2fa1-11d2-883f-0016d3cca427", "channelMode": { "remapTo": 1 } }
  ]
}
```

Ejemplos completos en `examples/prueba-etapa5.vstproj.json` y `examples/plugin-faltante.vstproj.json`.

## Campos

### Raíz

| Campo | Tipo | Obligatorio | Descripción |
|---|---|---|---|
| `version` | entero | no (1) | Versión del esquema. Esta versión escribe `1`. Un archivo con versión mayor se abre igual (con aviso) y se vuelve a guardar con la misma versión. |
| `name` | texto | no | Nombre del proyecto (título de la ventana). Si falta, el nombre del archivo sin `.vstproj.json`. "Guardar como" lo reemplaza por el nombre del archivo nuevo. |
| `audio` | objeto | no | Dispositivo de audio. Si falta, abrir el proyecto no cambia el audio. |
| `instances` | lista | no | Instancias de plugins, en el orden en que se muestran. |
| `midiRoutes` | lista | no | Rutas MIDI. |

### `audio`

| Campo | Tipo | Descripción |
|---|---|---|
| `deviceType` | texto | Tipo de dispositivo de JUCE: `"Windows Audio"` (WASAPI), `"DirectSound"`, `"ASIO"`… |
| `outputDevice` | texto | Nombre del dispositivo de salida tal como lo lista ese tipo. |
| `sampleRate` | número | Hz. |
| `bufferSize` | entero | Muestras. |

Al abrir: si el dispositivo actual ya coincide no se reabre nada. Si el tipo o el dispositivo no
existen en esta máquina se queda el actual y se avisa; si el dispositivo redondea el pedido (WASAPI
compartido sólo ofrece su período, p. ej. 480) también se avisa. Al guardar se escribe lo que está
abierto en ese momento. Valores vacíos o 0 = no cambiar ese dato.

### `instances[]`

| Campo | Tipo | Descripción |
|---|---|---|
| `id` | texto | Identificador único (UUID). Lo usan las rutas. Si falta o está repetido se genera otro. |
| `name` | texto | Nombre visible (editable). |
| `plugin` | objeto | Qué plugin cargar (ver abajo). |
| `gainDb` | número | Ganancia de la instancia en dB (−60 … +12; −60 = silencio). |
| `mute` | bool | Mute. |
| `state` | texto | `getStateInformation` del plugin en **base64 estándar** (RFC 4648). Vacío = estado inicial del plugin. Para VST3 es el bloque de JUCE (`VC2!` + XML con los estados `IComponent` / `IEditController`). Puede ser de varios MB (samplers). |
| `params` | objeto | Opcional. `{ "nombre del parámetro": valor normalizado 0..1 }`. Se aplica **después** de `state`. Si hay nombres repetidos en el plugin gana el primero. Un nombre que el plugin no tiene genera un aviso y se conserva. Al guardar, cada valor se actualiza con el valor actual del parámetro (así un cambio hecho en el editor no se deshace al volver a abrir). Si no hay `params`, no se escribe. |
| `window` | objeto | Ventana del editor: `x`, `y` (posición en píxeles lógicos de pantalla; sólo si alguna vez se abrió) y `open` (si estaba abierta al guardar; al abrir el proyecto se vuelve a abrir en esa posición, si la posición sigue estando en alguna pantalla). |

### `instances[].plugin`

| Campo | Tipo | Descripción |
|---|---|---|
| `name` | texto | Nombre del plugin. |
| `manufacturer` | texto | Fabricante. |
| `format` | texto | `"VST3"` (único formato soportado; otro valor = plugin faltante). |
| `fileOrIdentifier` | texto | Ruta del `.vst3`. |
| `uniqueId` | texto | `PluginDescription::uniqueId` de JUCE, 8 dígitos hexadecimales. |
| `version` | texto | Versión del plugin al guardar (informativo). |

**Resolución** contra la lista de plugins escaneados (`plugins.xml`), en este orden: por `uniqueId`,
por ruta (`fileOrIdentifier`, sin distinguir mayúsculas), por nombre (prefiriendo el mismo fabricante).
Si hay varias coincidencias se prefiere la de igual nombre.

**Plugin faltante**: si no se resuelve, la instancia aparece igual en la lista, en rojo
("FALTA: …"), sin cargar; se avisa al terminar de abrir. Al volver a guardar se escribe tal cual
estaba (plugin, `state`, `params`, ventana y campos desconocidos); sólo el nombre y la ganancia
siguen lo que se edite. Lo mismo vale para una instancia cuyo plugin existe pero falló al cargar.

### `midiRoutes[]`

| Campo | Tipo | Descripción |
|---|---|---|
| `inputDevice` | texto | Nombre del puerto MIDI de entrada (p. ej. `"loopMIDI Port"`). Si no existe al abrir, la ruta queda inactiva para ese puerto (se avisa) y se activa sola cuando aparece. **`""` (o ausente, o `null`) = ruta sin puerto** (ver abajo); se escribe siempre como `""`. |
| `channels` | lista de enteros | Canales de origen 1..16. Si falta: todos. Valores fuera de rango se ignoran (aviso). |
| `instanceId` | texto | `id` de la instancia destino. |
| `channelMode` | `"keep"` u objeto | `"keep"`: pasa el canal original. `{ "remapTo": n }`: todo sale por el canal `n` (1..16). También se acepta el texto `"remapTo:n"`. |

Una instancia puede tener varias rutas (de distintos puertos o canales).

**Rutas sin puerto** (`"inputDevice": ""`): la instancia toma esos canales, pero **ningún puerto MIDI**
le llega por esa ruta. La usan quienes aplican las rutas sin pasar por un puerto: la salida "VstPlayer"
de carrilion (lee `status.midiRoutes`), el comando `midi` sin `instanceId` de la API y `--render` (que
cuentan todas las rutas, con o sin puerto). Es la forma de usar VstPlayer sin loopMIDI ni ningún
dispositivo MIDI. En la UI: "Sin puerto (sólo API / carrilion)". Al abrir no generan aviso.

```json
{ "inputDevice": "", "channels": [1], "instanceId": "1b4e28ba-2fa1-11d2-883f-0016d3cca427", "channelMode": "keep" }
```

Compatibilidad: no cambia la versión del esquema. Un archivo viejo se abre igual (sus rutas tienen
puerto). Un archivo nuevo abierto con una versión anterior de VstPlayer: la ruta se lee sin avisos (el
texto vacío ya era válido) y queda igual de inerte para los puertos; esa versión la publica igual en
`status.midiRoutes` (carrilion la usa) y `--render` la respeta; en su UI la fila muestra "(sin entrada
MIDI)" con los canales y el modo editables, y elegir "(sin entrada MIDI)" ahí borra la ruta. Al volver a
guardar se conserva.

## Compatibilidad hacia adelante

Toda propiedad que esta versión no conoce, en cualquier nivel (raíz, `audio`, instancia, `plugin`,
`window`, ruta), se conserva y se vuelve a escribir igual (después de las conocidas, en su orden
original). Una versión futura puede agregar campos sin que una versión vieja los pierda al guardar.

Reglas de lectura: un archivo que no es JSON, o cuya raíz no es un objeto, es un error y no se abre
(el proyecto abierto no cambia). Campos con tipo equivocado toman su valor por defecto y generan un
aviso.

## Escritura

- Atómica: se escribe un temporal en la misma carpeta y se renombra sobre el destino.
- Formato: JSON con sangría, propiedades conocidas en orden fijo.
- "Guardar como" agrega `.vstproj.json` si falta.

## Archivos relacionados (en `%APPDATA%\VstPlayer\`)

(o en la carpeta indicada con `VstPlayer.exe --data-dir <carpeta>`)

- `recent.json`: `{ "version": 1, "recent": [ "C:\\...\\a.vstproj.json", ... ] }`, máximo 10, el más reciente primero.
- `autosave.vstproj.json`: respaldo automático cada 2 minutos mientras hay cambios sin guardar (mismo
  formato; no cambia el archivo del proyecto ni el `*`). Se borra al guardar. No se abre solo: para
  recuperar, Archivo → Abrir… y elegirlo.

## Historial

- v1 (etapa 5): primera versión.
- v1, después de v0.1.0: `"inputDevice": ""` pasa a significar "ruta sin puerto" (sin cambio de
  versión: ya se leía y escribía igual, sólo cambia cómo la muestra la UI y que la API la aplica con
  `midi` sin `instanceId`).
