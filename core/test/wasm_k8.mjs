// Runs the whole run of K8 against the wasm32 build:
//
//     node core/test/wasm_k8.mjs <xboot.wasm> <pack>
//
// The run of hosts/native/run.c, call for call: xb_init, xb_audio_init, the
// seven passes of the set-up, then at 60 Hz (17, 17, 16 ms) every frame
// with its draw list, the deformed mesh where the blob is drawn and the
// samples that fall into the frame, folded as run_whole folds them. Prints
// "digest <hex> <frames> <samples>" for core/build.py, which compares it
// with test_k8 and with the native host. The other checks of test_k8.c that
// make sense here follow: the run again, guard bytes, the pack unchanged.
//
// Layouts of xboot.h on wasm32. xb_frame_out 156 bytes: view @0, proj @64,
// draws @128, draw_cap @132, draw_count @136, params @140, param_cap @144,
// param_count @148, clear_depth @152. xb_draw 112 bytes, mesh @4 (u16).
// xb_timing 176 bytes: t @0, x18 @4, x1c @8, x20 @12, x24 @16. xb_track 20
// bytes: four float32, fx_draws @16. xb_logo 40 bytes: tm @0, texture @4,
// phase @8, rtt @12, pos @24, alpha @36. xb_blob 2552 bytes: draws @4,
// n_bulges @8, n_drops @12, flat @16.
import { readFileSync } from 'node:fs';

const [wasmPath, packPath] = process.argv.slice(2);
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), {});
const x = instance.exports;
const pack = readFileSync(packPath);

let checks = 0, failed = 0;
const check = (ok, what) => { checks++; if (!ok) failed++; console.log(`${ok ? 'ok  ' : 'FAIL'}  ${what}`); };
const align = (n) => (n + 15) & ~15;
const DRAW = 112, FRAME = 156, TRACK = 20, TIMING = 176, LOGO = 40, BLOB = 2552;
const DRAWS_MAX = 1320, PARAMS_MAX = 40960, PASSES = 7, BLOB_VERTICES = 1734, MESH_BLOB = 7;
const SAMPLES_MAX = 48000, GUARD = 64, GUARD_BYTE = 0xA5;

// every block gets GUARD bytes behind it, as in test_k8.c
const blocks = [];
let top = align(x.__heap_base.value);
function block(size) {
    const at = top;
    blocks.push([at, size]);
    top = align(at + size + GUARD);
    return at;
}
const stateSize = x.xb_state_size(), soundSize = x.xb_audio_size();
const stateAt = block(stateSize);
const soundAt = block(soundSize);
const packAt = block(pack.length);
const cfgAt = block(8);
const outAt = block(FRAME);
const drawsAt = block(DRAW * DRAWS_MAX);
const paramsAt = block(4 * PARAMS_MAX);
const meshAt = block(16 * BLOB_VERTICES);
const samplesAt = block(4 * SAMPLES_MAX);
const tmAt = block(TIMING);
const trackAt = block(TRACK);
const logoAt = block(LOGO);
const blobAt = block(BLOB);
if (top > x.memory.buffer.byteLength)
    x.memory.grow(Math.ceil((top - x.memory.buffer.byteLength) / 65536));
new Uint8Array(x.memory.buffer, packAt, pack.length).set(pack);
for (const [at, size] of blocks)
    new Uint8Array(x.memory.buffer, at + size, GUARD).fill(GUARD_BYTE);
console.log(`state ${stateSize} bytes, sound ${soundSize} bytes, pack ${pack.length} bytes, memory ${x.memory.buffer.byteLength} bytes`);

const dv = new DataView(x.memory.buffer);   // the memory does not grow after this
const u32 = (at) => dv.getUint32(at, true);

// FNV-1a, 64 bit, in two halves: the prime is 2^40 + 0x1B3
let lo = 0, hi = 0;
function fold(u) {
    for (let i = 0; i < 4; i++) {
        lo = (lo ^ ((u >>> (8 * i)) & 0xFF)) >>> 0;
        const t0 = (lo & 0xFFFF) * 0x1B3, t1 = (lo >>> 16) * 0x1B3 + (t0 >>> 16);
        hi = (Math.imul(hi, 0x1B3) + (t1 >>> 16) + (lo << 8)) >>> 0;
        lo = (((t1 & 0xFFFF) << 16) | (t0 & 0xFFFF)) >>> 0;
    }
}
// a float32 at `at`, every NaN as one pattern
function foldReal(at) {
    const u = u32(at);
    fold((u & 0x7FFFFFFF) > 0x7F800000 ? 0x7FC00000 : u);
}
function foldFrame() {
    const nd = u32(outAt + 136), np = u32(outAt + 148);
    fold(nd);
    fold(np);
    fold(u32(outAt + 152));
    for (let i = 0; i < 16; i++) {
        foldReal(outAt + 4 * i);
        foldReal(outAt + 64 + 4 * i);
    }
    for (let i = 0; i < nd * (DRAW / 4); i++)
        fold(u32(drawsAt + 4 * i));
    for (let i = 0; i < np; i++)
        foldReal(paramsAt + 4 * i);
}
function buffers() {
    new Uint8Array(x.memory.buffer, outAt, FRAME).fill(0);
    dv.setUint32(outAt + 128, drawsAt, true);
    dv.setUint32(outAt + 132, DRAWS_MAX, true);
    dv.setUint32(outAt + 140, paramsAt, true);
    dv.setUint32(outAt + 144, PARAMS_MAX, true);
}

