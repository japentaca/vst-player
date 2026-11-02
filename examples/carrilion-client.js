// VstPlayer local API client (ES module, no dependencies; browser or Node 22+).
// Protocol: docs/api.md. VstPlayer listens on 127.0.0.1:9471 (HTTP POST /api and WebSocket /ws).
//
// Quick use from the browser console of a page (e.g. carrilion):
//
//   const { connect } = await import('http://127.0.0.1:9471/client.js')  // VstPlayer serves this file
//   const vp = await connect()                         // ws://127.0.0.1:9471/ws  (connect(url, { token }))
//   const st = await vp.status()                       // project, audio, instances, routes
//   const pads = st.instances.find((i) => i.plugin.name === 'Surge XT')
//   const params = await vp.listParams(pads.id, { filter: 'M1' })
//   await vp.setParam(pads.id, params[0].index, 0.8)   // by index ...
//   await vp.setParam(pads.id, 'M1: -', 0.2)           // ... or by name
//   vp.on('paramChanged', (e) => console.log(e.instanceId, e.params))
//   await vp.midi(pads.id, [0x90, 60, 100])            // direct MIDI (no loopMIDI needed)
//   await vp.panic()
//   await vp.startRecording({ name: 'Ensayo' })      // ... await vp.stopRecording() -> { recording: { file } }
//   await vp.loadProject('C:\\Users\\me\\Documents\\pads.vstproj.json')
//
// carrilion has its own port now (apps/web/lib/core/vstplayer-port.js, "VstPlayer" in its Midi Out
// selector: applies VstPlayer's MIDI routes and sends CC7 / CC10). vstPlayerPort() below is the
// simpler form (one instance, no routes) of a MidiOut port { now(), on(ch, note, velocity, time),
// off(ch, note, time), all_off() }:
//
//   import { connect, vstPlayerPort } from './carrilion-client.js'
//   const vp = await connect()
//   const st = await vp.status()
//   midi_out.set_port(vstPlayerPort(vp, st.instances[0].id))
//
// Timing: the API has no timestamps; vstPlayerPort schedules future events with setTimeout, so
// expect a few ms of jitter (loopMIDI keeps the MIDI path; the API is meant for control:
// parameters, programs, projects, panic).

export const DEFAULT_URL = 'ws://127.0.0.1:9471/ws'

export class VstPlayerError extends Error {
    constructor(reply) {
        super(reply.error || 'error')
        this.code = reply.code
        this.reply = reply
    }
}

export class VstPlayerClient {
    constructor(ws) {
        this.ws = ws
        this.nextId = 1
        this.pending = new Map()
        this.handlers = new Map() // event name ('*' = all) -> Set of functions
        this.onclose = null

        ws.onmessage = (m) => {
            let msg
            try { msg = JSON.parse(m.data) } catch { return }

            if (msg.event) {
                for (const name of [msg.event, '*'])
                    for (const fn of this.handlers.get(name) || []) {
                        try { fn(msg) } catch (e) { console.error(e) }
                    }
                return
            }

            const p = this.pending.get(msg.id)
            if (!p) return
            this.pending.delete(msg.id)
            clearTimeout(p.timer)
            if (msg.ok) p.resolve(msg)
            else p.reject(new VstPlayerError(msg))
        }

        ws.onclose = (e) => {
            for (const p of this.pending.values()) {
                clearTimeout(p.timer)
                p.reject(new VstPlayerError({ code: 'closed', error: 'conexión cerrada' }))
            }
            this.pending.clear()
            if (this.onclose) this.onclose(e)
        }
    }

    get connected() { return this.ws.readyState === 1 }

