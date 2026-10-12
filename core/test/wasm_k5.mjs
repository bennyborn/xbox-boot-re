// Runs the K5 checks of test_k5.c against the wasm32 build:
//
//     node core/test/wasm_k5.mjs <xboot.wasm> <pack> <c8_gpu.json>
//
// The seed of the noise, then for each of the eleven textures the format
// word, the length and the SHA-256 against the guest's dump (vector,
// section "textures"), then the digest of all textures with the algorithm
// of test_k5.c. Prints "digest <hex> 11" for core/build.py, which compares
// it with the native build.
//
// Layouts of xboot.h on wasm32, all fields 4 bytes: xb_texture_desc 32
// bytes (width, height, levels, faces, format, texel_bytes, format_word
// @24, bytes @28); xb_timing 176 bytes (rng @28).
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
const DESC = 32, TIMING = 176, TEX_MAX = 6 * 128 * 128 * 4, TEXTURES = 11;

const stateAt = align(x.__heap_base.value);
const packAt = align(stateAt + x.xb_state_size());
const cfgAt = align(packAt + pack.length);
const descAt = align(cfgAt + 16);
const tmAt = align(descAt + DESC);
const bufAt = align(tmAt + TIMING);
const need = bufAt + TEX_MAX + 16;
if (need > x.memory.buffer.byteLength)
    x.memory.grow(Math.ceil((need - x.memory.buffer.byteLength) / 65536));
new Uint8Array(x.memory.buffer, packAt, pack.length).set(pack);
console.log(`state ${x.xb_state_size()} bytes, pack ${pack.length} bytes, memory ${x.memory.buffer.byteLength} bytes`);

const dv = () => new DataView(x.memory.buffer);
const u32 = (at) => dv().getUint32(at, true);
const bytes = (at, n) => new Uint8Array(x.memory.buffer, at, n);

function init() {
    dv().setUint32(cfgAt, 0, true);
    dv().setUint32(cfgAt + 4, 0, true);
    return x.xb_init(stateAt, packAt, pack.length, cfgAt);
}

check(init() === 0, 'xb_init');
x.xb_get_timing(stateAt, tmAt);
check(u32(tmAt + 28) === parseInt(vec.textures.fx_seed, 16),
      `seed of the noise ${u32(tmAt + 28).toString(16)}, vector fx_seed ${vec.textures.fx_seed}`);
const cases = vec.textures.cases;
check(cases.length === TEXTURES, 'c8_gpu.json: eleven textures made on the CPU');
cases.forEach((c, i) => {
    x.xb_texture_info(stateAt, i, descAt);
    const word = u32(descAt + 24), n = u32(descAt + 28);
    const rc = x.xb_texture(stateAt, i, bufAt, TEX_MAX);
    const sha = createHash('sha256').update(bytes(bufAt, n)).digest('hex');
    check(rc === 0 && c.differ === 0 && word === parseInt(c.format, 16) && n === c.bytes && sha === c.sha256,
          `${c.field}: format ${word.toString(16)}, ${n} bytes, SHA-256 ${sha.slice(0, 16)}... ` +
          `${sha === c.sha256 ? 'equals' : 'differs from'} the guest's dump`);
});

// ---- digest, as run_digest in test_k5.c
let h = 0xCBF29CE484222325n;
const P = 0x100000001B3n, MASK = 0xFFFFFFFFFFFFFFFFn;
function fold(u) {
    for (let i = 0; i < 4; i++) {
        h ^= BigInt((u >>> (8 * i)) & 0xFF);
        h = (h * P) & MASK;
    }
}
const foldW = (at, n) => { for (let i = 0; i + 4 <= n; i += 4) fold(u32(at + i)); };

check(init() === 0, 'xb_init for the digest');
for (let i = 0; i < TEXTURES; i++) {
    x.xb_texture_info(stateAt, i, descAt);
    x.xb_texture(stateAt, i, bufAt, TEX_MAX);
    foldW(descAt, DESC);
    foldW(bufAt, u32(descAt + 28));
}
console.log(`digest ${h.toString(16).toUpperCase().padStart(16, '0')} ${TEXTURES}`);
console.log(`${checks} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
