// Runs the K2 checks of test_k2.c against the wasm32 build:
//
//     node core/test/wasm_k2.mjs <xboot.wasm> <pack> <c5_chamber.json>
//
// Geometry of the twelve groups (counts, form tables, SHA-256 of indices,
// and of positions and attributes where the guest wrote every vertex),
// camera, detail, world matrices and light views at the five captured
// times, and the digest of a run over 8 s at 60 Hz with the algorithm of
// test_k2.c. Prints "digest <hex> <frames>" for core/build.py, which
// compares it with the native build.
//
// Layouts of xboot.h on wasm32: xb_camera 248 bytes (t, s, key, path,
// two switch bytes and two pad bytes, eye @20, target @32, up @44,
// view @56, view_inv @120, proj @184); xb_chamber_pose 2256 bytes (t, u,
// k, detail, rot @16, pos @1872); xb_part 28 bytes (group, type, side,
// pad, index u16 @4, form @6, pos_track @8, rot_track @10, scale @12);
// xb_mesh_info 7 u32; xb_form 6 u32.
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';

const [wasmPath, packPath, vecPath] = process.argv.slice(2);
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), {});
const x = instance.exports;
const pack = readFileSync(packPath);
const vec = JSON.parse(readFileSync(vecPath, 'utf8'));

let checks = 0, failed = 0;
const check = (ok, what) => { checks++; if (!ok) failed++; console.log(`${ok ? 'ok  ' : 'FAIL'}  ${what}`); };
const align = (n) => (n + 15) & ~15;
const PARTS = 271, CAM = 248, POSE = 2256, WORLD = PARTS * 64;

const stateAt = align(x.__heap_base.value);
const packAt = align(stateAt + x.xb_state_size());
const cfgAt = align(packAt + pack.length);
const camAt = align(cfgAt + 16);
const poseAt = align(camAt + CAM);
const worldAt = align(poseAt + POSE);
const partAt = align(worldAt + WORLD);
const infoAt = align(partAt + 32);
const formsAt = align(infoAt + 32);
const centerAt = align(formsAt + 16 * 24);
const lvAt = align(centerAt + 16);
const meshAt = align(lvAt + 64);
const need = meshAt + 200000;
if (need > x.memory.buffer.byteLength)
    x.memory.grow(Math.ceil((need - x.memory.buffer.byteLength) / 65536));
new Uint8Array(x.memory.buffer, packAt, pack.length).set(pack);
console.log(`state ${x.xb_state_size()} bytes, pack ${pack.length} bytes, memory ${x.memory.buffer.byteLength} bytes`);

const dv = () => new DataView(x.memory.buffer);
const f32s = (at, n) => Array.from({ length: n }, (_, i) => dv().getFloat32(at + 4 * i, true));
const u32s = (at, n) => Array.from({ length: n }, (_, i) => dv().getUint32(at + 4 * i, true));
const fbits = (f) => { const b = new DataView(new ArrayBuffer(4)); b.setFloat32(0, f, true); return b.getUint32(0, true); };
const vnum = (v) => (v === 'NaN' ? NaN : Math.fround(v));
const same = (a, b) => fbits(a) === fbits(b) || (Number.isNaN(a) && Number.isNaN(b));
const listEq = (want, got) => want.length === got.length && want.every((v, i) => same(vnum(v), got[i]));
const sha = (at, n) => createHash('sha256').update(new Uint8Array(x.memory.buffer, at, n)).digest('hex');

function init() {
    dv().setUint32(cfgAt, 0, true);
    dv().setUint32(cfgAt + 4, 0, true);
    return x.xb_init(stateAt, packAt, pack.length, cfgAt);
}

function mesh(g, lod) {
    if (x.xb_chamber_mesh_info(stateAt, g, lod, infoAt) !== 0) return null;
    const [type, format, af, prim, forms, nv, ni] = u32s(infoAt, 7);
    const posAt = meshAt, attrAt = align(posAt + 12 * nv), idxAt = align(attrAt + 4 * af * nv);
    new Uint8Array(x.memory.buffer, posAt, idxAt + 2 * ni - posAt).fill(0xEE);
    const rc = x.xb_chamber_mesh(stateAt, g, lod, posAt, attrAt, idxAt, formsAt);
    return { rc, type, format, af, prim, forms, nv, ni, posAt, attrAt, idxAt };
}

check(init() === 0, 'xb_init');

// ---- static part
const groups = vec.static.groups;
check(groups.length === 12, 'c5_chamber.json has twelve groups');
for (const [gi, G] of groups.entries()) {
    const m = mesh(G.slot % 6, G.detail);
    check(m && m.rc === 0 && m.nv === G.vertices && m.ni === G.indices,
          `group ${gi}: ${m.nv} vertices, ${m.ni} indices, guest ${G.vertices}, ${G.indices}`);
    const forms = Array.from({ length: m.forms }, (_, k) => u32s(formsAt + 24 * k, 6));
    check(JSON.stringify(forms) === JSON.stringify(G.forms), `group ${gi}: form table equals the guest's`);
    check(sha(m.idxAt, 2 * m.ni) === G.sha256.indices, `group ${gi}: indices, SHA-256`);
    if (G.vertices_not_written === 0) {
        check(sha(m.posAt, 12 * m.nv) === G.sha256.positions, `group ${gi}: vertex positions, SHA-256`);
        check(sha(m.attrAt, 4 * m.af * m.nv) === G.sha256.attributes, `group ${gi}: vertex attributes, SHA-256`);
    }
}

