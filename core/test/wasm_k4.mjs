// Runs the K4 checks of test_k4.c against the wasm32 build:
//
//     node core/test/wasm_k4.mjs <xboot.wasm> <pack> <c7_logo.json>
//
// The mesh counts, every draw of mesh D in both runs (vertex constants
// 0..8 with the core's camera, draws into the texture), the factor
// colours before E, Q1 and Q2, then the digest of a run over 8 s at 60 Hz
// with the algorithm of test_k4.c. Prints "digest <hex> <frames>" for
// core/build.py, which compares it with the native build.
//
// Layouts of xboot.h on wasm32, all fields 4 bytes: xb_logo 40 bytes (tm,
// texture, phase, rtt, updates, rtt_count, pos @24, alpha @36);
// xb_logo_params 288 bytes (solid, text, world @8, d_vs @72, quad_colour
// @216, text_colour @220, text_world @224); xb_logo_mesh_desc 12 bytes
// (vertices, indices, floats); xb_timing 176 bytes (t @0); xb_camera 248
// bytes (logo_on is the byte @17).
import { readFileSync } from 'node:fs';

const [wasmPath, packPath, vecPath] = process.argv.slice(2);
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), {});
const x = instance.exports;
const pack = readFileSync(packPath);
const vec = JSON.parse(readFileSync(vecPath, 'utf8'));

let checks = 0, failed = 0;
const check = (ok, what) => { checks++; if (!ok) failed++; console.log(`${ok ? 'ok  ' : 'FAIL'}  ${what}`); };
const align = (n) => (n + 15) & ~15;
const LOGO = 40, PARAMS = 288, TIMING = 176, CAM = 248;

const stateAt = align(x.__heap_base.value);
const packAt = align(stateAt + x.xb_state_size());
const cfgAt = align(packAt + pack.length);
const logoAt = align(cfgAt + 16);
const parAt = align(logoAt + LOGO);
const tmAt = align(parAt + PARAMS);
const camAt = align(tmAt + TIMING);
const descAt = align(camAt + CAM);
const vtxAt = align(descAt + 16);
const idxAt = align(vtxAt + 20 * 501);
const need = idxAt + 2 * 1008 + 16;
if (need > x.memory.buffer.byteLength)
    x.memory.grow(Math.ceil((need - x.memory.buffer.byteLength) / 65536));
new Uint8Array(x.memory.buffer, packAt, pack.length).set(pack);
console.log(`state ${x.xb_state_size()} bytes, pack ${pack.length} bytes, memory ${x.memory.buffer.byteLength} bytes`);

const dv = () => new DataView(x.memory.buffer);
const f32s = (at, n) => Array.from({ length: n }, (_, i) => dv().getFloat32(at + 4 * i, true));
const u32 = (at) => dv().getUint32(at, true);
const fbits = (f) => { const b = new DataView(new ArrayBuffer(4)); b.setFloat32(0, f, true); return b.getUint32(0, true); };
const fr = Math.fround;

function init() {
    dv().setUint32(cfgAt, 0, true);
    dv().setUint32(cfgAt + 4, 0, true);
    return x.xb_init(stateAt, packAt, pack.length, cfgAt);
}

function logo() {
    x.xb_get_logo(stateAt, logoAt);
    return { tm: u32(logoAt), texture: u32(logoAt + 4), phase: u32(logoAt + 8), rtt: u32(logoAt + 12),
             updates: u32(logoAt + 16), rttCount: u32(logoAt + 20), pos: f32s(logoAt + 24, 3),
             alpha: f32s(logoAt + 36, 1)[0] };
}

const NAMES = ['A', 'B', 'C', 'D', 'Q1', 'Q2', 'E'];
check(init() === 0, 'xb_init');
let meshBad = 0;
NAMES.forEach((name, m) => {
    x.xb_logo_mesh_info(stateAt, m, descAt);
    const [nv, ni, nf] = [u32(descAt), u32(descAt + 4), u32(descAt + 8)];
    x.xb_logo_mesh(stateAt, m, vtxAt, idxAt);
    let top = 0;
    for (let i = 0; i < ni; i++) top = Math.max(top, dv().getUint16(idxAt + 2 * i, true));
    const w = vec.meshes[name];
    if (nv !== w.vertices || ni !== w.indices || ni / 3 !== w.triangles || top !== nv - 1 || nf !== (m === 6 ? 3 : 5))
        meshBad++;
});
check(meshBad === 0, `seven meshes: counts, triangles and the largest index as in the vector (${meshBad} differ)`);

