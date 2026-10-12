// Where the parts of the chamber lie in their light view (GPU.md 3.1, 4.4),
// for the frame after the ticks of a capture of tools/xemu/frame_at.py:
// per group the range of the texel coordinates oT3.xy / oT3.w and of the
// depth oT3.z / oT3.w over all vertices of the draws of D1.
//
//     node hosts/web/lightspace.mjs out/xemu/runs/w1_t4400/capture.json
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { Core, Run } from './src/core.js';
import { ROOT } from './serve.mjs';

const core = await Core.load(readFileSync(join(ROOT, 'out/core/wasm/xboot.wasm')),
                             readFileSync(join(ROOT, 'out/pack/xboot.pak')).buffer,
                             JSON.parse(readFileSync(join(ROOT, 'hosts/web/layout.json'))));
const run = new Run(core);
const meshes = core.meshes();
run.replay(JSON.parse(readFileSync(process.argv[2])).ticks);
const fr = core.frame(), P = core.paramValues;
const groups = new Map();
for (const d of core.draws(fr.drawCount)) {
    if (d.step !== 0x0D01)
        continue;
    const mesh = meshes.chamber[d.mesh - 1][d.detail], c = (k) => P.subarray(d.vs[0].first + 4 * k, d.vs[0].first + 4 * k + 4);
    const key = `group ${d.mesh - 1} light ${d.tex[3]}`;
    const g = groups.get(key) || { n: 0, x: [1e9, -1e9], y: [1e9, -1e9], z: [1e9, -1e9], w: [1e9, -1e9], beyond: 0, behind: 0 };
    groups.set(key, g);
    for (const i of new Set(mesh.indices.subarray(d.first, d.first + d.count))) {
        const v = [mesh.vertices[3 * i], mesh.vertices[3 * i + 1], mesh.vertices[3 * i + 2], 1];
        const dot = (r) => v[0] * r[0] + v[1] * r[1] + v[2] * r[2] + v[3] * r[3];
        const w = dot(c(11)), t = [dot(c(8)) / w, dot(c(9)) / w, dot(c(10)) / w];
        g.n++;
        if (!(w > 0)) {
            g.behind++;
            continue;
        }
        g.beyond += t[2] >= 65535;
        [g.x, g.y, g.z, g.w].forEach((r, k) => {
            const a = k < 3 ? t[k] : w;
            r[0] = Math.min(r[0], a);
            r[1] = Math.max(r[1], a);
        });
    }
}
for (const [key, g] of [...groups].sort())
    console.log(key, `${g.n} vertices, ${g.behind} behind the light, ${g.beyond} at or beyond far;`,
                ['x', 'y', 'z', 'w'].map((k) => `${k} ${g[k][0].toFixed(1)} .. ${g[k][1].toFixed(1)}`).join(', '));
