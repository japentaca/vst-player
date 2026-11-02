// Stage 7 API test (Node 22+: global fetch and WebSocket, no dependencies).
// Driven by tools/run-api-test.ps1, which starts VstPlayer.exe --api-test with a temporary data
// folder, runs this script and then closes the app (checking its exit code, assertions, leaks).
//
//   node tools/api-test.mjs --port 9479 [--token T] --fixtures <dir> --tmp <dir>
//
// <dir> of --fixtures is tests/fixtures: test projects of their own (prueba-etapa5 with its three
// stage-5 instances, plugin-faltante, sin-puerto with routes that have no port), independent of
// the editable examples/. If ../carrilion is next to this repo, its VstPlayer output
// (apps/web/lib/core/vstplayer-port.js) plays the routes with no port.
//
// Exit code 0 = every check passed. Prints one line per check plus the measurements.

const args = Object.fromEntries(
    process.argv.slice(2).reduce((acc, a, i, all) => (a.startsWith('--') ? [...acc, [a.slice(2), all[i + 1]]] : acc), []));

const port = Number(args.port || 9479);
const token = args.token || '';
const examplesDir = args.fixtures;
const tmpDir = args.tmp;
const base = `http://127.0.0.1:${port}`;
const q = token ? `?token=${encodeURIComponent(token)}` : '';
const sep = examplesDir.includes('\\') ? '\\' : '/';

let failures = 0;
let checks = 0;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function check(ok, what, detail = '') {
    checks++;
    if (!ok) failures++;
    console.log(`${ok ? '  OK   ' : '  FALLO'} ${what}${detail ? '  -> ' + detail : ''}`);
    return ok;
}

async function http(body, { raw = false, withToken = true, method = 'POST', path = '/api' } = {}) {
    const res = await fetch(base + path + (withToken ? q : ''), {
        method,
        headers: { 'Content-Type': 'application/json' },
        body: method === 'POST' ? (raw ? body : JSON.stringify(body)) : undefined,
    });
    const text = await res.text();
    let json = null;
    try { json = JSON.parse(text); } catch { /* not JSON */ }
    return { status: res.status, json, text, headers: res.headers };
}

//==============================================================================
// WebSocket client with request ids and an event log.
class Client {
    constructor(url) {
        this.url = url;
        this.pending = new Map();
        this.events = [];
        this.nextId = 1;
        this.closed = null;
        this.unsolicited = [];
    }

    open() {
        return new Promise((resolve, reject) => {
            this.ws = new WebSocket(this.url);
            this.ws.onopen = () => resolve(this);
            this.ws.onerror = (e) => reject(new Error('WebSocket error ' + (e.message || '')));
            this.ws.onclose = (e) => { this.closed = { code: e.code, reason: e.reason }; };
            this.ws.onmessage = (m) => {
                const msg = JSON.parse(m.data);
                if (msg.event) { msg.t = performance.now(); this.events.push(msg); return; }
                const p = this.pending.get(msg.id);
                if (p) { this.pending.delete(msg.id); p(msg); } else this.unsolicited.push(msg);
            };
        });
    }

    send(cmd, timeoutMs = 130000) {
        const id = this.nextId++;
        return new Promise((resolve, reject) => {
            const t = setTimeout(() => { this.pending.delete(id); reject(new Error('timeout ' + cmd.cmd)); }, timeoutMs);
            this.pending.set(id, (msg) => { clearTimeout(t); resolve(msg); });
            this.ws.send(JSON.stringify({ ...cmd, id }));
        });
    }

    sendRaw(text) { this.ws.send(text); }

    async waitEvent(pred, timeoutMs = 3000, since = 0) {
        const end = performance.now() + timeoutMs;
        while (performance.now() < end) {
            const e = this.events.slice(since).find(pred);
            if (e) return e;
            await sleep(20);
        }
        return null;
    }

    close() { this.ws.close(); }
}

