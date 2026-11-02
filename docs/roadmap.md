# Próximos pasos (fuera del alcance de v0.1.0)

Lista de lo que quedó afuera del plan original (etapas 0–8) y de los problemas conocidos que se
documentaron en `docs/progress.md`. No hay orden de prioridad estricto; los pendientes conocidos
van primero porque afectan el uso diario.

## Pendientes conocidos de v0.1.0

- **Valores viejos de parámetros (caché del host VST3).** Cuando un plugin cambia sus propios
  parámetros sin avisar al host (Dexed al cambiar de programa, sforzando al cargar un instrumento),
  `AudioProcessorParameter::getValue()` del lado del host queda con el valor anterior: `listParams`
  y los overrides `params` al guardar pueden mostrar valores viejos y no se emite `paramChanged`.
  Hoy la salida es `listParams` con `"refresh": true` (restaura el estado completo; caro). Pasos
  posibles: releer los valores del `IEditController` directamente (JUCE 8.0.15 no lo expone:
  `resetParameters` es privado), o parchear/forkear el host VST3 de JUCE, o escuchar
  `restartComponent (kParamValuesChanged)`. Ver `docs/api.md` → "Valores viejos" y etapas 5 y 7.
- **Crear un VST3 bloquea el hilo de la UI** lo que tarde el plugin (Surge XT ~0.3–0.5 s,
  sforzando ~0.1 s). VST3/JUCE crean el componente en el hilo de mensajes; ya se precarga el DLL en
  otro hilo y se escalonan las creaciones (etapa 6). Solución de fondo: hostear cada plugin en un
  proceso aparte (también aislaría los crashes al instanciar, que hoy tumban la app). Relacionado: la
  precarga del DLL corre la inicialización estática del plugin en un hilo de fondo; si algún plugin
  lo tolera mal, agregar una lista de exclusión.
- **Fuga de sforzando 2.1.2.3**: al quitar una instancia queda vivo el `VST3HostContextHeadless`
  de JUCE (el plugin se queda con una referencia COM). Unos cientos de bytes por instancia; los
  tests en Debug lo permiten con `-AllowedLeaks VST3HostContextHeadless,AsyncUpdater`. Revisar con
  versiones nuevas de sforzando o de JUCE.
- **Buffer de 480 muestras con WASAPI compartido** (10 ms en esta máquina; el pedido de 256 se
  redondea). El modo exclusivo de JUCE con el driver Focusrite rindió por debajo de tiempo real;
  DirectSound baja a 128 pero con ritmo irregular. La salida real es **ASIO** (ver abajo).
- `setProgram` justo después de un `listParams` con `refresh` a veces no se aplica en Dexed (causa
  no investigada).
- `midi` **con** `instanceId` no pasa por las rutas ni enciende el LED de la fila (sin `instanceId`
  sí: ver `docs/api.md`). La salida "VstPlayer" de carrilion todavía aplica las rutas ella misma (una
  copia por instancia, lee `status.midiRoutes` cada 5 s); podría pasar a `midi` sin `instanceId` y
  dejar de seguir las rutas (un cambio de ruta en VstPlayer valdría al instante). Su `route_table` lee
  `"channels": []` como "todos los canales"; VstPlayer ya no lista esas rutas en `status`, pero
  convendría que carrilion las tome como "ningún canal".
- Rutas: no hay una semántica "cualquier puerto" (una ruta que tome el MIDI de todos los puertos
  abiertos). Se eligió "sin puerto" para las rutas que no dependen de un dispositivo; si hace falta
  tocar desde cualquier teclado sin elegirlo, agregarla como otra opción del selector.
- No hay UI para el puerto/token de la API ni para recuperar el autoguardado (se edita
  `settings.xml` / se abre `autosave.vstproj.json` a mano).
- La UI edita una sola ruta MIDI por instancia (el modelo y el formato admiten varias).
- Con remap, dos canales de origen que tocan la misma nota se fusionan (el segundo note-on corta al
  primero).
- La "asociación" de `.vstproj.json` no puede ser un doble clic directo (Windows usa la última
  extensión, `.json`); ver `tools/register-filetype.ps1`. Alternativa futura: extensión propia
  (`.vstproj`) además del JSON.
- Sin firma de código: SmartScreen avisa la primera vez.

## Funcionalidad nueva

- **ASIO con el SDK de Steinberg**: el CMake ya lo detecta en `third_party/asiosdk`
  (`JUCE_ASIO=1`). Falta probarlo con el driver del Focusrite, decidir si se distribuye un build con
  ASIO (licencia del SDK) y exponer el panel de control del driver.
- **CLAP** como segundo formato de plugin (JUCE 8 todavía no hostea CLAP de serie; evaluar
  `clap-juce-extensions` o un host propio).
- **Multisalida por instancia**: hoy sólo se mezclan las dos primeras salidas del bus principal
  (`disableNonMainBuses`). Exponer los buses extra como pares estéreo con su propia ganancia o rutearlos
  a salidas físicas.
- **Efectos en cadena por instancia** (inserts VST3 después del instrumento) y quizás envíos a buses
  de efectos compartidos.
- **Render offline** (`--render`, ya existe): falta `AudioPlayHead` con tempo y posición (plugins con
  LFOs / arpegiadores sincronizados usan su tempo interno), stems por instancia, y probarlo con
  sforzando con un instrumento cargado (el calentamiento a ritmo real está pensado para su carga de
  samples en segundo plano).
- **Grabación** (hecha: MP3 con LAME y WAV opcional de la mezcla, botón ● Grabar / Ctrl+R, API
  `startRecording` / `stopRecording`). Falta: grabar por instancia (stems), modo "armado" que empieza
  con la primera nota (sin silencio al principio), MP3 en `--render` (`--out x.mp3`, el mismo
  `Mp3Writer`).
- **MIDI clock**: hoy se descarta todo mensaje de sistema. Recibir clock/start/stop para plugins con
  arpegiadores o LFOs sincronizados (`AudioPlayHead` con tempo y posición).
- **Windows MIDI Services** (MIDI 2.0 de Windows 11) como alternativa a loopMIDI: crea puertos
  virtuales del sistema sin programas de terceros. Evaluar cuando JUCE lo soporte o con la API
  WinRT directamente.
- Hostear plugins fuera de proceso (ver arriba).
- Estado de plugins grandes en un archivo `.state` al lado del proyecto en lugar de base64 dentro
  del JSON.
- Recuperación automática del autoguardado al arrancar después de un cierre inesperado.
