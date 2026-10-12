// "Next gen": the intro as one might render it 25 years later
// (docs/web.md 12). The host's own remaster, not the original; nothing of it
// is in the ROM. What it takes from the core is the scene and its timing:
//
//   the 271 parts of the chamber in level 0 with their axes a, b, n and
//     their world matrices W at t (xb_chamber_mesh, xb_chamber_world,
//     Chamber.md 4, 7)
//   the light at the blob's centre (0, 0, 0) with the colour G and the
//     strength m = 2 x20, attenuated by 1 / (1 + a1 d + a2 d^2) with
//     a1 = a2 = 0.001 / m (Chamber.md 7, "Das Lichtobjekt")
//   the deformed blob, its radius k and colour c0 (Blob.md 6.4, 7, 7.2)
//   the drops (Blob.md 7.3)
//   the camera of the frame, also the free one (xb_frame, xb_frame_from)
//
// The logo, the veil and the glow it does not draw anew: renderer.js draws
// them as the original does on a surface up to twice as large in each
// direction, and the picture here takes them in by depth (COMPOSE).
//
// What it adds: HDR with 4 x MSAA and TAA, a cube of shadows from the
// light with soft edges, GGX on metal with the relief, reflections and the
// light the walls throw on each other traced through a BVH of two levels
// over all parts and against the deformed blob, god rays through the mist,
// the blob as curved triangles with a plasma, particles, depth of field,
// bloom, ACES.
import { triangles, SCREEN_W } from './renderer.js';
import * as SH from './nextgen-shaders.js';

const PARTS = 271;
const G = [0.2079, 1, 0.1];         // Chamber.md 7: K2 = G m
const SURF_RELIEF = 3;              // XB_SURF_TEX + 2, the normal map (Textures.md 1)
const VERTEX = 15;                  // floats: position, a, b, n, u, v, has relief
const HDR = 'rgba16float';
const NG_KEYS = ['chamber0', 'chamber1', 'chamber2', 'chamber3', 'chamber4', 'chamber5', 'blob_mesh', 'blob_drop', 'blob_glow'];

export const QUALITY = {
    fast: { scale: 0.75, shadow: 1024, steps: 24, pn: 3, particles: 3 * 1500, giRays: 1 },
    high: { scale: 1, shadow: 2048, steps: 40, pn: 4, particles: 3 * 3000, giRays: 2 },
    ultra: { scale: 1.5, shadow: 2048, steps: 64, pn: 6, particles: 3 * 6000, giRays: 4 },
};

// ---- matrices --------------------------------------------------------------------

// v . A . B for row-vector matrices of 16 numbers
function mulRow(a, b) {
    const m = new Float64Array(16);
    for (let r = 0; r < 4; r++)
        for (let c = 0; c < 4; c++)
            for (let k = 0; k < 4; k++)
                m[4 * r + c] += a[4 * r + k] * b[4 * k + c];
    return m;
}
function invert(m) {
    const a = Array.from(m), inv = new Float64Array(16);
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    const det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    for (let i = 0; i < 16; i++)
        inv[i] /= det;
    return inv;
}
// column-major, z in 0 .. 1 (the light's faces)
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
function mulCol(a, b) {
    const m = new Array(16).fill(0);
    for (let c = 0; c < 4; c++)
        for (let r = 0; r < 4; r++)
            for (let k = 0; k < 4; k++)
                m[4 * c + r] += a[4 * k + r] * b[4 * c + k];
    return m;
}
// +x, -x, +y, -y, +z, -z: the face whose axis is largest (face_of)
const FACES = [[[1, 0, 0], [0, 0, 1]], [[-1, 0, 0], [0, 0, 1]], [[0, 1, 0], [0, 0, 1]], [[0, -1, 0], [0, 0, 1]],
               [[0, 0, 1], [0, 1, 0]], [[0, 0, -1], [0, 1, 0]]];
const LIGHT_NEAR = 0.3, LIGHT_FAR = 700;

// ---- BVH ---------------------------------------------------------------------------

// A BVH over n boxes (lo, hi: 3 n floats each) by the surface area
// heuristic in 12 bins. Nodes { lo, hi, a, n }: a leaf has n > 0 primitives
// from `order[a]` on; an inner node n = 0 and its children at a and a + 1.
function buildBvh(lo, hi, n, leaf) {
    const order = Uint32Array.from({ length: n }, (_, i) => i);
    const c = new Float32Array(3 * n);
    for (let i = 0; i < 3 * n; i++)
        c[i] = (lo[i] + hi[i]) / 2;
    const nodes = [];
    const bounds = (s, k) => {
        const b = { lo: [Infinity, Infinity, Infinity], hi: [-Infinity, -Infinity, -Infinity], a: s, n: k };
        for (let i = s; i < s + k; i++) {
            const p = order[i];
            for (let a = 0; a < 3; a++) {
                b.lo[a] = Math.min(b.lo[a], lo[3 * p + a]);
                b.hi[a] = Math.max(b.hi[a], hi[3 * p + a]);
            }
        }
        nodes.push(b);
        return nodes.length - 1;
    };
    const area = (l, h) => { const d = [h[0] - l[0], h[1] - l[1], h[2] - l[2]]; return d[0] * d[1] + d[1] * d[2] + d[2] * d[0]; };
    const stack = [bounds(0, n)];
    const BINS = 12;
    while (stack.length) {
        const ni = stack.pop(), node = nodes[ni], s = node.a, k = node.n;
        if (k <= leaf)
            continue;
        const cl = [Infinity, Infinity, Infinity], ch = [-Infinity, -Infinity, -Infinity];
        for (let i = s; i < s + k; i++)
            for (let a = 0; a < 3; a++) {
                cl[a] = Math.min(cl[a], c[3 * order[i] + a]);
                ch[a] = Math.max(ch[a], c[3 * order[i] + a]);
            }
        let best = { cost: Infinity, axis: -1, at: 0 };
        for (let a = 0; a < 3; a++) {
            const ext = ch[a] - cl[a];
            if (ext <= 1e-9)
                continue;
            const bins = Array.from({ length: BINS }, () => ({ n: 0, lo: [Infinity, Infinity, Infinity], hi: [-Infinity, -Infinity, -Infinity] }));
            for (let i = s; i < s + k; i++) {
                const p = order[i], b = bins[Math.min(BINS - 1, Math.floor((c[3 * p + a] - cl[a]) / ext * BINS))];
                b.n++;
                for (let q = 0; q < 3; q++) {
                    b.lo[q] = Math.min(b.lo[q], lo[3 * p + q]);
                    b.hi[q] = Math.max(b.hi[q], hi[3 * p + q]);
                }
            }
            const left = [], right = [];
            const grow = (acc, b) => ({ n: acc.n + b.n, lo: acc.lo.map((x, q) => Math.min(x, b.lo[q])), hi: acc.hi.map((x, q) => Math.max(x, b.hi[q])) });
            let acc = { n: 0, lo: [Infinity, Infinity, Infinity], hi: [-Infinity, -Infinity, -Infinity] };
            for (let i = 0; i < BINS - 1; i++)
                left.push(acc = grow(acc, bins[i]));
            acc = { n: 0, lo: [Infinity, Infinity, Infinity], hi: [-Infinity, -Infinity, -Infinity] };
            for (let i = BINS - 1; i > 0; i--)
                right[i - 1] = acc = grow(acc, bins[i]);
            for (let i = 0; i < BINS - 1; i++) {
                const l = left[i], r = right[i];
                if (!l.n || !r.n)
                    continue;
                const cost = l.n * area(l.lo, l.hi) + r.n * area(r.lo, r.hi);
                if (cost < best.cost)
                    best = { cost, axis: a, at: cl[a] + ext * (i + 1) / BINS };
            }
        }
        let mid;
        if (best.axis < 0) {
            mid = s + (k >> 1);
        } else {
            let i = s, j = s + k - 1;
            while (i <= j) {
                if (c[3 * order[i] + best.axis] < best.at)
                    i++;
                else {
                    const t = order[i];
                    order[i] = order[j];
                    order[j--] = t;
                }
            }
            mid = i;
            if (mid === s || mid === s + k)
                mid = s + (k >> 1);
        }
        const l = bounds(s, mid - s);
        bounds(mid, s + k - mid);
        node.a = l;
        node.n = 0;
        stack.push(l, l + 1);
    }
    return { nodes, order };
}

