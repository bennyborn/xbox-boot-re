// The models tab of the workbench (docs/web.md 11): every mesh the core
// hands out, alone, in a viewer of its own. Positions, indices, the axes a,
// b, n of the chamber, texture coordinates and the textures come from the
// core (xb_chamber_mesh, xb_logo_mesh, xb_blob_mesh, xb_blob_deformed,
// xb_texture); the parts of the chamber stand where xb_chamber_world puts
// them. Light, camera and shading are the viewer's own and not the
// original's: its programs and combiners are in the renderer.
import { PATH } from './shaders.js';
import { triangles } from './renderer.js';

// Chamber.md 4.1: the six groups by their primitive
export const GROUPS = ['sphere', 'cylinder', 'box', 'ring', 'frustum', 'lathe'];
export const GROUPS_PLURAL = ['spheres', 'cylinders', 'boxes', 'rings', 'frustums', 'lathes'];
// xboot.h, XB_LOGO_*, and the texture each reads (XB_TEX_*, Logo.md 2.4, 5)
const LOGO = [['A', 4], ['B', 5], ['C', 6], ['D', -1], ['Q1 (TM)', 7], ['Q2 (TM)', 7], ['E (lettering)', -1]];
const PARTS = 271;
const F = 12;   // floats per vertex: position, normal, face normal, u, v, part

const CODE = `
struct U { mvp: mat4x4f, eye: vec4f, light: vec4f, base: vec4f, accent: vec4f, mode: vec4f }
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var tex: texture_2d<f32>;
@group(0) @binding(2) var smp: sampler;
struct V { @builtin(position) p: vec4f, @location(0) w: vec3f, @location(1) n: vec3f, @location(2) f: vec3f,
           @location(3) uv: vec2f, @location(4) id: f32 }
@vertex fn vs(@location(0) p: vec3f, @location(1) n: vec3f, @location(2) f: vec3f, @location(3) uv: vec2f, @location(4) id: f32) -> V {
    return V(u.mvp * vec4f(p, 1), p, n, f, uv, id);
}
@fragment fn fs(v: V, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    var n = normalize(select(v.n, v.f, u.mode.x != 0));
    if (!front) { n = -n; }
    let l = normalize(u.light.xyz - v.w);
    let e = normalize(u.eye.xyz - v.w);
    var base = u.base.rgb;
    if (u.mode.y != 0) {
        let t = textureSample(tex, smp, v.uv);
        base = base * select(t.rgb, t.rrr, u.mode.w != 0);
    }
    if (u.mode.z >= 0 && abs(v.id - u.mode.z) < 0.5) { base = mix(base, u.accent.rgb, 0.7); }
    let diff = max(dot(n, l), 0);
    let spec = pow(max(dot(n, normalize(l + e)), 0), 60) * 0.2;
    return vec4f(base * (0.16 + diff * u.light.w) + spec * u.light.w, 1);
}`;

