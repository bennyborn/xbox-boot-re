// Acceptance of W3: the workbench. Every part of the picture can be seen
// alone and leads to its specification.
//
//     node hosts/web/test_w3.mjs
//
// 1. xb_frame_from with the path's own camera gives the frame of xb_frame,
//    byte for byte, over a whole run; with another camera the same draws
//    in the same order, the light views unchanged, the screen's constants
//    changed
// 1b. xb_select_path: the drawn path changes nothing, another path only the camera
// 2. the page: &cam=path draws the same picture as without it
// 3. each part of bench.js, PARTS, alone: lit pixels where it is drawn,
//    none where it is not; a click on one of its pixels picks it
// 4. wireframe, the camera's path from outside, every target and texture
//    shown in place of the picture
// 5. every link: the narrative's section and the rows of
//    docs/decomp/README.md exist, for parts, targets, programs and events;
//    the server serves docs/ and nothing beside it
// 6. the page used as a person would: a part alone, a click on the
//    picture, an event of the time line, every button § of the panel
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT, startServer } from './serve.mjs';
import { shotsAt, drive } from './shot.mjs';
import { Core, Run } from './src/core.js';
import { PARTS, TARGETS, OTHER, PROGRAMS, events, readmeRow, section } from './src/bench.js';

let checks = 0, failed = 0;
const check = (ok, what) => { checks++; if (!ok) failed++; console.log(`${ok ? 'ok  ' : 'FAIL'}  ${what}`); };

const wasm = readFileSync(join(ROOT, 'out', 'core', 'wasm', 'xboot.wasm'));
const pack = readFileSync(join(ROOT, 'out', 'pack', 'xboot.pak'));
const layout = JSON.parse(readFileSync(join(ROOT, 'hosts', 'web', 'layout.json')));
const buf = (b) => b.buffer.slice(b.byteOffset, b.byteOffset + b.length);
const core = await Core.load(buf(wasm), buf(pack), layout);

// ---- 1. the core
const DRAW = layout.xb_draw.size;
const frameBytes = (look) => {
    const fr = core.frame(look);
    return { fr, draws: core.bytes(core.drawsAt, DRAW * fr.drawCount).slice(), params: core.bytes(core.params, 4 * fr.paramCount).slice(),
             list: core.draws(fr.drawCount) };
};
const same = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
{
    const run = new Run(core);
    let frames = 0, equal = 0, draws = 0;
    while (!run.done) {
        run.step(60);
        if (run.ticks.length % 7)
            continue;
        const c = core.camera(core.timing().t), a = frameBytes(), b = frameBytes({ eye: c.eye, target: c.target, up: c.up });
        frames++;
        draws += a.fr.drawCount;
        equal += same(a.draws, b.draws) && same(a.params, b.params);
    }
    check(frames > 60 && equal === frames, `xb_frame_from with the path's camera: ${equal} of ${frames} frames (${draws} draws) equal xb_frame byte for byte`);
}
{
    const run = new Run(core);
    run.seek(3200, 60);
    const a = frameBytes(), b = frameBytes({ eye: [60, -40, 30], target: [0, 0, 0], up: [0, 0, 1] });
    const order = a.list.length === b.list.length && a.list.every((d, i) => d.step === b.list[i].step && d.what === b.list[i].what && d.part === b.list[i].part);
    const u1 = (f) => f.list.filter((d) => d.step === 0x0B01).flatMap((d) => [d.world, d.view, d.proj].flatMap((i) => Array.from(core.paramValues.subarray(i, i + 16))));
    const fa = frameBytes(), la = u1(fa), fb = frameBytes({ eye: [60, -40, 30], target: [0, 0, 0], up: [0, 0, 1] }), lb = u1(fb);
    check(order && same(la, lb) && !same(a.params, b.params),
          `another camera at t = 3.2: the same ${a.list.length} draws in the same order, the light views' matrices unchanged (${la.length} values), the constants changed`);
    let rc = 0;
    try { core.frame({ eye: [0, 0, 5], target: [0, 0, 0], up: [0, 0, 1] }); } catch (e) { rc = e.message; }
    check(rc === 'xb_frame: -1', `a camera looking along up is refused: ${rc}`);
}
// W3b (docs/web.md 11): xb_select_path with the path xb_init draws changes
// nothing; another path changes the camera and nothing that hangs on the
// generator; a fifth path is refused.
{
    const lists = (path) => {
        const run = new Run(core), out = [];
        run.path = path;
        run.restart();
        while (!run.done) {
            run.step(60);
            if (run.ticks.length % 7 === 0)
                out.push(frameBytes());
        }
        return { out, pulses: core.pulses().flat(), path: core.camera(0).path };
    };
    const a = lists(null), b = lists(0), c = lists(2);
    // The sign of a NaN that arithmetic makes is not fixed in WebAssembly and
    // changes with the engine's tier; the parts with NaN until t = 1.17
    // (Chamber.md 6) show it. Here every NaN counts as the same value.
    const values = (bytes) => Array.from(new Float32Array(bytes.buffer, bytes.byteOffset, bytes.length / 4), (x) => (Number.isNaN(x) ? 'NaN' : x));
    const equal = a.out.filter((f, i) => same(f.draws, b.out[i].draws) && same(values(f.params), values(b.out[i].params))).length;
    check(a.path === 0 && equal === a.out.length,
          `xb_select_path(0), the drawn path: ${equal} of ${a.out.length} frames equal the run without it (draws byte for byte, values with NaN as NaN)`);
    check(c.path === 2 && same(a.pulses, c.pulses) && !same(a.out[30].params, c.out[30].params),
          `xb_select_path(2): camera path 2, the same ${a.pulses.length} values of the pulses, other constants`);
    core.init(0, 0);
    const ok = [0, 1, 2, 3, 4].map((p) => core.selectPath(p));
    check(ok.join() === 'true,true,true,true,false', `paths 0 to 4 taken: ${ok.join(', ')} (Camera.md 5.1: four paths)`);
}