function run(sound) {
    const steps = [17, 17, 16];
    const r = { ok: true, frames: 0, samples: 0, draws: 0, maxDraws: 0, blobFrames: 0, doneAt: -1, tEnd: 0 };
    lo = 0x84222325;
    hi = 0xCBF29CE4;
    dv.setUint32(cfgAt, 0, true);
    dv.setUint32(cfgAt + 4, 0, true);
    r.ok = x.xb_init(stateAt, packAt, pack.length, cfgAt) === 0;
    if (sound)
        r.ok = r.ok && x.xb_audio_init(stateAt, soundAt, soundSize) === 0;
    for (let p = 0; p < PASSES; p++) {
        buffers();
        r.ok = r.ok && x.xb_frame_setup(stateAt, p, outAt) === 0;
        foldFrame();
    }
    let tick = 0, rc = 0;
    do {
        tick += steps[r.frames % 3];
        rc = x.xb_advance(stateAt, tick);
        buffers();
        r.ok = r.ok && rc >= 0 && x.xb_frame(stateAt, outAt) === 0 && x.xb_get_timing(stateAt, tmAt) === 0 &&
               x.xb_get_track(stateAt, trackAt) === 0 && x.xb_get_logo(stateAt, logoAt) === 0 &&
               x.xb_get_blob(stateAt, blobAt) === 0;
        const nd = u32(outAt + 136);
        let blob = false;
        for (let i = 0; i < nd && !blob; i++)
            blob = dv.getUint16(drawsAt + DRAW * i + 4, true) === MESH_BLOB;
        if (blob) {
            r.ok = r.ok && x.xb_blob_deformed(stateAt, meshAt) === 0;
            r.blobFrames++;
        }
        let n = 0;
        if (sound) {
            n = tick * 48 - r.samples;
            r.ok = r.ok && x.xb_audio(stateAt, samplesAt, n) === 0;
            r.samples += n;
        }
        fold(tick);
        fold(rc);
        for (let i = 0; i < 5; i++)
            foldReal(tmAt + 4 * i);
        for (let i = 0; i < 4; i++)
            foldReal(trackAt + 4 * i);
        fold(u32(trackAt + 16));
        fold((u32(logoAt) << 3 | u32(logoAt + 4) << 2 | u32(logoAt + 8) << 1 | u32(logoAt + 12)) >>> 0);
        for (let i = 0; i < 4; i++)
            foldReal(logoAt + 24 + 4 * i);
        fold((u32(blobAt + 8) << 8 | u32(blobAt + 12)) >>> 0);
        fold(u32(blobAt + 4));
        fold(u32(blobAt + 16));
        foldFrame();
        if (blob)
            for (let i = 0; i < 4 * BLOB_VERTICES; i++)
                foldReal(meshAt + 4 * i);
        for (let i = 0; i < 2 * n; i++)
            fold(dv.getUint16(samplesAt + 2 * i, true));
        r.draws += nd;
        r.maxDraws = Math.max(r.maxDraws, nd);
        if (rc === 1 && r.doneAt < 0)
            r.doneAt = r.frames;
        r.tEnd = dv.getFloat32(tmAt, true);
        r.frames++;
    } while (rc === 0 && r.frames < 100000);
    r.digest = hi.toString(16).toUpperCase().padStart(8, '0') + lo.toString(16).toUpperCase().padStart(8, '0');
    return r;
}

const a = run(true);
check(a.ok && a.frames === 480 && a.doneAt === 479 && a.samples === 384000 && a.tEnd >= 8,
      `run at 60 Hz with sound: ${a.frames} frames, XB_DONE at frame ${a.doneAt}, t = ${a.tEnd.toFixed(6)}, ` +
      `${a.samples} samples, ${a.draws} draws, largest frame ${a.maxDraws}, the blob's mesh in ${a.blobFrames} frames`);
const b = run(true);
check(b.ok && a.digest === b.digest, `the same run again: digest ${a.digest} and ${b.digest}`);
const c = run(false);
check(c.ok && c.frames === 480 && c.draws === a.draws && c.digest !== a.digest,
      `without sound: ${c.frames} frames, ${c.draws} draws, digest ${c.digest}`);
let guards = true;
for (const [at, size] of blocks)
    for (let i = 0; i < GUARD; i++)
        guards = guards && dv.getUint8(at + size + i) === GUARD_BYTE;
check(guards, `guard bytes behind ${blocks.length} blocks untouched after all runs`);
check(Buffer.compare(Buffer.from(x.memory.buffer, packAt, pack.length), pack) === 0, 'the pack is unchanged after all runs');

console.log(`digest ${a.digest} ${a.frames} ${a.samples}`);
console.log(`${checks} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
