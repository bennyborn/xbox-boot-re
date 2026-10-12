// Acceptance of W0: pictures and sound on demand from the page in headless
// Chrome, each compared with the same calls of the core made here in node.
//
//     node hosts/web/test_w0.mjs
//
// 1. pictures at chosen times of a run at 60 Hz: the page's tick, frame
//    number, time values and counts equal node's, and the picture read back
//    from the GPU shows exactly the three bars those values give
// 2. the same pictures again: every byte equal
// 3. a picture after an explicit list of ticks
// 4. sound through the worklet, offline: every sample equal to xb_audio here,
//    from the start and from a place in the middle (the worklet's seek)
// 5. the transport over Chrome's debugging port: frame on and back, the time
//    line, play and stop, the end, play with sound
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { join } from 'node:path';
import { ROOT } from './serve.mjs';
import { shotsAt, shotOfTicks, soundOf, drive, WIDTH, HEIGHT } from './shot.mjs';
import { Core, Run, DRAWS_MAX, PARAMS_MAX } from './src/core.js';
import { BARS, BACKGROUND } from './src/gpu.js';

let checks = 0, failed = 0;
const check = (ok, what) => { checks++; if (!ok) failed++; console.log(`${ok ? 'ok  ' : 'FAIL'}  ${what}`); };

const wasm = readFileSync(join(ROOT, 'out', 'core', 'wasm', 'xboot.wasm'));
const pack = readFileSync(join(ROOT, 'out', 'pack', 'xboot.pak'));
const layout = JSON.parse(readFileSync(join(ROOT, 'hosts', 'web', 'layout.json')));
const buf = (b) => b.buffer.slice(b.byteOffset, b.byteOffset + b.length);
const core = await Core.load(buf(wasm), buf(pack), layout);
const hex = (u) => u.toString(16).toUpperCase().padStart(8, '0');

// What node's core says about the frame a run stands at, as main.js reports it.
function here(run) {
    const tm = run.core.timing(), fr = run.core.frame();
    return { tick: run.tick, frames: run.ticks.length, rc: run.rc, tBits: hex(tm.tBits), x18: tm.x18, x1c: tm.x1c,
             x20: tm.x20, x24: tm.x24, drawCount: fr.drawCount, paramCount: fr.paramCount,
             values: [Math.min(tm.t / 8, 1), fr.drawCount / DRAWS_MAX, fr.paramCount / PARAMS_MAX] };
}
const sameMeta = (page, node) => Object.keys(node).filter((k) => JSON.stringify(page[k]) !== JSON.stringify(node[k]));

// Pixels of the picture that are not what the bars for `values` look like.
// The GPU compares (x + 0.5) / 640 with a float32; the pixel at the edge of
// a bar may fall either way and is not counted.
function wrongPixels(rgba, values) {
    let wrong = 0;
    for (let y = 0; y < HEIGHT; y++) {
        const bar = BARS.findIndex((b) => y >= b.y0 && y < b.y1);
        const edge = bar < 0 ? -1 : Math.fround(values[bar]) * WIDTH - 0.5;
        for (let x = 0; x < WIDTH; x++) {
            if (bar >= 0 && Math.abs(x - edge) < 1)
                continue;
            const want = bar >= 0 && x < edge ? BARS[bar].rgb : BACKGROUND;
            const at = 4 * (y * WIDTH + x);
            if (rgba[at] !== want[0] || rgba[at + 1] !== want[1] || rgba[at + 2] !== want[2] || rgba[at + 3] !== 255)
                wrong++;
        }
    }
    return wrong;
}

const times = [0, 0.017, 0.5, 2, 5.2, 6.1, 7.99, 9];
const first = await shotsAt(times, 60, 'pattern=1');
check(first.length === times.length, `${first.length} pictures for ${times.length} times`);
const run = new Run(core);
for (const [i, s] of first.entries()) {
    run.seek(Math.round(times[i] * 1000), 60);
    const node = here(run), diff = sameMeta(s.meta, node), wrong = wrongPixels(s.rgba, node.values);
    check(diff.length === 0 && wrong === 0 && s.rgba.length === WIDTH * HEIGHT * 4,
          `t = ${times[i]}: tick ${s.meta.tick}, frame ${s.meta.frames}, t ${s.meta.tBits}, ${s.meta.drawCount} draws, ` +
          `${s.meta.paramCount} values, rc ${s.meta.rc}; differs from node in [${diff}], ${wrong} pixels off the bars`);
}

const second = await shotsAt(times, 60, 'pattern=1');
const unequal = first.filter((s, i) => Buffer.compare(s.rgba, second[i].rgba) !== 0).length;
check(second.length === first.length && unequal === 0, `the same ${times.length} pictures again: ${unequal} differ`);