// ---- 2. to 4. the page
const lit = (rgba) => { let n = 0; for (let i = 0; i < rgba.length; i += 4) n += rgba[i] + rgba[i + 1] + rgba[i + 2] > 0; return n; };
const firstLit = (rgba) => { for (let i = 0; i < rgba.length; i += 4) if (rgba[i] + rgba[i + 1] + rgba[i + 2] > 30) return [(i / 4) % 640, Math.floor(i / 4 / 640)]; return null; };
{
    const [plain] = await shotsAt([3.2], 60);
    const [path] = await shotsAt([3.2], 60, 'cam=path');
    check(Buffer.compare(plain.rgba, path.rgba) === 0, `&cam=path at t = 3.2: the picture equals the one without, ${lit(plain.rgba)} lit pixels`);
}
const WHEN = { chamber0: 3.2, chamber1: 3.2, chamber2: 3.2, chamber3: 3.2, chamber4: 3.2, chamber5: 3.2, blob_glow: 3.2, blob_mesh: 3.2,
               blob_drop: 3.2, logo_a: 5.9, logo_b: 5.9, logo_c: 5.9, logo_d: 5.9, logo_tm: 6.15, logo_text: 6.15, logo_image: 6.15,
               veil: 5.1, glow: 0.1 };
for (const [key, t] of Object.entries(WHEN)) {
    const [alone, absent] = await shotsAt([t, t < 5 ? 7 : 1], 60, `solo=${key}`);
    const at = firstLit(alone.rgba);
    const picked = at && (await shotsAt([t], 60, `solo=${key}&pick=${at.join(',')}`))[0].meta.picked;
    check(lit(alone.rgba) > 0 && lit(absent.rgba) === 0 && picked?.key === key,
          `${key} alone at t = ${t}: ${lit(alone.rgba)} lit pixels, at t = ${t < 5 ? 7 : 1} ${lit(absent.rgba)}; ` +
          `a click at (${at}) picks ${picked?.key}`);
}
{
    const [whole] = await shotsAt([3.2], 60, 'pick=320,200');
    const [early] = await shotsAt([0.3], 60, 'pick=320,240');
    check(whole.meta.picked?.key.startsWith('blob') && ['veil', 'glow'].includes(early.meta.picked?.key),
          `in the whole picture a click on the blob at t = 3.2 picks ${whole.meta.picked?.key} (not the veil over it), ` +
          `at t = 0.3, where nothing else is drawn, ${early.meta.picked?.key}`);
    const [wire] = await shotsAt([3.2], 60, 'wire=2');
    const [over] = await shotsAt([3.2], 60, 'wire=1');
    const [plain] = await shotsAt([3.2], 60);
    check(lit(wire.rgba) > 10000 && Buffer.compare(over.rgba, plain.rgba) !== 0,
          `wireframe at t = 3.2: alone ${lit(wire.rgba)} lit pixels; over the picture it differs from the picture`);
    const [outside] = await shotsAt([3.2], 60, 'cam=400,-600,300,0,0,0&path=1&solo=none');
    let yellow = 0;
    for (let i = 0; i < outside.rgba.length; i += 4)
        yellow += outside.rgba[i] > 60 && outside.rgba[i + 1] > 50 && outside.rgba[i + 2] < outside.rgba[i] / 2;
    check(yellow > 200, `the camera's path from outside, nothing else drawn: ${yellow} yellow pixels`);
}
for (const [view, t] of [['light0', 3.2], ['light1', 3.2], ['fx', 3.2], ['logo', 6.5], ...Array.from({ length: 9 }, (_, i) => [`tex${i}`, 1])]) {
    const [s] = await shotsAt([t], 60, `view=${view}`);
    const colours = new Set();
    for (let i = 0; i < s.rgba.length; i += 4 * 97)
        colours.add(s.rgba.readUInt32LE(i));
    check(colours.size > 8, `${view} in place of the picture at t = ${t}: ${colours.size} colours in a sample`);
}

