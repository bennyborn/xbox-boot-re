// The draw list of frames of a run at 60 Hz, from the wasm core in node:
// one line per kind of draw with its targets, textures and states.
//
//     node hosts/web/dump.mjs 300 2000 5600      (times in ms)
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { Core, Run } from './src/core.js';
import { ROOT } from './serve.mjs';

const core = await Core.load(readFileSync(join(ROOT, 'out/core/wasm/xboot.wasm')),
                             readFileSync(join(ROOT, 'out/pack/xboot.pak')).buffer,
                             JSON.parse(readFileSync(join(ROOT, 'hosts/web/layout.json'))));
const run = new Run(core);
const hex = (v) => v.toString(16);
console.log(core.textures().map((t) => `${t.width}x${t.height} levels ${t.levels} faces ${t.faces} format ${hex(t.format)}`).join(' | '));
for (const ms of process.argv.slice(2).map(Number)) {
    run.seek(ms, 60);
    const fr = core.frame(), draws = core.draws(fr.drawCount), P = core.paramValues;
    console.log(`== t ${core.timing().t}, ${fr.drawCount} draws, clear depth ${fr.clearDepth}`);
    const seen = new Map();
    for (const d of draws) {
        const k = [hex(d.step), 'what', d.what, 'mesh', d.mesh, 'prim', d.prim, 'target', d.color, d.zeta, 'tex', d.tex,
            'program', hex(d.program), 'combiners', hex(d.combiners), 'flags', d.flags,
            'alpha', d.alphaTest, hex(d.alphaFunc), d.alphaRef, 'blend', d.blend, hex(d.blendSrc), hex(d.blendDst),
            'depth', d.depthTest, d.depthWrite, hex(d.depthFunc), 'cull', d.cull, hex(d.cullFace),
            'shadow', hex(d.shadowFunc), 'mask', hex(d.colourMask), 'vs', d.vs.map((r) => r.base + '+' + r.count).join('/'),
            'ps', d.psCount, 'vtx', d.vtxCount, d.what === 1 ? '' : 'factor ' + hex(d.factor)].join(' ');
        if (!seen.has(k))
            seen.set(k, [0, d]);
        seen.get(k)[0]++;
    }
    for (const [k, [n, d]] of seen) {
        console.log(n, k);
        if (d.psCount && d.what !== 1)
            console.log('    ps', Array.from(P.subarray(d.psFirst, d.psFirst + 4 * d.psCount)).map((x) => x.toFixed(3)).join(' '));
    }
}
