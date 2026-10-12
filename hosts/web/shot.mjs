// Pictures and sound on demand from the web host, in Chrome without a window.
//
//     node hosts/web/shot.mjs --t 0.5,2,6.1 [--hz 60] [--out out/web/shots]
//     node hosts/web/shot.mjs --ticks 17,34,50
//     node hosts/web/shot.mjs --ticks @capture.json --full
//     node hosts/web/shot.mjs --t 3.2 --ng [--ngq ultra] [--size 1280,720]
//
// --ticks @FILE takes the list "ticks" of a JSON file, as tools/xemu/frame_at.py
// writes it. --full also writes the drawing surface 1280 x 480 as
// <name>_screen.png and its depth as <name>_depth.bin (float32, 1 = nothing
// drawn). --pattern asks for the test pattern of W0. --cam, --hide, --solo,
// --wire, --path, --view and --pick pass the workbench's settings
// (src/main.js, docs/web.md 10); the picked draw stands in shots.json.
// --ng draws the next gen picture (docs/web.md 12), --ngq its quality,
// --ngfx what of it is on; --size the picture's size (docs/web.md 11.7).
//
// Each picture is what the page drew and read back from the GPU itself, not
// a screenshot of the browser. Writes <out>/<name>.png and <out>/shots.json.
// Chrome is the copy in .tools/chrome/ (docs/provenance.md).
import { spawn } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { join, resolve } from 'node:path';
import { deflateSync, crc32 } from 'node:zlib';
import { startServer, ROOT } from './serve.mjs';

const CHROME = join(ROOT, '.tools', 'chrome', 'chrome-win64', 'chrome.exe');
export const WIDTH = 640, HEIGHT = 480;