// ---- small matrix helpers, column-major as WGSL reads them
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const norm = (a) => { const l = Math.hypot(...a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
function lookAt(eye, at, up) {
    const z = norm(sub(eye, at)), x = norm(cross(up, z)), y = cross(z, x);
    return [x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0, -dot(x, eye), -dot(y, eye), -dot(z, eye), 1];
}
function perspective(fov, aspect, n, f) {
    const k = 1 / Math.tan(fov / 2);
    return [k / aspect, 0, 0, 0, 0, k, 0, 0, 0, 0, f / (n - f), -1, 0, 0, n * f / (n - f), 0];
}
function mul(a, b) {
    const m = new Array(16).fill(0);
    for (let c = 0; c < 4; c++)
        for (let r = 0; r < 4; r++)
            for (let k = 0; k < 4; k++)
                m[4 * c + r] += a[4 * k + r] * b[4 * c + k];
    return m;
}
// v . W with row vectors (the core's matrices), and a direction by W
const place = (W, p) => [0, 1, 2].map((j) => p[0] * W[j] + p[1] * W[4 + j] + p[2] * W[8 + j] + W[12 + j]);
const turn = (W, d) => [0, 1, 2].map((j) => d[0] * W[j] + d[1] * W[4 + j] + d[2] * W[8 + j]);

// Builds the triangle list of a model into arrays: per vertex F floats,
// lines for the axes a, b of the chamber's vertices, the sub-parts' names.
class Builder {
    constructor() {
        this.v = [];
        this.tan = [];
        this.parts = [];
        this.unique = 0;
        this.indices = 0;
        this.edge = 0;
        this.edges = 0;
    }

    // `pos(i)`, `nrm(i)`, `uv(i)`, `ab(i)` for the mesh's vertex i; `tri` its
    // triangles as a flat list of vertex indices.
    add(tri, pos, nrm, uv, part, ab) {
        const seen = new Set();
        for (let t = 0; t + 2 < tri.length; t += 3) {
            const p = [pos(tri[t]), pos(tri[t + 1]), pos(tri[t + 2])];
            if (p.some((q) => q.some(Number.isNaN)))
                continue;
            // the meshes turn clockwise about their outward normal n (measured,
            // docs/web.md 11), as the renderer's FRONT says
            const f = norm(cross(sub(p[2], p[0]), sub(p[1], p[0])));
            if (f.some(Number.isNaN))
                continue;
            for (let k = 0; k < 3; k++) {
                const i = tri[t + k], n = nrm ? nrm(i) : f, w = uv ? uv(i) : [0, 0];
                this.v.push(...p[k], ...n, ...f, w[0], w[1], part);
                this.edge += Math.hypot(...sub(p[k], p[(k + 1) % 3]));
                this.edges++;
                if (!seen.has(i)) {
                    seen.add(i);
                    if (ab)
                        this.tan.push([p[k], ...ab(i)]);
                }
            }
        }
        this.unique += seen.size;
        this.indices += tri.length;
    }
}

export class ModelView {
    constructor(gpu, core, renderer) {
        this.gpu = gpu;
        this.core = core;
        this.renderer = renderer;
        this.data = renderer.data;
        this.device = gpu.device;
        this.opts = { shading: 'original', light: 'camera', bright: 1, coarse: false, wire: false, cull: false, spin: false,
                      normals: false, tangents: false, axes: false, box: false, highlight: -1, t: 3.2 };
        this.orbit = null;
        this.parts = Array.from({ length: PARTS }, (_, i) => core.part(i));
        const d = this.device;
        this.uniform = d.createBuffer({ size: 144, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        this.lineUniform = d.createBuffer({ size: 80, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        this.sampler = d.createSampler({ magFilter: 'linear', minFilter: 'linear', addressModeU: 'repeat', addressModeV: 'repeat' });
        this.white = d.createTexture({ size: [1, 1], format: 'rgba8unorm', usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
        d.queue.writeTexture({ texture: this.white }, new Uint8Array([255, 255, 255, 255]), {}, [1, 1]);
        const m = d.createShaderModule({ code: CODE });
        const at = (shaderLocation, offset, n) => ({ shaderLocation, offset: 4 * offset, format: n === 1 ? 'float32' : `float32x${n}` });
        this.layout = d.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: {} },
            { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: {} },
            { binding: 2, visibility: GPUShaderStage.FRAGMENT, sampler: {} }] });
        const pipe = (cull) => d.createRenderPipeline({
            layout: d.createPipelineLayout({ bindGroupLayouts: [this.layout] }),
            vertex: { module: m, buffers: [{ arrayStride: 4 * F, attributes: [at(0, 0, 3), at(1, 3, 3), at(2, 6, 3), at(3, 9, 2), at(4, 11, 1)] }] },
            fragment: { module: m, targets: [{ format: 'rgba8unorm' }] },
            primitive: { topology: 'triangle-list', cullMode: cull, frontFace: 'cw' },
            depthStencil: { format: 'depth32float', depthCompare: 'less', depthWriteEnabled: true,
                            depthBias: 2, depthBiasSlopeScale: 1 },
            multisample: { count: 4 } });
        this.solid = pipe('none');
        this.culled = pipe('back');
        const lm = d.createShaderModule({ code: PATH });
        this.linePipe = d.createRenderPipeline({ layout: 'auto',
            vertex: { module: lm, buffers: [{ arrayStride: 28, attributes: [at(0, 0, 3), at(1, 3, 4)] }] },
            fragment: { module: lm, targets: [{ format: 'rgba8unorm', blend: { color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha' },
                                                                             alpha: { srcFactor: 'one', dstFactor: 'one' } } }] },
            primitive: { topology: 'line-list' },
            depthStencil: { format: 'depth32float', depthCompare: 'less-equal', depthWriteEnabled: false },
            multisample: { count: 4 } });
        this.lineBind = d.createBindGroup({ layout: this.linePipe.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: this.lineUniform } }] });
        this.model = null;
    }

    // The models to choose from, grouped: [{ group, items: [{ id, name }] }].
    catalogue() {
        const count = (g, f) => this.parts.filter((p) => p.group === g && Math.max(p.form, 0) === f).length;
        const forms = this.data.chamber.flatMap((levels, g) => [
            { id: `group:${g}`, name: `${GROUPS_PLURAL[g]}: all ${levels[0].forms.length} forms` },
            ...levels[0].forms.map((_, f) => ({ id: `form:${g}:${f}`, name: `${GROUPS[g]}, form ${f} · ${count(g, f)}×` }))]);
        return [
            { group: 'Whole scene', items: [{ id: 'scene', name: `Chamber at time t (${PARTS} parts)` }, { id: 'logo', name: 'Logo, all seven meshes' }] },
            { group: 'Individual chamber parts', items: forms },
            { group: 'Logo parts', items: LOGO.map(([name], i) => ({ id: `logo:${i}`, name: `Mesh ${name}` })) },
            { group: 'Blob', items: [{ id: 'blob', name: 'Sphere, mesh n = 32' }, { id: 'blobnow', name: 'Sphere, deformed at time t' },
                                       { id: 'drop', name: 'Drop, mesh n = 8' }] },
        ];
    }

    // Chamber group g in level `detail`: form f as its triangles, the way the
    // draws take it (two fans and a strip, or the whole form).
    formTriangles(g, detail, f) {
        const mesh = this.data.chamber[g][detail], form = mesh.forms[f], idx = mesh.indices;
        const at = (a, n) => idx.subarray(a, a + n);
        if (!form.fan)
            return triangles(mesh.prim, at(form.firstIndex, form.indexCount));
        return [...triangles(7, at(form.firstIndex, form.fan)), ...triangles(7, at(form.firstIndex + form.fan, form.fan)),
                ...triangles(6, at(form.firstIndex + 2 * form.fan, form.indexCount - 2 * form.fan))];
    }

    chamberVertex(g, detail) {
        const mesh = this.data.chamber[g][detail], af = mesh.attrFloats, v = mesh.vertices, a = mesh.attributes;
        const o = af === 11 ? 2 : 0;
        return {
            pos: (i) => [v[3 * i], v[3 * i + 1], v[3 * i + 2]],
            nrm: (i) => [a[af * i + o + 6], a[af * i + o + 7], a[af * i + o + 8]],
            ab: (i) => [[a[af * i + o], a[af * i + o + 1], a[af * i + o + 2]], [a[af * i + o + 3], a[af * i + o + 4], a[af * i + o + 5]]],
            uv: af === 11 ? (i) => [a[af * i], a[af * i + 1]] : null,
        };
    }

    // Makes the geometry of model `id` and puts it on the GPU.
    build(id) {
        const o = this.opts, b = new Builder(), detail = o.coarse ? 1 : 0;
        let texture = -1, note = '', spec = null;
        if (id === 'scene') {
            this.upload(id, this.buildScene(o.t, detail), -1,
                        `All ${PARTS} parts with the world matrix W from xb_chamber_world at time t = ${o.t.toFixed(3)} (Chamber.md 6, 7). ` +
                        'Parts with NaN in W (up to t ≈ 1.17) are missing.', ['Chamber.md', '7']);
            return;
        }
        if (id.startsWith('group:') || id.startsWith('form:')) {
            const [, gs, fs] = id.split(':'), g = Number(gs), vx = this.chamberVertex(g, detail);
            const list = fs === undefined ? this.data.chamber[g][detail].forms.map((_, f) => f) : [Number(fs)];
            let x = 0;
            for (const f of list) {
                const tri = this.formTriangles(g, detail, f), shift = list.length > 1 ? this.extent(tri, vx.pos) : null;
                const off = shift ? x - shift[0] : 0;
                b.add(tri, (k) => { const p = vx.pos(k); return [p[0] + off, p[1], p[2]]; }, vx.nrm, vx.uv, f, vx.ab);
                b.parts.push(`Form ${f}`);
                if (shift)
                    x += shift[1] - shift[0] + 0.4;
            }
            const mesh = this.data.chamber[g][detail];
            note = `${GROUPS[g]} from prim_group_build (Chamber.md 4.3), detail level ${detail}, vertex format ${mesh.attrFloats === 11 ? 1 : 0} ` +
                   `(${mesh.attrFloats} values per vertex: ${mesh.attrFloats === 11 ? 'u, v, ' : ''}a, b, n). The normal is n, the tangents a and b.`;
            spec = ['Chamber.md', '4.3'];
        } else if (id === 'logo' || id.startsWith('logo:')) {
            const list = id === 'logo' ? [0, 1, 2, 3, 4, 5, 6] : [Number(id.slice(5))];
            for (const m of list) {
                const mesh = this.data.logo[m], n = mesh.floats, v = mesh.vertices;
                b.add(Array.from(mesh.indices), (k) => [v[n * k], v[n * k + 1], v[n * k + 2]], null, n === 5 ? (k) => [v[n * k + 3], v[n * k + 4]] : null, m);
                b.parts.push(`Mesh ${LOGO[m][0]}`);
            }
            if (list.length === 1)
                texture = LOGO[list[0]][1];
            note = 'Meshes from logo_setup (Logo.md 3.2), triangle lists; ' +
                   (texture >= 0 ? 'with the texture the mesh reads (Logo.md 5). ' : '') + 'The data has no normals: shading per triangle.';
            spec = ['Logo.md', '2'];
        } else {
            const blob = this.core.blob(), small = id === 'drop', mesh = small ? this.data.drop : this.data.blob, v = mesh.vertices;
            const unit = (k) => [v[3 * k], v[3 * k + 1], v[3 * k + 2]];
            const tri = triangles(6, mesh.indices);
            if (id === 'blobnow') {
                const tm = this.core.timing(), def = this.core.blobDeformed().slice();
                const k = Math.fround((Math.sqrt(Math.max(tm.x20 - tm.x1c, 0)) * 1.3 + 1) * blob.radius);
                b.add(tri, (i) => unit(i).map((x, a) => blob.center[a] + (k + def[4 * i + 3]) * x),
                      (i) => norm([def[4 * i], def[4 * i + 1], def[4 * i + 2]]), null, 0);
                note = `Centre + (k + o.w) · p with k = ${k.toFixed(3)} and the normal normalize(o.xyz), as the program +0x34 (Blob.md 6.4, 7.2); ` +
                       `${blob.bulges} bulges.`;
                spec = ['Blob.md', '6.4'];
            } else {
                const r = small ? 1 : blob.radius;
                b.add(tri, (i) => unit(i).map((x) => x * r), unit, null, 0);
                note = small ? 'Small mesh of the drop, unit sphere (Blob.md 3, 7.3).'
                    : `Large mesh from cube_sphere_build, unit vectors times R = ${blob.radius.toFixed(2)} (Blob.md 3).`;
                spec = ['Blob.md', '3'];
            }
            b.parts.push('Mesh');
        }
        this.upload(id, b, texture, note, spec);
    }

    // The triangles of every part as vertex indices, per detail; they do
    // not change with t.
    sceneTriangles(detail) {
        this.sceneTri ??= [];
        return (this.sceneTri[detail] ??= this.parts.map((p) => Int32Array.from(this.formTriangles(p.group, detail, Math.max(p.form, 0)))));
    }

    // All parts placed with W from xb_chamber_world at t: the vertices
    // Builder.add would make with place and turn, written into
    // one array kept from frame to frame, so that the scene can follow the
    // clock while it plays. The tangents only when the lines ask for them.
    buildScene(t, detail) {
        const tris = this.sceneTriangles(detail), total = tris.reduce((n, x) => n + x.length, 0);
        if (!(this.sceneOut?.length >= total * F))
            this.sceneOut = new Float32Array(total * F);
        const out = this.sceneOut, world = this.core.chamberWorld(t).world.slice(), parts = [];
        const P = new Float64Array(9);   // the corners of a triangle
        let o = 0, unique = 0, indices = 0, edge = 0, edges = 0;
        for (let i = 0; i < PARTS; i++) {
            const p = this.parts[i], tri = tris[i], mesh = this.data.chamber[p.group][detail];
            parts.push(`Part ${i}: ${GROUPS[p.group]}, form ${Math.max(p.form, 0)}, tracks ${p.posTrack}/${p.rotTrack}`);
            indices += tri.length;
            const W = world.subarray(16 * i, 16 * i + 16);
            if ([0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14].some((k) => Number.isNaN(W[k])))
                continue;
            const v = mesh.vertices, a = mesh.attributes, af = mesh.attrFloats, ao = af === 11 ? 2 : 0;
            // the cofactors of W's 3 x 3 part A: n . cof(A) / det is n by the
            // inverse transpose, up to scale, and so the normal
            const c0 = W[5] * W[10] - W[6] * W[9], c1 = W[6] * W[8] - W[4] * W[10], c2 = W[4] * W[9] - W[5] * W[8];
            const c3 = W[2] * W[9] - W[1] * W[10], c4 = W[0] * W[10] - W[2] * W[8], c5 = W[1] * W[8] - W[0] * W[9];
            const c6 = W[1] * W[6] - W[2] * W[5], c7 = W[2] * W[4] - W[0] * W[6], c8 = W[0] * W[5] - W[1] * W[4];
            const seen = new Uint8Array(v.length / 3);
            for (let k = 0; k + 2 < tri.length; k += 3) {
                const q = o;
                for (let j = 0; j < 3; j++) {
                    const m = 3 * tri[k + j], x = v[m], y = v[m + 1], z = v[m + 2];
                    P[3 * j] = x * W[0] + y * W[4] + z * W[8] + W[12];
                    P[3 * j + 1] = x * W[1] + y * W[5] + z * W[9] + W[13];
                    P[3 * j + 2] = x * W[2] + y * W[6] + z * W[10] + W[14];
                }
                // the face normal as in Builder.add, in double precision: (p2 - p0) x (p1 - p0)
                const ux = P[6] - P[0], uy = P[7] - P[1], uz = P[8] - P[2], wx = P[3] - P[0], wy = P[4] - P[1], wz = P[5] - P[2];
                let fx = uy * wz - uz * wy, fy = uz * wx - ux * wz, fz = ux * wy - uy * wx;
                const fl = Math.sqrt(fx * fx + fy * fy + fz * fz) || 1;
                if (Number.isNaN(fl))
                    continue;
                fx /= fl;
                fy /= fl;
                fz /= fl;
                for (let j = 0; j < 3; j++) {
                    const i3 = tri[k + j], at = q + F * j, b = af * i3 + ao + 6;
                    out[at] = P[3 * j];
                    out[at + 1] = P[3 * j + 1];
                    out[at + 2] = P[3 * j + 2];
                    const nx = a[b], ny = a[b + 1], nz = a[b + 2];
                    const mx = nx * c0 + ny * c3 + nz * c6, my = nx * c1 + ny * c4 + nz * c7, mz = nx * c2 + ny * c5 + nz * c8;
                    const ml = Math.sqrt(mx * mx + my * my + mz * mz) || 1;
                    out[at + 3] = mx / ml;
                    out[at + 4] = my / ml;
                    out[at + 5] = mz / ml;
                    out[at + 6] = fx;
                    out[at + 7] = fy;
                    out[at + 8] = fz;
                    out[at + 9] = ao ? a[af * i3] : 0;
                    out[at + 10] = ao ? a[af * i3 + 1] : 0;
                    out[at + 11] = i;
                    const n2 = 3 * ((j + 1) % 3), dx = P[3 * j] - P[n2], dy = P[3 * j + 1] - P[n2 + 1], dz = P[3 * j + 2] - P[n2 + 2];
                    edge += Math.sqrt(dx * dx + dy * dy + dz * dz);
                    edges++;
                    if (!seen[i3]) {
                        seen[i3] = 1;
                        unique++;
                    }
                }
                o += 3 * F;
            }
        }
        // the axes a, b of each vertex, turned by W and normalised
        const tan = () => {
            const list = [];
            for (let i = 0; i < PARTS; i++) {
                const p = this.parts[i], W = world.subarray(16 * i, 16 * i + 16), vx = this.chamberVertex(p.group, detail), seen = new Set();
                for (const k of tris[i])
                    if (!seen.has(k)) {
                        seen.add(k);
                        const pos = place(W, vx.pos(k));
                        if (!pos.some(Number.isNaN))
                            list.push([pos, ...vx.ab(k).map((d) => norm(turn(W, d)))]);
                    }
            }
            return list;
        };
        return { data: out.subarray(0, o), tan, parts, unique, indices, edge, edges };
    }

    extent(tri, pos) {
        let lo = Infinity, hi = -Infinity;
        for (const i of tri) {
            const x = pos(i)[0];
            lo = Math.min(lo, x);
            hi = Math.max(hi, x);
        }
        return [lo, hi];
    }

    // `b` is a Builder or what buildScene returns (data, tan as a function).
    upload(id, b, texture, note, spec) {
        const data = b.data || new Float32Array(b.v), n = data.length / F;
        const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
        for (let i = 0; i < n; i++)
            for (let a = 0; a < 3; a++) {
                lo[a] = Math.min(lo[a], data[F * i + a]);
                hi[a] = Math.max(hi[a], data[F * i + a]);
            }
        // the buffer is kept while it is large enough: the scene comes anew in every frame it plays
        let buffer = this.model?.buffer;
        if (!(buffer?.size >= data.byteLength)) {
            buffer?.destroy();
            buffer = this.device.createBuffer({ size: Math.max(data.byteLength, 16), usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
        }
        this.device.queue.writeBuffer(buffer, 0, data);
        let view = this.white.createView(), grey = false;
        if (texture >= 0) {
            const t = this.renderer.surfaces.get(1 + texture);
            view = t.createView({ dimension: '2d', mipLevelCount: 1 });
            grey = t.format === 'r8unorm';
        }
        const bind = this.device.createBindGroup({ layout: this.layout, entries: [
            { binding: 0, resource: { buffer: this.uniform } }, { binding: 1, resource: view }, { binding: 2, resource: this.sampler }] });
        const size = sub(hi, lo), centre = lo.map((x, a) => x + size[a] / 2);
        const keep = this.model?.id === id && this.orbit;
        this.model = { id, buffer, count: n, data, tan: b.tan, parts: b.parts, bind, textured: texture >= 0, grey, note, spec, lo, hi, centre,
                       radius: Math.hypot(...size) / 2 || 1, edge: b.edges ? b.edge / b.edges : 1,
                       stats: { vertices: b.unique, indices: b.indices, triangles: n / 3, parts: b.parts.length,
                                size: size.map((x) => x.toFixed(2)).join(' × ') } };
        if (!keep)
            this.reset();
        this.lines = null;
    }

    reset() {
        const m = this.model;
        this.orbit = { target: [...m.centre], dist: m.radius * 2.6, yaw: -Math.PI / 2 + 0.6, pitch: 0.45 };
    }

    eye() {
        const o = this.orbit, c = Math.cos(o.pitch);
        return [0, 1, 2].map((a) => o.target[a] + o.dist * [c * Math.cos(o.yaw), c * Math.sin(o.yaw), Math.sin(o.pitch)][a]);
    }

    // The checking lines: edges, normals, tangents, axes, bounding box.
    makeLines() {
        const m = this.model, o = this.opts, out = [], r = m.radius, len = Math.min(m.edge * 1.2, r * 0.05);
        const seg = (a, b, c) => out.push(...a, ...c, ...b, ...c);
        const d = m.data, n = m.count;
        if (o.wire)
            for (let t = 0; t < n; t += 3)
                for (let k = 0; k < 3; k++) {
                    const i = t + k, j = t + (k + 1) % 3;
                    seg([d[F * i], d[F * i + 1], d[F * i + 2]], [d[F * j], d[F * j + 1], d[F * j + 2]], [0.9, 0.95, 1, 0.45]);
                }
        const step = Math.max(1, Math.floor(n / 40000));
        if (o.normals)
            for (let i = 0; i < n; i += 3 * step)
                for (let k = 0; k < 3; k++) {
                    const q = F * (i + k), p = [d[q], d[q + 1], d[q + 2]];
                    seg(p, p.map((x, a) => x + d[q + 3 + a] * len), [1, 0.85, 0.2, 0.9]);
                }
        if (o.tangents && typeof m.tan === 'function')
            m.tan = m.tan();
        if (o.tangents)
            for (let i = 0; i < m.tan.length; i += step) {
                const [p, a, b] = m.tan[i];
                seg(p, p.map((x, k) => x + a[k] * len), [1, 0.35, 0.3, 0.9]);
                seg(p, p.map((x, k) => x + b[k] * len), [0.35, 1, 0.4, 0.9]);
            }
        if (o.axes) {
            const l = r * 0.8;
            seg([0, 0, 0], [l, 0, 0], [1, 0.3, 0.3, 1]);
            seg([0, 0, 0], [0, l, 0], [0.3, 1, 0.3, 1]);
            seg([0, 0, 0], [0, 0, l], [0.35, 0.55, 1, 1]);
        }
        if (o.box) {
            const c = (i) => [i & 1 ? m.hi[0] : m.lo[0], i & 2 ? m.hi[1] : m.lo[1], i & 4 ? m.hi[2] : m.lo[2]];
            for (const [a, b] of [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 4], [1, 5], [2, 6], [3, 7]])
                seg(c(a), c(b), [0.6, 0.8, 1, 0.8]);
        }
        const data = new Float32Array(out);
        this.lines?.buffer.destroy();
        this.lines = { count: data.length / 7, buffer: this.device.createBuffer({ size: Math.max(data.byteLength, 16), usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST }) };
        this.device.queue.writeBuffer(this.lines.buffer, 0, data);
    }

    // Draws the model into the picture and shows it. `accent` is the panel's colour.
    draw(accent = [0.55, 1, 0.35], dt = 0) {
        const m = this.model, o = this.opts;
        if (!m)
            return;
        if (o.spin)
            this.orbit.yaw += dt * 0.6;
        if (!this.lines)
            this.makeLines();
        // the multisampled targets as large as the picture (gpu.resize)
        const { width, height } = this.gpu;
        if (this.colour?.width !== width || this.colour?.height !== height) {
            this.colour?.destroy();
            this.depth?.destroy();
            const target = (format) => this.device.createTexture({ size: [width, height], format, sampleCount: 4, usage: GPUTextureUsage.RENDER_ATTACHMENT });
            this.colour = target('rgba8unorm');
            this.depth = target('depth32float');
        }
        const eye = this.eye(), r = m.radius;
        const vp = mul(perspective(Math.PI / 4, width / height, Math.max(this.orbit.dist - 3 * r, this.orbit.dist * 0.01, 0.01), this.orbit.dist + 3 * r),
                       lookAt(eye, this.orbit.target, [0, 0, 1]));
        const light = o.light === 'camera' ? eye : [0, 0, 0];
        const base = m.textured ? [1, 1, 1] : [0.78, 0.8, 0.84];
        this.device.queue.writeBuffer(this.uniform, 0, new Float32Array([...vp, ...eye, 1, ...light, o.bright, ...base, 1, ...accent, 1,
            o.shading === 'flat' || !m.textured && /^logo/.test(m.id) ? 1 : 0, m.textured ? 1 : 0, o.highlight, m.grey ? 1 : 0]));
        this.device.queue.writeBuffer(this.lineUniform, 0, new Float32Array([...vp, 1, 1, 1, 1]));
        const enc = this.device.createCommandEncoder();
        const p = enc.beginRenderPass({
            colorAttachments: [{ view: this.colour.createView(), resolveTarget: this.gpu.pictureView, loadOp: 'clear', storeOp: 'discard',
                                 clearValue: [0.055, 0.06, 0.07, 1] }],
            depthStencilAttachment: { view: this.depth.createView(), depthLoadOp: 'clear', depthStoreOp: 'discard', depthClearValue: 1 },
        });
        p.setPipeline(o.cull ? this.culled : this.solid);
        p.setBindGroup(0, m.bind);
        p.setVertexBuffer(0, m.buffer);
        p.draw(m.count);
        if (this.lines.count) {
            p.setPipeline(this.linePipe);
            p.setBindGroup(0, this.lineBind);
            p.setVertexBuffer(0, this.lines.buffer);
            p.draw(this.lines.count);
        }
        p.end();
        this.device.queue.submit([enc.finish()]);
        this.gpu.present();
    }
}
