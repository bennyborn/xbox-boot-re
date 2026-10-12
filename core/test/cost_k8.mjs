// Time of the wasm32 build for docs/core.md 6 (milestone K8):
//
//     node core/test/cost_k8.mjs <xboot.wasm> <pack> [runs]
//
// The run of cost_k8.c at 60 Hz (17, 17, 16 ms): around every call of
// xb_advance, xb_frame, xb_blob_deformed (in frames that draw the blob's
// mesh) and xb_audio the clock of node, summed over the frames, the
// smallest sum of `runs` runs (default 20). The first runs warm the
// engine's compiler up and never win. Node's clock steps in microseconds or
// coarser, so a sum over 480 calls of a microsecond each is rough; frame,
// mesh and sound are long enough.
import { readFileSync } from 'node:fs';
import { performance } from 'node:perf_hooks';

const [wasmPath, packPath, runsArg] = process.argv.slice(2);
const runs = Number(runsArg || 20);
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), {});
const x = instance.exports;
const pack = readFileSync(packPath);

const align = (n) => (n + 15) & ~15;
const DRAW = 112, FRAME = 156, DRAWS_MAX = 1320, PARAMS_MAX = 40960, BLOB_VERTICES = 1734, MESH_BLOB = 7;
const stateAt = align(x.__heap_base.value);
const soundAt = align(stateAt + x.xb_state_size());
const packAt = align(soundAt + x.xb_audio_size());
const cfgAt = align(packAt + pack.length);
const outAt = align(cfgAt + 16);
const drawsAt = align(outAt + FRAME);
const paramsAt = align(drawsAt + DRAW * DRAWS_MAX);
const meshAt = align(paramsAt + 4 * PARAMS_MAX);
const samplesAt = align(meshAt + 16 * BLOB_VERTICES);
const need = samplesAt + 4 * 48 * 17 + 16;
if (need > x.memory.buffer.byteLength)
    x.memory.grow(Math.ceil((need - x.memory.buffer.byteLength) / 65536));
new Uint8Array(x.memory.buffer, packAt, pack.length).set(pack);
const dv = new DataView(x.memory.buffer);

function run() {
    const steps = [17, 17, 16];
    const us = { advance: 0, frame: 0, mesh: 0, sound: 0 };
    dv.setUint32(cfgAt, 0, true);
    dv.setUint32(cfgAt + 4, 0, true);
    if (x.xb_init(stateAt, packAt, pack.length, cfgAt) !== 0 || x.xb_audio_init(stateAt, soundAt, x.xb_audio_size()) !== 0)
        throw new Error('set-up failed');
    let tick = 0, rc = 0, frames = 0, done = 0;
    do {
        tick += steps[frames % 3];
        let a = performance.now();
        rc = x.xb_advance(stateAt, tick);
        let b = performance.now();
        us.advance += b - a;
        new Uint8Array(x.memory.buffer, outAt, FRAME).fill(0);
        dv.setUint32(outAt + 128, drawsAt, true);
        dv.setUint32(outAt + 132, DRAWS_MAX, true);
        dv.setUint32(outAt + 140, paramsAt, true);
        dv.setUint32(outAt + 144, PARAMS_MAX, true);
        a = performance.now();
        if (x.xb_frame(stateAt, outAt) !== 0)
            throw new Error('xb_frame failed');
        b = performance.now();
        us.frame += b - a;
        const nd = dv.getUint32(outAt + 136, true);
        let blob = false;
        for (let i = 0; i < nd && !blob; i++)
            blob = dv.getUint16(drawsAt + DRAW * i + 4, true) === MESH_BLOB;
        if (blob) {
            a = performance.now();
            x.xb_blob_deformed(stateAt, meshAt);
            b = performance.now();
            us.mesh += b - a;
        }
        const n = tick * 48 - done;
        a = performance.now();
        x.xb_audio(stateAt, samplesAt, n);
        b = performance.now();
        us.sound += b - a;
        done += n;
        frames++;
    } while (rc === 0 && frames < 100000);
    for (const k of Object.keys(us))
        us[k] *= 1000;
    us.frames = frames;
    return us;
}

let best = null;
for (let i = 0; i < runs; i++) {
    const r = run();
    if (!best)
        best = r;
    for (const k of ['advance', 'frame', 'mesh', 'sound'])
        best[k] = Math.min(best[k], r[k]);
}
const n = best.frames, all = best.advance + best.frame + best.mesh + best.sound;
console.log(`wasm32 in node ${process.versions.node} (V8 ${process.versions.v8}), smallest of ${runs} runs, ${n} frames:`);
console.log(`mean per frame in microseconds: advance ${(best.advance / n).toFixed(1)}, frame ${(best.frame / n).toFixed(1)}, ` +
            `mesh ${(best.mesh / n).toFixed(1)}, sound ${(best.sound / n).toFixed(1)}, together ${(all / n).toFixed(1)}`);
console.log(`the whole run: ${(all / 1000).toFixed(1)} ms of the core for ${n} frames and 8.0 s of sound`);
