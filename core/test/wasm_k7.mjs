// Runs the K7 checks of test_k7.c against the wasm32 build:
//
//     node core/test/wasm_k7.mjs <xboot.wasm> <pack> <c4_audio.json>
//
// c4_gp.json is read next to c4_audio.json. The eight tables against the
// guest's memory, every command and call of the run c4_seq, the method
// values after each channel's first sample call, the impulse responses of
// the GP stage against c4_gp.json, then the digest of 12000 blocks with the
// algorithm of test_k7.c. Prints "digest <hex> 12000" for core/build.py,
// which compares it with the native build.
//
// Layouts of xboot.h on wasm32: xb_audio_cmd 8 bytes (addr, tick u16 @4,
// channel @6, op @7); xb_audio_call 16 bytes (a0, a1, tick u16 @8, channel
// @10, fn @11, effect @12); xb_audio_out 3280 bytes (block, tick, n_cmds,
// n_calls, cmd @16, call @144, voice @400, gain @2448, bin @2640, out
// @3024); xb_voice 64 bytes (alloc, on, active, sample, level @4, fmt @8,
// vbin @12, misc @16, env0 @20, enva @24, env1 @28, envf @32, lfo_env @36,
// fca @40, vola @44, volb @48, volc @52, pitch @56, ebo @60);
// xb_audio_state 1040 bytes (voice @16).
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';

const [wasmPath, packPath, vecPath] = process.argv.slice(2);
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), {});
const x = instance.exports;
const pack = readFileSync(packPath);
const vec = JSON.parse(readFileSync(vecPath, 'utf8'));
const gp = JSON.parse(readFileSync(vecPath.replace('c4_audio.json', 'c4_gp.json'), 'utf8'));

let checks = 0, failed = 0;
const check = (ok, what) => { checks++; if (!ok) failed++; console.log(`${ok ? 'ok  ' : 'FAIL'}  ${what}`); };
const align = (n) => (n + 15) & ~15;
const OUT = 3280, STATE = 1040, BLOCK = 32, CH = 16, TICK_BLOCKS = 9, DIGEST_BLOCKS = 12000, IR_FRAMES = 6000;

const stateAt = align(x.__heap_base.value);
const packAt = align(stateAt + x.xb_state_size());
const cfgAt = align(packAt + pack.length);
const soundAt = align(cfgAt + 16);
const outAt = align(soundAt + x.xb_audio_size());
const stAt = align(outAt + OUT);
const ioAt = align(stAt + STATE);                 // 5 blocks of int32 for xb_audio_gp
const tabAt = align(ioAt + 5 * BLOCK * 4);
const need = tabAt + 0x8000 * 2 + 16;
if (need > x.memory.buffer.byteLength)
    x.memory.grow(Math.ceil((need - x.memory.buffer.byteLength) / 65536));
new Uint8Array(x.memory.buffer, packAt, pack.length).set(pack);
console.log(`state ${x.xb_state_size()} bytes, sound ${x.xb_audio_size()} bytes, pack ${pack.length} bytes, ` +
            `memory ${x.memory.buffer.byteLength} bytes`);

const dv = () => new DataView(x.memory.buffer);
const u32 = (at) => dv().getUint32(at, true);
const i32 = (at) => dv().getInt32(at, true);
const u16 = (at) => dv().getUint16(at, true);
const u8 = (at) => dv().getUint8(at);

function start() {
    dv().setUint32(cfgAt, 0, true);
    dv().setUint32(cfgAt + 4, 0, true);
    const rc = x.xb_init(stateAt, packAt, pack.length, cfgAt);
    return rc !== 0 ? rc : x.xb_audio_init(stateAt, soundAt, x.xb_audio_size());
}

check(start() === 0, 'xb_init and xb_audio_init');

// ---- tables
const TABLES = ['0x800a1b78', '0x80093b78', '0x80083b70', '0x80083328', '0x80078898', '0x800970f8', '0x800a5b80',
                '0x80093c78'];
TABLES.forEach((key, t) => {
    const want = vec.tables.by_address[key];
    const n = x.xb_audio_table(stateAt, t, 0, 0);
    const rc = x.xb_audio_table(stateAt, t, tabAt, n);
    const sha = createHash('sha256').update(new Uint8Array(x.memory.buffer, tabAt, n * 2)).digest('hex');
    check(rc === n && n === want.values && want.mismatches === 0 && sha === want.sha256,
          `table ${key}: ${n} values, SHA-256 ${sha.slice(0, 16)}... ${sha === want.sha256 ? 'equals' : 'differs from'} ` +
          `the guest's memory`);
});