// ---- 5. the links
const readme = readFileSync(join(ROOT, 'docs', 'decomp', 'README.md'), 'utf8');
const narrative = (f) => readFileSync(join(ROOT, 'docs', 'decomp', f), 'utf8');
{
    const bad = [];
    let n = 0;
    for (const [key, e] of Object.entries({ ...PARTS, ...TARGETS, ...OTHER })) {
        n++;
        if (!section(narrative(e.file), e.sec))
            bad.push(`${key}: ${e.file} ${e.sec}`);
        for (const a of e.rows)
            if (!readmeRow(readme, a))
                bad.push(`${key}: row 0x${a.toString(16)}`);
    }
    for (const sec of Object.values(PROGRAMS).concat(['2.1', '4.3', '5']))
        if (!section(narrative('GPU.md'), sec))
            bad.push(`GPU.md ${sec}`);
    for (const [file, sec] of [['Chamber.md', '4.1'], ['Chamber.md', '5'], ['Chamber.md', '6']])
        if (!section(narrative(file), sec))
            bad.push(`${file} ${sec}`);
    const run = new Run(core);
    const ev = events(core);
    for (const e of ev)
        if (!section(narrative(e.file), e.sec))
            bad.push(`event ${e.what}: ${e.file} ${e.sec}`);
    const rows = Object.values({ ...PARTS, ...TARGETS, ...OTHER }).reduce((k, e) => k + e.rows.length, 0);
    check(bad.length === 0, `${n} entries with ${rows} rows of README.md, ${Object.keys(PROGRAMS).length} programs, ${ev.length} events: ` +
          `every section and row found${bad.length ? '; missing: ' + bad.join(', ') : ''}`);
    const logo = ev.find((e) => e.what.startsWith('Logo on')), off = ev.find((e) => e.what.startsWith('Chamber and blob off'));
    check(logo && Math.abs(logo.t - 5.225) < 0.01 && off && Math.abs(off.t - 5.936) < 0.01 && ev.filter((e) => e.pulse).length === 12,
          `events from the run: logo on at t = ${logo?.t}, chamber off at ${off?.t} (Camera.md 5.5: ≈ 5.225, ≈ 5.936), 12 pulses`);
    run.restart();
}
{
    const { server, url } = await startServer();
    const get = (p) => fetch(url + p.replace(/^\//, '')).then((r) => r.status);
    const ok = await get('/docs/decomp/README.md'), md = await get('/docs/web.md');
    const outside = [await get('/docs/../CLAUDE.md'), await get('/docs/%2e%2e/CLAUDE.md'), await get('/docs/vectors/c3_timing.json')];
    server.close();
    check(ok === 200 && md === 200 && outside.every((s) => s === 404),
          `the server: docs/decomp/README.md ${ok}, docs/web.md ${md}; beside docs/ or not Markdown: ${outside.join(', ')}`);
}

// ---- 6. the page by hand
const session = await drive(`(async () => {
    const $ = (id) => document.getElementById(id);
    const wait = (ms) => new Promise((ok) => setTimeout(ok, ms));
    while ($('status').textContent.startsWith('l') || !$('spec').innerText) await wait(50);
    // the page plays on its own: stopped first
    if ($('play').textContent.includes('Pause')) $('play').click();
    // choosing parts in the picture is off until switched on
    if (!$('sel').checked) $('sel').click();
    const seek = (ms) => { $('timeline').value = ms; $('timeline').dispatchEvent(new Event('input')); };
    const specFor = async (click) => { $('spec').innerHTML = ''; click(); for (let i = 0; i < 100 && !$('spec').innerHTML; i++) await wait(50); return $('spec').innerText; };
    const out = { start: $('spec').innerText.slice(0, 60) };
    seek(3200);
    out.solo = await specFor(() => document.querySelector('[data-solo=blob_mesh]').click());
    const r = $('screen').getBoundingClientRect();
    const tap = (type) => $('screen').dispatchEvent(new PointerEvent(type, { clientX: r.left + r.width / 2, clientY: r.top + r.height / 2, pointerId: 1, bubbles: true }));
    out.click = await specFor(() => { tap('pointerdown'); tap('pointerup'); });
    out.event = await specFor(() => [...document.querySelectorAll('.event')].find((b) => b.title.includes('Flash')).click());
    out.eventTick = Number(/tick\\s+(\\d+)/.exec($('values').textContent)[1]);
    out.specs = [];
    for (const b of document.querySelectorAll('[data-spec]'))
        out.specs.push([b.dataset.spec, (await specFor(() => b.click())).includes('not found')]);
    document.querySelector('[data-solo=blob_mesh]').click();
    tap('pointerdown');
    $('screen').dispatchEvent(new PointerEvent('pointermove', { clientX: r.left + r.width / 2 + 60, clientY: r.top + r.height / 2, pointerId: 1, bubbles: true }));
    tap('pointerup');
    out.free = [$('free').checked, $('statusline').textContent.includes('free camera')];
    out.errors = $('status').textContent;
    return out;
})()`, { timeout: 120000 });
check(session.start.includes('Order'), `the page opens with the order of a frame: "${session.start.split('\n')[0]}"`);
check(session.solo.includes('Blob.md, section 7.2') && session.solo.includes('0x800583BF'),
      `"alone" for the blob's mesh opens Blob.md 7.2 and its rows`);
check(session.click.includes('Blob.md, section 7.2') && session.click.includes('GPU.md, section 3.4'),
      `a click on the middle of the blob alone opens its narrative and its program (GPU.md 3.4)`);
check(session.event.includes('Effects.md, section 4') && Math.abs(session.eventTick - 5000) <= 17,
      `the event of the flash goes to tick ${session.eventTick} and opens Effects.md 4`);
check(session.specs.length >= 27 && session.specs.every(([, missing]) => !missing),
      `${session.specs.length} buttons §: every one opens its section${session.specs.filter(([, m]) => m).map(([k]) => ' ' + k).join('')}`);
check(session.free[0] && session.free[1] && session.errors.startsWith('WebGPU:') && !session.errors.includes('rror'),
      `a drag turns on the free camera and the status line shows it; status "${session.errors}"`);

console.log(`${checks} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
