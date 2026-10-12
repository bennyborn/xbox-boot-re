// The workbench for a person (docs/web.md 10 and 11): the stage with the
// canvas, a status line and hints, and a panel with the tabs Intro, Models,
// Blob and Sound over the parts every tab shares: playground, playback,
// export and the specification. The whole state stands in the URL's hash,
// so a link brings it back. Everything here is display and the host's own
// choice; what the animation is comes from the core.
import { Core, AUDIO_RATE } from './core.js';
import { Sound } from './sound.js';
import { WIDTH, HEIGHT } from './gpu.js';
import { LINE_FLOATS } from './renderer.js';
import { NextGen } from './nextgen.js';
import { ModelView } from './models.js';
import { TrackView, analyse, SAMPLES, SAMPLE_COLOUR, CALLS } from './tracks.js';
import { PARTS, TARGETS, OTHER, PROGRAMS, describe, events, phase, readmeRow, section, markdown } from './bench.js';

const $ = (id) => document.getElementById(id);
const UP = [0, 0, 1];
const RUN_END = 8000;     // t >= 8 ends the run (Timing.md 3)

// ---- the state in the URL ---------------------------------------------------------

const DEFAULTS = {
    tab: 'intro', t: 0,
    // intro
    path: -1, orig: 1, coarse: 0, halo: 1, shadow: 1, free: '', cpath: 0, wire: 0, view: 'picture', hide: '', solo: '',
    // models
    model: 'scene', msh: 'original', mli: 'camera', mbr: 1, mhl: -1, mchk: '',
    // blob
    bg: 'black', borig: 1, bwire: 0, bhalo: 1, bdrops: 1, bdiag: '',
    // sound
    mute: '', ssolo: '', vol: 1, scope: 'wave', layers: 'nveg',
    // playground and playback
    col: '', room: 0, speed: 1, loop: 1, snd: 1, nat: 0, sel: 0,
    // the next gen tab (nextgen.js, docs/web.md 12): quality, what of it
    ngq: 'high', ngfx: 'rgpbdia',
    // 1: stopped at t, so that a link shows that very frame
    pause: 0,
};

function readState() {
    const s = { ...DEFAULTS }, q = new URLSearchParams(location.hash.slice(1));
    for (const [k, v] of q)
        if (k in DEFAULTS)
            s[k] = typeof DEFAULTS[k] === 'number' ? Number(v) || 0 : v;
    // links from before the tab: ng=1 was a switch over the intro
    if (q.get('ng') === '1' && s.tab === 'intro')
        s.tab = 'nextgen';
    if (!['intro', 'models', 'blob', 'sound', 'nextgen'].includes(s.tab))
        s.tab = 'intro';
    return s;
}

let saveTimer = 0;
function saveState(S) {
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => {
        const q = new URLSearchParams();
        for (const [k, v] of Object.entries(S))
            if (v !== DEFAULTS[k])
                q.set(k, typeof v === 'number' ? String(Math.round(v * 1000) / 1000) : v);
        history.replaceState(null, '', location.pathname + (q.size ? '#' + q.toString().replace(/%2C/g, ',') : ''));
    }, 120);
}

const setOf = (s) => new Set(String(s).split(',').filter(Boolean));
const listOf = (set) => [...set].join(',');

// ---- the specification ------------------------------------------------------------

const docCache = new Map();
const doc = (file) => {
    if (!docCache.has(file))
        docCache.set(file, fetch('docs/' + file).then((r) => (r.ok ? r.text() : Promise.reject(new Error(`${file}: ${r.status}`)))));
    return docCache.get(file);
};

// Shows an entry of bench.js in the panel: its rows of docs/decomp/README.md
// and its section of the narrative. `more` are lines about the object
// itself, `also` further sections ([file, sec, title]).
async function openSpec(entry, more = [], also = []) {
    const readme = await doc('decomp/README.md');
    const rows = (entry.rows || []).map((a) => readmeRow(readme, a)).filter(Boolean);
    const sections = [[entry.file, entry.sec, entry.name], ...also];
    const parts = [`<h2>${entry.name}</h2>`];
    if (more.length)
        parts.push(`<pre class="facts">${more.join('\n')}</pre>`);
    if (rows.length)
        parts.push('<h3>docs/decomp/README.md</h3><table class="rows">' +
            rows.map((r) => `<tr>${r.map((c) => `<td>${markdown(c).replace(/^<p>|<\/p>$/g, '')}</td>`).join('')}</tr>`).join('') + '</table>');
    for (const [file, sec, title] of sections) {
        const text = section(await doc('decomp/' + file), sec);
        parts.push(`<h3>docs/decomp/${file}, section ${sec}${title && title !== entry.name ? ` (${title})` : ''}</h3>`);
        parts.push(text ? `<div class="md">${markdown(text)}</div>` : '<p>Section not found.</p>');
    }
    $('spec').innerHTML = parts.join('\n');
    $('spec').scrollTop = 0;
    $('specSec').open = true;
}

const hex = (u) => u.toString(16).toUpperCase().padStart(8, '0');
const v3 = (v) => v.map((x) => x.toFixed(3)).join(', ');
const STEP = { 0x0B01: 'U1', 0x0B02: 'U2', 0x0B03: 'U3', 0x0B04: 'U4', 0x0D01: 'D1', 0x0D02: 'D2', 0x0D03: 'D3', 0x0D04: 'D4', 0x0D05: 'D5' };
const GROUP = ['spheres', 'cylinders', 'boxes', 'rings', 'frustums', 'lathes'];

// What the panel says about a draw that was clicked.
function aboutDraw(core, { key, draw }) {
    const more = [`Step ${STEP[draw.step] || draw.step}, kind ${draw.what}, mesh ${draw.mesh}, primitive ${draw.prim}, ` +
                  `${draw.count} ${draw.flags & 1 ? 'indices' : 'vertices'} from ${draw.first}`];
    more.push(`Program ${draw.program > 0xFFFF ? '0x' + hex(draw.program) : 'fixed function 0x' + draw.program.toString(16)}, ` +
              `combiners ${draw.combiners ? '0x' + hex(draw.combiners) : 'from the stages'}`);
    const also = [];
    if (key.startsWith('chamber')) {
        const p = core.part(draw.part);
        more.unshift(`Part ${draw.part} of 271: group ${p.group} (${GROUP[p.group]}), number ${p.index} in the group, ` +
                     `form ${p.form}, position track ${p.posTrack}, rotation track ${p.rotTrack}, side ${p.side}, detail level ${draw.detail}`);
        also.push(['Chamber.md', '4.1', 'the six groups'], ['Chamber.md', '5', 'Placement'], ['Chamber.md', '6', 'Motion']);
    }
    if (key === 'blob_drop')
        more.unshift(`Drop ${draw.part}`);
    also.push(['GPU.md', PROGRAMS[draw.program] || (draw.combiners ? '4.3' : '5'), 'program and combiners']);
    return [describe(key), more, also];
}

// The name of what is under the pointer, short.
function nameOf(core, { key, draw }) {
    const name = describe(key)?.name || key;
    if (key.startsWith('chamber'))
        return [name, `Part ${draw.part}`];
    if (key === 'blob_drop')
        return [name, `Drop ${draw.part}`];
    return [name, STEP[draw.step] || ''];
}

