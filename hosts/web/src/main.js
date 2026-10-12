// The page: loads core and data, then either the workbench for a person
// (workbench.js, docs/web.md 10 and 11) or, with a query, pictures and sound
// on demand for hosts/web/shot.mjs.
//
//   ?shot=0.5,2,6.1[&hz=60]  the frame of a run at hz that first reaches each
//                            time in seconds; posted to /shot
//   ?ticks=17,34,50          the frame after exactly these ticks; posted to /shot
//   ?audiotest=96000[&from=N] that many frames of sound from frame N on,
//                            offline; posted to /audio
//   &full=1                  with a picture also the drawing surface 1280 x 480
//                            and its depth; posted to /screen and /depth
//   &pattern=1               the test pattern of W0 instead of the animation
//   &off=shadow,veil,glow,cull  the animation without that, for measurements
//   &size=1920,600           the picture in that size and shape instead of
//                            640 x 480 (docs/web.md 11.7), not for measurements
//   &ng=1[&ngq=hoch][&ngfx=rgpbdia][&ngacc=32]  the next gen picture (nextgen.js,
//                            docs/web.md 12): quality schnell, hoch or ultra; r reflections,
//                            g god rays, p particles, b bloom, d depth of field,
//                            i the bounce, a TAA; frames gathered per picture
//
// The workbench of W3 (docs/web.md 10), also on demand:
//   &cam=ex,ey,ez,tx,ty,tz   another camera, up (0, 0, 1) (xb_frame_from)
//   &cam=path                the path's camera through xb_frame_from
//   &hide=chamber0,veil      parts left out (bench.js, PARTS)
//   &solo=blob_mesh          only these parts
//   &wire=1|2                wireframe over the picture, or alone
//   &path=1                  the camera's path drawn in
//   &view=light0             a target or texture instead of the picture
//   &pick=x,y                the draw at that pixel, in the meta as `picked`
import { Core, Run, DRAWS_MAX, PARAMS_MAX, AUDIO_RATE } from './core.js';
import { Gpu } from './gpu.js';
import { Renderer } from './renderer.js';
import { NextGen } from './nextgen.js';
import { renderOffline } from './sound.js';
import { interactive } from './workbench.js';

const $ = (id) => document.getElementById(id);
const query = new URLSearchParams(location.search);
const END_MS = 8000;
const UP = [0, 0, 1];

async function load() {
    const get = async (url) => {
        const r = await fetch(url);
        if (!r.ok)
            throw new Error(`${url}: ${r.status}`);
        return r;
    };
    const [wasm, pack, layout] = await Promise.all([
        get('xboot.wasm').then((r) => r.arrayBuffer()),
        get('xboot.pak').then((r) => r.arrayBuffer()),
        get('layout.json').then((r) => r.json()),
    ]);
    return { wasm, pack, layout };
}

const hex = (u) => u.toString(16).toUpperCase().padStart(8, '0');
const list = (name) => (query.get(name) || '').split(',').filter(Boolean);

// The camera's path as a line list: the eye from t = 0 to 8 in steps of
// 10 ms, and a cross at the eye and the target of time t.
function pathLines(core, t) {
    if (!core.pathPoints) {
        const pts = [];
        for (let ms = 0; ms <= END_MS; ms += 10)
            pts.push(core.camera(ms / 1000).eye);
        core.pathPoints = pts.flatMap((p, i) => (i ? [...pts[i - 1], ...p] : []));
    }
    const c = core.camera(t), cross = [];
    for (const p of [c.eye, c.target])
        for (let a = 0; a < 3; a++) {
            const d = [0, 0, 0];
            d[a] = 2;
            cross.push(p[0] - d[0], p[1] - d[1], p[2] - d[2], p[0] + d[0], p[1] + d[1], p[2] + d[2]);
        }
    return new Float32Array([...core.pathPoints, ...cross]);
}

// Core, GPU and renderer for one run. The logo's texture is drawn in two
// frames of a run and shown in all later ones (Logo.md 6), so the renderer
// follows every step, also those nobody sees.
async function stage(assets) {
    const core = await Core.load(assets.wasm, assets.pack, assets.layout);
    const gpu = await Gpu.create($('screen'));
    const run = new Run(core);
    const renderer = query.has('pattern') ? null : new Renderer(gpu, core);
    if (renderer) {
        list('off').forEach((what) => renderer.off.add(what));
        list('hide').forEach((what) => renderer.hide.add(what));
        if (query.has('solo'))
            renderer.solo = new Set(list('solo'));
        renderer.wire = Number(query.get('wire') || 0);
        renderer.show = query.get('view') || 'picture';
        run.onRestart = () => renderer.clearLogo();
        run.onAdvance = () => core.logoDrawnToTexture() && renderer.draw('logo');
    }
    return { run, gpu, renderer };
}