const ticks = [20, 33, 51, 70, 1500, 1516, 1533, 2600];
const shot = await shotOfTicks(ticks, 'pattern=1');
run.replay(ticks);
{
    const node = here(run), diff = sameMeta(shot.meta, node), wrong = wrongPixels(shot.rgba, node.values);
    check(diff.length === 0 && wrong === 0,
          `after ticks ${ticks.join(', ')}: t ${shot.meta.tBits}, ${shot.meta.drawCount} draws; differs from node in ` +
          `[${diff}], ${wrong} pixels off the bars`);
}

// The sound here: the first `total` frames in one piece.
function soundHere(total) {
    core.init(0, 0);
    core.initSound(4800);
    const out = new Int16Array(2 * total);
    for (let done = 0; done < total; done += 4800) {
        const n = Math.min(4800, total - done);
        out.set(core.audio(n), 2 * done);
    }
    return out;
}
const FRAMES = 96000, FROM = 120000;
const whole = soundHere(FROM + FRAMES);
const differ = (a, b) => { let n = a.length === b.length ? 0 : -1; for (let i = 0; n >= 0 && i < a.length; i++) n += a[i] !== b[i]; return n; };
const sha = (a) => createHash('sha256').update(Buffer.from(a.buffer, a.byteOffset, a.byteLength)).digest('hex').slice(0, 16);
for (const from of [0, FROM]) {
    const page = await soundOf(FRAMES, from);
    const node = whole.subarray(2 * from, 2 * (from + FRAMES));
    const loud = node.reduce((n, v) => n + (v !== 0), 0);
    check(differ(page.samples, node) === 0 && loud > FRAMES,
          `sound, ${FRAMES} frames from frame ${from}: ${differ(page.samples, node)} of ${node.length} samples differ ` +
          `from xb_audio in node, ${loud} not zero, SHA-256 ${sha(page.samples)}… and ${sha(node)}…`);
}

// The transport, used as a person would: the buttons and the time line.
const session = await drive(`(async () => {
    const $ = (id) => document.getElementById(id);
    const wait = (ms) => new Promise((ok) => setTimeout(ok, ms));
    while ($('status').textContent.startsWith('l')) await wait(50);
    // the page plays on its own and loops: stopped, at the start, no loop
    if ($('play').textContent.includes('Pause')) $('play').click();
    $('loop').checked = false;
    $('loop').dispatchEvent(new Event('change'));
    $('start').click();
    const at = () => [Number(/tick\\s+(\\d+)/.exec($('values').textContent)[1]),
                      Number(/frame (\\d+)/.exec($('values').textContent)[1])];
    const seek = (ms) => { $('timeline').value = ms; $('timeline').dispatchEvent(new Event('input')); };
    const out = { status: $('status').textContent, start: at() };
    $('sound').checked = false;
    for (let i = 0; i < 3; i++) $('forward').click();
    out.forward3 = at();
    $('back').click();
    out.back = at();
    seek(2000);
    out.seek = at();
    $('play').click();
    await wait(600);
    $('play').click();
    out.played = at();
    await wait(300);
    out.paused = at();
    $('start').click();
    out.restart = at();
    seek(7600);
    $('play').click();
    await wait(1500);
    out.end = [...at(), $('values').textContent.includes('End'), $('play').textContent];
    $('sound').checked = true;
    seek(1000);
    $('play').click();
    await wait(1500);
    $('play').click();
    out.withSound = at();
    return out;
})()`);
const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
check(eq(session.start, [0, 0]) && eq(session.forward3, [50, 3]) && eq(session.back, [34, 2]),
      `transport: starts at tick ${session.start}, three frames on at ${session.forward3}, one back at ${session.back} (${session.status})`);
check(eq(session.seek, [2000, 120]), `time line to 2000 ms: tick and frame ${session.seek}`);
check(session.played[0] > 2300 && session.played[0] < 3000 && session.played[1] > 125 && eq(session.played, session.paused),
      `played for 0.6 s from there: tick and frame ${session.played}, 0.3 s after the stop ${session.paused}`);
check(eq(session.restart, [0, 0]), `back to the start: ${session.restart}`);
check(session.end[0] >= 8000 && session.end[2] === true && session.end[3].includes('Play'),
      `played from 7600 ms: stops by itself at tick ${session.end[0]}, frame ${session.end[1]}, end shown: ${session.end[2]}`);
check(session.withSound[0] > 1500 && session.withSound[0] < 3000,
      `played with sound from 1000 ms for 1.5 s: tick and frame ${session.withSound}`);

console.log(`${checks} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