// Two picks are the same thing: for the chamber the same part, for the
// drops the same drop, else the same kind of draw.
const sameAs = (got) => got && ((it) => it.key === got.key && it.draw.what === got.draw.what &&
    (!(got.key.startsWith('chamber') || got.key === 'blob_drop') || it.draw.part === got.draw.part));

// ---- colours of the playground -----------------------------------------------------

const ORIGINAL = '#40ff26';       // about the blob's colour (0.25, 1.0, 0.15) of Blob.md 2
const SWATCHES = [['Original', ORIGINAL], ['Blue', '#3d7bff'], ['Red', '#ff3b3b'], ['Purple', '#b04dff'],
                  ['Gold', '#ffc12e'], ['Turquoise', '#2ee6d6'], ['Pink', '#ff4fb0']];
const rgbOf = (h) => [1, 3, 5].map((i) => parseInt(h.slice(i, i + 2), 16) / 255);
const hexOf = (c) => '#' + c.map((x) => Math.round(Math.min(Math.max(x, 0), 1) * 255).toString(16).padStart(2, '0')).join('');
function hueOf([r, g, b]) {
    const mx = Math.max(r, g, b), mn = Math.min(r, g, b), d = mx - mn;
    if (!d)
        return 0;
    const h = mx === r ? (g - b) / d : mx === g ? (b - r) / d + 2 : (r - g) / d + 4;
    return ((h * 60) + 360) % 360;
}
// rotation about the grey axis by `deg` degrees, rows of a 3 x 3 matrix
function hueTurn(deg) {
    const a = deg * Math.PI / 180, c = Math.cos(a), s = Math.sin(a), k = 1 / 3, q = Math.sqrt(k);
    return [c + (1 - c) * k, (1 - c) * k - q * s, (1 - c) * k + q * s,
            (1 - c) * k + q * s, c + (1 - c) * k, (1 - c) * k - q * s,
            (1 - c) * k - q * s, (1 - c) * k + q * s, c + (1 - c) * k];
}
const turnColour = (rgb, deg) => { const m = hueTurn(deg); return [0, 1, 2].map((i) => m[3 * i] * rgb[0] + m[3 * i + 1] * rgb[1] + m[3 * i + 2] * rgb[2]); };

// ---- the workbench ------------------------------------------------------------------

