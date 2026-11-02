// Offline render test (VstPlayer.exe --render), run by tools/run-render-test.ps1.
// Writes small Standard MIDI Files and a test project (two Dexed instances), renders them with
// the exe spawned as Node would (piped stdio), parses the JSON summary and the WAV, and checks
// sample-accurate onsets, tempo map, routing (keep / remapTo, routes of any input device or
// with no port: tests/fixtures/sin-puerto), CC7, tail length, determinism, errors and JUCE
// assertions / leaks (--debug-to-stdout).
//
//   node tools/render-test.mjs --exe <VstPlayer.exe> --data-dir <dir> --fixtures <dir> --tmp <dir>
//                              [--allowed-leaks A,B]
import { spawn } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'

const argv = process.argv.slice(2)
const opt = (name, def) => {
  const i = argv.indexOf('--' + name)
  return i >= 0 && i + 1 < argv.length ? argv[i + 1] : def
}
const exe = opt('exe')
const dataDir = opt('data-dir')
const examples = opt('fixtures') // tests/fixtures: test projects, not the editable examples/
const tmp = opt('tmp')
const allowedLeaks = (opt('allowed-leaks', '') || '').split(',').map((s) => s.trim()).filter(Boolean)
if (!exe || !dataDir || !examples || !tmp) {
  console.error('uso: node render-test.mjs --exe <exe> --data-dir <dir> --fixtures <dir> --tmp <dir>')
  process.exit(2)
}

const SR = 44100
const failures = []
let passed = 0
function check(ok, label, detail = '') {
  if (ok) { passed++; console.log('  OK    ' + label + (detail ? '  (' + detail + ')' : '')) }
  else { failures.push(label); console.log('  FALLO ' + label + (detail ? '  (' + detail + ')' : '')) }
  return ok
}

