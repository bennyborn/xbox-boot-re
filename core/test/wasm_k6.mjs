// Runs the K6 checks of test_k6.c against the wasm32 build:
//
//     node core/test/wasm_k6.mjs <xboot.wasm> <pack> <c8_gpu.json>
//
// The set-up against frame 0 of the vector, the replay of the timeline
// with t after every step, at every recorded frame the draws of xb_frame
// (step, primitive, count, program, combiners, textures, targets, render
// states) and at every effect frame the track values, the veil's colours
// and the glow, with the rules of test_k6.c. Then the digest of a run over
// 8 s at 60 Hz with the algorithm of test_k6.c. Prints
// "digest <hex> <frames>" for core/build.py, which compares it with the
// native build.
//
// Layouts of xboot.h on wasm32. xb_draw 112 bytes: step @0, what @2, mesh
// @4, part @6 (u16), prim @8, detail @9, color @10, zeta @11, tex @12 (u8),
// flags @16, first @20, count @24, program @28, combiners @32; state:
// alpha_test @36, blend @37, depth_test @38, depth_write @39, cull @40,
// alpha_func @44, blend_src @46, blend_dst @48, depth_func @50, cull_face
// @52, shadow_func @54 (u16), alpha_ref @56, colour_mask @60; factor @64.
// xb_frame_out 156 bytes: view @0, proj @64, draws @128, draw_cap @132,
// draw_count @136, params @140, param_cap @144, param_count @148,
// clear_depth @152. xb_track 20 bytes: four float32, fx_draws @16.
// xb_timing 176 bytes: t @0, x20 @12. xb_camera 248 bytes. xb_fx_params
// 376 bytes: veil @160, veil_ps @164, glow @296, glow_alpha @300,
// glow_colour @304, glow_s @308, glow_world @312.
import { readFileSync } from 'node:fs';

const [wasmPath, packPath, vecPath] = process.argv.slice(2);
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), {});
const x = instance.exports;
const pack = readFileSync(packPath);
const vec = JSON.parse(readFileSync(vecPath, 'utf8'));

let checks = 0, failed = 0;
const check = (ok, what) => { checks++; if (!ok) failed++; console.log(`${ok ? 'ok  ' : 'FAIL'}  ${what}`); };
const align = (n) => (n + 15) & ~15;
const DRAW = 112, FRAME = 156, TRACK = 20, TIMING = 176, CAMERA = 248, FX = 376;
const DRAWS_MAX = 1320, PARAMS_MAX = 40960, PASSES = 7;
const SURF = { NONE: 0, TEX: 1, LIGHT0: 12, LIGHT1: 13, FX_A: 14, FX_B: 15, FX_DEPTH: 16, LOGO: 17,
               CUBE: 20, SCREEN: 27, SCREEN_DEPTH: 28, DISPLAY: 29, COUNT: 30 };
const DRAW_PRESENT = 0;

const stateAt = align(x.__heap_base.value);
const packAt = align(stateAt + x.xb_state_size());
const cfgAt = align(packAt + pack.length);
const outAt = align(cfgAt + 16);
const drawsAt = align(outAt + FRAME);
const paramsAt = align(drawsAt + DRAW * DRAWS_MAX);
const trackAt = align(paramsAt + 4 * PARAMS_MAX);
const tmAt = align(trackAt + TRACK);
const camAt = align(tmAt + TIMING);
const fxAt = align(camAt + CAMERA);
const need = fxAt + FX + 16;
if (need > x.memory.buffer.byteLength)
    x.memory.grow(Math.ceil((need - x.memory.buffer.byteLength) / 65536));
new Uint8Array(x.memory.buffer, packAt, pack.length).set(pack);
console.log(`state ${x.xb_state_size()} bytes, pack ${pack.length} bytes, memory ${x.memory.buffer.byteLength} bytes`);

const dv = () => new DataView(x.memory.buffer);
const u8 = (at) => dv().getUint8(at);
const u16 = (at) => dv().getUint16(at, true);
const u32 = (at) => dv().getUint32(at, true);
const f32 = (at) => dv().getFloat32(at, true);
const hex8 = (u) => (u >>> 0).toString(16).padStart(8, '0').toUpperCase();

function init() {
    dv().setUint32(cfgAt, 0, true);
    dv().setUint32(cfgAt + 4, 0, true);
    return x.xb_init(stateAt, packAt, pack.length, cfgAt);
}