    // Sends {cmd, ...params}; resolves with the reply (ok: true) or rejects with VstPlayerError.
    request(cmd, params = {}, timeoutMs = 130000) {
        const id = this.nextId++
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                this.pending.delete(id)
                reject(new VstPlayerError({ code: 'timeout', error: `sin respuesta a ${cmd}` }))
            }, timeoutMs)
            this.pending.set(id, { resolve, reject, timer })
            this.ws.send(JSON.stringify({ ...params, cmd, id }))
        })
    }

    // Events: instanceAdded, instanceRemoved, instanceLoaded, paramChanged, projectLoaded,
    // midiActivity, or '*' for all. Returns a function that removes the handler.
    on(event, fn) {
        if (!this.handlers.has(event)) this.handlers.set(event, new Set())
        this.handlers.get(event).add(fn)
        return () => this.off(event, fn)
    }

    off(event, fn) { this.handlers.get(event)?.delete(fn) }

    close() { this.ws.close() }

    //--------------------------------------------------------------------------
    status() { return this.request('status') }

    // opts: { filter, offset, limit, refresh }. Resolves with the parameter array.
    async listParams(instanceId, opts = {}) {
        return (await this.request('listParams', { instanceId, ...opts })).params
    }

    // param: index (number), name (string) or { paramId }. value: normalized 0..1.
    setParam(instanceId, param, value) {
        const which = typeof param === 'number' ? { index: param }
            : typeof param === 'string' ? { name: param } : param
        return this.request('setParam', { instanceId, ...which, value })
    }

    getParam(instanceId, param) {
        const which = typeof param === 'number' ? { index: param }
            : typeof param === 'string' ? { name: param } : param
        return this.request('getParam', { instanceId, ...which })
    }

    setGain(instanceId, gainDb) { return this.request('setGain', { instanceId, gainDb }) }
    setMute(instanceId, mute) { return this.request('setMute', { instanceId, mute }) }
    panic() { return this.request('panic') }

    async listPrograms(instanceId) { return this.request('listPrograms', { instanceId }) }
    setProgram(instanceId, index) { return this.request('setProgram', { instanceId, index }) }

    // opts: { discardChanges: true } to drop unsaved changes of the current project.
    loadProject(path, opts = {}) { return this.request('loadProject', { path, ...opts }) }
    listProjects(dir, opts = {}) { return this.request('listProjects', { ...(dir ? { dir } : {}), ...opts }) }
    saveProject(path) { return this.request('saveProject', path ? { path } : {}) }

    // Recording of what VstPlayer plays. opts (all optional; default = settings.xml):
    // { format: 'cbr256' | 'cbr320' | 'cbr192' | 'vbr0' | 'vbr2' | 'wav', keepWav, folder, name }.
    // Events: recordingStarted, recordingStopped ({ recording, reason, mp3Bytes, wavBytes }).
    startRecording(opts = {}) { return this.request('startRecording', opts) }
    stopRecording() { return this.request('stopRecording') }

    // bytes: one or more channel messages, e.g. [0x90, 60, 100] or [0x80, 60, 0, 0x90, 64, 90].
    midi(instanceId, bytes) { return this.request('midi', { instanceId, bytes }) }

    levels() { return this.request('levels') }
    audioStats() { return this.request('audioStats') }
}

// url: ws://127.0.0.1:9471/ws by default. opts: { token, timeoutMs }.
export function connect(url = DEFAULT_URL, opts = {}) {
    const full = opts.token ? `${url}${url.includes('?') ? '&' : '?'}token=${encodeURIComponent(opts.token)}` : url
    return new Promise((resolve, reject) => {
        const ws = new WebSocket(full)
        const timer = setTimeout(() => { ws.close(); reject(new Error(`VstPlayer no responde en ${url}`)) }, opts.timeoutMs || 5000)
        ws.onopen = () => { clearTimeout(timer); resolve(new VstPlayerClient(ws)) }
        ws.onerror = () => { clearTimeout(timer); reject(new Error(`No se pudo conectar a ${url} (¿VstPlayer abierto?)`)) }
    })
}

// One-off HTTP request without a WebSocket: await httpRequest({ cmd: 'panic' }).
export async function httpRequest(body, base = 'http://127.0.0.1:9471', token = '') {
    const res = await fetch(`${base}/api${token ? `?token=${encodeURIComponent(token)}` : ''}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
    })
    const reply = await res.json()
    if (!reply.ok) throw new VstPlayerError(reply)
    return reply
}

// A carrilion MidiOut port that plays a VstPlayer instance through the API.
// Times are performance.now() milliseconds (0 = now); future events use setTimeout.
export function vstPlayerPort(client, instanceId) {
    const send = (bytes, time) => {
        const delay = time > 0 ? time - performance.now() : 0
        const go = () => client.midi(instanceId, bytes).catch(() => {})
        if (delay > 1) setTimeout(go, delay)
        else go()
    }
    return {
        now: () => performance.now(),
        on: (ch, note, velocity, time) => send([0x90 | (ch - 1), note, velocity], time),
        off: (ch, note, time) => send([0x80 | (ch - 1), note, 0], time),
        all_off: () => { client.panic().catch(() => {}) },
    }
}