// ---- Standard MIDI File writer -------------------------------------------------------------
function vlq(n) {
  const out = [n & 0x7f]
  while ((n >>= 7)) out.unshift((n & 0x7f) | 0x80)
  return out
}
function track(events) { // [[tick, [bytes...]], ...] in tick order
  const data = []
  let last = 0
  for (const [tick, bytes] of events) { data.push(...vlq(tick - last), ...bytes); last = tick }
  data.push(0, 0xff, 0x2f, 0)
  const n = data.length
  return [0x4d, 0x54, 0x72, 0x6b, (n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255, ...data]
}
function smf(type, tracks, ppq = 480) {
  const header = [0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, type, 0, tracks.length, ppq >> 8, ppq & 255]
  return Buffer.from([...header, ...tracks.flatMap(track)])
}
const tempo = (us) => [0xff, 0x51, 0x03, (us >> 16) & 255, (us >> 8) & 255, us & 255]
const on = (ch, note, vel) => [0x90 | (ch - 1), note, vel]
const off = (ch, note) => [0x80 | (ch - 1), note, 0]
const cc = (ch, num, val) => [0xb0 | (ch - 1), num, val]
// 120 bpm (default tempo), 480 ppq: 960 ticks = 1 s.
const sec = (s) => Math.round(s * 960)

// ---- WAV reader (RIFF, 32-bit float) -------------------------------------------------------
function readWav(file) {
  const b = fs.readFileSync(file)
  if (b.toString('ascii', 0, 4) !== 'RIFF' || b.toString('ascii', 8, 12) !== 'WAVE') throw new Error('no es RIFF/WAVE')
  let pos = 12, fmt = null, data = null
  while (pos + 8 <= b.length) {
    const id = b.toString('ascii', pos, pos + 4), size = b.readUInt32LE(pos + 4)
    if (id === 'fmt ') {
      let format = b.readUInt16LE(pos + 8)
      if (format === 0xfffe) format = b.readUInt16LE(pos + 8 + 24) // extensible: sub-format GUID
      fmt = { format, channels: b.readUInt16LE(pos + 10), sampleRate: b.readUInt32LE(pos + 12), bits: b.readUInt16LE(pos + 22) }
    } else if (id === 'data') {
      data = b.subarray(pos + 8, pos + 8 + size)
    }
    pos += 8 + size + (size & 1)
  }
  if (!fmt || !data) throw new Error('faltan fmt/data')
  const frames = Math.floor(data.length / (4 * fmt.channels))
  const ch = [new Float32Array(frames), new Float32Array(frames)]
  for (let i = 0; i < frames; i++)
    for (let c = 0; c < fmt.channels && c < 2; c++) ch[c][i] = data.readFloatLE((i * fmt.channels + c) * 4)
  return { ...fmt, frames, ch }
}
const absAt = (w, i) => Math.max(Math.abs(w.ch[0][i]), Math.abs(w.ch[1][i]))
function maxAbs(w, from = 0, to = w.frames) {
  let m = 0
  for (let i = Math.max(0, from); i < Math.min(to, w.frames); i++) m = Math.max(m, absAt(w, i))
  return m
}
function firstAbove(w, threshold, from = 0) {
  for (let i = from; i < w.frames; i++) if (absAt(w, i) > threshold) return i
  return -1
}
function lastAbove(w, threshold) {
  for (let i = w.frames - 1; i >= 0; i--) if (absAt(w, i) > threshold) return i
  return -1
}
const db = (x) => (x > 0 ? 20 * Math.log10(x) : -Infinity)

// ---- running the exe -----------------------------------------------------------------------
function run(args) {
  return new Promise((resolve) => {
    const t0 = Date.now()
    const child = spawn(exe, [...args, '--data-dir', dataDir, '--debug-to-stdout'], { stdio: ['ignore', 'pipe', 'pipe'] })
    let out = '', err = ''
    child.stdout.on('data', (d) => { out += d })
    child.stderr.on('data', (d) => { err += d })
    const timer = setTimeout(() => child.kill(), 180000)
    child.on('close', (code) => {
      clearTimeout(timer)
      const lines = out.split(/\r?\n/).filter((l) => l.length > 0)
      const debug = lines.filter((l) => l.startsWith('[debug] '))
      const plain = lines.filter((l) => !l.startsWith('[debug] '))
      let summary = null
      try { summary = JSON.parse(plain[plain.length - 1]) } catch { }
      resolve({ code, summary, plain, debug, err, ms: Date.now() - t0 })
    })
  })
}

let totalAssertions = 0, totalBadLeaks = 0
function checkDebug(label, r) {
  const assertions = r.debug.filter((l) => /JUCE Assertion failure/.test(l) && !/LeakedObjectDetector/.test(l))
  const leaks = r.debug.filter((l) => /Leaked objects detected/.test(l))
  const bad = leaks.filter((l) => !allowedLeaks.some((a) => new RegExp('class ' + a + '\\b').test(l)))
  totalAssertions += assertions.length
  totalBadLeaks += bad.length
  check(assertions.length === 0, label + ': sin aserciones de JUCE', assertions.slice(0, 3).join(' | '))
  check(bad.length === 0, label + ': sin fugas', (leaks.length ? leaks.length + ' (permitidas ' + (leaks.length - bad.length) + ') ' : '') + bad.slice(0, 3).join(' | '))
}

async function render(label, project, mid, out, extra = []) {
  const r = await run(['--render', project, '--midi', mid, '--out', out, ...extra])
  console.log(`-- ${label}: exit ${r.code}, ${r.ms} ms`)
  if (r.code !== 0 || !r.summary || !r.summary.ok) console.log(r.err.split(/\r?\n/).map((l) => '   [stderr] ' + l).join('\n'))
  check(r.code === 0, label + ': exit 0', 'exit ' + r.code)
  check(r.plain.length === 1 && r.summary !== null, label + ': stdout = una linea JSON', r.plain.length + ' lineas')
  check(r.summary && r.summary.ok === true, label + ': ok true', r.summary && r.summary.error)
  check(/render: listo/.test(r.err), label + ': progreso en stderr')
  checkDebug(label, r)
  let wav = null
  try { wav = readWav(out) } catch (e) { check(false, label + ': WAV legible', e.message) }
  if (wav) {
    check(wav.format === 3 && wav.bits === 32 && wav.channels === 2 && wav.sampleRate === SR,
      label + ': WAV estereo float 32 a 44100', `formato ${wav.format}, ${wav.bits} bits, ${wav.channels} canales, ${wav.sampleRate} Hz`)
    check(r.summary && r.summary.frames === wav.frames, label + ': frames del resumen = WAV', `${r.summary && r.summary.frames} / ${wav.frames}`)
  }
  return { r, s: r.summary || {}, wav }
}
const inst = (s, name) => (s.instances || []).find((i) => i.name === name) || {}

// ---- test project --------------------------------------------------------------------------
const base = JSON.parse(fs.readFileSync(path.join(examples, 'prueba-etapa5.vstproj.json'), 'utf8'))
const dexed = base.instances.find((i) => i.plugin && i.plugin.name === 'Dexed' && i.id === '5a1e0001-0000-4000-8000-000000000001')
if (!dexed) { console.error('prueba-etapa5.vstproj.json: no se encontro la instancia Dexed'); process.exit(2) }
const idA = 'a0000000-0000-4000-8000-00000000000a', idB = 'b0000000-0000-4000-8000-00000000000b'
const project = {
  version: 1,
  name: 'Render test',
  // Ignored by --render: no audio device is opened (otherwise: warning about the device).
  audio: { deviceType: 'Windows Audio', outputDevice: 'Dispositivo que no existe', sampleRate: 48000, bufferSize: 480 },
  instances: [
    { ...dexed, id: idA, name: 'DexA', gainDb: 0, mute: false, window: { open: false } },
    { ...dexed, id: idB, name: 'DexB', gainDb: 0, mute: false, window: { open: false } },
  ],
  midiRoutes: [
    { inputDevice: 'loopMIDI Port', channels: [1], instanceId: idA, channelMode: 'keep' },
    // A port that does not exist: offline every route counts, whatever its device.
    { inputDevice: 'Puerto que no existe', channels: [3], instanceId: idB, channelMode: { remapTo: 2 } },
  ],
}
const projectFile = path.join(tmp, 'render-test.vstproj.json')
fs.writeFileSync(projectFile, JSON.stringify(project, null, 2))

const write = (name, buffer) => { const f = path.join(tmp, name); fs.writeFileSync(f, buffer); return f }
const midBasic = write('basico.mid', smf(0, [[[sec(0.5), on(1, 60, 100)], [sec(1.5), off(1, 60)]]]))
// Type 1, tempo map: 120 bpm, then 60 bpm from tick 960 (1 s). Tick 1200 = 1.5 s (1.25 s at 120).
const midTempo = write('tempo.mid', smf(1, [
  [[0, tempo(500000)], [960, tempo(1000000)]],
  [[1200, on(3, 60, 100)], [1440, off(3, 60)]],
]))
const midCc = write('cc7.mid', smf(0, [[
  [0, cc(1, 7, 0)],
  [sec(0.5), on(1, 60, 100)], [sec(0.5), on(3, 64, 100)],
  [sec(1.0), off(1, 60)], [sec(1.0), off(3, 64)],
]]))
const midUnrouted = write('sin-ruta.mid', smf(0, [[[sec(0.5), on(5, 60, 100)], [sec(1.0), off(5, 60)]]]))

const ONSET = 1e-4      // first sample above -80 dBFS
const SILENT = 1e-6
const PAD = Math.round(0.5 * SR)

// ---- 1. basic: onset, silence before, tail ---------------------------------------------------
{
  const out = path.join(tmp, 'basico.wav')
  const { s, wav } = await render('basico', projectFile, midBasic, out)
  if (wav) {
    const noteAt = Math.round(0.5 * SR)
    const onset = firstAbove(wav, ONSET)
    check(maxAbs(wav, 0, noteAt) < SILENT, 'basico: silencio antes de la nota', 'max ' + maxAbs(wav, 0, noteAt).toExponential(2))
    check(onset >= noteAt && onset <= noteAt + Math.round(0.003 * SR), 'basico: ataque a 0.5 s (+-3 ms)',
      `muestra ${onset} = ${(onset / SR * 1000).toFixed(2)} ms, nota en ${noteAt}`)
    check(db(maxAbs(wav)) > -40, 'basico: no esta en silencio', db(maxAbs(wav)).toFixed(1) + ' dBFS')
    check(Math.abs(s.peakDb - db(maxAbs(wav))) < 0.05, 'basico: peakDb del resumen', `${s.peakDb} / ${db(maxAbs(wav)).toFixed(2)}`)
    check(s.lastEventSeconds === 1.5, 'basico: lastEventSeconds 1.5', s.lastEventSeconds)
    const last = lastAbove(wav, 0.000316)
    check(wav.frames - 1 - last === PAD || (last < Math.round(1.5 * SR) && wav.frames === Math.round(1.5 * SR)),
      'basico: termina 0.5 s despues del ultimo sonido > -70 dBFS', `ultimo audible ${(last / SR).toFixed(3)} s, fin ${(wav.frames / SR).toFixed(3)} s`)
    check(s.seconds >= 2.0 && s.seconds <= 1.5 + 60 + 0.5, 'basico: largo = ultimo evento + cola', `${s.seconds} s, cola ${s.tailSeconds} s`)
    check(Math.abs(s.tailSeconds - (s.seconds - s.lastEventSeconds)) < 1e-5, 'basico: tailSeconds = seconds - lastEventSeconds')
    check(inst(s, 'DexA').noteOns?.[0] === 1 && inst(s, 'DexB').midiEvents === 0, 'basico: canal 1 solo a DexA (keep)',
      JSON.stringify(inst(s, 'DexA').noteOns))
    check(Array.isArray(s.warnings) && s.warnings.length === 0, 'basico: sin avisos (bloque audio ignorado)', JSON.stringify(s.warnings))

    // Determinism: the same input again.
    const { wav: again } = await render('basico (repeticion)', projectFile, midBasic, path.join(tmp, 'basico-2.wav'))
    if (again) {
      let diff = 0
      for (let i = 0; i < Math.min(wav.frames, again.frames); i++)
        diff = Math.max(diff, Math.abs(wav.ch[0][i] - again.ch[0][i]), Math.abs(wav.ch[1][i] - again.ch[1][i]))
      check(again.frames === wav.frames && diff <= 1e-4, 'determinismo: dos renders iguales', `frames ${wav.frames}/${again.frames}, diferencia max ${diff.toExponential(2)}`)
    }
  }
}

// ---- 2. tempo map (type 1) + remapTo through a route of a port that does not exist ----------
{
  const { s, wav } = await render('tempo + remap', projectFile, midTempo, path.join(tmp, 'tempo.wav'), ['--warmup', '1', '--block', '480'])
  if (wav) {
    const noteAt = Math.round(1.5 * SR)
    const onset = firstAbove(wav, ONSET)
    check(s.midiFileType === 1, 'tempo: archivo tipo 1', s.midiFileType)
    check(onset >= noteAt && onset <= noteAt + Math.round(0.003 * SR), 'tempo: ataque a 1.5 s (mapa de tempo; 1.25 s sin el)',
      `${(onset / SR * 1000).toFixed(2)} ms`)
    check(s.lastEventSeconds === 2, 'tempo: lastEventSeconds 2.0', s.lastEventSeconds)
    check(s.blockSize === 480, 'tempo: --block 480', s.blockSize)
    const b = inst(s, 'DexB')
    check(b.noteOns?.[1] === 1 && b.noteOns?.[2] === 0, 'remap: canal 3 llega a DexB por el canal 2', JSON.stringify(b.noteOns))
    check(inst(s, 'DexA').midiEvents === 0, 'remap: DexA no recibe el canal 3')
  }
}

// ---- 3. CC7 = 0 silences one instance, CC7 is not passed to the plugin ----------------------
{
  const { s, wav } = await render('cc7', projectFile, midCc, path.join(tmp, 'cc7.wav'), ['--warmup', '1'])
  if (wav) {
    const a = inst(s, 'DexA'), b = inst(s, 'DexB')
    check(a.peakDb === -100, 'cc7: CC7 = 0 silencia DexA', a.peakDb + ' dBFS')
    check(b.peakDb > -40, 'cc7: DexB suena', b.peakDb + ' dBFS')
    check(a.mixControllers === 1 && a.midiEvents === 2, 'cc7: el CC7 no llega al plugin', `mixControllers ${a.mixControllers}, eventos ${a.midiEvents}`)
    check(db(maxAbs(wav)) > -40, 'cc7: la mezcla no esta en silencio')
  }
}

// ---- 4. channel without a route; overwriting an existing file -------------------------------
{
  const out = path.join(tmp, 'sin-ruta.wav')
  fs.writeFileSync(out, 'contenido viejo que debe reemplazarse')
  const { s, wav } = await render('sin ruta', projectFile, midUnrouted, out, ['--warmup', '0'])
  if (wav) {
    check(s.midiUnrouted === 2 && s.midiRouted === 0, 'sin ruta: mensajes sin ruta contados', `${s.midiRouted} / ${s.midiUnrouted}`)
    check(s.peakDb === -100 && maxAbs(wav) === 0, 'sin ruta: silencio')
    check(wav.frames === SR, 'sin ruta: termina en el ultimo evento (nada audible)', wav.frames + ' frames')
    check((s.warnings || []).some((w) => /sin ruta/.test(w)), 'sin ruta: aviso', JSON.stringify(s.warnings))
  }
}

// ---- 4b. routes with no port (tests/fixtures/sin-puerto: "inputDevice": "") ----------------
{
  const midNoPort = write('sin-puerto.mid', smf(0, [[
    [sec(0.5), on(1, 60, 100)], [sec(0.5), on(3, 48, 100)], [sec(0.5), on(5, 72, 100)],
    [sec(1.0), off(1, 60)], [sec(1.0), off(3, 48)], [sec(1.0), off(5, 72)],
  ]]))
  const { s, wav } = await render('sin puerto', path.join(examples, 'sin-puerto.vstproj.json'), midNoPort, path.join(tmp, 'sin-puerto.wav'), ['--warmup', '1'])
  if (wav) {
    const piano = inst(s, 'Piano'), bajo = inst(s, 'Bajo')
    check(piano.noteOns?.[0] === 1 && piano.midiEvents === 2, 'sin puerto: canal 1 llega a Piano (keep)', JSON.stringify(piano.noteOns))
    check(bajo.noteOns?.[0] === 1 && bajo.noteOns?.[2] === 0 && bajo.midiEvents === 2, 'sin puerto: canal 3 llega a Bajo por el canal 1 (remapTo)', JSON.stringify(bajo.noteOns))
    check(piano.peakDb > -60 && bajo.peakDb > -60, 'sin puerto: las dos instancias suenan', `Piano ${piano.peakDb}, Bajo ${bajo.peakDb} dBFS`)
    check(s.midiRouted === 4 && s.midiUnrouted === 2, 'sin puerto: canal 5 sin ruta', `${s.midiRouted} / ${s.midiUnrouted}`)
    check((s.warnings || []).length === 1 && /sin ruta/.test(s.warnings[0]), 'sin puerto: solo el aviso del canal sin ruta', JSON.stringify(s.warnings))
  }
}

// ---- 5. errors ------------------------------------------------------------------------------
{
  const out = path.join(tmp, 'error.wav')
  let r = await run(['--render', projectFile, '--midi', path.join(tmp, 'no-existe.mid'), '--out', out])
  console.log(`-- error (MIDI inexistente): exit ${r.code}`)
  check(r.code === 2 && r.summary && r.summary.ok === false && /MIDI/.test(r.summary.error), 'error: MIDI inexistente -> exit 2, ok false', r.summary && r.summary.error)
  checkDebug('error MIDI', r)

  r = await run(['--render', projectFile, '--midi', projectFile, '--out', out])
  check(r.code === 2 && r.summary && r.summary.ok === false, 'error: archivo que no es MIDI -> exit 2', r.summary && r.summary.error)

  r = await run(['--render', projectFile, '--midi', midBasic, '--out', out, '--sample-rate', 'abc'])
  check(r.code === 2 && r.summary && r.summary.ok === false, 'error: --sample-rate invalido -> exit 2', r.summary && r.summary.error)

  r = await run(['--render', path.join(examples, 'plugin-faltante.vstproj.json'), '--midi', midBasic, '--out', out])
  console.log(`-- error (plugin faltante): exit ${r.code}`)
  check(r.code === 1 && r.summary && r.summary.ok === false && /no cargaron/.test(r.summary.error), 'error: plugin faltante -> exit 1, ok false', r.summary && r.summary.error)
  checkDebug('error plugin faltante', r)
  check(!fs.existsSync(out), 'error: no queda WAV')
}

const leftovers = fs.readdirSync(tmp).filter((f) => /_temp/i.test(f))
check(leftovers.length === 0, 'sin archivos temporales sobrantes', leftovers.join(', '))

console.log(`render-test: ${passed} OK, ${failures.length} fallos; aserciones ${totalAssertions}, fugas no permitidas ${totalBadLeaks}`)
if (failures.length) { console.log('Fallos: ' + failures.join('; ')); process.exit(1) }
process.exit(0)