function buffers() {
    new Uint8Array(x.memory.buffer, outAt, FRAME).fill(0);
    dv().setUint32(outAt + 128, drawsAt, true);
    dv().setUint32(outAt + 132, DRAWS_MAX, true);
    dv().setUint32(outAt + 140, paramsAt, true);
    dv().setUint32(outAt + 144, PARAMS_MAX, true);
}

// ---- names of the vector, as surf_init and surf_eq in test_k6.c
const surfName = new Array(SURF.COUNT).fill('');
surfName[SURF.NONE] = '0x0';
['normal_cube128', 'normal_cube64', 'fractal128', 'falloff256', 'gradient_green', 'radial256',
 'gradient_grey', 'tm16', 'noise_a', 'noise_b', 'noise_c'].forEach((n, i) => { surfName[SURF.TEX + i] = n; });
surfName[SURF.LIGHT0] = 'light_view_a';
surfName[SURF.LIGHT1] = 'light_view_b';
surfName[SURF.FX_A] = 'fx_target_14';
surfName[SURF.FX_B] = 'fx_target_18';
surfName[SURF.FX_DEPTH] = 'fx_depth';
surfName[SURF.LOGO] = 'logo_target';
surfName[SURF.CUBE] = 'chamber_cube256';
surfName[SURF.SCREEN] = '0x3628000';
surfName[SURF.SCREEN_DEPTH] = '0x33d0000';
surfName[SURF.DISPLAY] = '0x32a4000';

function surfEq(got, id) {
    if (typeof got !== 'string')
        return -1;
    if (id >= SURF.COUNT)
        return 0;
    if (!surfName[id]) {
        if (surfName.includes(got) || !got.startsWith('0x'))
            return 0;
        surfName[id] = got;
        return 1;
    }
    return surfName[id] === got ? 1 : 0;
}

const stepName = (s) => ((s >> 8) === 0x0A ? 'A' : (s >> 8) === 0x0B ? 'U' : 'D') + (s & 0xFF);

// ---- one frame against the vector, as compare_draws in test_k6.c
const kinds = vec.draws.kinds;

function stateDiffers(st, at, ty) {
    let bad = 0;
    const one = (key, want) => { if (key in st) bad += parseInt(st[key], 16) !== (want >>> 0); else ty.unsent++; };
    const alpha = u8(at + 36), blend = u8(at + 37), dtest = u8(at + 38), cull = u8(at + 40);
    one('0x0300', alpha);
    if (alpha) { one('0x033c', u16(at + 44)); one('0x0340', u32(at + 56)); }
    one('0x0304', blend);
    if (blend) { one('0x0344', u16(at + 46)); one('0x0348', u16(at + 48)); }
    one('0x030c', dtest);
    if (dtest) one('0x0354', u16(at + 50));
    one('0x035c', u8(at + 39));
    if ('0x0308' in st && parseInt(st['0x0308'], 16) <= 1) {
        bad += parseInt(st['0x0308'], 16) !== cull;
        if (cull) one('0x039c', u16(at + 52));
    } else {
        ty.unsent++;
    }
    one('0x0358', u32(at + 60));
    if (u16(at + 54)) one('0x1e6c', u16(at + 54) - 0x200);
    return bad !== 0;
}

function compareDraws(list, from, ty) {
    const n = u32(outAt + 136);
    for (let i = 0; i < n; i++) {
        const at = drawsAt + DRAW * i, v = list[from + i], what = u16(at + 2);
        ty.draws++;
        if (!v) { ty.order++; continue; }
        const count = u32(at + 24), program = u32(at + 28), combiners = u32(at + 32);
        ty.order += (stepName(u16(at)) !== v.step || v.prim !== u8(at + 8) ||
                     v.count !== (what === DRAW_PRESENT ? 4 * count : count)) ? 1 : 0;
        if (what !== DRAW_PRESENT) {
            const p = program < 0x10000 ? `fixed function 0x${program.toString(16)}` : `0x${program.toString(16).padStart(8, '0')}`;
            const c = combiners ? `0x${combiners.toString(16).padStart(8, '0')}` : 'from texture stages';
            ty.objects += (p !== v.program || c !== v.combiners) ? 1 : 0;
        }
        let bad = 0;
        for (let k = 0; k < 4; k++) {
            const id = u8(at + 12 + k);
            if (id === SURF.NONE)
                continue;
            if (!(String(k) in v.textures)) {
                const reg = '0x' + (0x1b00 + 0x40 * k).toString(16).padStart(4, '0');
                const st = kinds[v.kind].state;
                if (reg in st) {
                    bad |= ('0x' + parseInt(st[reg], 16).toString(16)) !== surfName[id];
                    continue;
                }
            }
            bad |= surfEq(v.textures[String(k)], id) !== 1;
        }
        ty.textures += bad ? 1 : 0;
        const rc = surfEq(v.color, u8(at + 10)), rz = surfEq(v.zeta, u8(at + 11));
        ty.unnamed += (rc < 0) + (rz < 0);
        ty.targets += (rc === 0 || rz === 0) ? 1 : 0;
        ty.states += stateDiffers(kinds[v.kind].state, at, ty) ? 1 : 0;
    }
}