// ---- captures
const groupFirst = [];
for (let i = 0; i < PARTS; i++) {
    x.xb_chamber_part(stateAt, i, partAt);
    if (dv().getUint16(partAt + 4, true) === 0) groupFirst[dv().getUint8(partAt)] = i;
}
for (const c of vec.captures) {
    const t = Math.fround(c.t), tag = `t=${t.toFixed(4)}`;
    x.xb_camera_at(stateAt, t, camAt);
    const s = f32s(camAt + 4, 1)[0], key = dv().getInt32(camAt + 8, true);
    check(same(s, vnum(c.camera.s)) && key === c.camera.key, `${tag}: path time and section ${key}`);
    check(listEq(c.camera.eye, f32s(camAt + 20, 3)) && listEq(c.camera.target, f32s(camAt + 32, 3)) &&
          listEq(c.camera.up, f32s(camAt + 44, 3)), `${tag}: eye, target, up equal the guest's`);
    check(listEq(c.camera.view, f32s(camAt + 56, 16)) && listEq(c.camera.projection, f32s(camAt + 184, 16)),
          `${tag}: view and projection equal the guest's`);
    x.xb_chamber_at(stateAt, t, poseAt);
    const detail = dv().getUint32(poseAt + 12, true);
    check(detail === c.detail, `${tag}: detail ${detail}, guest ${c.detail}`);
    x.xb_chamber_world(stateAt, poseAt, 0, PARTS, worldAt);
    let bad = 0, nan = 0;
    for (const p of c.parts) {
        const w = f32s(worldAt + 64 * (groupFirst[p.group % 6] + p.index), 16);
        const got = [w[12], w[13], w[14]], ax = [w[0], w[1], w[2], w[4], w[5], w[6], w[8], w[9], w[10]];
        if (!listEq(p.position, got) || !listEq(p.axes, ax)) bad++;
        if (w.some(Number.isNaN)) nan++;
    }
    check(c.parts.length === PARTS && bad === 0 && nan === c.parts_nan,
          `${tag}: world matrices of ${c.parts.length} parts, ${bad} differ, ${nan} with NaN, guest ${c.parts_nan}`);
    for (const [side, name] of [[0, 'down'], [1, 'up']]) {
        const v = c.light_views[name];
        v.center.forEach((q, i) => dv().setFloat32(centerAt + 4 * i, vnum(q), true));
        x.xb_light_view(centerAt, side, lvAt);
        check(listEq(v.matrix, f32s(lvAt, 16)), `${tag}: light view ${name} equals the guest's`);
    }
}

// ---- digest, as run_digest in test_k2.c
let h = 0xCBF29CE484222325n;
const P = 0x100000001B3n, MASK = 0xFFFFFFFFFFFFFFFFn;
function fold(u) {
    for (let i = 0; i < 4; i++) {
        h ^= BigInt((u >>> (8 * i)) & 0xFF);
        h = (h * P) & MASK;
    }
}
const foldF = (at, n) => { for (const f of f32s(at, n)) fold(Number.isNaN(f) ? 0x7FC00000 : fbits(f)); };

check(init() === 0, 'xb_init for the run');
for (let g = 0; g < 6; g++)
    for (let lod = 0; lod < 2; lod++) {
        const m = mesh(g, lod);
        foldF(m.posAt, 3 * m.nv);
        foldF(m.attrAt, m.af * m.nv);
        for (let i = 0; i < m.ni; i++) fold(dv().getUint16(m.idxAt + 2 * i, true));
    }
const steps = [17, 16, 17];
let tick = 0, frames = 0, rc = 0;
while (rc === 0) {
    tick += steps[frames % 3];
    rc = x.xb_advance(stateAt, tick);
    const t = x.xb_time(stateAt);
    x.xb_camera_at(stateAt, t, camAt);
    x.xb_chamber_at(stateAt, t, poseAt);
    x.xb_chamber_world(stateAt, poseAt, 0, PARTS, worldAt);
    fold(fbits(t));
    foldF(camAt + 4, 1);
    fold(dv().getUint32(camAt + 8, true));
    fold(dv().getUint8(camAt + 16) | (dv().getUint8(camAt + 17) << 8));
    foldF(camAt + 20, 3);
    foldF(camAt + 32, 3);
    foldF(camAt + 56, 16);
    foldF(camAt + 120, 16);
    foldF(camAt + 184, 16);
    foldF(poseAt + 4, 2);
    fold(dv().getUint32(poseAt + 12, true));
    foldF(poseAt + 16, 16 * 29);
    foldF(poseAt + 1872, 3 * 32);
    foldF(worldAt, 16 * PARTS);
    frames++;
}
check(rc === 1, `run over 8 s at 60 Hz: ${frames} frames, ends with XB_DONE`);
console.log(`digest ${h.toString(16).toUpperCase().padStart(16, '0')} ${frames}`);
console.log(`${checks} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
