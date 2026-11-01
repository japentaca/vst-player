// Stage 7 API test (Node 22+: global fetch and WebSocket, no dependencies).
// Driven by tools/run-api-test.ps1, which starts VstPlayer.exe --api-test with a temporary data
// folder, runs this script and then closes the app (checking its exit code, assertions, leaks).
//
//   node tools/api-test.mjs --port 9479 [--token T] --examples <dir> --tmp <dir>
//
// Exit code 0 = every check passed. Prints one line per check plus the measurements.

const args = Object.fromEntries(
    process.argv.slice(2).reduce((acc, a, i, all) => (a.startsWith('--') ? [...acc, [a.slice(2), all[i + 1]]] : acc), []));

const port = Number(args.port || 9479);
const token = args.token || '';
const examplesDir = args.examples;
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
    check(r.json.ok && names.includes('prueba-etapa5') && names.includes('plugin-faltante'), 'listProjects de examples', names.join(', '));
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