const tally = () => ({ draws: 0, order: 0, objects: 0, textures: 0, targets: 0, states: 0, unnamed: 0, unsent: 0 });

function report(what, nVector, ty, extra = true) {
    check(extra && ty.draws === nVector && !ty.order && !ty.objects && !ty.textures && !ty.targets && !ty.states,
          `${what}: ${ty.draws} draws, vector ${nVector}; step, primitive or count differ in ${ty.order}, ` +
          `program or combiners in ${ty.objects}, textures in ${ty.textures}, targets in ${ty.targets}, ` +
          `states in ${ty.states} (left out: ${ty.unnamed} targets without a name, ${ty.unsent} states not sent)`);
}

// ---- effects, as check_effects in test_k6.c
function packColour(at) {
    const b = [];
    for (let i = 0; i < 4; i++) {
        const c = f32(at + 4 * i), v = c > 1 ? 1 : (c < 0 ? 0 : c);
        b.push(Math.trunc(Math.fround(v * 255 + 0.5)) & 0xFF);
    }
    return ((b[3] << 24) | (b[0] << 16) | (b[1] << 8) | b[2]) >>> 0;
}

function checkEffects(rec, frame) {
    x.xb_get_timing(stateAt, tmAt);
    const t = f32(tmAt), x20 = f32(tmAt + 12);
    x.xb_camera_at(stateAt, t, camAt);
    x.xb_get_track(stateAt, trackAt);
    x.xb_fx_draw(stateAt, camAt, t, x20, trackAt, fxAt);
    const tr = [0, 4, 8, 12].map((o) => u32(trackAt + o));
    check(tr.every((u, i) => u === parseInt(rec.track[i], 16)) && Math.fround(rec.x20) === x20 && Math.fround(rec.t) === t,
          `frame ${frame} t=${t.toFixed(3)}: track values equal the guest's (${tr.map(hex8).join(' ')})`);
    const veil = u32(fxAt + 160);
    if (rec.colours) {
        const k0 = packColour(fxAt + 164), k1 = packColour(fxAt + 180);
        check(veil && k0 === parseInt(rec.colours[0], 16) && k1 === parseInt(rec.colours[1], 16),
              `frame ${frame}: veil colours ${hex8(k0)} ${hex8(k1)}, vector ${rec.colours.join(' ')}`);
    } else {
        check(!veil, `frame ${frame}: no veil in the vector, fx_draw ${veil ? 'called' : 'not called'}`);
    }
    if (rec.glow) {
        check(u32(fxAt + 296) && u32(fxAt + 300) === rec.glow.alpha && u32(fxAt + 304) === rec.glow.factor &&
              f32(fxAt + 308) === Math.fround(rec.glow.s) && f32(fxAt + 312) === Math.fround(rec.glow.inv_s) &&
              f32(fxAt + 312 + 20) === f32(fxAt + 312),
              `frame ${frame}: glow alpha ${u32(fxAt + 300)}, factor ${hex8(u32(fxAt + 304))}, s ${f32(fxAt + 308)} as in the vector`);
    } else {
        check(!u32(fxAt + 296), `frame ${frame}: no glow in the vector, alpha ${u32(fxAt + 300)} in the core`);
    }
}

