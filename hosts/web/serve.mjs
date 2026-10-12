// Serves the web host on localhost.
//
//     node hosts/web/serve.mjs [port]
//
// The pages and scripts come from hosts/web/, the core from
// out/core/wasm/xboot.wasm and the data pack from out/pack/xboot.pak; build
// both with `python core/build.py --no-test`. Neither is in the repo.
// /docs/<path>.md serves the Markdown of docs/, which the workbench shows
// next to the part it belongs to (docs/web.md 10).
// shot.mjs uses the same server and takes what the page posts.
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { dirname, extname, join, resolve, sep } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
export const ROOT = resolve(here, '..', '..');

const BUILT = {
    '/xboot.wasm': join(ROOT, 'out', 'core', 'wasm', 'xboot.wasm'),
    '/xboot.pak': join(ROOT, 'out', 'pack', 'xboot.pak'),
};
const TYPES = {
    '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
    '.json': 'application/json', '.wasm': 'application/wasm', '.pak': 'application/octet-stream',
    '.md': 'text/markdown; charset=utf-8', '.svg': 'image/svg+xml',
};
const DOCS = join(ROOT, 'docs');

function fileFor(path) {
    if (BUILT[path])
        return BUILT[path];
    if (path.startsWith('/docs/')) {
        const doc = resolve(DOCS, '.' + path.slice(5));
        return doc.startsWith(DOCS + sep) && extname(doc) === '.md' ? doc : null;
    }
    const file = resolve(here, '.' + (path === '/' ? '/index.html' : path));
    return file.startsWith(here + sep) && TYPES[extname(file)] ? file : null;
}

// onPost(path, meta, body) gets every POST: meta is the parsed x-meta header
// or null, body a Buffer.
export function startServer({ port = 0, onPost } = {}) {
    const server = createServer(async (req, res) => {
        const path = new URL(req.url, 'http://localhost').pathname;
        if (req.method === 'POST') {
            const chunks = [];
            for await (const c of req)
                chunks.push(c);
            const meta = req.headers['x-meta'] ? JSON.parse(req.headers['x-meta']) : null;
            res.writeHead(204).end();
            onPost?.(path, meta, Buffer.concat(chunks));
            return;
        }
        const file = fileFor(path);
        try {
            const data = await readFile(file);
            res.writeHead(200, { 'content-type': TYPES[extname(file)], 'cache-control': 'no-store' }).end(data);
        } catch {
            res.writeHead(404).end('not found');
        }
    });
    return new Promise((ok) => server.listen(port, '127.0.0.1', () =>
        ok({ server, url: `http://127.0.0.1:${server.address().port}/` })));
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const { url } = await startServer({ port: Number(process.argv[2] || 8360) });
    console.log(url);
}