// nodes as the shader's array<Node>: lo, a, hi, n; leaves offset by `prim`, children by `base`
function packNodes(nodes, base, prim, f32, u32, at) {
    nodes.forEach((nd, i) => {
        const o = 8 * (at + i);
        f32.set(nd.lo, o);
        f32.set(nd.hi, o + 4);
        u32[o + 3] = nd.n ? nd.a + prim : nd.a + base;
        u32[o + 7] = nd.n;
    });
}

// ---- the renderer ------------------------------------------------------------------

export class NextGen {
    constructor(gpu, core, renderer) {
        this.gpu = gpu;
        this.core = core;
        this.renderer = renderer;
        this.device = gpu.device;
        this.on = false;
        this.quality = 'high';
        this.fx = { reflect: true, rays: true, particles: true, bloom: true, dof: true, gi: true, taa: true };
        // TAA: frames drawn, how many in a row were the same, what the last was
        this.frame = 0;
        this.still = 0;
        this.last = null;
        this.halo = true;
        this.grade = null;
        this.exposure = 1.0;
        this.debug = 0;
        this.parts = Array.from({ length: PARTS }, (_, i) => core.part(i));
        const t0 = performance.now();
        this.buildGeometry();
        this.buildTargetsOnce();
        this.buildPipelines();
        this.buildTime = performance.now() - t0;
    }

