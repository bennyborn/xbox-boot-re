// Runs the K0 check against the wasm32 build of the core:
//
//     node core/test/wasm_k0.mjs <xboot.wasm> <pack> <c3_timing.json>
//
// The module must instantiate without imports. State, pack and config are
// placed in its linear memory from __heap_base on; the core allocates
// nothing itself.
import { readFileSync } from 'node:fs';

const [wasmPath, packPath, vecPath] = process.argv.slice(2);
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), {});
const x = instance.exports;
const pack = readFileSync(packPath);
const vec = JSON.parse(readFileSync(vecPath, 'utf8').replace(/\bNaN\b/g, 'null'));

let checks = 0, failed = 0;
const check = (ok, what) => { checks++; if (!ok) failed++; console.log(`${ok ? 'ok  ' : 'FAIL'}  ${what}`); };
const align = (n) => (n + 15) & ~15;
const f32bits = (f) => { const b = new DataView(new ArrayBuffer(4)); b.setFloat32(0, f, true); return b.getUint32(0, true); };
const hex = (u) => u.toString(16).toUpperCase().padStart(8, '0');

const stateAt = align(x.__heap_base.value);
const packAt = align(stateAt + x.xb_state_size());
const cfgAt = align(packAt + pack.length);
const need = cfgAt + 16;
if (need > x.memory.buffer.byteLength)
    x.memory.grow(Math.ceil((need - x.memory.buffer.byteLength) / 65536));
new Uint8Array(x.memory.buffer, packAt, pack.length).set(pack);
console.log(`state ${x.xb_state_size()} bytes, pack ${pack.length} bytes, memory ${x.memory.buffer.byteLength} bytes`);

check(vec.runs.length === 2, 'c3_timing.json has two runs');
for (const run of vec.runs) {
    const flag = run.checks.flag_0x8003aeb8;
    const short = flag !== undefined && !flag.startsWith('00');
    const tick = run.checks.start.tick_this_0x2c;
    const mem = new DataView(x.memory.buffer);
    mem.setUint32(cfgAt, short ? 1 : 0, true);
    mem.setUint32(cfgAt + 4, tick, true);
    const rc = x.xb_init(stateAt, packAt, pack.length, cfgAt);
    check(rc === 0, `${run.run}: xb_init = ${rc}`);
    const got = f32bits(x.xb_time(stateAt)), want = f32bits(run.checks.start.t);
    check(got === want, `${run.run}: t = ${hex(got)}, vector ${hex(want)} (${run.checks.start.t})`);
    const last = x.xb_last_tick(stateAt) >>> 0;
    check(last === tick, `${run.run}: setup tick kept: ${last}, given ${tick} (this+0x2C)`);
}
console.log(`${checks} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
