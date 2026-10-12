// Runs the K3 checks of test_k3.c against the wasm32 build:
//
//     node core/test/wasm_k3.mjs <xboot.wasm> <pack> <c6_blob.json>
//
// For each run of the vector file the replay through xb_blob_update and,
// for the runs without a restart, the run through xb_advance_free, with
// the comparisons of test_k3.c; then the digest of a run over 8 s at
// 60 Hz with the algorithm of test_k3.c. Prints "digest <hex> <frames>"
// for core/build.py, which compares it with the native build.
//
// Layouts of xboot.h on wasm32, all fields 4 bytes: xb_blob 2552 bytes
// (rng, draws, n_bulges, n_drops, flat, updates, center @24, radius @36,
// colour @40, bulges @56 of 64 bytes, drops @2104 of 56 bytes);
// xb_bulge: size, size2, inv2, h, c @16, mask @28, dir @32, t0 @44,
// freq @48, amp @52, drop @56 (int32), flag @60; xb_drop: size,
// pos @4, neg @16, dir @20, t0 @32, freq @36, amp @40, disp @44,
// scale @48, rate @52; xb_blob_params 988 bytes (k, colour @4, glow @8,
// mesh_vs @88, mesh_ps @296, drop_vs @328, drop_ps @424, drops @472,
// drop @476); xb_timing 176 bytes (t, x18, x1c @8, x20 @12); xb_camera
// 248 bytes.
import { readFileSync } from 'node:fs';

const [wasmPath, packPath, vecPath] = process.argv.slice(2);
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), {});
const x = instance.exports;
const pack = readFileSync(packPath);
const vec = JSON.parse(readFileSync(vecPath, 'utf8'));

let checks = 0, failed = 0;
const check = (ok, what) => { checks++; if (!ok) failed++; console.log(`${ok ? 'ok  ' : 'FAIL'}  ${what}`); };
const align = (n) => (n + 15) & ~15;
const BLOB = 2552, PARAMS = 988, TIMING = 176, CAM = 248, VERTS = 1734;

const stateAt = align(x.__heap_base.value);
const packAt = align(stateAt + x.xb_state_size());
const cfgAt = align(packAt + pack.length);
const blobAt = align(cfgAt + 16);
const parAt = align(blobAt + BLOB);
const tmAt = align(parAt + PARAMS);
const camAt = align(tmAt + TIMING);
const lvAt = align(camAt + CAM);
const defAt = align(lvAt + 64);
const unitAt = align(defAt + 16 * VERTS);
const idxAt = align(unitAt + 12 * VERTS);
const need = idxAt + 2 * 4054 + 16;
if (need > x.memory.buffer.byteLength)
    x.memory.grow(Math.ceil((need - x.memory.buffer.byteLength) / 65536));
new Uint8Array(x.memory.buffer, packAt, pack.length).set(pack);
console.log(`state ${x.xb_state_size()} bytes, pack ${pack.length} bytes, memory ${x.memory.buffer.byteLength} bytes`);

const dv = () => new DataView(x.memory.buffer);
const f32s = (at, n) => Array.from({ length: n }, (_, i) => dv().getFloat32(at + 4 * i, true));
const u32 = (at) => dv().getUint32(at, true);
const fbits = (f) => { const b = new DataView(new ArrayBuffer(4)); b.setFloat32(0, f, true); return b.getUint32(0, true); };
const same = (a, b) => fbits(Math.fround(a)) === fbits(b);
const listEq = (want, got) => want.length === got.length && want.every((v, i) => same(v, got[i]));
const fr = Math.fround;

function init() {
    dv().setUint32(cfgAt, 0, true);
    dv().setUint32(cfgAt + 4, 0, true);
    return x.xb_init(stateAt, packAt, pack.length, cfgAt);
}