    // The forms of every group in level 0 as one list of triangles for the
    // raster and, per form, a BVH in the form's own space.
    buildGeometry() {
        const data = this.renderer.data, d = this.device;
        const verts = [], forms = new Map();
        const triPos = [], triNrm = [], blasNodes = [];
        let triCount = 0, nodeCount = 0;
        for (let g = 0; g < data.chamber.length; g++) {
            const mesh = data.chamber[g][0], v = mesh.vertices, a = mesh.attributes, af = mesh.attrFloats, ao = af === 11 ? 2 : 0;
            mesh.forms.forEach((form, f) => {
                const at = (s, n) => mesh.indices.subarray(s, s + n);
                const tri = form.fan
                    ? [...triangles(7, at(form.firstIndex, form.fan)), ...triangles(7, at(form.firstIndex + form.fan, form.fan)),
                       ...triangles(6, at(form.firstIndex + 2 * form.fan, form.indexCount - 2 * form.fan))]
                    : triangles(mesh.prim, at(form.firstIndex, form.indexCount));
                const first = verts.length / VERTEX;
                const P = [], N = [];
                for (const i of tri) {
                    const b = af * i + ao;
                    verts.push(v[3 * i], v[3 * i + 1], v[3 * i + 2], a[b], a[b + 1], a[b + 2], a[b + 3], a[b + 4], a[b + 5],
                               a[b + 6], a[b + 7], a[b + 8], ao ? a[af * i] : 0, ao ? a[af * i + 1] : 0, ao ? 1 : 0);
                }
                // the BVH: triangles with an area only
                const keep = [];
                for (let k = 0; k + 2 < tri.length; k += 3) {
                    const p = [0, 1, 2].map((j) => [v[3 * tri[k + j]], v[3 * tri[k + j] + 1], v[3 * tri[k + j] + 2]]);
                    const c = cross(sub(p[1], p[0]), sub(p[2], p[0]));
                    if (Math.hypot(...c) > 1e-10) {
                        keep.push(k);
                        P.push(p);
                        N.push([0, 1, 2].map((j) => { const b = af * tri[k + j] + ao + 6; return [a[b], a[b + 1], a[b + 2]]; }));
                    }
                }
                const n = P.length, lo = new Float32Array(3 * n), hi = new Float32Array(3 * n);
                P.forEach((p, i) => {
                    for (let q = 0; q < 3; q++) {
                        lo[3 * i + q] = Math.min(p[0][q], p[1][q], p[2][q]);
                        hi[3 * i + q] = Math.max(p[0][q], p[1][q], p[2][q]);
                    }
                });
                const bvh = buildBvh(lo, hi, n, 4);
                for (const i of bvh.order) {
                    triPos.push(...P[i][0], 0, ...P[i][1], 0, ...P[i][2], 0);
                    triNrm.push(...N[i][0], 0, ...N[i][1], 0, ...N[i][2], 0);
                }
                blasNodes.push({ nodes: bvh.nodes, base: nodeCount, prim: triCount });
                forms.set(`${g}:${f}`, { first, count: tri.length, root: nodeCount, lo: bvh.nodes[0].lo, hi: bvh.nodes[0].hi });
                nodeCount += bvh.nodes.length;
                triCount += n;
            });
        }
        this.forms = forms;
        this.partForm = this.parts.map((p) => forms.get(`${p.group}:${Math.max(p.form, 0)}`));
        this.chamberTriangles = this.parts.reduce((k, p, i) => k + (this.partForm[i]?.count || 0) / 3, 0);
        const buffer = (array, usage) => {
            const b = d.createBuffer({ size: Math.max(16, array.byteLength), usage: usage | GPUBufferUsage.COPY_DST });
            d.queue.writeBuffer(b, 0, array);
            return b;
        };
        this.vertexBuffer = buffer(new Float32Array(verts), GPUBufferUsage.VERTEX);
        const nodeData = new ArrayBuffer(32 * nodeCount), nf = new Float32Array(nodeData), nu = new Uint32Array(nodeData);
        for (const b of blasNodes)
            packNodes(b.nodes, b.base, b.prim, nf, nu, b.base);
        this.blas = buffer(nf, GPUBufferUsage.STORAGE);
        this.triPos = buffer(new Float32Array(triPos), GPUBufferUsage.STORAGE);
        this.triNrm = buffer(new Float32Array(triNrm), GPUBufferUsage.STORAGE);
        this.rtTriangles = triCount;

        // the parts: W, W^-1, the root of their form, how they look
        this.instData = new ArrayBuffer(144 * PARTS);
        this.instF = new Float32Array(this.instData);
        this.instU = new Uint32Array(this.instData);
        this.inst = d.createBuffer({ size: 144 * PARTS, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
        // the same a frame before, for the velocity
        this.prevInstF = new Float32Array(36 * PARTS);
        this.prevInst = d.createBuffer({ size: 144 * PARTS, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
        this.tlas = d.createBuffer({ size: 32 * 2 * PARTS, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
        this.torder = d.createBuffer({ size: 4 * PARTS, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
        // a look per part: metal from bright to dark, and how rough
        this.look = this.parts.map((p, i) => {
            const h = Math.sin(i * 12.9898 + p.group * 78.233) * 43758.5453, r = h - Math.floor(h);
            const dark = p.group === 2 || r < 0.18;
            return [dark ? 0.32 + 0.12 * r : 0.62 + 0.25 * r, 0.22 + 0.25 * ((r * 7.31) % 1)];
        });

        // the blob: its unit sphere, the triangles of its strip, the cut of each into n * n
        const blob = data.blob;
        this.blobUnit = blob.vertices;
        this.blobTris = buffer(Uint32Array.from(triangles(6, blob.indices)), GPUBufferUsage.STORAGE);
        this.blobTriCount = triangles(6, blob.indices).length / 3;
        this.blobCtrlData = new Float32Array(8 * blob.vertices.length / 3);
        this.blobPrevData = new Float32Array(this.blobCtrlData.length);
        this.blobCtrl = d.createBuffer({ size: this.blobCtrlData.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
        this.blobPrev = d.createBuffer({ size: this.blobCtrlData.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
        // The mesh is a cube of six faces of 17 x 17 vertices, 289 one after
        // the other (Blob.md 2, +0x30), each on the grid of its face (measured:
        // every vertex within 1e-4 of a point). Slot of each vertex in the
        // table the ray reads (nextgen-shaders.js, blob_at).
        const u = blob.vertices, nb = u.length / 3;
        this.blobSlot = new Uint32Array(nb);
        const used = new Set();
        for (let b = 0; b < nb / 289; b++) {
            const mean = [0, 0, 0];
            for (let i = 289 * b; i < 289 * (b + 1); i++)
                for (let a = 0; a < 3; a++)
                    mean[a] += u[3 * i + a];
            const axis = mean.map(Math.abs).indexOf(Math.max(...mean.map(Math.abs)));
            const face = 2 * axis + (mean[axis] > 0 ? 0 : 1);
            const [ia, ib] = [[1, 2], [0, 2], [0, 1]][axis];
            for (let i = 289 * b; i < 289 * (b + 1); i++) {
                const m = Math.abs(u[3 * i + axis]);
                const gs = Math.round((u[3 * i + ia] / m + 1) * 8), gt = Math.round((u[3 * i + ib] / m + 1) * 8);
                const slot = face * 289 + gt * 17 + gs;
                if (used.has(slot) || gs < 0 || gs > 16 || gt < 0 || gt > 16)
                    throw new Error('the blob is not a cube of 17 x 17 a face');
                used.add(slot);
                this.blobSlot[i] = slot;
            }
        }
        this.gridData = new Float32Array(4 * 6 * 289);
        this.grid = d.createBuffer({ size: this.gridData.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
        this.bary = new Map();
        // a finely cut unit sphere for the drops
        const ico = icosphere(4);
        this.dropVerts = buffer(ico, GPUBufferUsage.VERTEX);
        this.dropCount = ico.length / 3;

        this.frameData = new Float32Array(FRAME_FLOATS);
        this.frameBuffer = d.createBuffer({ size: FRAME_FLOATS * 4, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        this.faceBuffer = d.createBuffer({ size: 256 * 6, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        for (let f = 0; f < 6; f++)
            d.queue.writeBuffer(this.faceBuffer, 256 * f, new Uint32Array([f, 0, 0, 0]));
    }

    baryFor(n) {
        if (!this.bary.has(n)) {
            const b = [];
            for (let i = 0; i < n; i++)
                for (let j = 0; i + j < n; j++) {
                    b.push(i / n, j / n, (i + 1) / n, j / n, i / n, (j + 1) / n);
                    if (i + j < n - 1)
                        b.push((i + 1) / n, j / n, (i + 1) / n, (j + 1) / n, i / n, (j + 1) / n);
                }
            const data = new Float32Array(b), buf = this.device.createBuffer({ size: data.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
            this.device.queue.writeBuffer(buf, 0, data);
            this.bary.set(n, { buf, count: data.length / 2 });
        }
        return this.bary.get(n);
    }

    // The relief with all its levels, for a highlight that does not shimmer.
    buildTargetsOnce() {
        const d = this.device, src = this.renderer.surfaces.get(SURF_RELIEF);
        const levels = Math.floor(Math.log2(Math.max(src.width, src.height))) + 1;
        this.relief = d.createTexture({ size: [src.width, src.height], format: 'rgba8unorm', mipLevelCount: levels,
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_DST });
        const enc = d.createCommandEncoder();
        const m = d.createShaderModule({ code: SH.MIP });
        const mip = (entryPoint) => d.createRenderPipeline({ layout: 'auto', vertex: { module: m }, fragment: { module: m, entryPoint, targets: [{ format: 'rgba8unorm' }] } });
        const copy = mip('copy'), down = mip('fs');
        for (let l = 0; l < levels; l++) {
            const pipe = l ? down : copy;
            const bind = d.createBindGroup({ layout: pipe.getBindGroupLayout(0), entries: [
                { binding: 0, resource: l ? this.relief.createView({ baseMipLevel: l - 1, mipLevelCount: 1 }) : src.createView({ dimension: '2d', mipLevelCount: 1 }) }] });
            const p = enc.beginRenderPass({ colorAttachments: [{ view: this.relief.createView({ baseMipLevel: l, mipLevelCount: 1 }), loadOp: 'clear', storeOp: 'store' }] });
            p.setPipeline(pipe);
            p.setBindGroup(0, bind);
            p.draw(3);
            p.end();
        }
        d.queue.submit([enc.finish()]);
        this.samplers = {
            cmp: d.createSampler({ compare: 'less-equal', magFilter: 'linear', minFilter: 'linear' }),
            lin: d.createSampler({ magFilter: 'linear', minFilter: 'linear' }),
            rep: d.createSampler({ magFilter: 'linear', minFilter: 'linear', mipmapFilter: 'linear', addressModeU: 'repeat', addressModeV: 'repeat', maxAnisotropy: 16 }),
            edge: d.createSampler({ magFilter: 'linear', minFilter: 'linear' }),
        };
    }

    buildPipelines() {
        const d = this.device, ALL = GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT | GPUShaderStage.COMPUTE;
        this.sceneLayout = d.createBindGroupLayout({ entries: [
            { binding: 0, visibility: ALL, buffer: {} },
            { binding: 1, visibility: ALL, buffer: { type: 'read-only-storage' } },
            { binding: 2, visibility: ALL, texture: { sampleType: 'depth', viewDimension: '2d-array' } },
            { binding: 3, visibility: ALL, sampler: { type: 'comparison' } },
            { binding: 4, visibility: ALL, sampler: {} },
            { binding: 5, visibility: GPUShaderStage.FRAGMENT, texture: {} },
            { binding: 6, visibility: GPUShaderStage.FRAGMENT, sampler: {} },
            { binding: 7, visibility: GPUShaderStage.VERTEX, buffer: { type: 'read-only-storage' } },
        ] });
        this.shadowLayout = d.createBindGroupLayout({ entries: [
            { binding: 0, visibility: ALL, buffer: {} },
            { binding: 1, visibility: ALL, buffer: { type: 'read-only-storage' } }] });
        this.faceLayout = d.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX, buffer: { hasDynamicOffset: true, minBindingSize: 16 } }] });
        const storage = (n, vis) => d.createBindGroupLayout({ entries: Array.from({ length: n }, (_, binding) => ({ binding, visibility: vis, buffer: { type: 'read-only-storage' } })) });
        this.blobLayout = storage(4, GPUShaderStage.VERTEX);
        this.rtLayout = storage(6, GPUShaderStage.COMPUTE);
        const module = (code) => d.createShaderModule({ code });
        const layout = (...groups) => d.createPipelineLayout({ bindGroupLayouts: groups });
        const at = (shaderLocation, offset, n) => ({ shaderLocation, offset: 4 * offset, format: `float32x${n}` });

        const sm = module(SH.SHADOW);
        this.shadowPipe = d.createRenderPipeline({ layout: layout(this.shadowLayout, this.faceLayout),
            vertex: { module: sm, buffers: [{ arrayStride: 4 * VERTEX, attributes: [at(0, 0, 3)] }] },
            primitive: { topology: 'triangle-list', cullMode: 'none' },
            depthStencil: { format: 'depth32float', depthCompare: 'less', depthWriteEnabled: true, depthBias: 3, depthBiasSlopeScale: 2.5 } });

        const targets = [{ format: HDR }, { format: HDR }, { format: HDR }];
        const ms = { count: 4 };
        const depth = { format: 'depth32float', depthCompare: 'less', depthWriteEnabled: true };
        const cm = module(SH.CHAMBER);
        this.chamberPipe = d.createRenderPipeline({ layout: layout(this.sceneLayout),
            vertex: { module: cm, buffers: [{ arrayStride: 4 * VERTEX, attributes: [at(0, 0, 3), at(1, 3, 3), at(2, 6, 3), at(3, 9, 3), at(4, 12, 3)] }] },
            fragment: { module: cm, targets }, primitive: { topology: 'triangle-list', cullMode: 'none' }, depthStencil: depth, multisample: ms });
        const bm = module(SH.BLOB);
        this.blobPipe = d.createRenderPipeline({ layout: layout(this.sceneLayout, this.blobLayout), vertex: { module: bm },
            fragment: { module: bm, targets }, primitive: { topology: 'triangle-list', cullMode: 'none' }, depthStencil: depth, multisample: ms });
        const dm = module(SH.DROP);
        this.dropPipe = d.createRenderPipeline({ layout: layout(this.sceneLayout),
            vertex: { module: dm, buffers: [{ arrayStride: 12, attributes: [at(0, 0, 3)] }] },
            fragment: { module: dm, targets }, primitive: { topology: 'triangle-list', cullMode: 'none' }, depthStencil: depth, multisample: ms });
        const pm = module(SH.PARTICLES);
        const add = { color: { srcFactor: 'one', dstFactor: 'one' }, alpha: { srcFactor: 'zero', dstFactor: 'one' } };
        // the particles into a target of their own, against the main pass's depth
        this.particlePipe = d.createRenderPipeline({ layout: layout(this.sceneLayout), vertex: { module: pm },
            fragment: { module: pm, targets: [{ format: HDR, blend: add, writeMask: GPUColorWrite.RED | GPUColorWrite.GREEN | GPUColorWrite.BLUE }] },
            primitive: { topology: 'triangle-list' }, depthStencil: { format: 'depth32float', depthCompare: 'less', depthWriteEnabled: false }, multisample: ms });

        this.traceLayout = d.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: 'unfilterable-float' } },
            { binding: 1, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: 'unfilterable-float' } },
            { binding: 2, visibility: GPUShaderStage.COMPUTE, storageTexture: { access: 'write-only', format: HDR } },
            { binding: 3, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: 'unfilterable-float' } }] });
        const rm = module(SH.REFLECT);
        this.reflectPipe = d.createComputePipeline({ layout: layout(this.sceneLayout, this.rtLayout, this.traceLayout), compute: { module: rm, entryPoint: 'main' } });
        this.giPipe = d.createComputePipeline({ layout: layout(this.sceneLayout, this.rtLayout, this.traceLayout), compute: { module: rm, entryPoint: 'gi' } });
        this.fogPipe = d.createComputePipeline({ layout: layout(this.sceneLayout, d.createBindGroupLayout({ entries: [] }), d.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: 'unfilterable-float' } },
            { binding: 1, visibility: GPUShaderStage.COMPUTE, storageTexture: { access: 'write-only', format: HDR } }] })),
            compute: { module: module(SH.FOG) } });
        // the bounce over frames (GI_ACC): this frame's rays, the history, velocity, depth; into the next history
        this.giAccPipe = d.createComputePipeline({ layout: layout(this.sceneLayout, d.createBindGroupLayout({ entries: [
            ...[0, 1, 2, 3].map((binding) => ({ binding, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: 'unfilterable-float' } })),
            { binding: 4, visibility: GPUShaderStage.COMPUTE, storageTexture: { access: 'write-only', format: HDR } }] })),
            compute: { module: module(SH.GI_ACC) } });
        this.emptyBind = d.createBindGroup({ layout: this.fogPipe.getBindGroupLayout(1), entries: [] });
        this.blurPipe = d.createComputePipeline({ layout: 'auto', compute: { module: module(SH.BLUR) } });
        this.blurU = [0, 1, 2, 3, 4, 5].map((i) => {
            const b = d.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
            // reflections 5 taps a side, fog 4, the bounce 7
            d.queue.writeBuffer(b, 0, new Int32Array([i & 1 ? 0 : 1, i & 1 ? 1 : 0, [5, 5, 4, 4, 7, 7][i], 0]));
            return b;
        });
        const om = module(SH.COMPOSE);
        this.composePipe = d.createRenderPipeline({ layout: layout(this.sceneLayout, d.createBindGroupLayout({ entries: [
            ...[0, 1].map((binding) => ({ binding, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'unfilterable-float' } })),
            ...[2, 3, 4, 7].map((binding) => ({ binding, visibility: GPUShaderStage.FRAGMENT, texture: {} })),
            { binding: 5, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'depth' } },
            { binding: 6, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'unfilterable-float' } }] })),
            vertex: { module: om }, fragment: { module: om, targets: [{ format: HDR }, { format: HDR }] } });
        const blm = module(SH.BLOOM);
        this.downPipe = d.createRenderPipeline({ layout: 'auto', vertex: { module: blm }, fragment: { module: blm, entryPoint: 'down', targets: [{ format: HDR }] } });
        this.upPipe = d.createRenderPipeline({ layout: 'auto', vertex: { module: blm },
            fragment: { module: blm, entryPoint: 'up', targets: [{ format: HDR, blend: { color: { srcFactor: 'one', dstFactor: 'one' }, alpha: { srcFactor: 'one', dstFactor: 'one' } } }] } });
        const dfm = module(SH.DOF);
        // textures for a fragment shader; `sampled` those read through a filter
        const loads = (n, sampled) => d.createBindGroupLayout({ entries: Array.from({ length: n }, (_, binding) => ({
            binding, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: binding === sampled ? 'float' : 'unfilterable-float' } })) });
        this.dofPipe = d.createRenderPipeline({ layout: layout(this.sceneLayout, loads(2, 0)), vertex: { module: dfm }, fragment: { module: dfm, targets: [{ format: HDR }] } });
        const tm = module(SH.TAA);
        this.taaPipe = d.createRenderPipeline({ layout: layout(this.sceneLayout, loads(5, 1)), vertex: { module: tm },
            fragment: { module: tm, targets: [{ format: HDR }, { format: HDR }] } });
        const fm = module(SH.FINISH);
        this.finishPipe = d.createRenderPipeline({ layout: layout(this.sceneLayout, d.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: {} }, { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: {} }] })),
            vertex: { module: fm }, fragment: { module: fm, targets: [{ format: 'rgba8unorm' }] } });
    }

    // The renderer's surface for a picture of w x h: as much larger as the
    // GPU and about 14 million pixels allow, up to twice in each direction,
    // so that the logo's edges are averaged as well.
    legacySize(w, h) {
        const max = this.device.limits.maxTextureDimension2D;
        const s = Math.max(1, Math.min(2, Math.sqrt(14e6 / (2 * w * h)), max / (2 * w), max / h));
        return [Math.floor(w * s), Math.floor(h * s)];
    }

    // The targets at the size of the render.
    targets(w, h) {
        const q = QUALITY[this.quality] || QUALITY.high;
        const rw = Math.max(1, Math.round(w * q.scale)), rh = Math.max(1, Math.round(h * q.scale));
        const key = `${rw}x${rh}/${q.shadow}`;
        if (this.size === key)
            return;
        this.size = key;
        for (const t of this.owned || [])
            t.destroy();
        const d = this.device, owned = this.owned = [];
        const tex = (w2, h2, format, usage, more = {}) => {
            const t = d.createTexture({ size: [w2, h2, more.layers || 1], format, usage, sampleCount: more.samples || 1 });
            owned.push(t);
            return t;
        };
        const RT = GPUTextureUsage.RENDER_ATTACHMENT, TB = GPUTextureUsage.TEXTURE_BINDING, ST = GPUTextureUsage.STORAGE_BINDING;
        this.rw = rw;
        this.rh = rh;
        this.msColour = tex(rw, rh, HDR, RT, { samples: 4 });
        this.msG = tex(rw, rh, HDR, RT, { samples: 4 });
        this.msMotion = tex(rw, rh, HDR, RT, { samples: 4 });
        this.motion = tex(rw, rh, HDR, RT | TB);
        this.motion2 = tex(rw, rh, HDR, RT | TB);
        this.msDepth = tex(rw, rh, 'depth32float', RT, { samples: 4 });
        this.hdr = tex(rw, rh, HDR, RT | TB);
        this.msParts = tex(rw, rh, HDR, RT, { samples: 4 });
        this.parts = tex(rw, rh, HDR, RT | TB);
        this.gbuf = tex(rw, rh, HDR, RT | TB);
        const hw = Math.ceil(rw / 2), hh = Math.ceil(rh / 2);
        this.half = [hw, hh];
        this.reflA = tex(hw, hh, HDR, ST | TB);
        this.reflB = tex(hw, hh, HDR, ST | TB);
        this.fogA = tex(hw, hh, HDR, ST | TB);
        this.fogB = tex(hw, hh, HDR, ST | TB);
        this.giA = tex(hw, hh, HDR, ST | TB);
        this.giB = tex(hw, hh, HDR, ST | TB);
        this.giHist = [tex(hw, hh, HDR, ST | TB), tex(hw, hh, HDR, ST | TB)];
        this.giOk = false;
        this.hist = [tex(rw, rh, HDR, RT | TB), tex(rw, rh, HDR, RT | TB)];
        this.shown = tex(rw, rh, HDR, RT | TB);
        this.histOk = false;
        this.scene = tex(rw, rh, HDR, RT | TB);
        this.sharp = tex(rw, rh, HDR, RT | TB);
        this.bloom = [];
        let bw = rw, bh = rh;
        for (let i = 0; i < 6 && bw > 2 && bh > 2; i++) {
            bw = Math.ceil(bw / 2);
            bh = Math.ceil(bh / 2);
            this.bloom.push(tex(bw, bh, HDR, RT | TB));
        }
        this.shadowMap = tex(q.shadow, q.shadow, 'depth32float', RT | TB, { layers: 6 });
        this.faceViews = [0, 1, 2, 3, 4, 5].map((f) => this.shadowMap.createView({ dimension: '2d', baseArrayLayer: f, arrayLayerCount: 1 }));

        const v = (t) => t.createView();
        this.sceneBind = d.createBindGroup({ layout: this.sceneLayout, entries: [
            { binding: 0, resource: { buffer: this.frameBuffer } }, { binding: 1, resource: { buffer: this.inst } },
            { binding: 2, resource: this.shadowMap.createView({ dimension: '2d-array' }) }, { binding: 3, resource: this.samplers.cmp },
            { binding: 4, resource: this.samplers.lin }, { binding: 5, resource: this.relief.createView() },
            { binding: 6, resource: this.samplers.rep }, { binding: 7, resource: { buffer: this.prevInst } }] });
        this.shadowBind = d.createBindGroup({ layout: this.shadowLayout, entries: [
            { binding: 0, resource: { buffer: this.frameBuffer } }, { binding: 1, resource: { buffer: this.inst } }] });
        this.faceBind = d.createBindGroup({ layout: this.faceLayout, entries: [{ binding: 0, resource: { buffer: this.faceBuffer, size: 16 } }] });
        this.rtBind = d.createBindGroup({ layout: this.rtLayout, entries: [this.blas, this.tlas, this.triPos, this.triNrm, this.torder, this.grid]
            .map((buffer, binding) => ({ binding, resource: { buffer } })) });
        const traced = (out) => d.createBindGroup({ layout: this.traceLayout, entries: [
            { binding: 0, resource: v(this.hdr) }, { binding: 1, resource: v(this.gbuf) }, { binding: 2, resource: v(out) },
            { binding: 3, resource: v(this.motion) }] });
        this.reflectBind = traced(this.reflA);
        // the bounce's rays into giB, over frames into history o, blurred through giB into giA
        this.giBind = traced(this.giB);
        this.giAccBinds = [0, 1].map((o) => d.createBindGroup({ layout: this.giAccPipe.getBindGroupLayout(1), entries: [
            { binding: 0, resource: v(this.giB) }, { binding: 1, resource: v(this.giHist[1 - o]) }, { binding: 2, resource: v(this.motion) },
            { binding: 3, resource: v(this.gbuf) }, { binding: 4, resource: v(this.giHist[o]) }] }));
        this.fogBind = d.createBindGroup({ layout: this.fogPipe.getBindGroupLayout(2), entries: [
            { binding: 0, resource: v(this.gbuf) }, { binding: 1, resource: v(this.fogA) }] });
        const blur = (src, dst, u) => d.createBindGroup({ layout: this.blurPipe.getBindGroupLayout(0), entries: [
            { binding: 0, resource: v(src) }, { binding: 1, resource: v(this.gbuf) }, { binding: 2, resource: v(dst) }, { binding: 3, resource: { buffer: u } }] });
        this.blurs = [blur(this.reflA, this.reflB, this.blurU[0]), blur(this.reflB, this.reflA, this.blurU[1]),
                      blur(this.fogA, this.fogB, this.blurU[2]), blur(this.fogB, this.fogA, this.blurU[3])];
        this.giBlurs = [0, 1].map((o) => [blur(this.giHist[o], this.giB, this.blurU[4]), blur(this.giB, this.giA, this.blurU[5])]);
        const bl = (pipe, src) => d.createBindGroup({ layout: pipe.getBindGroupLayout(0), entries: [{ binding: 0, resource: v(src) }, { binding: 1, resource: this.samplers.edge }] });
        this.dofBind = d.createBindGroup({ layout: this.dofPipe.getBindGroupLayout(1), entries: [
            { binding: 0, resource: v(this.sharp) }, { binding: 1, resource: v(this.gbuf) }] });
        // TAA from history i into history 1 - i
        this.taaBinds = [0, 1].map((i) => d.createBindGroup({ layout: this.taaPipe.getBindGroupLayout(1), entries: [
            { binding: 0, resource: v(this.scene) }, { binding: 1, resource: v(this.hist[i]) }, { binding: 2, resource: v(this.motion2) },
            { binding: 3, resource: v(this.gbuf) }, { binding: 4, resource: v(this.parts) }] }));
        this.downBinds = this.bloom.map((t, i) => bl(this.downPipe, i ? this.bloom[i - 1] : this.shown));
        this.upBinds = this.bloom.map((t) => bl(this.upPipe, t));
        this.finishBind = d.createBindGroup({ layout: this.finishPipe.getBindGroupLayout(1), entries: [
            { binding: 0, resource: v(this.shown) }, { binding: 1, resource: v(this.bloom[0] || this.shown) }] });
        this.composeKey = null;
    }

    // The bind group of COMPOSE: it reads the renderer's surface, which
    // changes with the size of the picture.
    composeBind() {
        const r = this.renderer, key = r.screen;
        if (this.composeKey !== key || !this.composeGroup || this.composeFor !== this.size) {
            this.composeFor = this.size;
            const v = (t) => t.createView();
            this.composeGroup = this.device.createBindGroup({ layout: this.composePipe.getBindGroupLayout(1), entries: [
                { binding: 0, resource: v(this.hdr) }, { binding: 1, resource: v(this.gbuf) }, { binding: 2, resource: v(this.reflA) },
                { binding: 3, resource: v(this.fogA) }, { binding: 4, resource: v(r.screen) }, { binding: 5, resource: v(r.screenDepth) },
                { binding: 6, resource: v(this.motion) }, { binding: 7, resource: v(this.giA) }] });
            this.composeKey = key;
        }
        return this.composeGroup;
    }

    // The original draws what this picture keeps: the logo, the veil, the
    // glow. Everything the workbench may have switched is put aside meanwhile.
    legacy() {
        const r = this.renderer;
        const keep = ['hide', 'solo', 'wire', 'flat', 'room', 'show', 'grade', 'path', 'lines', 'hover', 'chosen', 'background', 'coarse', 'ghost'];
        const saved = Object.fromEntries(keep.map((k) => [k, r[k]]));
        r.hide = new Set([...NG_KEYS, ...(this.halo ? [] : ['glow'])]);
        Object.assign(r, { solo: null, wire: 0, flat: false, room: false, show: 'picture', grade: null, path: null, lines: null,
                           hover: null, chosen: null, background: null, coarse: false, ghost: new Set(), noShow: true });
        const snap = r.off.has('snap');
        r.off.add('snap');
        let fr;
        try {
            fr = r.draw();
        } finally {
            Object.assign(r, saved);
            r.noShow = false;
            if (!snap)
                r.off.delete('snap');
        }
        return fr;
    }

    // True while a standing frame still gathers: the workbench draws it
    // again until TAA has averaged it (docs/web.md 12.2).
    settling() {
        return this.on && this.fx.taa && this.still < 40;
    }

    // Draws the frame of the core's last advance into the picture.
    draw() {
        const q = QUALITY[this.quality] || QUALITY.high, gpu = this.gpu, d = this.device, core = this.core, r = this.renderer;
        const fr = this.legacy();
        const seen = r.seen;
        const view = Float64Array.from(fr.view), proj = Float64Array.from(fr.proj);
        this.targets(gpu.width, gpu.height);
        const tm = core.timing(), t = tm.t;
        const chamber = [...seen].some((k) => k.startsWith('chamber'));
        const blobOn = seen.has('blob_mesh');
        const dropsOn = seen.has('blob_drop');
        const world = core.chamberWorld(t).world.slice();
        const blob = core.blobFull();

        // ---- the frame's values
        const vp = mulRow(view, proj);
        for (let i = 0; i < 4; i++) {
            vp[4 * i] *= r.wide[0];
            vp[4 * i + 1] *= r.wide[1];
        }
        // TAA: is this the frame before once more (standing still), or a cut?
        const key = [t, ...vp].join(','), how = `${this.size}/${Object.values(this.fx).join('')}/${this.halo}`;
        const cut = !this.fx.taa || !this.histOk || !this.last || this.last.how !== how || t < this.last.t - 1e-6 || t > this.last.t + 0.15;
        this.still = !cut && this.last.key === key ? this.still + 1 : 0;
        const plain = Float64Array.from(vp), before = cut ? plain : this.last.vp;
        // the jitter: Halton 2, 3 over 16 frames, within one pixel of the render
        this.frame = (this.frame + 1) % 4096;
        const halton = (i, b) => { let f = 1, x = 0; for (; i > 0; i = Math.floor(i / b)) { f /= b; x += f * (i % b); } return x; };
        const jx = this.fx.taa ? (halton(this.frame % 16 + 1, 2) - 0.5) * 2 / this.rw : 0;
        const jy = this.fx.taa ? (halton(this.frame % 16 + 1, 3) - 0.5) * 2 / this.rh : 0;
        for (let i = 0; i < 4; i++) {
            vp[4 * i] += jx * vp[4 * i + 3];
            vp[4 * i + 1] += jy * vp[4 * i + 3];
        }
        this.last = { key, how, t, vp: plain };
        const ivp = invert(vp), iv = invert(view);
        const eye = [iv[12], iv[13], iv[14]];
        const m = 2 * tm.x20, a = 0.001 / Math.max(m, 1e-3);
        const k = Math.fround((Math.sqrt(Math.max(tm.x20 - tm.x1c, 0)) * 1.3 + 1) * blob.radius);
        const br = ((tm.x20 - tm.x1c) * 0.8 + tm.x1c * 1.2) * 4 + 0.3, f = Math.min(4 * t, 1) * br;
        const c0 = blob.colour.slice(0, 3).map((x) => x * f);
        const flash = Math.max(0, 1 - Math.abs(t - 5.1) / 0.2);
        const F = this.frameData;
        F.fill(0);
        F.set(vp, 0);
        F.set(ivp, 16);
        F.set([...eye, t], 32);
        F.set([...blob.center, k], 36);
        F.set([...G.map((x) => x * m * 1.6), m], 40);
        F.set([1, a, a, 0.012 + 0.04 * flash], 44);
        F.set([...c0, blobOn ? 1 : 0], 48);
        F.set([jx, jy, this.frame, this.fx.gi ? 1 : 0], 288);
        F.set([this.still ? Math.max(1 / (this.still + 1), 0.03) : 0.12, cut ? 0 : 1, this.still ? 0 : 0.85, q.giRays], 292);
        F.set(before, 296);
        F.set([this.rw, this.rh, 1 / this.rw, 1 / this.rh], 52);
        F.set([this.fx.reflect ? 1 : 0, this.fx.rays ? 1 : 0, this.fx.particles ? 1 : 0, this.exposure], 56);
        const drops = dropsOn ? blob.drop.slice(0, SH.MAX_DROPS) : [];
        F.set([tm.x20, tm.x1c, 0, drops.length], 60);
        F.set([view[0], view[4], view[8], gpu.width], 64);
        F.set([view[1], view[5], view[9], gpu.height], 68);
        F.set([LIGHT_NEAR, LIGHT_FAR, q.shadow, chamber ? 1 : 0], 72);
        F.set([this.fx.bloom ? 0.09 : 0, this.debug, q.particles, q.steps], 76);
        const g = this.grade?.rot || [1, 0, 0, 0, 1, 0, 0, 0, 1];
        // rows of the playground's turn as the columns of a WGSL mat3x3f
        F.set([g[0], g[3], g[6], this.giOk ? 1 : 0, g[1], g[4], g[7], 0, g[2], g[5], g[8], 0], 80);
        // depth of field: sharp at the blob, the circle grows with the distance from it
        const focus = Math.hypot(eye[0] - blob.center[0], eye[1] - blob.center[1], eye[2] - blob.center[2]);
        F.set([focus, this.fx.dof ? 0.8 : 0, 18 * this.rh / 1080, 1], 92);
        const was = cut ? [] : this.lastDrops || [];
        drops.forEach((dr, i) => {
            const s = dr.size / Math.sqrt(dr.scale), z = dr.scale * dr.size - s, w = was[i] || dr.pos;
            F.set([...dr.pos, w[0], ...dr.dir, w[1], s, z, 1, w[2]], 96 + 12 * i);
        });
        this.lastDrops = drops.map((dr) => dr.pos);
        const L = blob.center;
        FACES.forEach(([fwd, up], i) => {
            F.set(mulCol(perspective(Math.PI / 2, 1, LIGHT_NEAR, LIGHT_FAR), lookAt(L, [L[0] + fwd[0], L[1] + fwd[1], L[2] + fwd[2]], up)), 96 + 12 * SH.MAX_DROPS + 16 * i);
        });

        // ---- the parts and the BVH over them
        const lo = new Float32Array(3 * PARTS), hi = new Float32Array(3 * PARTS), live = [];
        for (let i = 0; i < PARTS; i++) {
            const W = world.subarray(16 * i, 16 * i + 16), form = this.partForm[i], o = 36 * i;
            const on = form && chamber && !W.some(Number.isNaN);
            this.instF.set(on ? W : new Float32Array(16), o);
            this.instF.set(on ? invert(W) : new Float32Array(16), o + 16);
            this.instU[o + 32] = form?.root || 0;
            this.instU[o + 33] = on ? 1 : 0;
            this.instF[o + 34] = this.look[i][0];
            this.instF[o + 35] = this.look[i][1];
            if (!on)
                continue;
            const b = [[Infinity, Infinity, Infinity], [-Infinity, -Infinity, -Infinity]];
            for (let c = 0; c < 8; c++) {
                const p = [c & 1 ? form.hi[0] : form.lo[0], c & 2 ? form.hi[1] : form.lo[1], c & 4 ? form.hi[2] : form.lo[2]];
                for (let j = 0; j < 3; j++) {
                    const x = p[0] * W[j] + p[1] * W[4 + j] + p[2] * W[8 + j] + W[12 + j];
                    b[0][j] = Math.min(b[0][j], x);
                    b[1][j] = Math.max(b[1][j], x);
                }
            }
            lo.set(b[0], 3 * live.length);
            hi.set(b[1], 3 * live.length);
            live.push(i);
        }
        d.queue.writeBuffer(this.inst, 0, this.instF);
        d.queue.writeBuffer(this.prevInst, 0, cut ? this.instF : this.prevInstF);
        this.prevInstF.set(this.instF);
        if (live.length) {
            const tl = buildBvh(lo, hi, live.length, 2), nodes = new ArrayBuffer(32 * tl.nodes.length);
            packNodes(tl.nodes, 0, 0, new Float32Array(nodes), new Uint32Array(nodes), 0);
            d.queue.writeBuffer(this.tlas, 0, nodes);
            d.queue.writeBuffer(this.torder, 0, Uint32Array.from(tl.order, (j) => live[j]));
        }
        const traceable = live.length > 0;

        // ---- the blob's control points: centre + (k + o.w) p, normal o.xyz (Blob.md 6.4)
        // and the same as a table over the cube for the rays, with the
        // largest distance as the sphere around it
        let reach = k;
        if (blobOn) {
            const def = core.blobDeformed(), u = this.blobUnit, C = this.blobCtrlData, P = this.blobPrevData;
            P.set(C);
            for (let i = 0; i < u.length / 3; i++) {
                const s = k + def[4 * i + 3];
                C.set([L[0] + s * u[3 * i], L[1] + s * u[3 * i + 1], L[2] + s * u[3 * i + 2], 1], 8 * i);
                const n = norm([def[4 * i], def[4 * i + 1], def[4 * i + 2]]);
                C.set([...n, 0], 8 * i + 4);
                this.gridData.set([...n, s], 4 * this.blobSlot[i]);
                reach = Math.max(reach, s);
            }
            d.queue.writeBuffer(this.blobCtrl, 0, C);
            d.queue.writeBuffer(this.blobPrev, 0, cut || !this.blobWas ? C : P);
            d.queue.writeBuffer(this.grid, 0, this.gridData);
        }
        this.blobWas = blobOn;
        F[62] = reach * 1.02;
        d.queue.writeBuffer(this.frameBuffer, 0, F);
        const pieces = this.baryFor(q.pn);
        if (this.blobBindKey !== pieces.buf) {
            this.blobBind = d.createBindGroup({ layout: this.blobLayout, entries: [this.blobCtrl, this.blobTris, pieces.buf, this.blobPrev]
                .map((buffer, binding) => ({ binding, resource: { buffer } })) });
            this.blobBindKey = pieces.buf;
        }

        // ---- passes
        const enc = d.createCommandEncoder();
        let calls = 0, tris = 0;
        if (chamber)
            for (let f = 0; f < 6; f++) {
                const p = enc.beginRenderPass({ colorAttachments: [], depthStencilAttachment: { view: this.faceViews[f], depthLoadOp: 'clear', depthClearValue: 1, depthStoreOp: 'store' } });
                p.setPipeline(this.shadowPipe);
                p.setBindGroup(0, this.shadowBind);
                p.setBindGroup(1, this.faceBind, [256 * f]);
                p.setVertexBuffer(0, this.vertexBuffer);
                for (const i of live) {
                    p.draw(this.partForm[i].count, 1, this.partForm[i].first, i);
                    calls++;
                    tris += this.partForm[i].count / 3;
                }
                p.end();
            }
        const main = enc.beginRenderPass({
            colorAttachments: [
                { view: this.msColour.createView(), resolveTarget: this.hdr.createView(), loadOp: 'clear', storeOp: 'discard', clearValue: [0, 0, 0, 0] },
                { view: this.msG.createView(), resolveTarget: this.gbuf.createView(), loadOp: 'clear', storeOp: 'discard', clearValue: [0, 0, 0, 60000] },
                { view: this.msMotion.createView(), resolveTarget: this.motion.createView(), loadOp: 'clear', storeOp: 'discard', clearValue: [0, 0, 0, 0] }],
            depthStencilAttachment: { view: this.msDepth.createView(), depthLoadOp: 'clear', depthClearValue: 1, depthStoreOp: 'store' } });
        main.setBindGroup(0, this.sceneBind);
        if (live.length) {
            main.setPipeline(this.chamberPipe);
            main.setVertexBuffer(0, this.vertexBuffer);
            for (const i of live) {
                main.draw(this.partForm[i].count, 1, this.partForm[i].first, i);
                calls++;
                tris += this.partForm[i].count / 3;
            }
        }
        if (blobOn) {
            main.setPipeline(this.blobPipe);
            main.setBindGroup(1, this.blobBind);
            main.draw(this.blobTriCount * pieces.count);
            calls++;
            tris += this.blobTriCount * pieces.count / 3;
        }
        if (drops.length) {
            main.setPipeline(this.dropPipe);
            main.setVertexBuffer(0, this.dropVerts);
            main.draw(this.dropCount, drops.length);
            calls++;
            tris += drops.length * this.dropCount / 3;
        }
        main.end();
        const pp = enc.beginRenderPass({
            colorAttachments: [{ view: this.msParts.createView(), resolveTarget: this.parts.createView(), loadOp: 'clear', storeOp: 'discard', clearValue: [0, 0, 0, 0] }],
            depthStencilAttachment: { view: this.msDepth.createView(), depthReadOnly: true } });
        if (this.fx.particles && (chamber || blobOn)) {
            pp.setPipeline(this.particlePipe);
            pp.setBindGroup(0, this.sceneBind);
            pp.draw(6, q.particles);
            calls++;
            tris += 2 * q.particles;
        }
        pp.end();

        const [hw, hh] = this.half, gx = Math.ceil(hw / 8), gy = Math.ceil(hh / 8);
        const cp = enc.beginComputePass();
        cp.setBindGroup(0, this.sceneBind);
        cp.setPipeline(this.reflectPipe);
        cp.setBindGroup(1, this.rtBind);
        cp.setBindGroup(2, this.reflectBind);
        cp.dispatchWorkgroups(gx, gy);
        const gi = this.fx.gi && traceable, go = this.giOut = (this.giOut || 0) ^ 1;
        if (gi) {
            cp.setPipeline(this.giPipe);
            cp.setBindGroup(2, this.giBind);
            cp.dispatchWorkgroups(gx, gy);
            cp.setPipeline(this.giAccPipe);
            cp.setBindGroup(1, this.giAccBinds[go]);
            cp.dispatchWorkgroups(gx, gy);
        }
        this.giOk = gi;
        cp.setPipeline(this.fogPipe);
        cp.setBindGroup(1, this.emptyBind);
        cp.setBindGroup(2, this.fogBind);
        cp.dispatchWorkgroups(gx, gy);
        cp.setPipeline(this.blurPipe);
        for (const b of [...this.blurs, ...(gi ? this.giBlurs[go] : [])]) {
            cp.setBindGroup(0, b);
            cp.dispatchWorkgroups(gx, gy);
        }
        cp.end();

        const full = (view2, pipe, binds, load = 'clear') => {
            const p = enc.beginRenderPass({ colorAttachments: [view2].flat().map((view) => ({ view, loadOp: load, storeOp: 'store', clearValue: [0, 0, 0, 0] })) });
            p.setPipeline(pipe);
            binds.forEach((b, i) => p.setBindGroup(i, b));
            p.draw(3);
            p.end();
        };
        // COMPOSE into sharp (and motion2), DOF from there into scene, TAA
        // into the next history and shown, with the particles
        full([this.sharp.createView(), this.motion2.createView()], this.composePipe, [this.sceneBind, this.composeBind()]);
        full(this.scene.createView(), this.dofPipe, [this.sceneBind, this.dofBind]);
        const h = this.histIndex = (this.histIndex || 0) ^ 1;
        full([this.hist[h].createView(), this.shown.createView()], this.taaPipe, [this.sceneBind, this.taaBinds[h ^ 1]]);
        this.histOk = true;
        if (this.fx.bloom) {
            this.bloom.forEach((t, i) => full(t.createView(), this.downPipe, [this.downBinds[i]]));
            for (let i = this.bloom.length - 1; i > 0; i--)
                full(this.bloom[i - 1].createView(), this.upPipe, [this.upBinds[i]], 'load');
        }
        full(gpu.pictureView, this.finishPipe, [this.sceneBind, this.finishBind]);
        d.queue.submit([enc.finish()]);
        r.triangles = tris + r.triangles;
        this.stats = { calls, tris, traceable, rays: traceable ? hw * hh * ((this.fx.reflect ? 1 : 0) + (this.fx.gi ? q.giRays : 0)) : 0,
                       render: [this.rw, this.rh], still: this.still };
        return { ...fr, drawn: (fr.drawn || 0) + calls };
    }
}