// ---- sequencer and voice layer, as check_sequencer in test_k7.c
const FN = ['filter', 'stop', 'sample', 'level', 'pitch_play', 'pitch', 'nop_b2'];
const REG = { 304: 8, 318: 16, 308: 20, '30c': 24, 310: 28, 314: 32, '36c': 36, 374: 40, 378: 40, 300: 12, 360: 44,
              364: 48, 368: 52, '37c': 56 };
{
    const list = vec.commands.list, calls = vec.calls_model, last = vec.ticks.last[0];
    let ci = 0, fi = 0, cmdBad = 0, callBad = 0, regBad = 0, regN = 0, ticks = 0, spacingBad = 0;
    const seen = new Set();
    for (;;) {
        x.xb_audio_block(stateAt, outAt);
        const tick = u32(outAt + 4);
        if ((tick !== 0) !== (u32(outAt) % TICK_BLOCKS === 0)) spacingBad++;
        if (!tick) continue;
        if (tick > last + 1) break;
        ticks++;
        for (let i = 0; i < u32(outAt + 8); i++, ci++) {
            const at = outAt + 16 + 8 * i, w = list[ci];
            if (w && (u16(at + 4) !== w[0] || u8(at + 6) !== w[1] || u32(at) !== w[2] || u8(at + 7) !== w[3])) cmdBad++;
        }
        x.xb_get_audio(stateAt, stAt);
        for (let i = 0; i < u32(outAt + 12); i++) {
            const at = outAt + 144 + 16 * i, ch = u8(at + 10), fn = u8(at + 11);
            if (u16(at + 8) > last) continue;
            const w = calls[fi++];
            if (!w) continue;
            let ok = u16(at + 8) === w[0] && ch === w[1] && FN[fn] === w[2];
            if (ok && w.length > 3) ok = i32(at) === w[3];
            if (ok && w.length > 4) ok = typeof w[4] === 'boolean' ? (i32(at + 4) !== 0) === w[4] : i32(at + 4) === w[4];
            if (!ok) callBad++;
            if (fn === 2 && !seen.has(ch) && vec.first_sample_config[ch]) {
                seen.add(ch);
                for (const [key, off] of Object.entries(REG)) {
                    regN++;
                    if (u32(stAt + 16 + 64 * ch + off) !== parseInt(vec.first_sample_config[ch][key], 16)) regBad++;
                }
            }
        }
    }
    check(spacingBad === 0 && ci >= list.length && list.length === 900 && cmdBad === 0,
          `c4_seq: ${ticks} ticks, one every ${TICK_BLOCKS} blocks; ${ci} commands, vector ${list.length}; differ in ${cmdBad}`);
    check(fi === calls.length && callBad === 0, `c4_seq: ${fi} calls into the voice layer, vector ${calls.length}; differ in ${callBad}`);
    check(seen.size === Object.keys(vec.first_sample_config).length && regBad === 0,
          `c4_seq: after the first sample call of ${seen.size} channels, ${regN} method values, ${regBad} differ from the guest's`);
}

// ---- GP stage: impulse responses as in check_gp
{
    const n = IR_FRAMES * BLOCK;
    const keys = [['b0_L', null], [null, 'b1_R'], ['b11_L', 'b11_R']], names = ['0', '1', '11'];
    for (let b = 0; b < 3; b++) {
        const resp = [new Float32Array(n), new Float32Array(n)];
        let ok = start() === 0;
        for (let f = 0; f < IR_FRAMES; f++) {
            new Int32Array(x.memory.buffer, ioAt, 5 * BLOCK).fill(0);
            if (f === 0) dv().setInt32(ioAt + b * BLOCK * 4, 0x400000, true);
            ok = x.xb_audio_gp(stateAt, ioAt, ioAt + BLOCK * 4, ioAt + 2 * BLOCK * 4, ioAt + 3 * BLOCK * 4,
                               ioAt + 4 * BLOCK * 4) === 0 && ok;
            const io = new Int32Array(x.memory.buffer, ioAt + 3 * BLOCK * 4, 2 * BLOCK);
            for (let i = 0; i < BLOCK; i++) {
                resp[0][f * BLOCK + i] = Math.fround(Math.fround(io[i] / 8388608) / 0.5);
                resp[1][f * BLOCK + i] = Math.fround(Math.fround(io[BLOCK + i] / 8388608) / 0.5);
            }
        }
        for (let side = 0; side < 2; side++) {
            const sha = createHash('sha256').update(new Uint8Array(resp[side].buffer)).digest('hex');
            const nonzero = resp[side].reduce((a, v) => a + (v !== 0), 0);
            if (keys[b][side])
                check(ok && sha === gp.ir_sha256[keys[b][side]],
                      `impulse in bin ${names[b]}, ${side ? 'right' : 'left'}: ${nonzero} samples not 0, SHA-256 ` +
                      `${sha.slice(0, 16)}... ${sha === gp.ir_sha256[keys[b][side]] ? 'equals' : 'differs from'} ${keys[b][side]}`);
            else
                check(ok && nonzero === 0, `impulse in bin ${names[b]}, ${side ? 'right' : 'left'}: silent`);
        }
    }
}

// ---- digest, as run_digest in test_k7.c
let h = 0xCBF29CE484222325n;
const P = 0x100000001B3n, MASK = 0xFFFFFFFFFFFFFFFFn;
function fold(u) {
    for (let i = 0; i < 4; i++) {
        h ^= BigInt((u >>> (8 * i)) & 0xFF);
        h = (h * P) & MASK;
    }
}
check(start() === 0, 'xb_init and xb_audio_init for the digest');
for (let n = 0; n < DIGEST_BLOCKS; n++) {
    x.xb_audio_block(stateAt, outAt);
    const nc = u32(outAt + 8), nf = u32(outAt + 12);
    fold(u32(outAt + 4));
    fold(nc);
    fold(nf);
    for (let i = 0; i < nc; i++) {
        const at = outAt + 16 + 8 * i;
        fold(u32(at));
        fold(u8(at + 6) << 8 | u8(at + 7));
    }
    for (let i = 0; i < nf; i++) {
        const at = outAt + 144 + 16 * i;
        fold(u32(at));
        fold(u32(at + 4));
        fold(u8(at + 10) << 16 | u8(at + 11) << 8 | u8(at + 12));
    }
    for (let i = 0; i < 3 * BLOCK; i++) fold(u32(outAt + 2640 + 4 * i));
    for (let i = 0; i < 2 * BLOCK; i++) fold(u32(outAt + 3024 + 4 * i));
}
console.log(`digest ${h.toString(16).toUpperCase().padStart(16, '0')} ${DIGEST_BLOCKS}`);
console.log(`${checks} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
