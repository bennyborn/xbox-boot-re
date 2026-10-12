// Runs the K1 checks of test_k1.c for both runs against the wasm32 build:
//
//     node core/test/wasm_k1.mjs <xboot.wasm> <pack> <c3_timing.json>
//
// Pulse table, generator state, every frame step bit for bit, return codes,
// t after the loop. xb_timing is read from linear memory with the layout of
// xboot.h on wasm32: t, x18, x1c, x20, x24 (5 floats), last_tick,
// setup_tick, rng (3 u32), pulse[12][3] (36 floats); 176 bytes.
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
const tmAt = align(cfgAt + 16);
const need = tmAt + 176;
if (need > x.memory.buffer.byteLength)
    x.memory.grow(Math.ceil((need - x.memory.buffer.byteLength) / 65536));
new Uint8Array(x.memory.buffer, packAt, pack.length).set(pack);
console.log(`state ${x.xb_state_size()} bytes, pack ${pack.length} bytes, memory ${x.memory.buffer.byteLength} bytes`);

function timing() {
    if (x.xb_get_timing(stateAt, tmAt) !== 0) throw new Error('xb_get_timing failed');
    const m = new DataView(x.memory.buffer, tmAt, 176);
    const u = (i) => m.getUint32(4 * i, true);
    return {
        bits: [0, 1, 2, 3, 4].map(u),
        last: u(5), setup: u(6), rng: u(7),
        pulse: Array.from({ length: 36 }, (_, i) => u(8 + i)),
    };
}
const tableEqual = (tab, tm) => tab.flat().every((v, i) => f32bits(v) === tm.pulse[i]);

check(vec.runs.length === 2, 'c3_timing.json has two runs');
for (const run of vec.runs) {
    const c = run.checks;
    const flag = c.flag_0x8003aeb8;
    const short = flag !== undefined && !flag.startsWith('00');
    const mem = new DataView(x.memory.buffer);
    mem.setUint32(cfgAt, short ? 1 : 0, true);
    mem.setUint32(cfgAt + 4, c.start.tick_this_0x2c, true);
    check(x.xb_init(stateAt, packAt, pack.length, cfgAt) === 0, `${run.run}: xb_init`);

    let tm = timing();
    check(tableEqual(c.pulse_table.table_guest, tm), `${run.run}: pulse table equals the guest's`);
    check(tableEqual(vec.pulse_table_model, tm), `${run.run}: pulse table equals the model's`);
    if (c.pulse_table.guest_state_this_0x68c)
        check(tm.rng === parseInt(c.pulse_table.guest_state_this_0x68c, 16),
              `${run.run}: generator ${hex(tm.rng)}, guest ${c.pulse_table.guest_state_this_0x68c}`);

    let bad = 0, rcBad = 0, shown = 0;
    run.steps.forEach((row, i) => {
        const rc = x.xb_advance(stateAt, row[1]);
        tm = timing();
        for (let k = 0; k < 5; k++) {
            const want = f32bits(row[3 + k]);
            if (tm.bits[k] !== want) {
                bad++;
                if (shown++ < 5) console.log(`      step ${i} field ${k}: core ${hex(tm.bits[k])}, guest ${hex(want)}`);
            }
        }
        if (rc !== (i === run.steps.length - 1 ? 1 : 0)) rcBad++;
    });
    check(bad === 0, `${run.run}: ${run.steps.length} steps, ${5 * run.steps.length} values, ${bad} differ from the guest`);
    check(rcBad === 0, `${run.run}: XB_OK before the last step, XB_DONE at it (${rcBad} wrong)`);
    check(tm.bits[0] === f32bits(c.end.t_after_loop), `${run.run}: t after the loop ${hex(tm.bits[0])}, guest ${hex(f32bits(c.end.t_after_loop))}`);
    check(((tm.last - tm.setup) >>> 0) === c.end.ticks_total, `${run.run}: ticks in the run ${(tm.last - tm.setup) >>> 0}, guest ${c.end.ticks_total}`);
}
console.log(`${checks} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