// factor words of Q1 and Q2 for every float32 t in [6, 6.25]
const seen = new Set();
let eColour = 0;
init();
x.xb_camera_at(stateAt, 6, camAt);
for (let u = fbits(6), last = fbits(6.25); u <= last; u++) {
    const t = new DataView(new Uint32Array([u]).buffer).getFloat32(0, true);
    x.xb_logo_update(stateAt, t);
    x.xb_logo_draw(stateAt, camAt, t, u === last ? 1 : 0, parAt);
    if (u32(parAt + 4)) {
        seen.add(u32(parAt + 216) >>> 24);
        eColour = u32(parAt + 220);
    }
}
check(seen.size === 256, `factor colours of Q1, Q2 for every float32 t in [6, 6.25]: ${seen.size} values`);

for (const name of ['c7_main', 'c7_b']) {
    const run = vec.runs[name];
    init();
    let nD = 0, dOk = 0, nTex = 0, texBad = 0, nE = 0, eOk = 0, nQ = 0, qOk = 0, prevQ = -1, why = '';
    for (const row of run.stream.draws) {
        if (row.mesh === 'D') {
            const toTex = row.matched.by === 'texture', t = fr(row.matched.t);
            nD++;
            x.xb_logo_update(stateAt, t);
            const lg = logo();
            x.xb_camera_at(stateAt, t, camAt);
            if (toTex) {
                if (!lg.rtt || fbits(fr(run.texture_draws.t[nTex])) !== fbits(t)) texBad++;
                nTex++;
            } else if (lg.rtt) texBad++;
            x.xb_logo_draw(stateAt, camAt, t, toTex ? 1 : 0, parAt);
            let ok = dv().getUint8(camAt + 17) === 1 && u32(parAt) === 1;
            row.constants_0_8.forEach((r, i) => r.forEach((h, k) => {
                if (parseInt(h, 16) !== u32(parAt + 72 + 16 * i + 4 * k)) {
                    ok = false;
                    if (!why) why = `D at t = ${t.toFixed(4)}, constant ${i}.${k}`;
                }
            }));
            dOk += ok;
        } else if (row.mesh === 'E') {
            nE++;
            eOk += parseInt(row.factor, 16) === eColour;
        } else if (row.mesh === 'Q1' || row.mesh === 'Q2') {
            const f = parseInt(row.factor, 16), w = f >>> 24;
            nQ++;
            if ((f & 0xFFFFFF) === 0 && seen.has(w) && w >= prevQ) { qOk++; prevQ = w; }
        }
    }
    check(nD > 0 && dOk === nD && texBad === 0 && nTex === run.texture_draws.t.length && logo().rttCount === nTex,
          `${name}: ${nD} draws of D, constants 0..8 equal in ${dOk}; ${nTex} into the texture (${texBad} wrong)` +
          `${why ? '; first: ' + why : ''}`);
    check(eOk === nE && qOk === nQ, `${name}: factor colour before E in ${eOk} of ${nE}, before Q1 and Q2 in ` +
          `${qOk} of ${nQ} draws`);
}

// ---- digest, as run_digest in test_k4.c
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
NAMES.forEach((_, m) => {
    x.xb_logo_mesh_info(stateAt, m, descAt);
    x.xb_logo_mesh(stateAt, m, vtxAt, idxAt);
    foldW(vtxAt, 4 * u32(descAt + 8) * u32(descAt));
    foldW(idxAt, 2 * u32(descAt + 4));
});
const steps = [17, 16, 17];
let tick = 0, frames = 0, rc = 0;
while (rc === 0) {
    tick += steps[frames % 3];
    rc = x.xb_advance(stateAt, tick);
    x.xb_get_timing(stateAt, tmAt);
    const t = f32s(tmAt, 1)[0];
    x.xb_camera_at(stateAt, t, camAt);
    const lg = logo();
    fold(fbits(t));
    foldW(logoAt, LOGO);
    if (lg.rtt) {
        x.xb_logo_draw(stateAt, camAt, t, 1, parAt);
        foldW(parAt, PARAMS);
    }
    if (dv().getUint8(camAt + 17)) {
        x.xb_logo_draw(stateAt, camAt, t, 0, parAt);
        foldW(parAt, PARAMS);
    }
    frames++;
}
check(rc === 1, `run over 8 s at 60 Hz: ${frames} frames, ends with XB_DONE`);
console.log(`digest ${h.toString(16).toUpperCase().padStart(16, '0')} ${frames}`);
console.log(`${checks} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