// Draws the frame the run stands at and says what it is.
function render(st) {
    const { run, gpu, renderer } = st;
    const tm = run.core.timing(), fr = run.core.frame();
    const values = [Math.min(tm.t / 8, 1), fr.drawCount / DRAWS_MAX, fr.paramCount / PARAMS_MAX];
    let drawn;
    if (renderer && st.ng?.on) {
        // the same frame drawn again until TAA has gathered it, as the workbench does when it stands
        for (let i = 0; i < (st.ng.gather || 1); i++)
            drawn = st.ng.draw().drawn;
    }
    else if (renderer)
        drawn = renderer.draw().drawn;
    else
        gpu.drawPattern(values);
    gpu.present();
    return {
        tick: run.tick, frames: run.ticks.length, rc: run.rc, t: tm.t, tBits: hex(tm.tBits),
        x18: tm.x18, x1c: tm.x1c, x20: tm.x20, x24: tm.x24,
        drawCount: fr.drawCount, paramCount: fr.paramCount, values, drawn,
    };
}

const post = (path, body, meta) =>
    fetch(path, { method: 'POST', body, headers: meta ? { 'x-meta': JSON.stringify(meta) } : {} });

async function onDemand(assets) {
    if (query.has('audiotest')) {
        const frames = Number(query.get('audiotest')), from = Number(query.get('from') || 0);
        const samples = await renderOffline(assets, frames, from);
        await post('/audio', samples.buffer, { frames, from });
    }
    if (query.has('shot') || query.has('ticks')) {
        const st = await stage(assets), { run, gpu, renderer } = st;
        if (renderer && query.has('ng')) {
            st.ng = new NextGen(gpu, run.core, renderer);
            st.ng.on = true;
            st.ng.quality = query.get('ngq') || 'hoch';
            st.ng.debug = Number(query.get('ngdbg') || 0);
            st.ng.gather = Number(query.get('ngacc') || 32);
            if (query.has('ngfx')) {
                const fx = query.get('ngfx');
                st.ng.fx = { reflect: fx.includes('r'), rays: fx.includes('g'), particles: fx.includes('p'), bloom: fx.includes('b'),
                             dof: fx.includes('d'), gi: fx.includes('i'), taa: fx.includes('a') };
            }
        }
        if (query.has('size') || st.ng) {
            const [w, h] = query.has('size') ? (renderer ? renderer.largest(...list('size').map(Number)) : list('size').map(Number)) : [gpu.width, gpu.height];
            gpu.resize(w, h);
            renderer?.resize(...(st.ng ? st.ng.legacySize(w, h) : [w, h]));
        }
        const hz = Number(query.get('hz') || 60);
        const jobs = query.has('ticks')
            ? [{ ticks: query.get('ticks').split(',').filter(Boolean).map(Number) }]
            : query.get('shot').split(',').map((s) => ({ seconds: Number(s) }));
        for (const job of jobs) {
            if (job.ticks)
                run.replay(job.ticks);
            else
                run.seek(Math.round(job.seconds * 1000), hz);
            if (renderer && query.has('cam')) {
                const c = query.get('cam') === 'path' ? run.core.camera(run.core.timing().t) : null;
                const v = c ? [...c.eye, ...c.target] : list('cam').map(Number);
                renderer.look = { eye: v.slice(0, 3), target: v.slice(3, 6), up: c ? c.up : UP };
            }
            if (renderer && query.has('path'))
                renderer.path = pathLines(run.core, run.core.timing().t);
            const meta = render(st);
            if (renderer && query.has('pick')) {
                const [x, y] = list('pick').map(Number), got = await renderer.pick(x, y);
                meta.picked = got && { key: got.key, step: got.draw.step, what: got.draw.what, part: got.draw.part, mesh: got.draw.mesh };
            }
            const picture = await gpu.read();
            if (gpu.errors.length)
                throw new Error('WebGPU: ' + gpu.errors.slice(0, 4).join(' | '));
            await post('/shot', picture, { ...job, hz: job.ticks ? undefined : hz, gpu: gpu.info, width: gpu.width, height: gpu.height, ...meta });
            if (query.has('full') && renderer) {
                await post('/screen', await renderer.readScreen());
                await post('/depth', await renderer.readDepth());
            }
        }
    }
    await post('/done', '');
}

try {
    const assets = await load();
    if (query.has('shot') || query.has('ticks') || query.has('audiotest'))
        await onDemand(assets);
    else
        await interactive(assets, { stage, render });
} catch (e) {
    $('status').textContent = String(e);
    if (query.has('shot') || query.has('ticks') || query.has('audiotest'))
        await post('/error', String(e.stack || e));
    else
        throw e;
}