export async function interactive(assets, { stage, render, pathLines }) {
    const st = await stage(assets), { run, gpu, renderer } = st, core = run.core;
    const S = readState();
    const save = () => saveState(S);
    let sound = null, playing = false, startMs = 0, startTime = 0, holdMs = 0, dirty = true, busy = false;
    let orbit = null;            // free camera: { target, dist, yaw, pitch }
    let lastFrame = null;        // what render() said about the last frame
    let chosen = null;           // the pick that was clicked
    const hide = setOf(S.hide);
    let solo = S.solo ? setOf(S.solo) : null;
    const models = new ModelView(gpu, core, renderer);
    const tracks = new TrackView($('tracks'), { onSeek: (ms) => seekTo(ms), onMute: () => muteChanged() });
    tracks.mute = new Set([...setOf(S.mute)].map(Number));
    tracks.solo = new Set([...setOf(S.ssolo)].map(Number));
    let analysis = null;

    const end = () => RUN_END;
    // the next gen picture, made the first time its tab is opened; it draws
    // the intro with the intro's camera path, halo and parts
    const ngActive = () => S.tab === 'nextgen';
    const nextGen = () => {
        if (!st.ng) {
            st.ng = new NextGen(gpu, core, renderer);
            $('ngNote').textContent = `BVH built over ${st.ng.rtTriangles.toLocaleString('en-US')} triangles in ${Math.round(st.ng.buildTime)} ms.`;
        }
        return st.ng;
    };
    const shownMs = () => (run.done ? Math.max(run.tick, holdMs) : run.tick);
    const tNow = () => core.timing().t;

    // --- paths: as many as the table has (Camera.md 5.1), the drawn one first
    const paths = [];
    {
        core.init(0, 0);
        const drawn = core.camera(0).path;
        for (let p = 0; p < 16 && core.selectPath(p); p++)
            paths.push({ p, keys: core.campath().nTable, drawn: p === drawn });
        $('path').innerHTML = paths.map((x) => `<option value="${x.p}">Path ${x.p} · ${x.keys} keys${x.drawn ? ' (default)' : ''}</option>`).join('');
        run.path = S.path >= 0 && paths.some((x) => x.p === S.path) ? S.path : null;
        run.restart();
        $('path').value = String(run.path ?? drawn);
    }

    // --- drawing ------------------------------------------------------------------

    const lookOf = (o) => {
        const c = Math.cos(o.pitch);
        return { eye: [0, 1, 2].map((a) => o.target[a] + o.dist * [c * Math.cos(o.yaw), c * Math.sin(o.yaw), Math.sin(o.pitch)][a]),
                 target: o.target, up: UP };
    };

    // lines in the world: LINE_FLOATS per vertex, two vertices a segment
    const segs = [];
    const seg = (a, b, c) => segs.push(...a, ...c, ...b, ...c);
    const cross = (p, r, c) => { for (let a = 0; a < 3; a++) { const d = [0, 0, 0]; d[a] = r; seg(p.map((x, i) => x - d[i]), p.map((x, i) => x + d[i]), c); } };

    let pathCache = null;
    // The camera's path: eye from t = 0 to 8 every 10 ms, its target, the
    // keys of the path, and eye and target now (Camera.md 5).
    const pathOverlay = () => {
        if (!pathCache || pathCache.path !== run.path) {
            const eyes = [], targets = [];
            for (let ms = 0; ms <= RUN_END; ms += 10) {
                const c = core.camera(ms / 1000);
                eyes.push(c.eye);
                targets.push(c.target);
            }
            pathCache = { path: run.path, eyes, targets, keys: core.campath() };
        }
        const { eyes, targets, keys } = pathCache;
        for (let i = 1; i < eyes.length; i++) {
            seg(eyes[i - 1], eyes[i], [1, 0.85, 0.2, 0.95]);
            seg(targets[i - 1], targets[i], [0.3, 0.85, 1, 0.5]);
        }
        keys.keys.forEach((k, i) => cross(k.eye, 1.6, i < keys.nTable ? [1, 1, 1, 1] : [1, 0.55, 0.2, 1]));
        cross(keys.look, 1.6, [0.3, 0.85, 1, 1]);
        const c = core.camera(tNow());
        cross(c.eye, 3, [1, 0.3, 0.8, 1]);
        cross(c.target, 3, [0.3, 0.85, 1, 1]);
        seg(c.eye, c.target, [1, 1, 1, 0.35]);
    };

    // Markers on the blob: each bulge where it lifts the surface (Blob.md
    // 6.4: k + R h along its direction), each drop where it is (2.2).
    const trails = Array.from({ length: 8 }, () => []);
    run.onRestart = ((prev) => (r) => { prev?.(r); trails.forEach((t) => (t.length = 0)); })(run.onRestart);
    run.onAdvance = ((prev) => (r) => {
        prev?.(r);
        if (S.tab === 'blob' && S.bdiag.includes('t')) {
            const b = core.blobFull();
            b.drop.forEach((d, i) => {
                const tr = trails[i], last = tr[tr.length - 1];
                if (last && Math.hypot(...d.pos.map((x, a) => x - last[a])) > 3)
                    tr.push(null);
                tr.push(d.pos);
                if (tr.length > 400)
                    tr.shift();
            });
        }
    })(run.onAdvance);
    const kOf = (tm, b) => Math.fround((Math.sqrt(Math.max(tm.x20 - tm.x1c, 0)) * 1.3 + 1) * b.radius);
    const blobOverlay = () => {
        const b = core.blobFull(), tm = core.timing(), k = kOf(tm, b);
        if (S.bdiag.includes('m')) {
            b.bulge.forEach((bu, i) => {
                const l = Math.hypot(...bu.c) || 1, p = bu.c.map((x, a) => b.center[a] + x / l * (k + b.radius * bu.h));
                const col = turnColour([1, 0.2, 0.2], i * 360 / 32);
                cross(p, 0.22, [...col, 1]);
            });
            b.drop.forEach((d) => {
                cross(d.pos, 0.3, [1, 1, 1, 1]);
                seg(b.center.map((x, a) => x + d.dir[a] * k), d.pos, [1, 1, 1, 0.3]);
            });
        }
        if (S.bdiag.includes('t'))
            trails.forEach((tr) => {
                for (let i = 1; i < tr.length; i++)
                    if (tr[i] && tr[i - 1])
                        seg(tr[i - 1], tr[i], [0.4, 0.9, 1, 0.15 + 0.75 * i / tr.length]);
            });
    };

    // The renderer as the tab and the state want it.
    const configure = () => {
        const r = renderer;
        r.off.delete('shadow');
        if (S.tab === 'blob') {
            const keep = ['blob_mesh', ...(S.bdrops ? ['blob_drop'] : []), ...(S.bhalo ? ['blob_glow'] : [])];
            r.solo = S.bg === 'scene' ? null : new Set(keep);
            r.hide = S.bg === 'scene' ? new Set([...(S.bdrops ? [] : ['blob_drop']), ...(S.bhalo ? [] : ['blob_glow', 'glow'])]) : new Set();
            r.background = { grey: [0.3, 0.31, 0.33], light: [0.74, 0.75, 0.76] }[S.bg] || null;
            r.flat = !S.borig;
            r.coarse = false;
            r.wire = S.bwire ? 1 : 0;
            r.show = 'picture';
            r.ghost = S.bdiag.includes('x') ? new Set(['blob_mesh']) : new Set();
            r.linesThrough = S.bdiag.includes('x');
        } else {
            r.hide = new Set([...hide, ...(S.halo ? [] : ['glow', 'blob_glow'])]);
            r.solo = solo;
            r.background = null;
            r.flat = !S.orig;
            r.coarse = !!S.coarse;
            r.wire = S.wire;
            r.show = S.view;
            r.ghost = new Set();
            r.linesThrough = false;
            if (!S.shadow)
                r.off.add('shadow');
        }
        const grading = S.col && S.col !== ORIGINAL, turn = sceneHue() - hueOf(rgbOf(ORIGINAL));
        r.grade = grading ? { rot: hueTurn(turn) } : null;
        r.room = !!S.room;
        r.accent = rgbOf(accentHex());
        r.look = orbit ? lookOf(orbit) : null;
        if (ngActive()) {
            const ng = nextGen();
            ng.on = true;
            ng.quality = S.ngq;
            ng.fx = { reflect: S.ngfx.includes('r'), rays: S.ngfx.includes('g'), particles: S.ngfx.includes('p'),
                      bloom: S.ngfx.includes('b'), dof: S.ngfx.includes('d'), gi: S.ngfx.includes('i'), taa: S.ngfx.includes('a') };
            ng.halo = !!S.halo;
            // the playground is not shown in this tab and does not reach its picture
            ng.grade = null;
        } else if (st.ng) {
            st.ng.on = false;
        }
        r.path = null;
        segs.length = 0;
        if (S.tab !== 'blob' && S.cpath)
            pathOverlay();
        if (S.tab === 'blob')
            blobOverlay();
        r.lines = segs.length ? new Float32Array(segs) : null;
    };

    // --- the colours of the playground and the panel
    const sceneHue = () => hueOf(rgbOf(S.col || ORIGINAL));
    const accentHex = () => S.col || ORIGINAL;
    const paintAccent = () => {
        const h = accentHex(), [r, g, b] = rgbOf(h), l = 0.299 * r + 0.587 * g + 0.114 * b;
        document.documentElement.style.setProperty('--accent', h);
        document.documentElement.style.setProperty('--accent-ink', l > 0.55 ? '#0b0d0a' : '#ffffff');
    };

    const statusLine = (m) => {
        const t = m.t, cam = core.camera(t), logo = core.logoState();
        const ph = run.done && holdMs > run.tick ? 'Logo held' : phase(t, cam, logo, run.done);
        const bits = S.tab === 'models'
            ? [`Model <b>${$('model').selectedOptions[0]?.text || ''}</b>`, `${models.model?.stats.triangles.toLocaleString('en-US') || 0} triangles`,
               `t ${t.toFixed(3)} s`, `${gpu.width} × ${gpu.height}`]
            : [`t <b>${(shownMs() / 1000).toFixed(3)}</b> s`, `Frame ${m.frames}`, `<b>${m.drawn ?? m.drawCount}</b> draw calls`,
               `${Math.round(renderer.triangles).toLocaleString('en-US')} triangles`, `Phase <b>${ph}</b>`,
               `Path ${cam.path}`, `${gpu.width} × ${gpu.height}`, ...(orbit ? ['free camera'] : []),
               ...(ngActive() && st.ng?.stats ? [`<b>Next Gen</b> ${S.ngq}`, `${st.ng.stats.rays.toLocaleString('en-US')} rays`] : [])];
        $('statusline').innerHTML = bits.map((b) => `<span>${b}</span>`).join('');
    };

    const blobTables = () => {
        const b = core.blobFull(), tm = core.timing(), k = kOf(tm, b), f = (x, n = 3) => x.toFixed(n);
        $('bLive').innerHTML = [
            ['t', f(tm.t, 4)], ['k (radius with pulses)', f(k)], ['R', f(b.radius)], ['Centre', v3(b.center)],
            ['Bulges', b.bulges], ['Drops', b.drops], ['Steps of blob_update', b.updates],
            ['deformed', b.flat ? 'no (before t = 0.6)' : 'yes'], ['Generator', '0x' + hex(b.rng)], ['x20, x1c', `${f(tm.x20, 4)}, ${f(tm.x1c, 4)}`],
        ].map(([a, v]) => `<tr><td>${a}</td><td>${v}</td></tr>`).join('');
        if (!$('bDetail').closest('details').open)
            return;
        const bulges = b.bulge.map((x, i) => `<tr><td><span class="dot" style="background:${hexOf(turnColour([1, 0.2, 0.2], i * 360 / 32))}"></span>${i}</td>` +
            `<td>${f(x.size)}</td><td>${f(x.h)}</td><td>${f(x.t0)}</td><td>${f(x.freq, 2)}</td><td>${f(x.amp)}</td><td>${x.drop < 0 ? '–' : x.drop}</td></tr>`).join('');
        const drops = b.drop.map((x, i) => `<tr><td>${i}</td><td>${f(x.size)}</td><td>${v3(x.pos)}</td><td>${f(x.disp)}</td><td>${f(x.scale)}</td><td>${f(x.rate, 2)}</td></tr>`).join('');
        $('bDetail').innerHTML = `<table class="grid"><tr><th>Bulge</th><th>Size</th><th>h</th><th>t0</th><th>freq</th><th>amp</th><th>Drop</th></tr>${bulges}</table>` +
            `<table class="grid" style="margin-top:6px"><tr><th>Drop</th><th>Size</th><th>Position</th><th>Displ.</th><th>Scale</th><th>Rate</th></tr>${drops}</table>`;
    };

    // The picture in the size and shape of the stage, in the screen's
    // pixels; with nat the original's 640 x 480 in 4:3, scaled up by the
    // browser.
    const fit = () => {
        $('stage').classList.toggle('wide', !S.nat);
        let w = WIDTH, h = HEIGHT;
        if (!S.nat) {
            const r = $('screen').getBoundingClientRect(), dpr = devicePixelRatio || 1;
            if (!r.width || !r.height)
                return;
            [w, h] = renderer.largest(Math.round(r.width * dpr), Math.round(r.height * dpr));
        }
        gpu.resize(w, h);
        if (ngActive())
            renderer.resize(...nextGen().legacySize(w, h));
        else
            renderer.resize(w, h);
    };

    let modelT = null;
    const show = () => {
        dirty = false;
        if (busy)
            return void (dirty = true);
        fit();
        const ms = shownMs();
        $('timeline').value = Math.min(ms, end());
        $('time').innerHTML = `<b>${(ms / 1000).toFixed(3)}</b> / ${(end() / 1000).toFixed(1)} s<br>Frame ${run.ticks.length}`;
        if (S.tab === 'models') {
            const t = tNow();
            // the models at time t follow the clock in every frame (buildScene)
            if (/^(scene|blobnow)$/.test(S.model) && modelT !== t) {
                models.opts.t = t;
                models.build(S.model);
                modelStats();
                modelT = t;
            }
            models.opts.highlight = S.mhl;
            models.draw(rgbOf(accentHex()), 0);
            lastFrame = { ...core.timing(), frames: run.ticks.length };
            statusLine({ ...lastFrame, t });
        } else {
            configure();
            const m = render(st);
            lastFrame = m;
            $('values').textContent =
                `t      ${m.t.toFixed(6)}  (${m.tBits})\n` +
                `tick   ${m.tick} ms, frame ${m.frames}${run.done ? ', End' : ''}\n` +
                `x18    ${m.x18.toFixed(6)}\nx1c    ${m.x1c.toFixed(6)}\n` +
                `x20    ${m.x20.toFixed(6)}\nx24    ${m.x24.toFixed(6)}\n` +
                `draw calls ${m.drawCount}, of them drawn ${m.drawn ?? '–'}, values ${m.paramCount}`;
            statusLine(m);
            if (S.tab === 'blob')
                blobTables();
            if (S.tab === 'sound')
                tracks.update(ms);
        }
        if (gpu.errors.length)
            $('status').textContent = 'WebGPU: ' + gpu.errors.slice(-1)[0];
        S.t = Math.round(ms);
        save();
    };
    const redraw = () => { dirty = true; };

    // --- time ---------------------------------------------------------------------

    const ensureSound = async () => {
        if (!sound) {
            sound = await Sound.create(assets, run.tick * (AUDIO_RATE / 1000));
            sound.mute(tracks.mask);
            sound.volume(S.vol);
            sound.rate(S.speed);
        }
        return sound;
    };

    // Browsers start no sound before the person has clicked or pressed a
    // key; the run that plays from the start gets its sound with the first.
    const startSound = async () => {
        if (!S.snd || run.done || navigator.userActivation?.hasBeenActive === false)
            return;
        const s = await ensureSound();
        s.rate(S.speed);
        s.seek(run.tick);
        if (playing)
            s.play();
    };

    const pause = () => {
        playing = false;
        sound?.pause();
        $('play').textContent = '▶ Play';
        S.pause = 1;
        save();
    };

    const play = async () => {
        if (shownMs() >= end() - 1) {
            run.restart();
            holdMs = 0;
        }
        playing = true;
        startMs = shownMs();
        startTime = performance.now();
        $('play').textContent = '❚❚ Pause';
        S.pause = 0;
        save();
        await startSound();
    };

    const seekTo = (ms) => {
        ms = Math.max(0, Math.min(Math.round(ms), end()));
        run.seek(Math.min(ms, RUN_END), 60);
        holdMs = run.done ? ms : 0;
        if (playing) {
            startMs = shownMs();
            startTime = performance.now();
            if (run.done)
                sound?.pause();
            else
                startSound();
        }
        redraw();
        show();
    };

    // a frame forward or back, or a second; a move by hand stops the run
    const move = (how) => () => {
        pause();
        how();
        redraw();
        show();
    };

    const tick = (now) => {
        if (playing && !busy) {
            const clock = startMs + (now - startTime) * S.speed;
            if (!run.done) {
                const target = Math.round(Math.min(clock, RUN_END + 100));
                if (target > run.tick) {
                    run.advance(target);
                    if (run.done)
                        sound?.pause();
                    dirty = true;
                }
            }
            if (run.done) {
                if (clock > holdMs) {
                    holdMs = Math.min(clock, end());
                    dirty = true;
                }
                if (clock >= end()) {
                    if (S.loop) {
                        run.restart();
                        holdMs = 0;
                        startMs = 0;
                        startTime = now;
                        startSound();
                    } else {
                        pause();
                    }
                    dirty = true;
                }
            }
        }
        if (S.tab === 'models' && models.opts.spin)
            dirty = true;
        // a standing frame of the next gen picture gathers until TAA has averaged it
        if (ngActive() && st.ng?.settling())
            dirty = true;
        if (dirty)
            show();
        if (S.tab === 'sound')
            drawScope();
        requestAnimationFrame(tick);
    };

    $('play').onclick = () => (playing ? pause() : play());
    $('start').onclick = move(() => { run.restart(); holdMs = 0; });
    $('back').onclick = move(() => { if (holdMs > run.tick) holdMs = 0; else run.back(); });
    $('forward').onclick = move(() => { if (!run.done) run.step(60); else holdMs = Math.min(Math.max(holdMs, run.tick) + 17, end()); });
    $('timeline').oninput = () => seekTo(Number($('timeline').value));

    // --- events of the time line (Timing.md and the narratives it names)
    const buildEvents = () => {
        $('events').innerHTML = '';
        for (const e of events(core, run.path ?? 0)) {
            const b = document.createElement('button');
            b.className = 'event' + (e.pulse ? ' pulse' : '');
            b.style.left = `${(e.t * 1000 / end()) * 100}%`;
            b.title = `t = ${e.t.toFixed(3)}: ${e.what} (${e.file} ${e.sec})`;
            b.onclick = () => {
                move(() => { run.seek(Math.round(e.t * 1000), 60); holdMs = 0; })();
                openSpec({ name: `t = ${e.t.toFixed(3)}: ${e.what}`, file: e.file, sec: e.sec, rows: [] });
            };
            $('events').append(b);
        }
    };

    // --- tabs ----------------------------------------------------------------------

    const HINTS = {
        intro: [['Drag', 'rotate'], ['Wheel', 'zoom'], ['⇧ / right button', 'pan'], ['Click', 'choose part'], ['A', 'choosing parts on, off'], ['N', 'Next Gen'], ['Space', 'Play'], ['← →', 'frame'], ['F', 'fullscreen']],
        models: [['Drag', 'rotate'], ['Wheel', 'zoom'], ['⇧ / right button', 'pan'], ['Double-click', 'reset']],
        blob: [['Drag', 'rotate'], ['Wheel', 'zoom'], ['Click', 'choose part'], ['A', 'choosing parts on, off'], ['Space', 'Play'], ['⇧ ← →', 'one second']],
        sound: [['Click / drag in the tracks', 'jump'], ['Hover', 'details'], ['M / S', 'mute, solo'], ['Space', 'Play']],
        nextgen: [['Drag', 'rotate'], ['Wheel', 'zoom'], ['⇧ / right button', 'pan'], ['N', 'back to the intro'], ['Space', 'Play'], ['← →', 'frame'], ['F', 'fullscreen']],
    };
    // "Click: choose part" only while choosing parts is switched on
    const hints = () => {
        $('hints').innerHTML = HINTS[S.tab].filter(([k]) => S.sel || k !== 'Click')
            .map(([k, v]) => `<span><kbd>${k}</kbd> ${v}</span>`).join('');
    };
    const setTab = (tab) => {
        S.tab = tab;
        document.querySelectorAll('.tabs button').forEach((b) => b.setAttribute('aria-selected', String(b.dataset.tab === tab)));
        document.querySelectorAll('.page').forEach((p) => { p.hidden = p.dataset.page !== tab; });
        $('playground').hidden = !(tab === 'intro' || tab === 'blob');
        $('stage').classList.toggle('sound', tab === 'sound');
        $('tracks').hidden = tab !== 'sound';
        hints();
        $('selChip').hidden = !(tab === 'intro' || tab === 'blob');
        $('hovertag').hidden = true;
        renderer.hover = null;
        if (tab === 'models' && !models.model) {
            models.opts.t = tNow();
            models.build(S.model);
            modelStats();
        }
        if (tab === 'sound')
            startAnalysis();
        redraw();
        save();
    };
    document.querySelectorAll('.tabs button').forEach((b) => { b.onclick = () => setTab(b.dataset.tab); });

    // --- bindings -------------------------------------------------------------------

    const chip = (id, key, after) => {
        $(id).checked = !!S[key];
        $(id).onchange = () => { S[key] = $(id).checked ? 1 : 0; after?.(); redraw(); save(); };
    };
    const flagChip = (id, key, flag, after) => {
        $(id).checked = S[key].includes(flag);
        $(id).onchange = () => {
            S[key] = $(id).checked ? S[key] + flag : S[key].replaceAll(flag, '');
            after?.();
            redraw();
            save();
        };
    };
    const segment = (id, key, after) => {
        const mark = () => $(id).querySelectorAll('button').forEach((b) => b.classList.toggle('on', b.dataset.v === String(S[key])));
        mark();
        $(id).onclick = (e) => {
            const v = e.target.dataset?.v;
            if (v === undefined)
                return;
            S[key] = typeof DEFAULTS[key] === 'number' ? Number(v) : v;
            mark();
            after?.();
            redraw();
            save();
        };
    };

    // intro
    $('path').onchange = () => {
        const p = Number($('path').value), drawn = paths.find((x) => x.drawn)?.p;
        S.path = p === drawn ? -1 : p;
        run.path = S.path >= 0 ? p : null;
        const ms = shownMs();
        run.seek(Math.min(ms, RUN_END), 60);
        pathCache = null;
        buildEvents();
        redraw();
        save();
    };
    chip('orig', 'orig');
    chip('coarse', 'coarse');
    chip('halo', 'halo');
    chip('shadow', 'shadow');
    chip('showPath', 'cpath');
    segment('wireSeg', 'wire');
    $('view').value = S.view;
    $('view').onchange = () => { S.view = $('view').value; redraw(); save(); };

    // parts on and off, each alone, each to its specification
    const groups = {};
    for (const [key, p] of Object.entries(PARTS)) {
        const row = document.createElement('div');
        row.className = 'part';
        row.innerHTML = `<label><input type="checkbox" data-key="${key}"><span class="swatch" style="background:${hexOf(p.colour)}"></span>` +
                        `${p.name.replace(/^[^:]+: /, '')}</label>` +
                        `<button data-solo="${key}" title="show only this">alone</button><button data-spec="${key}" title="specification">§</button>`;
        (groups[p.group] ??= []).push(row);
    }
    for (const [name, rows] of Object.entries(groups)) {
        const box = document.createElement('fieldset');
        box.innerHTML = `<legend>${name}</legend>`;
        box.append(...rows);
        $('parts').append(box);
    }
    const others = document.createElement('fieldset');
    others.innerHTML = '<legend>Others</legend>' + Object.entries({ ...TARGETS, ...OTHER })
        .map(([key, p]) => `<button data-spec="${key}">${p.name}</button>`).join(' ');
    $('parts').append(others);
    const partsChanged = () => {
        document.querySelectorAll('#parts input[type=checkbox]').forEach((c) => { c.checked = !hide.has(c.dataset.key); });
        $('soloNote').textContent = solo ? `alone: ${[...solo].map((k) => PARTS[k]?.name || k).join(', ')}` : hide.size ? `${hide.size} hidden` : '';
        S.hide = listOf(hide);
        S.solo = solo ? listOf(solo) : '';
        redraw();
        save();
    };
    $('parts').onchange = (e) => {
        const key = e.target.dataset.key;
        if (!key)
            return;
        e.target.checked ? hide.delete(key) : hide.add(key);
        solo = null;
        partsChanged();
    };
    const soloOn = (key) => {
        solo = solo?.has(key) && solo.size === 1 ? null : new Set([key]);
        partsChanged();
    };
    $('parts').onclick = (e) => {
        const { solo: k, spec } = e.target.dataset;
        if (k) {
            soloOn(k);
            openSpec(PARTS[k]);
        }
        if (spec)
            openSpec(describe(spec));
    };
    $('allParts').onclick = () => {
        hide.clear();
        solo = null;
        S.halo = 1;
        $('halo').checked = true;
        partsChanged();
    };
    partsChanged();

    // --- the selection: hover outline and the clicked part
    const selectionPanel = () => {
        const got = chosen;
        $('selection').hidden = !got;
        if (!got)
            return;
        const [name, sub] = nameOf(core, got);
        $('selName').textContent = `${name}${sub ? ' · ' + sub : ''}`;
        $('selSwatch').style.background = hexOf(PARTS[got.key]?.colour || [1, 1, 1]);
        const [, more] = aboutDraw(core, got);
        $('selFacts').textContent = more.join('\n');
        $('selModel').disabled = !modelFor(got);
        $('selSolo').textContent = solo?.has(got.key) && solo.size === 1 ? 'show all' : 'alone';
    };
    const modelFor = (got) => {
        const what = got.draw.what;
        if (got.key.startsWith('chamber'))
            return { id: 'scene', highlight: got.draw.part };
        if (what >= 5 && what <= 11)
            return { id: `logo:${what - 5}`, highlight: -1 };
        if (got.key === 'logo_image')
            return { id: 'logo', highlight: -1 };
        if (got.key === 'blob_mesh')
            return { id: 'blobnow', highlight: -1 };
        if (got.key === 'blob_drop')
            return { id: 'drop', highlight: -1 };
        return null;
    };
    const choose = (got) => {
        chosen = got;
        renderer.chosen = sameAs(got);
        selectionPanel();
        redraw();
    };
    $('selClear').onclick = () => choose(null);
    $('selSpec').onclick = () => chosen && openSpec(...aboutDraw(core, chosen));
    $('selSolo').onclick = () => { if (chosen) { soloOn(chosen.key); selectionPanel(); } };
    $('selHide').onclick = () => { if (chosen) { hide.add(chosen.key); solo = null; partsChanged(); } };
    $('selModel').onclick = () => {
        const m = chosen && modelFor(chosen);
        if (!m)
            return;
        S.model = m.id;
        S.mhl = m.highlight;
        $('model').value = m.id;
        models.opts.t = tNow();
        models.build(m.id);
        modelStats();
        setTab('models');
    };

    // --- the canvas: free camera, hover and click ----------------------------------
    const canvas = $('screen');
    const startOrbit = () => {
        if (orbit)
            return;
        const c = core.camera(tNow()), d = c.eye.map((x, a) => x - c.target[a]);
        const dist = Math.hypot(...d);
        orbit = { target: [...c.target], dist, yaw: Math.atan2(d[1], d[0]), pitch: Math.asin(d[2] / dist) };
        $('free').checked = true;
    };
    const saveOrbit = () => { S.free = orbit ? [...orbit.target, orbit.dist, orbit.yaw, orbit.pitch].map((x) => x.toFixed(3)).join(',') : ''; save(); };
    if (S.free) {
        const v = S.free.split(',').map(Number);
        if (v.length === 6 && v.every(Number.isFinite))
            orbit = { target: v.slice(0, 3), dist: v[3], yaw: v[4], pitch: v[5] };
        $('free').checked = !!orbit;
    }
    $('free').onchange = () => {
        if ($('free').checked)
            startOrbit();
        else
            orbit = null;
        saveOrbit();
        redraw();
    };

    const pixel = (e) => {
        const r = canvas.getBoundingClientRect();
        return [Math.floor((e.clientX - r.left) * gpu.width / r.width), Math.floor((e.clientY - r.top) * gpu.height / r.height), e.clientX - r.left, e.clientY - r.top];
    };
    const pickable = () => S.sel && (S.tab === 'intro' || S.tab === 'blob') && renderer.show === 'picture' && !ngActive();
    const pickAt = async (x, y) => {
        busy = true;
        try {
            configure();
            return await renderer.pick(x, y);
        } finally {
            busy = false;
        }
    };
    // hover: one pick at a time, the newest place wins
    let hoverAt = null, hovering = false;
    const hoverPick = async () => {
        if (hovering || !hoverAt)
            return;
        hovering = true;
        const [x, y, cx, cy] = hoverAt;
        hoverAt = null;
        const got = await pickAt(x, y);
        renderer.hover = sameAs(got);
        canvas.classList.toggle('hovering', !!got);
        const tag = $('hovertag');
        if (got) {
            const [name, sub] = nameOf(core, got);
            tag.innerHTML = `${name}${sub ? `<small>${sub}</small>` : ''}`;
            tag.style.left = `${cx}px`;
            tag.style.top = `${cy}px`;
            tag.hidden = false;
        } else {
            tag.hidden = true;
        }
        redraw();
        hovering = false;
        if (hoverAt)
            setTimeout(hoverPick, 40);
    };

    let drag = null;
    canvas.oncontextmenu = (e) => e.preventDefault();
    canvas.onpointerdown = (e) => {
        drag = { x: e.clientX, y: e.clientY, moved: false, pan: e.shiftKey || e.button === 2 };
        try {
            canvas.setPointerCapture(e.pointerId);
        } catch {
            // a pointer the browser does not know, as test_w3.mjs sends it
        }
    };
    canvas.onpointermove = (e) => {
        if (!drag) {
            if (pickable()) {
                hoverAt = pixel(e);
                hoverPick();
            }
            return;
        }
        const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
        if (!drag.moved && Math.hypot(dx, dy) < 3)
            return;
        drag.moved = true;
        drag.x = e.clientX;
        drag.y = e.clientY;
        $('hovertag').hidden = true;
        renderer.hover = null;
        let o;
        if (S.tab === 'models')
            o = models.orbit;
        else {
            startOrbit();
            o = orbit;
        }
        if (drag.pan) {
            const k = o.dist * 0.002, s = Math.sin(o.yaw), c = Math.cos(o.yaw);
            const right = [-s, c, 0], up = [-Math.sin(o.pitch) * c, -Math.sin(o.pitch) * s, Math.cos(o.pitch)];
            o.target = o.target.map((x, a) => x - (right[a] * dx - up[a] * dy) * k);
        } else {
            o.yaw -= dx * 0.01;
            o.pitch = Math.max(-1.55, Math.min(1.55, o.pitch + dy * 0.01));
        }
        if (S.tab !== 'models')
            saveOrbit();
        show();
    };
    canvas.onpointerup = async (e) => {
        const d = drag;
        drag = null;
        if (!d || d.moved || !pickable())
            return;
        const [x, y] = pixel(e), got = await pickAt(x, y);
        gpu.present();
        choose(got);
        if (got)
            openSpec(...aboutDraw(core, got));
        else
            $('spec').innerHTML = `<p>Nothing is drawn at (${x}, ${y}).</p>`;
    };
    canvas.onpointerleave = () => {
        hoverAt = null;
        $('hovertag').hidden = true;
        if (renderer.hover) {
            renderer.hover = null;
            redraw();
        }
    };
    canvas.onwheel = (e) => {
        e.preventDefault();
        if (S.tab === 'models')
            models.orbit.dist = Math.max(0.05, models.orbit.dist * Math.exp(e.deltaY * 0.001));
        else {
            startOrbit();
            orbit.dist = Math.max(0.5, orbit.dist * Math.exp(e.deltaY * 0.001));
            saveOrbit();
        }
        redraw();
    };
    canvas.ondblclick = () => {
        if (S.tab === 'models') {
            models.reset();
            redraw();
        }
    };

    // --- models ---------------------------------------------------------------------
    $('model').innerHTML = models.catalogue().map((g) => `<optgroup label="${g.group}">` +
        g.items.map((it) => `<option value="${it.id}">${it.name}</option>`).join('') + '</optgroup>').join('');
    if (![...$('model').options].some((o) => o.value === S.model))
        S.model = DEFAULTS.model;
    $('model').value = S.model;
    const modelOpts = () => {
        const o = models.opts;
        o.shading = S.msh;
        o.light = S.mli;
        o.bright = S.mbr;
        for (const [flag, key] of [['c', 'coarse'], ['w', 'wire'], ['b', 'cull'], ['r', 'spin'], ['n', 'normals'], ['t', 'tangents'], ['a', 'axes'], ['x', 'box']])
            o[key] = S.mchk.includes(flag);
        o.highlight = S.mhl;
        $('mBrightV').textContent = S.mbr.toFixed(2);
    };
    let highlightList = '';
    const modelStats = () => {
        const m = models.model;
        if (!m)
            return;
        const st = m.stats;
        $('mStats').innerHTML = [['Vertices', st.vertices.toLocaleString('en-US')], ['Indices', st.indices.toLocaleString('en-US')],
            ['Triangles', st.triangles.toLocaleString('en-US')], [m.parts.length > 1 ? 'Parts' : 'Part', st.parts], ['Extent', st.size],
            ['Texture', m.textured ? 'yes' : 'no']].map(([a, v]) => `<tr><td>${a}</td><td>${v}</td></tr>`).join('');
        $('mNote').textContent = m.note;
        // the list of parts only when it changes, not in every frame of a playing scene
        const list = '<option value="-1">none</option>' + (m.parts.length > 1 ? m.parts.map((p, i) => `<option value="${i}">${p}</option>`).join('') : '');
        if (list !== highlightList) {
            $('mHighlight').innerHTML = highlightList = list;
            $('mHighlight').disabled = m.parts.length < 2;
        }
        if (S.mhl >= m.parts.length)
            S.mhl = -1;
        $('mHighlight').value = String(S.mhl);
    };
    $('model').onchange = () => {
        S.model = $('model').value;
        S.mhl = -1;
        models.opts.t = tNow();
        models.build(S.model);
        modelStats();
        redraw();
        save();
    };
    const rebuild = () => { modelOpts(); if (models.model) { models.build(S.model); modelStats(); } };
    segment('mShading', 'msh', modelOpts);
    segment('mLight', 'mli', modelOpts);
    $('mBright').value = S.mbr;
    $('mBright').oninput = () => { S.mbr = Number($('mBright').value); modelOpts(); redraw(); save(); };
    $('mHighlight').onchange = () => { S.mhl = Number($('mHighlight').value); redraw(); save(); };
    for (const [id, flag, how] of [['mCoarse', 'c', 'build'], ['mWire', 'w', 'lines'], ['mCull', 'b'], ['mSpin', 'r'],
                                   ['mNormals', 'n', 'lines'], ['mTangents', 't', 'lines'], ['mAxes', 'a', 'lines'], ['mBox', 'x', 'lines']])
        flagChip(id, 'mchk', flag, () => {
            modelOpts();
            if (how === 'build' && models.model)
                rebuild();
            if (how === 'lines')
                models.lines = null;
        });
    $('mSpec').onclick = () => {
        const m = models.model;
        if (m?.spec)
            openSpec({ name: $('model').selectedOptions[0].text, file: m.spec[0], sec: m.spec[1], rows: [] });
    };
    modelOpts();

    // --- blob -----------------------------------------------------------------------
    segment('bBg', 'bg');
    chip('bOrig', 'borig');
    chip('bWire', 'bwire');
    chip('bHalo', 'bhalo');
    chip('bDrops', 'bdrops');
    flagChip('bMarkers', 'bdiag', 'm');
    flagChip('bXray', 'bdiag', 'x');
    flagChip('bTrails', 'bdiag', 't');
    $('bDetail').closest('details').ontoggle = redraw;

    // --- sound ----------------------------------------------------------------------
    const muteChanged = () => {
        S.mute = listOf(tracks.mute);
        S.ssolo = listOf(tracks.solo);
        sound?.mute(tracks.mask);
        const n = tracks.mute.size, s = tracks.solo.size;
        $('muteHint').hidden = !n && !s;
        $('muteText').textContent = [n ? `${n} track${n > 1 ? 's' : ''} muted` : '', s ? `Solo: channel ${[...tracks.solo].join(', ')}` : ''].filter(Boolean).join(' · ');
        tracks.headers();
        redraw();
        save();
    };
    const clearMute = () => { tracks.mute.clear(); tracks.solo.clear(); muteChanged(); };
    $('sndClear').onclick = clearMute;
    $('muteClear').onclick = clearMute;
    $('sndVol').value = S.vol;
    $('sndVolV').textContent = `${Math.round(S.vol * 100)} %`;
    $('sndVol').oninput = () => {
        S.vol = Number($('sndVol').value);
        $('sndVolV').textContent = `${Math.round(S.vol * 100)} %`;
        sound?.volume(S.vol);
        save();
    };
    segment('scopeSeg', 'scope');
    for (const [id, flag, layer] of [['lyNotes', 'n', 'notes'], ['lyVolume', 'v', 'volume'], ['lyEvents', 'e', 'events'], ['lyGrid', 'g', 'grid']]) {
        tracks.layers[layer] = S.layers.includes(flag);
        flagChip(id, 'layers', flag, () => { tracks.layers[layer] = S.layers.includes(flag); tracks.draw(); });
    }
    $('legend').innerHTML = Object.entries(SAMPLES).map(([n, name]) => `<span><span class="dot" style="background:${SAMPLE_COLOUR[n]}"></span>Sample ${n}: ${name}</span>`).join('') +
        `<span><span class="dot" style="background:#ff6a5a"></span>${CALLS[1]}</span><span><span class="dot" style="background:#5aa9ff"></span>${CALLS[0]}</span>` +
        `<span><span class="dot" style="background:#ffb347"></span>${CALLS[3]}</span><span><span class="dot" style="background:rgba(140,200,255,.7)"></span>Gain</span>`;
    const startAnalysis = () => {
        if (analysis)
            return;
        analysis = (async () => {
            $('tracks').insertAdjacentHTML('afterbegin', '<div class="tv-progress" id="tvProgress">Reading sound from the core …</div>');
            const own = await Core.load(assets.wasm, assets.pack, assets.layout);
            const data = await analyse(own, RUN_END, (f) => { const p = $('tvProgress'); if (p) p.textContent = `Reading sound from the core … ${Math.round(f * 100)} %`; });
            $('tvProgress')?.remove();
            tracks.span = RUN_END;
            tracks.set(data);
            tracks.update(shownMs());
        })();
    };
    const scope = $('scope');
    const drawScope = () => {
        const w = scope.clientWidth, h = scope.clientHeight, dpr = devicePixelRatio || 1;
        if (scope.width !== Math.round(w * dpr)) {
            scope.width = Math.round(w * dpr);
            scope.height = Math.round(h * dpr);
        }
        const g = scope.getContext('2d');
        g.setTransform(dpr, 0, 0, dpr, 0, 0);
        g.clearRect(0, 0, w, h);
        g.strokeStyle = g.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--accent');
        const a = sound?.analyser;
        if (!a || !playing) {
            g.globalAlpha = 0.4;
            g.fillRect(0, h / 2, w, 1);
            g.globalAlpha = 1;
            return;
        }
        if (S.scope === 'spectrum') {
            const d = new Uint8Array(a.frequencyBinCount);
            a.getByteFrequencyData(d);
            const n = 96;
            for (let i = 0; i < n; i++) {
                const k = Math.floor(Math.pow(d.length, i / n)), v = d[Math.min(k, d.length - 1)] / 255;
                g.fillRect(i * w / n, h - v * h, w / n - 1, v * h);
            }
        } else {
            const d = new Float32Array(a.fftSize);
            a.getFloatTimeDomainData(d);
            g.beginPath();
            for (let i = 0; i < d.length; i++)
                g[i ? 'lineTo' : 'moveTo'](i * w / d.length, h / 2 - d[i] * h * 0.9);
            g.lineWidth = 1.2;
            g.stroke();
        }
    };

    // --- playground -----------------------------------------------------------------
    $('pgSwatches').innerHTML = SWATCHES.map(([name, c]) => `<button title="${name}" data-c="${c}" style="background:${c}"></button>`).join('');
    const pgShow = () => {
        $('pgColour').value = S.col || ORIGINAL;
        $('pgRoom').checked = !!S.room;
        paintAccent();
    };
    $('pgSwatches').onclick = (e) => {
        const c = e.target.dataset?.c;
        if (!c)
            return;
        S.col = c === ORIGINAL ? '' : c;
        pgShow();
        redraw();
        save();
    };
    $('pgColour').oninput = () => { S.col = $('pgColour').value === ORIGINAL ? '' : $('pgColour').value; pgShow(); redraw(); save(); };
    chip('pgRoom', 'room');
    $('pgReset').onclick = () => { S.col = ''; S.room = 0; pgShow(); redraw(); save(); };
    pgShow();

    // --- playback ---------------------------------------------------------------------
    $('speed').value = S.speed;
    const speedShow = () => { $('speedV').textContent = `${S.speed.toFixed(2)}×`; };
    speedShow();
    $('speed').oninput = () => {
        const now = performance.now();
        if (playing) {
            startMs = startMs + (now - startTime) * S.speed;
            startTime = now;
        }
        S.speed = Number($('speed').value);
        speedShow();
        sound?.rate(S.speed);
        save();
    };
    chip('loop', 'loop');
    chip('native', 'nat');
    // choosing parts in the picture: off, the canvas only turns the camera
    chip('sel', 'sel', () => {
        hoverAt = null;
        $('hovertag').hidden = true;
        canvas.classList.remove('hovering');
        renderer.hover = null;
        if (!S.sel)
            choose(null);
        hints();
    });
    segment('ngQuality', 'ngq');
    for (const [id, flag] of [['ngReflect', 'r'], ['ngRays', 'g'], ['ngParticles', 'p'], ['ngBloom', 'b'], ['ngDof', 'd'], ['ngGi', 'i'], ['ngTaa', 'a']])
        flagChip(id, 'ngfx', flag);
    chip('sound', 'snd', () => {
        if (!S.snd)
            sound?.pause();
        else if (playing)
            startSound();
    });

    // --- export -----------------------------------------------------------------------
    $('shot').onclick = async () => {
        const hover = renderer.hover;
        renderer.hover = null;
        show();
        const px = await gpu.read();
        renderer.hover = hover;
        const c = document.createElement('canvas'), { width, height } = gpu;
        c.width = width;
        c.height = height;
        c.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(px.buffer), width, height), 0, 0);
        c.toBlob((blob) => {
            const a = document.createElement('a');
            a.href = URL.createObjectURL(blob);
            a.download = `xboot_${S.tab}_t${(shownMs() / 1000).toFixed(3)}.png`;
            a.click();
            setTimeout(() => URL.revokeObjectURL(a.href), 1000);
        }, 'image/png');
        $('exportNote').textContent = `Picture ${width} × ${height} saved.`;
        redraw();
    };
    $('link').onclick = async () => {
        S.t = Math.round(shownMs());
        clearTimeout(saveTimer);
        saveState(S);
        await new Promise((ok) => setTimeout(ok, 150));
        try {
            await navigator.clipboard.writeText(location.href);
            $('exportNote').textContent = `Link with t = ${(S.t / 1000).toFixed(3)} s copied.`;
        } catch {
            $('exportNote').textContent = location.href;
        }
    };
    const fullscreen = () => (document.fullscreenElement ? document.exitFullscreen() : $('stage').requestFullscreen());
    $('full').onclick = fullscreen;
    // F11 makes the window fill the screen; then the canvas alone as well
    const f11 = () => document.body.classList.toggle('canvas-only', !document.fullscreenElement &&
        (matchMedia('(display-mode: fullscreen)').matches || (innerHeight >= screen.height - 1 && innerWidth >= screen.width - 1)));
    addEventListener('resize', () => { f11(); redraw(); });
    // the canvas also changes its size with the tab and the panel
    new ResizeObserver(redraw).observe(canvas);
    f11();

    // --- keys
    addEventListener('keydown', (e) => {
        const t = e.target;
        if (t.tagName === 'SELECT' || t.tagName === 'TEXTAREA' || (t.tagName === 'INPUT' && !['checkbox', 'range'].includes(t.type)) || e.ctrlKey || e.metaKey || e.altKey)
            return;
        if (e.key === ' ') {
            e.preventDefault();
            playing ? pause() : play();
        } else if (e.key === 'ArrowRight' || e.key === 'ArrowLeft') {
            e.preventDefault();
            if (e.shiftKey)
                return void move(() => seekTo(shownMs() + (e.key === 'ArrowRight' ? 1000 : -1000)))();
            $(e.key === 'ArrowRight' ? 'forward' : 'back').click();
        } else if (e.key === 'Home') {
            e.preventDefault();
            $('start').click();
        } else if (e.key === 'f' || e.key === 'F') {
            fullscreen();
        } else if (e.key === 'n' || e.key === 'N') {
            setTab(S.tab === 'nextgen' ? 'intro' : 'nextgen');
        } else if ((e.key === 'a' || e.key === 'A') && !$('selChip').hidden) {
            $('sel').click();
        }
    });

    // --- start
    $('status').textContent = `WebGPU: ${gpu.info.vendor} ${gpu.info.architecture}`;
    $('timeline').max = String(end());
    buildEvents();
    muteChanged();
    setTab(S.tab);
    if (S.t > 0)
        seekTo(S.t);
    show();
    openSpec(OTHER.frame);
    requestAnimationFrame(tick);
    // the intro plays as soon as the page is open, unless the link was
    // made while it stood still
    if (S.pause)
        pause();
    else
        play();
    const firstGesture = () => {
        removeEventListener('pointerdown', firstGesture, true);
        removeEventListener('keydown', firstGesture, true);
        if (playing)
            startSound();
    };
    addEventListener('pointerdown', firstGesture, true);
    addEventListener('keydown', firstGesture, true);
}