// RGBA bytes as an RGB PNG, filter 0 on every row.
export function png(rgba, width, height) {
    const raw = Buffer.alloc(height * (1 + 3 * width));
    for (let y = 0; y < height; y++)
        for (let x = 0; x < width; x++)
            rgba.copy(raw, y * (1 + 3 * width) + 1 + 3 * x, 4 * (y * width + x), 4 * (y * width + x) + 3);
    const chunk = (type, data) => {
        const body = Buffer.concat([Buffer.from(type), data]);
        const out = Buffer.alloc(body.length + 8);
        out.writeUInt32BE(data.length, 0);
        body.copy(out, 4);
        out.writeUInt32BE(crc32(body), body.length + 4);
        return out;
    };
    const head = Buffer.alloc(13);
    head.writeUInt32BE(width, 0);
    head.writeUInt32BE(height, 4);
    head.set([8, 2, 0, 0, 0], 8);
    return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', head),
                          chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

// Opens the page with `query` and collects what it posts until it is done:
// { shots: [{ meta, rgba, screen, depth }], audio: { meta, samples } }.
export async function ask(query, { timeout = 120000 } = {}) {
    const got = { shots: [], audio: null };
    let finish, fail;
    const done = new Promise((ok, no) => { finish = ok; fail = no; });
    const { server, url } = await startServer({
        onPost(path, meta, body) {
            if (path === '/shot')
                got.shots.push({ meta, rgba: body });
            else if (path === '/screen')
                got.shots.at(-1).screen = body;
            else if (path === '/depth')
                got.shots.at(-1).depth = body;
            else if (path === '/audio')
                got.audio = { meta, samples: new Int16Array(body.buffer, body.byteOffset, body.length / 2) };
            else if (path === '/error')
                fail(new Error('the page failed: ' + body.toString()));
            else if (path === '/done')
                finish();
        },
    });
    // Without the last flag the first start on a new profile never loads the
    // page: the network service dies in its sandbox, which cannot reach the
    // executable under .tools/. The page only
    // talks to this server.
    const chrome = spawn(CHROME, ['--headless=new', '--no-first-run', '--disable-extensions',
        '--disable-features=NetworkServiceSandbox',
        `--user-data-dir=${join(ROOT, 'out', 'web', 'chrome-profile')}`, url + '?' + query], { stdio: 'ignore' });
    chrome.on('exit', (code) => fail(new Error(`Chrome ended with ${code} before the page was done`)));
    const timer = setTimeout(() => fail(new Error(`no answer after ${timeout} ms`)), timeout);
    try {
        await done;
    } finally {
        clearTimeout(timer);
        chrome.removeAllListeners('exit');
        chrome.kill();
        server.close();
    }
    return got;
}

// Opens the page as a person would and evaluates `expression` in it over
// Chrome's debugging port, as if started by a click; returns its value. For
// tests of the transport.
export async function drive(expression, { port = 9339, timeout = 60000 } = {}) {
    const { server, url } = await startServer();
    const chrome = spawn(CHROME, ['--headless=new', '--no-first-run', '--disable-extensions',
        '--disable-features=NetworkServiceSandbox', `--remote-debugging-port=${port}`,
        `--user-data-dir=${join(ROOT, 'out', 'web', 'chrome-profile')}`, url], { stdio: 'ignore' });
    try {
        let page = null;
        for (const end = Date.now() + timeout; !page && Date.now() < end;) {
            await new Promise((ok) => setTimeout(ok, 200));
            const list = await fetch(`http://127.0.0.1:${port}/json`).then((r) => r.json()).catch(() => []);
            page = list.find((p) => p.type === 'page' && p.url.startsWith(url));
        }
        if (!page)
            throw new Error('Chrome did not open the page');
        const ws = new WebSocket(page.webSocketDebuggerUrl);
        await new Promise((ok, no) => { ws.onopen = ok; ws.onerror = no; });
        const answer = new Promise((ok, no) => {
            ws.onmessage = (e) => ok(JSON.parse(e.data));
            setTimeout(() => no(new Error(`no answer after ${timeout} ms`)), timeout);
        });
        ws.send(JSON.stringify({ id: 1, method: 'Runtime.evaluate',
                                 params: { expression, awaitPromise: true, returnByValue: true, userGesture: true } }));
        const { result } = await answer;
        ws.close();
        if (result.exceptionDetails)
            throw new Error('the page threw: ' + JSON.stringify(result.exceptionDetails.exception));
        return result.result.value;
    } finally {
        chrome.kill();
        server.close();
    }
}

// `more` is added to the query: 'full=1' for the drawing surface and its
// depth with each picture, 'pattern=1' for the test pattern of W0.
export const shotsAt = (seconds, hz = 60, more = '') => ask(`shot=${seconds.join(',')}&hz=${hz}&${more}`).then((g) => g.shots);
export const shotOfTicks = (ticks, more = '') => ask(`ticks=${ticks.join(',')}&${more}`).then((g) => g.shots[0]);
export const soundOf = (frames, from = 0) => ask(`audiotest=${frames}&from=${from}`).then((g) => g.audio);

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const arg = (name) => {
        const i = process.argv.indexOf('--' + name);
        return i < 0 ? null : process.argv[i + 1];
    };
    const out = resolve(arg('out') || join(ROOT, 'out', 'web', 'shots'));
    if (!arg('t') && !arg('ticks')) {
        console.error('usage: node hosts/web/shot.mjs --t 0.5,2,6.1 [--hz 60] [--out DIR] | --ticks 17,34,50');
        process.exit(2);
    }
    const more = [process.argv.includes('--full') ? 'full=1' : '', process.argv.includes('--pattern') ? 'pattern=1' : '',
                  process.argv.includes('--path') ? 'path=1' : '', process.argv.includes('--ng') ? 'ng=1' : '',
                  ...['off', 'cam', 'hide', 'solo', 'wire', 'view', 'pick', 'size', 'ngq', 'ngfx', 'ngdbg', 'ngacc'].map((k) => (arg(k) ? `${k}=${arg(k)}` : ''))].join('&');
    const ticks = arg('ticks') && (arg('ticks').startsWith('@')
        ? JSON.parse(readFileSync(arg('ticks').slice(1))).ticks : arg('ticks').split(',').map(Number));
    const shots = ticks ? [await shotOfTicks(ticks, more)] : await shotsAt(arg('t').split(',').map(Number), Number(arg('hz') || 60), more);
    mkdirSync(out, { recursive: true });
    const list = shots.map(({ meta, rgba, screen, depth }) => {
        const name = `tick_${String(meta.tick).padStart(5, '0')}.png`;
        writeFileSync(join(out, name), png(rgba, meta.width || WIDTH, meta.height || HEIGHT));
        if (screen) {
            writeFileSync(join(out, name.replace('.png', '_screen.png')), png(screen, 1280, 480));
            writeFileSync(join(out, name.replace('.png', '_depth.bin')), depth);
        }
        console.log(`${name}  t = ${meta.t.toFixed(6)} (${meta.tBits}), frame ${meta.frames}, ${meta.drawCount} draws` +
                    (meta.picked !== undefined ? `, picked ${JSON.stringify(meta.picked)}` : ''));
        return { png: name, ...meta };
    });
    writeFileSync(join(out, 'shots.json'), JSON.stringify(list, null, 1) + '\n');
}