// ---- set-up
check(init() === 0, 'xb_init');
const recs = vec.draws.frames;
{
    const ty = tally();
    let at = 0, rc = 0;
    for (let p = 0; p < PASSES && rc === 0; p++) {
        buffers();
        rc = x.xb_frame_setup(stateAt, p, outAt);
        compareDraws(recs[0].draws, at, ty);
        at += u32(outAt + 136);
    }
    report('set-up (A1, A2)', recs[0].draws.length, ty, rc === 0 && recs[0].frame === 0);
}

// ---- the run
const tl = vec.timeline;
check(tl.ms.length > 0 && tl.ms.length === tl.t_after.length && tl.first_frame === 1 && tl.not_replayable === 0,
      'c8_gpu.json: a timeline in whole milliseconds, every frame replayable');
{
    const byFrame = new Map(recs.map((r) => [r.frame, r])), eff = new Map(vec.effects.frames.map((r) => [r.frame, r]));
    const reps = new Set(tl.repeated_stops);
    let tick = 0, wrong = 0, seen = 0, seenE = 0;
    for (let i = 1; i <= tl.ms.length; i++) {
        const want = parseInt(tl.t_after[i - 1], 16);
        if (reps.has(i)) {
            wrong += (tl.ms[i - 1] !== 0 || u32FromF(x.xb_time(stateAt)) !== want) ? 1 : 0;
            continue;
        }
        tick += tl.ms[i - 1];
        if (x.xb_advance(stateAt, tick) < 0)
            wrong++;
        wrong += u32FromF(x.xb_time(stateAt)) !== want ? 1 : 0;
        if (byFrame.has(i)) {
            const ty = tally();
            buffers();
            const rc = x.xb_frame(stateAt, outAt);
            compareDraws(byFrame.get(i).draws, 0, ty);
            report(`frame ${i} t=${x.xb_time(stateAt).toFixed(3)}`, byFrame.get(i).draws.length, ty, rc === 0);
            seen++;
        }
        if (eff.has(i)) {
            checkEffects(eff.get(i), i);
            seenE++;
        }
    }
    check(!wrong && seen === recs.length - 1 && seenE === vec.effects.frames.length,
          `replay of ${tl.ms.length} stops, ${reps.size} of them repeated and skipped: t after each step equals ` +
          `the guest's (${wrong} differ); ${seen} of ${recs.length - 1} recorded frames and ${seenE} of ` +
          `${vec.effects.frames.length} effect frames met`);
}

function u32FromF(f) {
    const b = new DataView(new ArrayBuffer(4));
    b.setFloat32(0, f, true);
    return b.getUint32(0, true);
}

// ---- digest, as run_digest in test_k6.c
let h = 0xCBF29CE484222325n;
const P = 0x100000001B3n, MASK = 0xFFFFFFFFFFFFFFFFn;
function fold(u) {
    for (let i = 0; i < 4; i++) {
        h ^= BigInt((u >>> (8 * i)) & 0xFF);
        h = (h * P) & MASK;
    }
}
function foldFrame() {
    const nd = u32(outAt + 136), np = u32(outAt + 148);
    fold(nd);
    fold(np);
    fold(u32(outAt + 152));
    for (let i = 0; i < 16; i++) {
        fold(u32(outAt + 4 * i));
        fold(u32(outAt + 64 + 4 * i));
    }
    for (let i = 0; i < nd * (DRAW / 4); i++)
        fold(u32(drawsAt + 4 * i));
    for (let i = 0; i < np; i++)
        fold(Number.isNaN(f32(paramsAt + 4 * i)) ? 0x7FC00000 : u32(paramsAt + 4 * i));
}

check(init() === 0, 'xb_init for the digest');
let ok = true;
for (let p = 0; p < PASSES; p++) {
    buffers();
    ok = ok && x.xb_frame_setup(stateAt, p, outAt) === 0;
    foldFrame();
}
const steps = [17, 17, 16];
let tick = 0, frames = 0, rc = 0;
do {
    tick += steps[frames % 3];
    rc = x.xb_advance(stateAt, tick);
    buffers();
    ok = ok && rc >= 0 && x.xb_frame(stateAt, outAt) === 0;
    foldFrame();
    frames++;
} while (rc === 0 && frames < 100000);
check(ok && rc === 1, `run over 8 s at 60 Hz: ${frames} frames, ends with XB_DONE`);
console.log(`digest ${h.toString(16).toUpperCase().padStart(16, '0')} ${frames}`);

console.log(`${checks} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