function blob() {
    x.xb_get_blob(stateAt, blobAt);
    const b = { rng: u32(blobAt), draws: u32(blobAt + 4), nb: u32(blobAt + 8), nd: u32(blobAt + 12),
                flat: u32(blobAt + 16), updates: u32(blobAt + 20), bulges: [], drops: [] };
    for (let i = 0; i < b.nb; i++) {
        const o = blobAt + 56 + 64 * i, f = f32s(o, 16);
        b.bulges.push({ size: f[0], h: f[3], dir: f.slice(8, 11), mask: u32(o + 28), t0: f[11], freq: f[12],
                        amp: f[13], drop: dv().getInt32(o + 56, true) });
    }
    for (let i = 0; i < b.nd; i++) {
        const f = f32s(blobAt + 2104 + 56 * i, 14);
        b.drops.push({ size: f[0], pos: f.slice(1, 4), dir: f.slice(5, 8), t0: f[8], freq: f[9], amp: f[10],
                       disp: f[11], scale: f[12], rate: f[13] });
    }
    return b;
}

// Returns '' if the capture is met, else what differs first.
function captureBad(c, t, x1c, x20, quad) {
    const b = blob();
    x.xb_camera_at(stateAt, t, camAt);
    x.xb_blob_draw(stateAt, camAt, t, x1c, x20, parAt);
    if (c.bulges.length !== b.nb || c.drops.length !== b.nd) return `counts ${b.nb}, ${b.nd}`;
    for (const [i, w] of c.bulges.entries()) {
        const g = b.bulges[i];
        if (!same(w.size, g.size) || !same(w.h, g.h) || !listEq(w.dir, g.dir) || w.mask !== g.mask ||
            !same(w.t0, g.t0) || !same(w.freq, g.freq) || !same(w.amp, g.amp) || (w.drop ?? -1) !== g.drop)
            return `bulge ${i}`;
    }
    for (const [i, w] of c.drops.entries()) {
        const p = b.drops[i];
        for (const k of ['size', 'disp', 'scale', 'rate', 't0', 'freq', 'amp'])
            if (!same(w[k], p[k])) return `drop ${i} ${k}`;
        if (!listEq(w.pos, p.pos) || !listEq(w.dir, p.dir)) return `drop ${i}`;
    }
    const k = f32s(parAt, 1)[0], colour = '0x' + u32(parAt + 4).toString(16).padStart(8, '0');
    if (!same(c.glow.k, k) || c.glow.colour !== colour) return `glow k, colour ${colour}`;
    if (quad && !c.glow.quad.every((v, i) => listEq(v, f32s(parAt + 8 + 20 * i, 5)))) return 'glow quad';
    if (!listEq(c.constants.vs_c10, f32s(parAt + 88 + 160, 4)) || !listEq(c.constants.ps1_c0, f32s(parAt + 296, 4)) ||
        !listEq(c.constants.ps2_c0, f32s(parAt + 424, 4))) return 'colour constants';
    const dc = c.constants.drops_c10_c13;
    if (dc.length !== u32(parAt + 472) || !dc.every((v, i) => listEq(v, f32s(parAt + 476 + 64 * i, 16))))
        return 'drop constants';
    x.xb_blob_deformed(stateAt, defAt);
    const w = f32s(defAt, 4 * VERTS).filter((_, i) => i % 4 === 3);
    let lo = w[0], hi = w[0];
    for (const v of w) { if (v < lo) lo = v; if (v > hi) hi = v; }
    if (!same(c.mesh_extent[0], lo) || !same(c.mesh_extent[1], hi)) return 'height in the mesh';
    return '';
}

check(init() === 0, 'xb_init');
const b0 = blob();
check(b0.nb === 32 && b0.nd === 8 && b0.draws === 208 && b0.flat === 1,
      `after set-up: ${b0.nb} bulges, ${b0.nd} drops, ${b0.draws} generator steps, mesh undeformed`);