// Floats of the uniform Frame (nextgen-shaders.js, COMMON).
const FRAME_FLOATS = 96 + 12 * SH.MAX_DROPS + 16 * 6 + 4 + 4 + 16;

// A unit sphere cut from an icosahedron `n` times: a triangle list.
function icosphere(n) {
    const t = (1 + Math.sqrt(5)) / 2;
    let v = [[-1, t, 0], [1, t, 0], [-1, -t, 0], [1, -t, 0], [0, -1, t], [0, 1, t], [0, -1, -t], [0, 1, -t], [t, 0, -1], [t, 0, 1], [-t, 0, -1], [-t, 0, 1]].map(norm);
    let f = [[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8],
             [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1]];
    for (let s = 0; s < n; s++) {
        const mid = new Map(), next = [];
        const m = (a, b) => {
            const key = a < b ? `${a}/${b}` : `${b}/${a}`;
            if (!mid.has(key)) {
                v.push(norm(v[a].map((x, i) => (x + v[b][i]) / 2)));
                mid.set(key, v.length - 1);
            }
            return mid.get(key);
        };
        for (const [a, b, c] of f) {
            const ab = m(a, b), bc = m(b, c), ca = m(c, a);
            next.push([a, ab, ca], [b, bc, ab], [c, ca, bc], [ab, bc, ca]);
        }
        f = next;
    }
    return new Float32Array(f.flatMap((tri) => tri.flatMap((i) => v[i])));
}

export { NG_KEYS, SCREEN_W };