//==============================================================================
async function main() {
    console.log(`API en ${base} (token: ${token ? 'sí' : 'no'})`);

    // Wait for the server (the app may take a moment to start).
    let up = false;
    for (let i = 0; i < 150 && !up; i++) {
        try { up = (await fetch(base + '/' + q)).ok; } catch { await sleep(200); }
    }
    if (!check(up, 'el servidor responde')) return;

    //--------------------------------------------------------------------------
    // Token
    if (token) {
        const r = await http({ cmd: 'status' }, { withToken: false });
        check(r.status === 401 && r.json?.code === 'unauthorized', 'sin token: HTTP 401', `${r.status} ${r.text}`);
        const bad = new Client(`ws://127.0.0.1:${port}/ws`);
        await bad.open();
        await sleep(500);
        check(bad.unsolicited.some((m) => m.code === 'unauthorized') && bad.closed?.code === 1008,
            'sin token: el WebSocket recibe el error y se cierra con 1008', JSON.stringify(bad.closed));
        const wrong = await http({ cmd: 'status' }, { withToken: false, path: '/api?token=otro' });
        check(wrong.status === 401, 'token equivocado: HTTP 401');
    }

    // CORS
    const pre = await fetch(base + '/api' + q, { method: 'OPTIONS' });
    check(pre.status === 204 && pre.headers.get('access-control-allow-origin') === '*', 'OPTIONS (preflight CORS) 204 con Allow-Origin *');

    const ws = await new Client(`ws://127.0.0.1:${port}/ws${q}`).open();

    //--------------------------------------------------------------------------
    // status (empty)
    let r = await http({ cmd: 'status' });
    check(r.status === 200 && r.json.ok && Array.isArray(r.json.instances) && r.json.instances.length === 0,
        'status por HTTP (proyecto vacío)', `${r.json?.project?.name}, ${r.json?.audio?.deviceType} ${r.json?.audio?.sampleRate}/${r.json?.audio?.bufferSize}`);
    check(r.headers.get('access-control-allow-origin') === '*', 'respuesta HTTP con Access-Control-Allow-Origin: *');
    r = await http(null, { method: 'GET', path: '/api/status' });
    check(r.status === 200 && r.json.ok && r.json.cmd === 'status', 'GET /api/status');
    const s0 = await ws.send({ cmd: 'status' });
    check(s0.ok && s0.cmd === 'status' && s0.api.clients === 1, 'status por WebSocket (id devuelto, 1 cliente)', `clientes=${s0.api?.clients}`);

    // listProjects
    r = await http({ cmd: 'listProjects', dir: examplesDir });
    const names = (r.json.projects || []).map((p) => p.name);
    check(r.json.ok && names.includes('prueba-etapa5') && names.includes('plugin-faltante'), 'listProjects de tests/fixtures', names.join(', '));
    r = await http({ cmd: 'listProjects', dir: examplesDir + sep + 'no-existe' });
    check(r.status === 404 && r.json.code === 'notFound', 'listProjects de una carpeta inexistente: 404');

    //--------------------------------------------------------------------------
    // loadProject
    const project1 = examplesDir + sep + 'prueba-etapa5.vstproj.json';
    let since = ws.events.length;
    const t0 = performance.now();
    const loaded = await ws.send({ cmd: 'loadProject', path: project1 });
    const loadMs = performance.now() - t0;
    const states = (loaded.instances || []).map((i) => `${i.name}:${i.state}`);
    check(loaded.ok && loaded.instances.length === 3 && loaded.instances.every((i) => i.state === 'ready'),
        'loadProject prueba-etapa5 responde cuando las 3 instancias están cargadas', `${states.join(', ')} en ${loadMs.toFixed(0)} ms`);
    check(loaded.project.name === 'Prueba etapa 5' && !loaded.project.loading, 'loadProject: proyecto actual cambiado', loaded.project.name);
    const ev = await ws.waitEvent((e) => e.event === 'projectLoaded', 3000, since);
    check(!!ev && ev.project.name === 'Prueba etapa 5', 'evento projectLoaded');
    const added = ws.events.slice(since).filter((e) => e.event === 'instanceAdded').length;
    const loadedEv = ws.events.slice(since).filter((e) => e.event === 'instanceLoaded' && e.instance.state === 'ready').length;
    check(added === 3 && loadedEv === 3, 'eventos instanceAdded x3 e instanceLoaded x3', `added=${added} loaded=${loadedEv}`);

    r = await http({ cmd: 'status' });
    const inst = Object.fromEntries(r.json.instances.map((i) => [i.name, i]));
    const dexed = inst['Dexed'], pads = inst['Pads'], sampler = inst['Sampler'];
    check(dexed && pads && sampler && r.json.midiRoutes.length === 3 && pads.plugin.name === 'Surge XT' && pads.midiRoutes[0].channelMode.remapTo === 1,
        'status: 3 instancias con plugin, rutas MIDI (remapTo), gain, mute',
        r.json.instances.map((i) => `${i.name}=${i.plugin.name} ${i.gainDb} dB params=${i.numParams} progs=${i.numPrograms}`).join('; '));

    // Quieter for the sound checks (and setGain / setMute)
    for (const i of [dexed, pads, sampler]) {
        const g = await ws.send({ cmd: 'setGain', instanceId: i.id, gainDb: -30 });
        check(g.ok && g.gainDb === -30, `setGain ${i.name} -30 dB`);
    }
    r = await http({ cmd: 'setMute', instanceId: sampler.id, mute: true });
    check(r.json.ok && r.json.mute === true, 'setMute Sampler true');
    r = await http({ cmd: 'status' });
    const sm = r.json.instances.find((i) => i.id === sampler.id);
    check(sm.mute === true && sm.gainDb === -30 && r.json.project.dirty === true, 'status refleja gain/mute y el proyecto queda modificado');
    r = await http({ cmd: 'setMute', instanceId: 'Sampler', mute: false });
    check(r.json.ok && r.json.mute === false, 'setMute por nombre de instancia');
    r = await http({ cmd: 'setGain', instanceId: 'no-existe', gainDb: 0 });
    check(r.status === 404, 'setGain a una instancia inexistente: 404');

    //--------------------------------------------------------------------------
    // listParams / setParam
    r = await http({ cmd: 'listParams', instanceId: pads.id });
    const surgeParams = r.json.params;
    check(r.json.ok && surgeParams.length === r.json.total && surgeParams.length > 1000, 'listParams Surge XT', `${surgeParams.length} parámetros`);
    const sample = surgeParams[0];
    check('index' in sample && 'name' in sample && 'value' in sample && 'text' in sample && 'label' in sample,
        'listParams devuelve index, name, value, text, label', JSON.stringify(sample));
    const target = surgeParams.find((p) => /Macro 1|M1/i.test(p.name)) || surgeParams[0];
    console.log(`  parámetro elegido: [${target.index}] "${target.name}" = ${target.value} (${target.text})`);

    since = ws.events.length;
    r = await http({ cmd: 'setParam', instanceId: pads.id, index: target.index, value: 0.4 });
    check(r.json.ok && Math.abs(r.json.value - 0.4) < 1e-6, 'setParam por index (HTTP)', JSON.stringify(r.json));
    r = await http({ cmd: 'listParams', instanceId: pads.id, filter: target.name });
    const after = r.json.params.find((p) => p.index === target.index);
    check(after && Math.abs(after.value - 0.4) < 1e-4, 'listParams lee el valor nuevo', `${after?.value} (${after?.text})`);
    const pc = await ws.waitEvent((e) => e.event === 'paramChanged' && e.instanceId === pads.id && e.params.some((p) => p.index === target.index), 2000, since);
    check(!!pc, 'evento paramChanged del parámetro movido', pc ? JSON.stringify(pc.params.find((p) => p.index === target.index)) : '');

    const byName = await ws.send({ cmd: 'setParam', instanceId: pads.id, name: target.name, value: 0.7 });
    check(byName.ok && byName.index === target.index, 'setParam por nombre (WebSocket, id devuelto)', `id=${byName.id}`);
    r = await http({ cmd: 'getParam', instanceId: pads.id, index: target.index });
    check(Math.abs(r.json.param.value - 0.7) < 1e-4, 'getParam lee 0.7', r.json.param.text);
    if (after?.id) {
        const byId = await ws.send({ cmd: 'setParam', instanceId: pads.id, paramId: after.id, value: 0.4 });
        check(byId.ok && byId.index === target.index, 'setParam por paramId');
    }
    r = await http({ cmd: 'setParam', instanceId: pads.id, name: 'No existe este parámetro', value: 0.5 });
    check(r.status === 404 && r.json.code === 'paramNotFound', 'setParam con nombre inexistente: 404');
    r = await http({ cmd: 'setParam', instanceId: pads.id, index: target.index });
    check(r.status === 400, 'setParam sin value: 400');
    r = await http({ cmd: 'setParam', instanceId: 'x', index: 0, value: 0.1 });
    check(r.status === 404, 'setParam a instancia inexistente: 404');
    r = await http({ cmd: 'setParam', instanceId: pads.id, index: target.index, value: 7 });
    check(r.json.ok && r.json.value === 1, 'setParam fuera de rango se limita a 0..1');
    await http({ cmd: 'setParam', instanceId: pads.id, index: target.index, value: 0.4 });

    //--------------------------------------------------------------------------
    // Programs (Dexed) + the stale host cache
    r = await http({ cmd: 'listPrograms', instanceId: dexed.id });
    check(r.json.ok && r.json.programs.length === 32, 'listPrograms Dexed', `${r.json.programs.length} programas, actual ${r.json.current} "${r.json.programs[r.json.current]?.name}"`);
    const progNames = r.json.programs.map((p) => p.name);
    async function notePeak(id, note = 48) {
        await http({ cmd: 'levels' });
        await http({ cmd: 'midi', instanceId: id, bytes: [144, note, 100] });
        await sleep(600);
        const lvl = (await http({ cmd: 'levels' })).json;
        await http({ cmd: 'midi', instanceId: id, bytes: [128, note, 0] });
        await http({ cmd: 'panic' });
        await sleep(500);
        return lvl.instances.find((i) => i.instanceId === id).peakDb;
    }
    const before = (await http({ cmd: 'listParams', instanceId: dexed.id })).json.params;
    r = await http({ cmd: 'setProgram', instanceId: dexed.id, index: 20 });
    check(r.json.ok && r.json.current === 20, 'setProgram Dexed 20', `"${r.json.name}"`);
    await sleep(400);
    const peak20 = await notePeak(dexed.id);
    r = await http({ cmd: 'setProgram', instanceId: dexed.id, index: 5 });
    check(r.json.ok && r.json.current === 5, 'setProgram Dexed 5', `"${r.json.name}" (lista: "${progNames[5]}")`);
    await sleep(400);
    const peak5 = await notePeak(dexed.id);
    check(Math.abs(peak5 - peak20) > 1, 'setProgram cambia el sonido (pico de la misma nota con otro programa)', `programa 20: ${peak20.toFixed(2)} dBFS, programa 5: ${peak5.toFixed(2)} dBFS`);
    const afterProg = (await http({ cmd: 'listParams', instanceId: dexed.id })).json.params;
    const changedNoRefresh = afterProg.filter((p, i) => Math.abs(p.value - before[i].value) > 1e-6 && !/program/i.test(p.name)).length;
    const refreshed = (await http({ cmd: 'listParams', instanceId: dexed.id, refresh: true })).json.params;
    const changedRefresh = refreshed.filter((p, i) => Math.abs(p.value - before[i].value) > 1e-6 && !/program/i.test(p.name)).length;
    console.log(`  Dexed tras setProgram: parámetros distintos (sin contar el de programa) sin refresh = ${changedNoRefresh}, con refresh = ${changedRefresh} (de ${before.length})`);
    check(changedRefresh > changedNoRefresh, 'listParams con refresh:true relee los valores que el plugin cambió sin avisar');
    r = await http({ cmd: 'setProgram', instanceId: dexed.id, index: 999 });
    check(r.status === 404, 'setProgram fuera de rango: 404');

    //--------------------------------------------------------------------------
    // Direct MIDI: sound measured on the gain node
    await http({ cmd: 'levels' }); // reset peaks
    await sleep(200);
    let lv = (await http({ cmd: 'levels' })).json;
    const silentBefore = lv.instances.find((i) => i.instanceId === dexed.id).peakDb;
    since = ws.events.length;
    const on = await ws.send({ cmd: 'midi', instanceId: dexed.id, bytes: [144, 60, 100] });
    check(on.ok && on.messages === 1, 'midi note-on a Dexed por WebSocket');
    await sleep(400);
    lv = (await http({ cmd: 'levels' })).json;
    const dexedPeak = lv.instances.find((i) => i.instanceId === dexed.id).peakDb;
    check(silentBefore < -90 && dexedPeak > -80, 'la nota suena: pico en el nodo de ganancia de Dexed', `antes ${silentBefore.toFixed(1)} dBFS, con la nota ${dexedPeak.toFixed(1)} dBFS, mezcla ${lv.masterPeakDb.toFixed(1)} dBFS`);
    const act = await ws.waitEvent((e) => e.event === 'midiActivity' && e.instanceId === dexed.id, 1000, since);
    check(!!act, 'evento midiActivity', act ? `count=${act.count}` : '');
    r = await http({ cmd: 'midi', instanceId: dexed.id, bytes: [128, 60, 0, 144, 64, 90, 144, 67, 90] });
    check(r.json.ok && r.json.messages === 3, 'midi con varios mensajes en un array (HTTP)');
    await sleep(200);

    // Panic cuts everything
    const panic = await ws.send({ cmd: 'panic' });
    check(panic.ok, 'panic');
    await sleep(600);
    await http({ cmd: 'levels' });
    await sleep(300);
    lv = (await http({ cmd: 'levels' })).json;
    const afterPanic = lv.instances.find((i) => i.instanceId === dexed.id).peakDb;
    check(afterPanic < -90 && lv.masterPeakDb < -90, 'después de panic: silencio (notas sostenidas cortadas)', `Dexed ${afterPanic.toFixed(1)} dBFS, mezcla ${lv.masterPeakDb.toFixed(1)} dBFS`);

    // CC7 / CC10: VstPlayer's gain node applies them (volume = value / 127, pan like Web Audio's
    // StereoPannerNode); they do not reach the plugin.
    async function ccNote(cc) {
        await http({ cmd: 'midi', instanceId: dexed.id, bytes: cc });
        await sleep(150); // past the 20 ms smoothing ramp
        await http({ cmd: 'levels' });
        await http({ cmd: 'midi', instanceId: dexed.id, bytes: [144, 60, 100] });
        await sleep(500);
        const l = (await http({ cmd: 'levels' })).json.instances.find((i) => i.instanceId === dexed.id);
        await http({ cmd: 'midi', instanceId: dexed.id, bytes: [128, 60, 0] });
        await http({ cmd: 'panic' });
        await sleep(500);
        return l;
    }
    r = await http({ cmd: 'status' });
    let mix = r.json.instances.find((i) => i.id === dexed.id).midiMix;
    check(mix && mix.volume === null && mix.pan === null, 'status: midiMix sin CC recibidos (null, null)');
    let l = await ccNote([0xb0, 7, 0]);
    check(l.peakDb < -90, 'CC7 = 0 silencia la instancia', `${l.peakDb.toFixed(1)} dBFS`);
    l = await ccNote([0xb0, 7, 127, 0xb0, 10, 0]);
    check(l.peakDbL > -80 && l.peakDbR < -90, 'CC7 = 127 + CC10 = 0: todo a la izquierda', `L ${l.peakDbL.toFixed(1)}, R ${l.peakDbR.toFixed(1)} dBFS`);
    l = await ccNote([0xb0, 10, 127]);
    check(l.peakDbR > -80 && l.peakDbL < -90, 'CC10 = 127: todo a la derecha', `L ${l.peakDbL.toFixed(1)}, R ${l.peakDbR.toFixed(1)} dBFS`);
    const full = await ccNote([0xb0, 10, 64]);
    const half = await ccNote([0xb0, 7, 64]);
    const expected = 20 * Math.log10(64 / 127);
    check(Math.abs(half.peakDb - full.peakDb - expected) < 1.5, 'CC7 = 64: ganancia lineal 64/127 (centro con CC10 = 64)',
        `${(half.peakDb - full.peakDb).toFixed(2)} dB (esperado ${expected.toFixed(2)}); L/R ${full.peakDbL.toFixed(1)}/${full.peakDbR.toFixed(1)}`);
    await http({ cmd: 'midi', instanceId: dexed.id, bytes: [0xb0, 7, 127] });
    await sleep(100);
    mix = (await http({ cmd: 'status' })).json.instances.find((i) => i.id === dexed.id).midiMix;
    check(mix.volume === 127 && mix.pan === 64, 'status: midiMix con los últimos CC7 / CC10', JSON.stringify(mix));

    for (const [bytes, what] of [[[240, 1, 247], 'sysex'], [[60, 100], 'sin status'], [[144, 60], 'incompleto'], [[144, 200, 1], 'dato > 127']]) {
        r = await http({ cmd: 'midi', instanceId: dexed.id, bytes });
        check(r.status === 400 && r.json.code === 'badMidi', `midi inválido (${what}): 400`);
    }

    //--------------------------------------------------------------------------
    // Invalid input does not bring the server down
    r = await http('{"cmd": "status"', { raw: true });
    check(r.status === 400 && r.json.code === 'badJson', 'HTTP con JSON inválido: 400 badJson', r.json.error);
    r = await http('', { raw: true });
    check(r.status === 400, 'HTTP con cuerpo vacío: 400');
    r = await http('[1,2,3]', { raw: true });
    check(r.status === 400 && r.json.code === 'badJson', 'HTTP con JSON que no es objeto: 400');
    r = await http({ cmd: 'hacerCafe' });
    check(r.status === 400 && r.json.code === 'unknownCommand', 'comando desconocido: 400 unknownCommand');
    r = await http({ cmd: 'status' }, { method: 'GET', path: '/no-existe' });
    check(r.status === 404, 'ruta HTTP desconocida: 404');
    ws.sendRaw('esto no es json {');
    ws.sendRaw('{"cmd":"otroComando","id":"x1"}');
    await sleep(300);
    const wsErrs = ws.unsolicited.filter((m) => m.code === 'badJson').length;
    const sAfter = await ws.send({ cmd: 'status' });
    const wsUnknown = ws.unsolicited.some((m) => m.code === "unknownCommand" && m.id === "x1");
    check(wsErrs === 1 && wsUnknown && sAfter.ok && ws.closed === null, 'WebSocket con JSON inválido: error y la conexión sigue abierta');
    // Deeply nested JSON (stack overflow in the parser before stage 8): rejected without parsing.
    r = await http('['.repeat(300000) + ']'.repeat(300000), { raw: true });
    check(r.status === 400 && r.json.code === 'badJson', 'HTTP con JSON anidado 300000 niveles: 400 badJson', r.json.error);
    ws.sendRaw('{"cmd":"status","x":' + '['.repeat(100000) + '}');
    await sleep(300);
    check(ws.unsolicited.filter((m) => m.code === 'badJson').length === 2 && ws.closed === null, 'WebSocket con JSON anidado: badJson y la conexión sigue abierta');
    r = await http({ cmd: 'status' });
    check(r.json.ok, 'el servidor sigue vivo');

    //--------------------------------------------------------------------------
    // Unsaved changes / saveProject / reload
    r = await http({ cmd: 'loadProject', path: examplesDir + sep + 'plugin-faltante.vstproj.json' });
    check(r.status === 409 && r.json.code === 'unsavedChanges', 'loadProject con cambios sin guardar: 409 unsavedChanges');
    const savedPath = tmpDir + sep + 'api-guardado.vstproj.json';
    r = await http({ cmd: 'saveProject', path: savedPath });
    check(r.json.ok && r.json.path === savedPath, 'saveProject a una ruta temporal', r.json.path);
    r = await http({ cmd: 'status' });
    check(r.json.project.path === savedPath && r.json.project.dirty === false, 'status: proyecto guardado, sin cambios');
    r = await http({ cmd: 'saveProject' });
    check(r.json.ok, 'saveProject sin ruta (mismo archivo)');
    r = await http({ cmd: 'listProjects', dir: tmpDir });
    check(r.json.projects.some((p) => p.path === savedPath) && r.json.recent.includes(savedPath), 'listProjects ve el archivo guardado y está en recientes');

    since = ws.events.length;
    r = await http({ cmd: 'loadProject', path: examplesDir + sep + 'plugin-faltante.vstproj.json' });
    const missing = (r.json.instances || []).find((i) => i.state === 'missing');
    check(r.json.ok && missing && r.json.warnings.length > 0, 'loadProject plugin-faltante: instancia faltante y aviso', r.json.warnings?.[0]?.slice(0, 80));
    check(ws.events.slice(since).filter((e) => e.event === 'instanceRemoved').length === 3, 'eventos instanceRemoved x3 al cambiar de proyecto');
    r = await http({ cmd: 'listParams', instanceId: missing.id });
    check(r.status === 409 && r.json.code === 'notReady', 'listParams de una instancia faltante: 409 notReady');
    r = await http({ cmd: 'loadProject', path: tmpDir + sep + 'no-existe.vstproj.json' });
    check(r.status === 404, 'loadProject de un archivo inexistente: 404');

    r = await http({ cmd: 'loadProject', path: savedPath });
    check(r.json.ok && r.json.instances.every((i) => i.state === 'ready'), 'loadProject del archivo guardado');
    const re = Object.fromEntries(r.json.instances.map((i) => [i.name, i]));
    check(re['Pads'].gainDb === -30 && re['Dexed'].currentProgram === 5, 'el proyecto guardado conserva gain y programa', `Pads ${re['Pads'].gainDb} dB, Dexed programa ${re['Dexed'].currentProgram}`);
    r = await http({ cmd: 'getParam', instanceId: re['Pads'].id, index: target.index });
    check(Math.abs(r.json.param.value - 0.4) < 1e-3, 'el proyecto guardado conserva el parámetro movido por la API', `${r.json.param.value}`);

    // Reopening while the previous project is still loading, with the same instance ids
    // (api-guardado is prueba-etapa5 saved): before stage 8 the first load's queued creations
    // were applied to the second load's instances (old state, orphaned nodes). Two waiters on
    // one load also shared one reply object.
    {
        const nodesBefore = (await http({ cmd: 'audioStats' })).json.graphNodes;
        const first = http({ cmd: 'loadProject', path: project1, discardChanges: true, id: 'carga-1' });
        await sleep(30);
        const second = http({ cmd: 'loadProject', path: savedPath, discardChanges: true, id: 'carga-2' });
        const [r1, r2] = await Promise.all([first, second]);
        check(r1.json.ok && r2.json.ok && r1.json.id === 'carga-1' && r2.json.id === 'carga-2',
            'dos loadProject seguidos: cada respuesta con su id', `${r1.json.id} / ${r2.json.id}`);
        r = await http({ cmd: 'status' });
        const byName = Object.fromEntries(r.json.instances.map((i) => [i.name, i]));
        check(r.json.project.path === savedPath && r.json.instances.length === 3 && r.json.instances.every((i) => i.state === 'ready'),
            'gana el segundo proyecto: 3 instancias listas', r.json.instances.map((i) => i.name + ':' + i.state).join(', '));
        r = await http({ cmd: 'getParam', instanceId: byName['Pads'].id, index: target.index });
        const p2 = r.json.param.value;
        check(Math.abs(p2 - 0.4) < 1e-3 && byName['Dexed'].currentProgram === 5 && byName['Pads'].gainDb === -30,
            'las instancias tienen el estado del segundo proyecto (no el del primero)', `param ${p2}, Dexed programa ${byName['Dexed'].currentProgram}, Pads ${byName['Pads'].gainDb} dB`);
        // Orphaned plugin nodes would stay in the graph: same node count as a plain load.
        const nodesAfter = (await http({ cmd: 'audioStats' })).json.graphNodes;
        check(nodesAfter === nodesBefore, 'mismo número de nodos en el grafo que con una carga normal (sin plugins huérfanos)', `${nodesAfter} vs ${nodesBefore}`);
        await http({ cmd: 'panic' });
        r = await http({ cmd: 'loadProject', path: savedPath, discardChanges: true });
        check(r.json.ok, 'proyecto guardado reabierto para lo que sigue');
    }

    //--------------------------------------------------------------------------
    // 50 setParam per second while a note sounds: audio callback health
    const padsId = re['Pads'].id;
    await ws.send({ cmd: 'midi', instanceId: padsId, bytes: [144, 57, 90, 144, 64, 90] });
    await sleep(500);

    async function measure(label, seconds, rate) {
        await http({ cmd: 'audioStats' }); // reset
        const startStats = (await http({ cmd: 'audioStats' })).json;
        const latencies = [];
        let sent = 0;
        const tEnd = performance.now() + seconds * 1000;
        const period = rate > 0 ? 1000 / rate : 0;
        let next = performance.now();
        let errors = 0;
        while (performance.now() < tEnd) {
            if (rate > 0) {
                const v = 0.2 + 0.6 * (0.5 + 0.5 * Math.sin(sent / 10));
                const t = performance.now();
                const resp = await ws.send({ cmd: 'setParam', instanceId: padsId, index: target.index, value: v });
                latencies.push(performance.now() - t);
                if (!resp.ok) errors++;
                sent++;
                next += period;
                const wait = next - performance.now();
                if (wait > 0) await sleep(wait);
            } else {
                await sleep(100);
            }
        }
        const st = (await http({ cmd: 'audioStats' })).json;
        latencies.sort((a, b) => a - b);
        const pct = (p) => (latencies.length ? latencies[Math.min(latencies.length - 1, Math.floor(p * latencies.length))] : 0);
        const expected = (st.sampleRate / st.bufferSize) * seconds;
        const res = {
            label, sent, errors, rate: sent / seconds,
            callbacks: st.callbacks, expected: Math.round(expected),
            cbPerSec: st.callbacks / seconds, avgMs: st.callbackAvgMs, maxMs: st.callbackMaxMs, blockMs: st.blockMs,
            overBudget: st.overBudget, xrunsDelta: st.xruns >= 0 && startStats.xruns >= 0 ? st.xruns - startStats.xruns : st.xruns,
            cpu: st.cpu, p50: pct(0.5), p99: pct(0.99),
        };
        console.log(`  [${label}] setParam enviados ${sent} (${res.rate.toFixed(1)}/s, errores ${errors}), latencia p50 ${res.p50.toFixed(2)} ms p99 ${res.p99.toFixed(2)} ms; `
            + `callbacks ${st.callbacks} (esperados ~${res.expected}, ${res.cbPerSec.toFixed(1)}/s), processBlock medio ${st.callbackAvgMs.toFixed(3)} ms máx ${st.callbackMaxMs.toFixed(3)} ms `
            + `(bloque ${st.blockMs.toFixed(2)} ms), sobre el presupuesto ${st.overBudget}, xruns ${res.xrunsDelta}, CPU ${st.cpu.toFixed(1)} %`);
        return res;
    }

    const baseline = await measure('sin setParam', 5, 0);
    const burst = await measure('50 setParam/s', 10, 50);
    const stress = await measure('500 setParam/s', 3, 500);
    const okCallbacks = (m) => Math.abs(m.callbacks - m.expected) <= Math.max(3, m.expected * 0.03);
    check(burst.errors === 0 && burst.rate >= 45, '50 setParam/s sostenidos durante 10 s sin errores', `${burst.rate.toFixed(1)}/s`);
    check(okCallbacks(baseline) && okCallbacks(burst), 'el audio no pierde bloques (callbacks por segundo = sample rate / buffer)', `${burst.callbacks} de ~${burst.expected}`);
    check(burst.overBudget === 0 && burst.xrunsDelta <= 0, 'ningún callback excede la duración del bloque ni hay xruns con 50 setParam/s', `máx ${burst.maxMs.toFixed(3)} ms / ${burst.blockMs.toFixed(2)} ms`);
    check(stress.errors === 0 && okCallbacks(stress), 'estrés 500 setParam/s: sin errores ni bloques perdidos (informativo)', `${stress.rate.toFixed(0)}/s, sobre presupuesto ${stress.overBudget}`);
    lv = (await http({ cmd: 'levels' })).json;
    check(lv.instances.find((i) => i.instanceId === padsId).peakDb > -80, 'la nota de Pads sigue sonando durante la ráfaga');
    await ws.send({ cmd: 'panic' });
    const pcount = ws.events.filter((e) => e.event === 'paramChanged').length;
    console.log(`  eventos paramChanged recibidos en total: ${pcount}`);

    // Throttle: during the 10 s burst the paramChanged events for Pads stay <= ~20 Hz
    const padsEvents = ws.events.filter((e) => e.event === 'paramChanged' && e.instanceId === padsId);
    let maxPerSecond = 0;
    for (const e of padsEvents) {
        const n = padsEvents.filter((x) => x.t >= e.t && x.t < e.t + 1000).length;
        maxPerSecond = Math.max(maxPerSecond, n);
    }
    check(maxPerSecond <= 22, 'paramChanged limitado a ~20 por segundo por instancia', `máximo ${maxPerSecond} en 1 s`);

    // Second client sees the count
    const ws2 = await new Client(`ws://127.0.0.1:${port}/ws${q}`).open();
    const s2 = await ws2.send({ cmd: 'status' });
    check(s2.api.clients === 2, 'dos clientes WebSocket', `${s2.api.clients}`);
    ws2.close();
    ws.close();
    await sleep(500);
    r = await http({ cmd: 'status' });
    check(r.json.api.clients === 0, 'clientes vuelve a 0 al cerrar los WebSockets');

    //--------------------------------------------------------------------------
    // Recording (startRecording / stopRecording): MP3 encoded while recording (LAME), optional WAV.
    // The files are checked structurally: ID3v2 tag, LAME "Info" / "Xing" frame, every MPEG-1
    // Layer III frame header up to the end, bitrate, and the MP3 duration against the WAV's.
    {
        const fs = await import('node:fs');
        const path = await import('node:path');
        const recDir = path.join(tmpDir, 'grabaciones');
        const wsr = await new Client(`ws://127.0.0.1:${port}/ws${q}`).open();

        // Parses an MP3: ID3v2 size, frames (sync, MPEG-1 Layer III), info tag, duration.
        function parseMp3(file) {
            const b = fs.readFileSync(file);
            const out = { ok: false, bytes: b.length, id3: false, frames: 0, bitrates: new Set(), sampleRate: 0, tag: '', title: '' };
            let pos = 0;
            if (b.toString('latin1', 0, 3) === 'ID3') {
                out.id3 = true;
                const size = (b[6] << 21) | (b[7] << 14) | (b[8] << 7) | b[9];
                const tagText = b.subarray(10, 10 + size);
                const tit = tagText.indexOf('TIT2');
                if (tit >= 0) {
                    const len = tagText.readUInt32BE(tit + 4);
                    const body = tagText.subarray(tit + 10, tit + 10 + len); // encoding byte + BOM + UTF-16LE
                    if (body[0] === 1) out.title = body.subarray(3).toString('utf16le').replace(/\u0000+$/, '');
                }
                pos = 10 + size;
            }
            const rates = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0];
            const srs = [44100, 48000, 32000, 0];
            let first = true;
            while (pos + 4 <= b.length) {
                const h = b.readUInt32BE(pos);
                if ((h >>> 21) !== 0x7ff || ((h >>> 19) & 3) !== 3 || ((h >>> 17) & 3) !== 1) return out; // not MPEG-1 Layer III
                const br = rates[(h >>> 12) & 15], sr = srs[(h >>> 10) & 3], pad = (h >>> 9) & 1;
                if (!br || !sr) return out;
                const len = Math.floor(144000 * br / sr) + pad;
                if (first) {
                    const frame = b.toString('latin1', pos, pos + len);
                    out.tag = frame.includes('Xing') ? 'Xing' : frame.includes('Info') ? 'Info' : '';
                    out.lame = frame.includes('LAME');
                    first = false;
                } else {
                    out.frames++;
                    out.bitrates.add(br);
                }
                out.sampleRate = sr;
                pos += len;
            }
            out.ok = pos === b.length;
            out.seconds = out.frames * 1152 / out.sampleRate;
            return out;
        }

        // 32-bit float stereo WAV: format, rate, duration, peak.
        function parseWav(file) {
            const b = fs.readFileSync(file);
            const out = { ok: false };
            if (b.toString('latin1', 0, 4) !== 'RIFF' || b.toString('latin1', 8, 12) !== 'WAVE') return out;
            let pos = 12;
            while (pos + 8 <= b.length) {
                const id = b.toString('latin1', pos, pos + 4), size = b.readUInt32LE(pos + 4);
                if (id === 'fmt ') {
                    out.format = b.readUInt16LE(pos + 8);
                    if (out.format === 0xfffe) out.format = b.readUInt16LE(pos + 8 + 24); // extensible: sub-format
                    out.channels = b.readUInt16LE(pos + 10);
                    out.sampleRate = b.readUInt32LE(pos + 12);
                    out.bits = b.readUInt16LE(pos + 22);
                } else if (id === 'data') {
                    const n = Math.min(size, b.length - pos - 8) / 4;
                    let peak = 0;
                    for (let i = 0; i < n; i++) peak = Math.max(peak, Math.abs(b.readFloatLE(pos + 8 + 4 * i)));
                    out.peak = peak;
                    out.seconds = n / (out.channels || 2) / out.sampleRate;
                    out.ok = true;
                    break;
                }
                pos += 8 + size + (size & 1);
            }
            return out;
        }

        r = await http({ cmd: 'stopRecording' });
        check(r.status === 409 && r.json.code === 'notRecording', 'stopRecording sin grabar: 409 notRecording');
        r = await http({ cmd: 'startRecording', format: 'ogg' });
        check(r.status === 400 && r.json.code === 'badRequest', 'startRecording con un formato desconocido: 400', r.json.error);

        since = wsr.events.length;
        r = await http({ cmd: 'startRecording', folder: recDir, format: 'cbr256', keepWav: true, name: 'Prueba grabación' });
        const rec = r.json.recording || {};
        check(r.json.ok && rec.active && rec.format === 'cbr256' && /Prueba grabación \d{4}-\d\d-\d\d \d\d-\d\d-\d\d\.mp3$/.test(rec.mp3File || '')
            && /\.wav$/.test(rec.wavFile || ''), 'startRecording: MP3 256 + WAV, nombre "<nombre> AAAA-MM-DD HH-MM-SS"', rec.mp3File);
        const started = await wsr.waitEvent((e) => e.event === 'recordingStarted', 1000, since);
        check(!!started && started.recording.mp3File === rec.mp3File, 'evento recordingStarted');
        r = await http({ cmd: 'startRecording', folder: recDir });
        check(r.status === 409 && r.json.code === 'alreadyRecording', 'startRecording mientras graba: 409 alreadyRecording');

        await http({ cmd: 'midi', instanceId: dexed.id, bytes: [0x90, 60, 100] });
        await sleep(1200);
        await http({ cmd: 'midi', instanceId: dexed.id, bytes: [0x80, 60, 0] });
        await sleep(600);
        const st = (await http({ cmd: 'status' })).json;
        check(st.recording && st.recording.active && st.recording.seconds > 1.5, 'status.recording mientras graba', `${st.recording && st.recording.seconds} s`);

        since = wsr.events.length;
        r = await http({ cmd: 'stopRecording' });
        const done = r.json.recording || {};
        check(r.json.ok && !done.active && r.json.reason === 'api' && r.json.mp3Bytes > 10000 && r.json.wavBytes > 100000
            && done.droppedSeconds === 0, 'stopRecording', `${done.seconds} s, MP3 ${r.json.mp3Bytes} B, WAV ${r.json.wavBytes} B, pico ${done.peakDb} dBFS`);
        const stopped = await wsr.waitEvent((e) => e.event === 'recordingStopped', 1000, since);
        check(!!stopped && stopped.reason === 'api' && stopped.mp3Bytes === r.json.mp3Bytes, 'evento recordingStopped');

        const mp3 = parseMp3(done.mp3File);
        const wav = parseWav(done.wavFile);
        check(mp3.ok && mp3.id3 && mp3.title === 'Prueba grabación' && mp3.tag === 'Info' && mp3.lame,
            'MP3: ID3v2 con el título, frame Info de LAME, frames MPEG-1 Layer III válidos hasta el final', `${mp3.frames} frames, título "${mp3.title}"`);
        check(mp3.bitrates.size === 1 && mp3.bitrates.has(256) && mp3.sampleRate === Math.round(st.audio.sampleRate),
            'MP3: 256 kbps CBR a la frecuencia del dispositivo', `${[...mp3.bitrates].join(',')} kbps, ${mp3.sampleRate} Hz`);
        check(wav.ok && wav.format === 3 && wav.channels === 2 && wav.bits === 32 && wav.sampleRate === mp3.sampleRate,
            'WAV: 32 bits float estéreo', `${wav.sampleRate} Hz`);
        check(wav.peak > 0.001, 'WAV: la nota de Dexed quedó grabada', `pico ${(20 * Math.log10(wav.peak || 1e-9)).toFixed(1)} dBFS`);
        check(Math.abs(wav.seconds - done.seconds) < 0.05 && Math.abs(mp3.seconds - wav.seconds) < 0.1,
            'duración: WAV = la informada, MP3 = WAV (± retardo del encoder)', `WAV ${wav.seconds.toFixed(3)} s, MP3 ${mp3.seconds.toFixed(3)} s`);

        // VBR V0 without the WAV, then WAV only.
        r = await http({ cmd: 'startRecording', folder: recDir, format: 'vbr0', keepWav: false, name: 'vbr' });
        const vbr = r.json.recording || {};
        await http({ cmd: 'midi', instanceId: dexed.id, bytes: [0x90, 64, 100] });
        await sleep(800);
        await http({ cmd: 'midi', instanceId: dexed.id, bytes: [0x80, 64, 0] });
        r = await http({ cmd: 'stopRecording' });
        const vbrMp3 = r.json.ok ? parseMp3(vbr.mp3File) : {};
        check(r.json.ok && vbr.wavFile === null && r.json.wavBytes === 0 && vbrMp3.ok && vbrMp3.tag === 'Xing' && vbrMp3.bitrates.size > 1,
            'MP3 VBR V0 sin WAV: frame Xing, bitrate variable, sin .wav', `${[...(vbrMp3.bitrates || [])].sort((a, b) => a - b).join(',')} kbps`);
        check(fs.readdirSync(recDir).filter((f) => f.startsWith('vbr ')).length === 1, 'VBR: un solo archivo en la carpeta');

        r = await http({ cmd: 'startRecording', folder: recDir, format: 'wav', name: 'solo wav' });
        const wavOnly = r.json.recording || {};
        await sleep(500);
        r = await http({ cmd: 'stopRecording' });
        check(r.json.ok && wavOnly.mp3File === null && /solo wav .*\.wav$/.test(wavOnly.wavFile || '') && parseWav(wavOnly.wavFile).ok
            && r.json.mp3Bytes === 0, 'formato wav: sólo el WAV');
        wsr.close();
        await http({ cmd: 'panic' });
        await sleep(300);
    }

    //--------------------------------------------------------------------------
    // examples/carrilion-client.js (the module a web page would use)
    const fs = await import('node:fs');
    const fileText = fs.readFileSync(new URL('../examples/carrilion-client.js', import.meta.url), 'utf8');
    const served = await fetch(base + '/client.js');
    const servedText = await served.text();
    check(served.status === 200 && /javascript/.test(served.headers.get('content-type')) && servedText === fileText
        && served.headers.get('access-control-allow-origin') === '*',
        'GET /client.js sirve examples/carrilion-client.js (sin token, con CORS)', `${servedText.length} bytes`);
    const client = await import(new URL('../examples/carrilion-client.js', import.meta.url).href);
    const vp = await client.connect(`ws://127.0.0.1:${port}/ws`, { token });
    const cst = await vp.status();
    check(cst.ok && cst.instances.length === 3, 'carrilion-client: connect() + status()');
    const cPads = cst.instances.find((i) => i.name === 'Pads');
    const seen = [];
    const offEv = vp.on('paramChanged', (e) => seen.push(e));
    await vp.setParam(cPads.id, target.name, 0.55);
    const cp = await vp.listParams(cPads.id, { filter: target.name });
    check(Math.abs(cp.find((p) => p.index === target.index).value - 0.55) < 1e-4, 'carrilion-client: setParam por nombre + listParams');
    await sleep(300);
    check(seen.some((e) => e.instanceId === cPads.id), 'carrilion-client: on("paramChanged")');
    offEv();
    let clientErr = null;
    try { await vp.setParam(cPads.id, 'no existe', 0.1); } catch (e) { clientErr = e; }
    check(clientErr instanceof client.VstPlayerError && clientErr.code === 'paramNotFound', 'carrilion-client: error como VstPlayerError con code');
    const cDexed = cst.instances.find((i) => i.name === 'Dexed');
    const port_ = client.vstPlayerPort(vp, cDexed.id);
    await vp.levels();
    port_.on(1, 62, 100, 0);
    port_.off(1, 62, port_.now() + 400);
    await sleep(250);
    const cl = await vp.levels();
    check(cl.instances.find((i) => i.instanceId === cDexed.id).peakDb > -80, 'carrilion-client: vstPlayerPort (interfaz de puerto de MidiOut) toca Dexed');
    await sleep(1500);
    port_.all_off();
    await sleep(600);
    await vp.levels();
    await sleep(200);
    const cl2 = await vp.levels();
    check(cl2.instances.find((i) => i.instanceId === cDexed.id).peakDb < -90, 'carrilion-client: el note-off programado y all_off dejan en silencio');
    const http1 = await client.httpRequest({ cmd: 'status' }, base, token);
    check(http1.ok, 'carrilion-client: httpRequest()');
    vp.close();

    //--------------------------------------------------------------------------
    // Routes with no port (tests/fixtures/sin-puerto: Piano <- ch 1 keep, Bajo <- ch 2, 3 remapTo 1,
    // both "inputDevice": ""): what a carrilion user with no MIDI device sets up. status lists
    // them, carrilion's VstPlayer output (route_table) plays them, and `midi` without instanceId
    // routes inside VstPlayer.
    {
        r = await http({ cmd: 'loadProject', path: examplesDir + sep + 'sin-puerto.vstproj.json', discardChanges: true });
        check(r.json.ok && r.json.instances.length === 2 && r.json.instances.every((i) => i.state === 'ready'), 'loadProject sin-puerto (2 Dexed, rutas sin puerto)',
            (r.json.instances || []).map((i) => i.name + ':' + i.state).join(', '));
        check(Array.isArray(r.json.warnings) ? r.json.warnings.length === 0 : true, 'sin-puerto: sin avisos (una ruta sin puerto no es un puerto faltante)', JSON.stringify(r.json.warnings));
        // Saved elsewhere right away: the final saveProject must not write into tests/fixtures.
        const noPortSaved = tmpDir + sep + 'api-sin-puerto.vstproj.json';
        r = await http({ cmd: 'saveProject', path: noPortSaved });
        check(r.json.ok, 'sin-puerto guardado en la carpeta temporal');
        const savedRoutes = JSON.parse((await import('node:fs')).readFileSync(noPortSaved, 'utf8')).midiRoutes;
        check(savedRoutes.length === 2 && savedRoutes.every((x) => x.inputDevice === ''), 'el archivo guardado conserva "inputDevice": "" en las dos rutas', JSON.stringify(savedRoutes.map((x) => x.inputDevice)));

        r = await http({ cmd: 'status' });
        const st = r.json;
        const byName = Object.fromEntries(st.instances.map((i) => [i.name, i]));
        const piano = byName['Piano'], bajo = byName['Bajo'];
        check(st.midiRoutes.length === 2 && st.midiRoutes.every((x) => x.inputDevice === ''), 'status.midiRoutes: 2 rutas con "inputDevice": ""',
            JSON.stringify(st.midiRoutes.map((x) => [x.inputDevice, x.channels, x.channelMode])));
        check(piano.midiRoutes.length === 1 && piano.midiRoutes[0].channels.join() === '1' && piano.midiRoutes[0].channelMode === 'keep'
            && bajo.midiRoutes[0].channels.join() === '2,3' && bajo.midiRoutes[0].channelMode.remapTo === 1,
            'status: rutas por instancia (Piano canal 1 keep, Bajo canales 2,3 remapTo 1)');
        for (const i of [piano, bajo]) await http({ cmd: 'setGain', instanceId: i.id, gainDb: -30 });

        // carrilion's own module when the sibling repo is there (the real client), else a copy of
        // its route_table rule: every route counts, whatever its device.
        const fsm = await import('node:fs');
        const carrilionModule = new URL('../../carrilion/apps/web/lib/core/vstplayer-port.js', import.meta.url);
        let carrilion = null;
        if (fsm.existsSync(carrilionModule)) {
            try { carrilion = await import(carrilionModule.href); } catch (e) { console.log('  (no se pudo importar el puerto de carrilion: ' + e.message + ')'); }
        }
        let cport = null, statusText = '';
        if (carrilion) {
            let routesRead = null;
            const routesReady = new Promise((res) => { routesRead = res; });
            cport = carrilion.create_vstplayer_port({ url: `ws://127.0.0.1:${port}/ws${q}`, on_status: (t) => { statusText = t; }, on_routes: () => routesRead() });
            await Promise.race([routesReady, sleep(5000)]);
            check(/1→Piano/.test(statusText) && /2→Bajo:1/.test(statusText) && /3→Bajo:1/.test(statusText),
                'puerto VstPlayer de carrilion (vstplayer-port.js): lee las rutas sin puerto', statusText);
        } else {
            console.log('  (sin ../carrilion: se usa una copia de route_table)');
            const table = [];
            for (let ch = 0; ch <= 16; ch++) table.push([]);
            for (const route of st.midiRoutes) {
                const remap = route.channelMode && typeof route.channelMode === 'object' ? Number(route.channelMode.remapTo) : 0;
                const sources = route.channels.length ? route.channels : [...Array(16).keys()].map((k) => k + 1);
                for (const src of sources) table[src].push({ id: route.instanceId, out: remap >= 1 && remap <= 16 ? remap : src });
            }
            const sendTo = (ch, statusNibble, d1, d2) => table[ch].forEach((t) => ws2send(t.id, [statusNibble | (t.out - 1), d1, d2]));
            const ws2send = (id, bytes) => http({ cmd: 'midi', instanceId: id, bytes });
            cport = { on: (ch, n, v) => sendTo(ch, 0x90, n, v), off: (ch, n) => sendTo(ch, 0x80, n, 0), close() { } };
            check(table[1].length === 1 && table[3][0]?.out === 1, 'copia de route_table: canal 1 -> Piano, canal 3 -> Bajo por el canal 1');
        }

        async function peaksAfter(play, ms = 450) {
            await http({ cmd: 'levels' });
            await play();
            await sleep(ms);
            const lvl = (await http({ cmd: 'levels' })).json;
            const peak = (id) => lvl.instances.find((i) => i.instanceId === id).peakDb;
            return { piano: peak(piano.id), bajo: peak(bajo.id) };
        }
        async function quiet() {
            await http({ cmd: 'panic' });
            await sleep(500);
        }

        let pk = await peaksAfter(async () => cport.on(1, 60, 100, 0));
        check(pk.piano > -80 && pk.bajo < -90, 'carrilion, canal 1 -> suena Piano (y no Bajo)', `Piano ${pk.piano.toFixed(1)}, Bajo ${pk.bajo.toFixed(1)} dBFS`);
        cport.off(1, 60, 0);
        await quiet();
        pk = await peaksAfter(async () => cport.on(3, 48, 100, 0));
        check(pk.bajo > -80 && pk.piano < -90, 'carrilion, canal 3 -> suena Bajo (remapTo 1) (y no Piano)', `Piano ${pk.piano.toFixed(1)}, Bajo ${pk.bajo.toFixed(1)} dBFS`);
        cport.off(3, 48, 0);
        cport.close();
        await quiet();

        // `midi` without instanceId: VstPlayer applies the routes itself.
        let routedReply = null;
        const ws3 = await new Client(`ws://127.0.0.1:${port}/ws${q}`).open(); // `ws` was closed above
        pk = await peaksAfter(async () => { routedReply = await ws3.send({ cmd: 'midi', bytes: [0x92, 50, 100] }); });
        ws3.close();
        check(routedReply.ok && routedReply.routed === 1 && routedReply.messages === 1 && !('instanceId' in routedReply),
            'midi sin instanceId (WebSocket): ruteado por VstPlayer, 1 entrega', JSON.stringify(routedReply));
        check(pk.bajo > -80 && pk.piano < -90, 'midi sin instanceId, canal 3 -> suena Bajo', `Piano ${pk.piano.toFixed(1)}, Bajo ${pk.bajo.toFixed(1)} dBFS`);
        r = await http({ cmd: 'midi', bytes: [0x82, 50, 0] });
        check(r.json.ok && r.json.routed === 1, 'midi sin instanceId (HTTP): note-off ruteado');
        await quiet();
        r = await http({ cmd: 'midi', bytes: [0x94, 60, 100, 0x84, 60, 0] });
        check(r.json.ok && r.json.routed === 0 && r.json.messages === 2, 'midi sin instanceId por un canal sin ruta: routed 0', JSON.stringify(r.json));
        r = await http({ cmd: 'midi', instanceId: '', bytes: [0x90, 60, 100] });
        check(r.status === 400 && r.json.code === 'badRequest', 'midi con instanceId vacío: 400 (para rutear, omitirlo)');
        r = await http({ cmd: 'midi', bytes: [0xf8] });
        check(r.status === 400 && r.json.code === 'badMidi', 'midi sin instanceId con un mensaje de sistema: 400 badMidi');
    }

    // Leave a clean, unmodified project so the app closes without questions.
    await http({ cmd: 'saveProject' });
}

try {
    await main();
} catch (e) {
    failures++;
    console.log('  FALLO excepción: ' + (e.stack || e));
}
console.log(`\n${checks} comprobaciones, ${failures} fallos`);
process.exit(failures === 0 ? 0 : 1);