check(vec.runs.length === 5, 'c6_blob.json has five runs');
for (const run of vec.runs) {
    const ev = (n, what) => run.events.find((e) => e.frame === n && e.event === what);
    const caps = new Map(run.captures.map((c) => [c.frame, c]));
    // replay
    init();
    let prev = -1, restarts = 0, restartBad = 0, ok = 0, why = '';
    run.steps_t_dt.forEach(([t, dt], n) => {
        t = fr(t); dt = fr(dt);
        if (t < prev) {
            x.xb_blob_restart(stateAt);
            const e = ev(n, 'blob_restart');
            restarts++;
            if (!e || blob().rng !== parseInt(e.rand_after, 16)) restartBad++;
        }
        prev = t;
        const p = ev(n, 'generator set');
        if (p) x.xb_blob_set_rng(stateAt, parseInt(p.value, 16));
        x.xb_blob_update(stateAt, t, dt);
        const c = caps.get(n);
        if (c) {
            const r = captureBad(c, fr(c.t), fr(c.x1c), fr(c.x20), restarts === 0);
            if (r && !why) why = r;
            ok += !r;
        }
    });
    const b = blob();
    check(ok === run.captures.length && restartBad === 0,
          `${run.run} replay: ${run.frames} calls, ${ok} of ${run.captures.length} captures equal` +
          `${why ? ', first: ' + why : ''}; ${restarts} restarts (${restartBad} differ)`);
    check(b.draws === run.rand_steps && b.nd === run.drops,
          `${run.run} replay: ${b.draws} generator steps, guest ${run.rand_steps}`);
    if (restarts) continue;
    // run through xb_advance_free
    init();
    let bad = 0;
    ok = 0; why = '';
    run.steps_t_dt.forEach(([t, dt], n) => {
        const p = ev(n, 'generator set');
        if (p) x.xb_blob_set_rng(stateAt, parseInt(p.value, 16));
        x.xb_advance_free(stateAt, fr(dt));
        if (!same(t, x.xb_time(stateAt)) || blob().updates !== n + 1) bad++;
        const c = caps.get(n);
        if (c) {
            x.xb_get_timing(stateAt, tmAt);
            const [tt, , x1c, x20] = f32s(tmAt, 4);
            const r = !same(c.x1c, x1c) || !same(c.x20, x20) ? 'x1c, x20' : captureBad(c, tt, x1c, x20, true);
            if (r && !why) why = r;
            ok += !r;
        }
    });
    check(bad === 0 && ok === run.captures.length,
          `${run.run} run: ${run.frames} steps of xb_advance_free, ${bad} differ; ${ok} of ` +
          `${run.captures.length} captures equal${why ? ', first: ' + why : ''}`);
}

// ---- digest, as run_digest in test_k3.c
let h = 0xCBF29CE484222325n;
const P = 0x100000001B3n, MASK = 0xFFFFFFFFFFFFFFFFn;
function fold(u) {
    for (let i = 0; i < 4; i++) {
        h ^= BigInt((u >>> (8 * i)) & 0xFF);
        h = (h * P) & MASK;
    }
}
const foldW = (at, bytes) => { for (let i = 0; i + 4 <= bytes; i += 4) fold(u32(at + i)); };

check(init() === 0, 'xb_init for the run');
for (const [small, nv, ni] of [[0, 1734, 4054], [1, 150, 346]]) {
    x.xb_blob_mesh(stateAt, small, unitAt, idxAt);
    foldW(unitAt, 12 * nv);
    foldW(idxAt, 2 * ni);
}
const steps = [17, 16, 17];
let tick = 0, frames = 0, rc = 0;
while (rc === 0) {
    tick += steps[frames % 3];
    rc = x.xb_advance(stateAt, tick);
    x.xb_get_timing(stateAt, tmAt);
    const [t, , x1c, x20] = f32s(tmAt, 4);
    x.xb_camera_at(stateAt, t, camAt);
    x.xb_get_blob(stateAt, blobAt);
    x.xb_blob_deformed(stateAt, defAt);
    x.xb_blob_draw(stateAt, camAt, t, x1c, x20, parAt);
    fold(fbits(t));
    foldW(blobAt, BLOB);
    foldW(defAt, 16 * VERTS);
    foldW(parAt, PARAMS);
    for (let side = 0; side < 2; side++) {
        x.xb_blob_light_view(stateAt, side, lvAt);
        foldW(lvAt, 64);
    }
    frames++;
}
check(rc === 1, `run over 8 s at 60 Hz: ${frames} frames, ends with XB_DONE`);
console.log(`digest ${h.toString(16).toUpperCase().padStart(16, '0')} ${frames}`);
console.log(`${checks} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
