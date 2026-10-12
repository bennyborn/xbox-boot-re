// The renderer of the original look (W2). It draws what xb_frame lists
// (docs/core.md 2, FrameOrder.md), every draw into the target the core
// names, with the program and combiners of shaders.js, the textures of
// textures.js and the render states the core gives with the draw:
//
//   U1  the chamber into the two light views, depth alone
//   U2, U3  the disc and the chamber into the veil's target
//   U4  the logo into its texture
//   D1 to D4  chamber, blob, logo, veil and glow into the surface 1280 x 480
//   D5  the surface onto the picture 640 x 480
//
// The workbench may ask for another picture (resize, docs/web.md 11.7): the
// surface is then twice as wide as the picture and as high, so that D5 stays
// the mean of two pixels. When the picture is not 4:3, the screen and the
// veil's target see more to the sides or above and below (this.wide); the
// 640 x 480 of the original stay whole in the middle. The other targets keep
// the original's sizes and views.
//
// Not drawn: the cube of the set-up (A2). No draw of a frame reads it
// (vectors/c8_gpu.json, kinds), so the picture does not depend on it.
//
// For the workbench of W3 (docs/web.md 10) the renderer also takes another
// camera (xb_frame_from), leaves parts out, draws wireframes, finds the
// draw under a pixel, draws the camera's path and shows the targets and
// textures in place of the picture. None of it changes the picture while
// it is switched off.
import { WIDTH, HEIGHT } from './gpu.js';
import { SHADERS, SHOW, GRADE, OUTLINE, EDGE, EXTEND, VIEWS, PATH, CONSTANTS, ROOM } from './shaders.js';
import { upload } from './textures.js';
import { keyOf, PARTS, SEEN } from './bench.js';

export const SCREEN_W = 1280, SCREEN_H = 480;   // GPU.md 7
export const LOGO_SIZE = 1024;                  // Logo.md 6
const LIGHT_SIZE = 512, FX_W = 1024, FX_H = 512;   // Textures.md 1

// XB_* of xboot.h
const STEP_U1 = 0x0B01, STEP_D3 = 0x0D03;
const DRAW_PRESENT = 0, DRAW_BLOB_GLOW = 2, DRAW_LOGO_Q1 = 9, DRAW_LOGO_Q2 = 10, DRAW_LOGO_E = 11, DRAW_VEIL = 14, DRAW_GLOW = 15;
const MESH_CHAMBER = 1, MESH_BLOB = 7, MESH_DROP = 8, MESH_LOGO = 9, MESH_VEIL = 16, MESH_FX_QUAD = 17, MESH_INLINE = 18;
const SURF_TEX = 1, SURF_LIGHT0 = 12, SURF_LIGHT1 = 13, SURF_FX_A = 14, SURF_FX_B = 15, SURF_FX_DEPTH = 16,
      SURF_LOGO = 17, SURF_LOGO_TARGET = 18, SURF_LOGO_DEPTH = 19, SURF_SCREEN = 27, SURF_SCREEN_DEPTH = 28, SURF_DISPLAY = 29;
const DRAW_INDEXED = 1, DRAW_FACTOR = 2, DRAW_MATRICES = 4;

// programs of GPU.md 3 by their address in the ROM
const KIND = { 0x80065B60: 'smooth', 0x80065998: 'relief', 0x80065D28: 'depth', 0x800662E8: 'blob',
               0x80066470: 'drop', 0x80066690: 'veil', 0x80066868: 'logod' };

// GPU.md 5: what the library's combiners take, by kind of draw (shaders.js,
// "fixed"). The logo's meshes A, B, C and its picture take the texture alone,
// with the border colour of GPU.md 6.1.
const TEXTURE = 1, FACTOR = 2, TEXTURE_ALPHA = 4, BORDER = 8;
const STAGES = { [DRAW_BLOB_GLOW]: TEXTURE | FACTOR, [DRAW_GLOW]: TEXTURE | FACTOR | TEXTURE_ALPHA,
                 [DRAW_LOGO_Q1]: TEXTURE | TEXTURE_ALPHA | BORDER, [DRAW_LOGO_Q2]: TEXTURE | TEXTURE_ALPHA | BORDER };

// SET_VIEWPORT_OFFSET, measured in the stream (GPU.md 3)
const PIXEL_BIAS = 0.53125;
// SET_POLYGON_OFFSET_BIAS and _SCALE_FACTOR of the light views (GPU.md 6)
const LIGHT_BIAS = 10, LIGHT_SLOPE = 4;
// Which winding is the front (GPU.md 6): the veil's quad, clockwise on the
// screen, is drawn while SET_CULL_FACE is 0x405, "back".
const FRONT = 'cw';

// a slot per draw, and as many again for its wireframe
const SLOT = 512, SLOTS = 2 * 1400;
const AT_K = 4 * CONSTANTS, AT_BIAS = AT_K + 16, AT_MISC = AT_BIAS + 4, AT_TINT = AT_MISC + 4, AT_WIDE = AT_TINT + 8;
// what the workbench of W3b draws in its own way (docs/web.md 11)
const EFFECTS = ['veil', 'glow', 'blob_glow'], AS_IMAGE = ['logo_image', 'logo_tm'];
const ON_WALL = ['logo_a', 'logo_b', 'logo_c', 'logo_d', 'logo_image'];
const LINE_FLOATS = 7;   // x, y, z, r, g, b, a

// NV2A comparison 0x200 + n and blend factors (GPU.md 6)
const COMPARE = ['never', 'less', 'equal', 'less-equal', 'greater', 'not-equal', 'greater-equal', 'always'];
const DEPTH_EQUAL = 0x202;
const FACTORS = { 0: 'zero', 1: 'one', 0x302: 'src-alpha', 0x303: 'one-minus-src-alpha' };

// The indices of a draw as a triangle list. WebGPU has no fans, and one
// index buffer per frame is simpler than one pipeline per kind of primitive.
export function triangles(prim, idx) {
    const n = idx.length, out = [];
    if (prim === 5)
        return Array.from(idx.subarray(0, n - n % 3));
    for (let i = 2; i < n; i++) {
        if (prim === 7)
            out.push(idx[0], idx[i - 1], idx[i]);
        else if (i & 1)
            out.push(idx[i - 1], idx[i - 2], idx[i]);
        else
            out.push(idx[i - 2], idx[i - 1], idx[i]);
    }
    return out;
}

const mul4 = (a, b) => {
    const m = new Float64Array(16);
    for (let r = 0; r < 4; r++)
        for (let c = 0; c < 4; c++)
            for (let k = 0; k < 4; k++)
                m[4 * r + c] += a[4 * r + k] * b[4 * k + c];
    return m;
};

// 0x800410B0 packs a colour to bytes: clamped, trunc(f32(x * 255 + 0.5)) (GPU.md 4.2)
const packed = (x) => Math.trunc(Math.fround(Math.min(Math.max(x, 0), 1) * 255 + 0.5)) / 255;

export class Renderer {
    constructor(gpu, core) {
        const device = gpu.device;
        this.gpu = gpu;
        this.device = device;
        this.core = core;
        this.skipped = { nan: 0 };
        // what the page may switch off, for measurements (docs/web.md 9)
        this.off = new Set();
        // the workbench (docs/web.md 10): parts left out of the screen, or
        // the only ones drawn; another camera; wireframe 0 none, 1 over the
        // picture, 2 alone; the camera's path as points; what is shown
        this.hide = new Set();
        this.solo = null;
        this.look = null;
        this.wire = 0;
        this.path = null;
        this.show = 'picture';
        this.items = [];
        // W3b (docs/web.md 11), all display only: the surface's clear colour;
        // the chamber in its coarse level throughout; every part in its own
        // colour, shaded by its slope (original shaders off); parts drawn see-through;
        // coloured lines (this.lines, LINE_FLOATS each, `linesThrough` over
        // everything); the picture turned to another colour (this.grade);
        // the scene drawn in a white room (this.room, docs/web.md 11.5);
        // outlines round the draws under the pointer and those chosen
        // (this.hover, this.chosen: functions of an item { key, draw })
        this.background = null;
        this.coarse = false;
        this.flat = false;
        this.ghost = new Set();
        this.lines = null;
        this.linesThrough = false;
        this.grade = null;
        this.room = false;
        this.hover = null;
        this.chosen = null;
        this.accent = [0.55, 1, 0.35];
        this.triangles = 0;
        // for the next gen picture (nextgen.js, docs/web.md 12): the parts
        // a frame has on the screen, hidden or not, and D5 left out
        this.seen = new Set();
        this.noShow = false;

        const m = this.data = core.meshes();
        const buffer = (data) => {
            const b = device.createBuffer({ size: data.byteLength, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
            device.queue.writeBuffer(b, 0, data);
            return b;
        };
        const vb = (mesh) => ({ ...mesh, buffer: buffer(mesh.vertices), attr: mesh.attributes && buffer(mesh.attributes) });
        this.forms = m.chamber.map((levels) => levels.map((mesh) => mesh.forms));
        this.meshes = new Map();
        m.chamber.forEach((levels, g) => levels.forEach((mesh, d) => this.meshes.set(`${MESH_CHAMBER + g}/${d}`, vb(mesh))));
        this.meshes.set(`${MESH_BLOB}/0`, vb(m.blob));
        this.meshes.set(`${MESH_DROP}/0`, vb(m.drop));
        m.logo.forEach((mesh, i) => this.meshes.set(`${MESH_LOGO + i}/0`, vb(mesh)));
        this.meshes.set(`${MESH_VEIL}/0`, vb(m.veil));
        this.meshes.set(`${MESH_FX_QUAD}/0`, vb(m.quad));
        // the blob's glow quad comes with the frame: four vertices x, y, z, u, v
        this.inline = { floats: 5, buffer: device.createBuffer({ size: 80, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST }) };
        this.deformed = device.createBuffer({ size: 16 * m.blob.vertices.length / 3, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
        this.lists = new Map();

        const target = (w, h, format, more = 0) => device.createTexture({
            size: [w, h], format, usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC | GPUTextureUsage.TEXTURE_BINDING | more });
        const colour = (w, h) => target(w, h, 'rgba8unorm', GPUTextureUsage.COPY_DST);
        this.target = target;
        this.logoTarget = colour(LOGO_SIZE, LOGO_SIZE);
        this.logo = colour(LOGO_SIZE, LOGO_SIZE);
        // XB_SURF_* -> what is drawn into or read
        this.surfaces = new Map([
            [SURF_LIGHT0, target(LIGHT_SIZE, LIGHT_SIZE, 'depth16unorm')], [SURF_LIGHT1, target(LIGHT_SIZE, LIGHT_SIZE, 'depth16unorm')],
            [SURF_FX_A, colour(FX_W, FX_H)], [SURF_FX_B, colour(FX_W, FX_H)], [SURF_FX_DEPTH, target(FX_W, FX_H, 'depth32float')],
            [SURF_LOGO, this.logo], [SURF_LOGO_TARGET, this.logoTarget], [SURF_LOGO_DEPTH, target(LOGO_SIZE, LOGO_SIZE, 'depth32float')],
        ]);
        this.views = new Map([...this.surfaces].map(([id, t]) => [id, t.createView()]));
        upload(device, core.textures()).forEach((t, i) => {
            this.surfaces.set(SURF_TEX + i, t);
            this.views.set(SURF_TEX + i, t.createView({ dimension: t.depthOrArrayLayers === 6 ? 'cube' : '2d', mipLevelCount: 1 }));
        });

        this.uniforms = device.createBuffer({ size: SLOT * SLOTS, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        this.uniformData = new Float32Array(SLOT * SLOTS / 4);
        this.indices = device.createBuffer({ size: 16 << 20, usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST });
        this.drawLayout = device.createBindGroupLayout({ entries: [{
            binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
            buffer: { type: 'uniform', hasDynamicOffset: true, minBindingSize: SLOT } }] });
        this.drawBind = device.createBindGroup({ layout: this.drawLayout,
            entries: [{ binding: 0, resource: { buffer: this.uniforms, size: SLOT } }] });
        // every stage of the original filters between texels (SET_TEXTURE_FILTER 0x02022000, GPU.md 6)
        this.samplers = {
            clamped: device.createSampler({ magFilter: 'linear', minFilter: 'linear' }),
            repeated: device.createSampler({ magFilter: 'linear', minFilter: 'linear', addressModeU: 'repeat', addressModeV: 'repeat' }),
        };
        const entry = { '2d': { texture: {} }, cube: { texture: { viewDimension: 'cube' } }, light: { texture: { sampleType: 'depth' } },
                        clamped: { sampler: {} }, repeated: { sampler: {} } };
        this.kinds = {};
        for (const [name, sh] of Object.entries(SHADERS)) {
            const textures = device.createBindGroupLayout({ entries: sh.textures.map((t, binding) => ({
                binding, visibility: GPUShaderStage.FRAGMENT, ...entry[t] })) });
            this.kinds[name] = { ...sh, module: device.createShaderModule({ code: sh.code }), texLayout: textures,
                layout: device.createPipelineLayout({ bindGroupLayouts: sh.textures.length ? [this.drawLayout, textures] : [this.drawLayout] }) };
        }
        this.pipelines = new Map();
        this.binds = new Map();

        const show = device.createShaderModule({ code: SHOW });
        this.present = device.createRenderPipeline({ layout: 'auto', vertex: { module: show },
            fragment: { module: show, targets: [{ format: 'rgba8unorm' }] } });
        // the workbench's views of targets and textures, and the path
        this.viewers = Object.fromEntries(Object.entries(VIEWS).map(([name, code]) => {
            const m = device.createShaderModule({ code });
            return [name, device.createRenderPipeline({ layout: 'auto', vertex: { module: m },
                fragment: { module: m, targets: [{ format: 'rgba8unorm' }] } })];
        }));
        this.viewUniform = device.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        const pm = device.createShaderModule({ code: PATH });
        const pathGroup = device.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX, buffer: {} }] });
        const pathLayout = device.createPipelineLayout({ bindGroupLayouts: [pathGroup] });
        const lines = (depthCompare) => device.createRenderPipeline({ layout: pathLayout,
            vertex: { module: pm, buffers: [{ arrayStride: 4 * LINE_FLOATS, attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x3' },
                                                                                   { shaderLocation: 1, offset: 12, format: 'float32x4' }] }] },
            fragment: { module: pm, targets: [{ format: 'rgba8unorm', blend: { color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha' },
                                                                             alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha' } } }] },
            primitive: { topology: 'line-list' },
            depthStencil: { format: 'depth32float', depthCompare, depthWriteEnabled: false } });
        this.pathPipe = lines('less-equal');
        this.pathThrough = lines('always');
        this.pathUniform = device.createBuffer({ size: 80, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        this.pathBind = device.createBindGroup({ layout: pathGroup,
            entries: [{ binding: 0, resource: { buffer: this.pathUniform } }] });
        const gm = device.createShaderModule({ code: GRADE });
        this.gradePipe = device.createRenderPipeline({ layout: 'auto', vertex: { module: gm },
            fragment: { module: gm, targets: [{ format: 'rgba8unorm' }] } });
        this.gradeUniform = device.createBuffer({ size: 64, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        const om = device.createShaderModule({ code: OUTLINE });
        this.outlinePipe = device.createRenderPipeline({ layout: 'auto', vertex: { module: om },
            fragment: { module: om, targets: [{ format: 'rgba8unorm', blend: { color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha' },
                                                                             alpha: { srcFactor: 'one', dstFactor: 'one' } } }] } });
        this.outlineUniform = device.createBuffer({ size: 32, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        this.edgePipe = device.createComputePipeline({ layout: 'auto', compute: { module: device.createShaderModule({ code: EDGE }) } });
        const em = device.createShaderModule({ code: EXTEND });
        this.extendPipe = device.createRenderPipeline({ layout: 'auto', vertex: { module: em }, fragment: { module: em, targets: [] },
            depthStencil: { format: 'depth32float', depthCompare: 'less', depthWriteEnabled: true } });
        this.edgeOpen = device.createBuffer({ size: 4, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
        this.extendUniform = device.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        this.resize(WIDTH, HEIGHT);
        this.clearLogo();
    }

    // The largest picture of this shape whose surface the GPU can hold:
    // width x height, made smaller where need be, the shape kept.
    largest(width, height) {
        const max = this.device.limits.maxTextureDimension2D, k = Math.min(1, max / (2 * width), max / height);
        return [Math.max(1, Math.floor(width * k)), Math.max(1, Math.floor(height * k))];
    }

    // The surface, its depth and the outline's mask for a picture of
    // width x height (gpu.resize): 2 width x height, at 640 x 480 the
    // original's 1280 x 480 (GPU.md 7).
    resize(width, height) {
        const shape = width / height / (4 / 3);
        this.wide = [Math.min(1, 1 / shape), Math.min(1, shape)];
        const w = 2 * width;
        if (w > this.device.limits.maxTextureDimension2D || height > this.device.limits.maxTextureDimension2D)
            throw new Error(`a picture of ${width} x ${height} is larger than this GPU draws; see largest()`);
        if (this.screen && this.screen.width === w && this.screen.height === height)
            return;
        for (const t of [this.screen, this.mask, this.screenDepth])
            t?.destroy();
        const colour = 'rgba8unorm', more = GPUTextureUsage.COPY_DST;
        this.screen = this.target(w, height, colour, more);
        // the outline's mask: red where the pointer is, green what is chosen
        this.mask = this.target(w, height, colour, more);
        this.maskView = this.mask.createView();
        this.screenDepth = this.target(w, height, 'depth32float');
        this.surfaces.set(SURF_SCREEN, this.screen).set(SURF_SCREEN_DEPTH, this.screenDepth);
        this.views.set(SURF_SCREEN, this.screen.createView()).set(SURF_SCREEN_DEPTH, this.screenDepth.createView());
        const device = this.device, screen = this.views.get(SURF_SCREEN);
        this.presentBind = device.createBindGroup({ layout: this.present.getBindGroupLayout(0),
            entries: [{ binding: 0, resource: screen }] });
        this.gradeBind = device.createBindGroup({ layout: this.gradePipe.getBindGroupLayout(0),
            entries: [{ binding: 0, resource: screen }, { binding: 1, resource: { buffer: this.gradeUniform } }] });
        this.outlineBind = device.createBindGroup({ layout: this.outlinePipe.getBindGroupLayout(0),
            entries: [{ binding: 0, resource: this.maskView }, { binding: 1, resource: { buffer: this.outlineUniform } }] });
        // the 4:3 frame in the surface: its first and last pixel in x and y
        const fw = Math.round(w * this.wide[0]), fh = Math.round(height * this.wide[1]);
        const x0 = Math.floor((w - fw) / 2), y0 = Math.floor((height - fh) / 2);
        device.queue.writeBuffer(this.extendUniform, 0, new Float32Array([x0, y0, x0 + fw - 1, y0 + fh - 1]));
        this.edgeCount = 2 * (fw + fh);
        this.edgeBind = device.createBindGroup({ layout: this.edgePipe.getBindGroupLayout(0), entries: [
            { binding: 0, resource: this.screenDepth.createView() }, { binding: 1, resource: { buffer: this.extendUniform } },
            { binding: 2, resource: { buffer: this.edgeOpen } }] });
        this.extendBind = device.createBindGroup({ layout: this.extendPipe.getBindGroupLayout(0), entries: [
            { binding: 0, resource: { buffer: this.extendUniform } }, { binding: 1, resource: { buffer: this.edgeOpen } }] });
    }

    // Which shader a draw takes, and which stages of the draw it reads.
    kindOf(draw) {
        if (draw.step === STEP_U1)
            return ['lightdepth', []];
        const byProgram = KIND[draw.program];
        if (byProgram)
            return [byProgram, { smooth: [1, 3], relief: [0, 1, 3], blob: [0], drop: [0], veil: [0, 1, 2, 3] }[byProgram] || []];
        return draw.what === DRAW_LOGO_E ? ['plain', []] : ['fixed', [0]];
    }

    // `variant` '' is the draw as the original makes it; 'pick' the same
    // without blending, writing the number of the draw (bench.js, SEEN);
    // 'wire' its triangles' edges over the depth the picture left. For W3b
    // 'flat' is the draw without blending, 'ghost' one blended by alpha that
    // writes no depth and keeps both sides, 'mask' the draw into the
    // outline's mask where it is seen, adding its colour, 'tint' the draw
    // multiplying what is there (the white room).
    pipeline(kind, mesh, draw, variant = '') {
        const depthFormat = draw.zeta === SURF_LIGHT0 || draw.zeta === SURF_LIGHT1 ? 'depth16unorm' : 'depth32float';
        const wire = variant === 'wire', ghost = variant === 'ghost', toMask = variant === 'mask', tint = variant === 'tint';
        // without the depth test nothing is compared and nothing written
        const compare = wire || toMask ? 'less-equal' : draw.depthTest ? COMPARE[draw.depthFunc & 7] : 'always';
        const write = !wire && !ghost && !toMask && !!(draw.depthTest && draw.depthWrite);
        const blend = ghost ? '770/771' : toMask ? '1/1' : tint ? 'tint' : draw.blend && !variant ? `${draw.blendSrc}/${draw.blendDst}` : '';
        const cull = wire || ghost || !draw.cull || this.off.has('cull') ? 'none' : draw.cullFace === 0x404 ? 'front' : 'back';
        const mask = variant ? 1 : draw.colourMask;
        const stride = mesh.floats * 4;
        const key = [kind, stride, mesh.attrFloats, depthFormat, compare, write, blend, cull, mask, variant].join('/');
        let p = this.pipelines.get(key);
        if (!p) {
            const sh = this.kinds[kind];
            const at = (shaderLocation, offset, n) => ({ shaderLocation, offset, format: n === 1 ? 'float32' : `float32x${n}` });
            const buffers = [{ arrayStride: stride, attributes: [at(0, 0, 3)] }];
            if (kind === 'smooth')
                buffers.push({ arrayStride: 36, attributes: [at(1, 0, 3), at(2, 12, 3), at(3, 24, 3)] });
            if (kind === 'relief')
                buffers.push({ arrayStride: 44, attributes: [at(4, 0, 2), at(1, 8, 3), at(2, 20, 3), at(3, 32, 3)] });
            if (kind === 'blob')
                buffers.push({ arrayStride: 16, attributes: [at(1, 0, 4)] });
            if (kind === 'veil')
                buffers[0].attributes.push(at(1, 12, 2), at(2, 20, 2));
            if (kind === 'logod' || kind === 'fixed')
                buffers[0].attributes.push(at(1, 12, 2));
            const factor = (v) => {
                if (!(v in FACTORS))
                    throw new Error(`blend factor 0x${v.toString(16)}`);
                return FACTORS[v];
            };
            const component = ghost ? { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha' }
                : toMask ? { srcFactor: 'one', dstFactor: 'one' }
                : tint ? { srcFactor: 'dst', dstFactor: 'zero' }
                : blend && { srcFactor: factor(draw.blendSrc), dstFactor: factor(draw.blendDst) };
            p = this.device.createRenderPipeline({
                layout: sh.layout,
                vertex: { module: sh.module, buffers },
                fragment: sh.depthOnly ? undefined : { module: sh.module, targets: [{
                    format: 'rgba8unorm', writeMask: mask ? GPUColorWrite.ALL : 0,
                    blend: blend ? { color: component, alpha: component } : undefined }] },
                primitive: { topology: wire ? 'line-list' : 'triangle-list', cullMode: cull, frontFace: this.off.has('flip') ? 'ccw' : FRONT },
                depthStencil: { format: depthFormat, depthCompare: compare, depthWriteEnabled: write,
                                depthBias: sh.depthOnly ? LIGHT_BIAS : 0, depthBiasSlopeScale: sh.depthOnly ? LIGHT_SLOPE : 0 },
            });
            this.pipelines.set(key, p);
        }
        return p;
    }

    // The textures of a draw as a bind group, cached by what is bound.
    textures(kind, draw, stages) {
        const sh = this.kinds[kind];
        if (!sh.textures.length)
            return null;
        const ids = stages.map((s) => draw.tex[s]);
        const key = kind + '/' + ids.join('/');
        let g = this.binds.get(key);
        if (!g) {
            let next = 0;
            g = this.device.createBindGroup({ layout: sh.texLayout, entries: sh.textures.map((t, binding) => ({
                binding, resource: this.samplers[t] || this.views.get(ids[next++]) })) });
            this.binds.set(key, g);
        }
        return g;
    }

    // Triangle list of a draw, cached: [first index in this frame's buffer,
    // count]. With `edges` the three edges of each triangle as a line list.
    list(draw, mesh, frameIndices, edges = false) {
        const key = `${draw.mesh}/${draw.detail}/${draw.prim}/${draw.flags & DRAW_INDEXED}/${draw.first}/${draw.count}/${edges}`;
        let tri = this.lists.get(key);
        if (!tri) {
            const idx = draw.flags & DRAW_INDEXED
                ? mesh.indices.subarray(draw.first, draw.first + draw.count)
                : Uint16Array.from({ length: draw.count }, (_, i) => draw.first + i);
            tri = triangles(draw.prim, idx);
            if (edges)
                tri = tri.flatMap((_, i) => i % 3 ? [] : [tri[i], tri[i + 1], tri[i + 1], tri[i + 2], tri[i + 2], tri[i]]);
            this.lists.set(key, tri);
        }
        const first = frameIndices.length;
        for (const i of tri)
            frameIndices.push(i);
        return [first, tri.length];
    }

    // What the renderer needs to draw one entry of the frame, or null when
    // the entry is not drawn.
    prepare(draw, params, slot, width, height) {
        const mesh = draw.mesh === MESH_INLINE ? this.inline : this.meshes.get(`${draw.mesh}/${draw.mesh < MESH_BLOB ? draw.detail : 0}`);
        if (!mesh || (draw.what === DRAW_GLOW && this.off.has('glow')))
            return null;
        const [kind, stages] = this.kindOf(draw);
        if (draw.step === STEP_U1 && (this.off.has('cast' + draw.mesh) || this.off.has('prim' + draw.prim)))
            return null;
        if (kind === 'veil' && this.off.has('veil'))
            return null;
        const u = this.uniformData.subarray(slot * SLOT / 4, (slot + 1) * SLOT / 4);
        u.fill(0);
        if (draw.flags & DRAW_MATRICES) {
            // clip = v * world * view * proj with row vectors: row j of the
            // constants is column j of the product
            const at = (i) => params.subarray(i, i + 16);
            const m = mul4(mul4(at(draw.world), at(draw.view)), at(draw.proj));
            for (let j = 0; j < 4; j++)
                for (let i = 0; i < 4; i++)
                    u[4 * j + i] = m[4 * i + j];
        } else {
            for (const run of draw.vs)
                for (let k = 0; k < run.count && run.base + k < CONSTANTS; k++)
                    u.set(params.subarray(run.first + 4 * k, run.first + 4 * k + 4), 4 * (run.base + k));
        }
        // Parts at the ROM's quaternion 238 have NaN in their matrices until
        // t = 1.17 (Chamber.md 6). In xemu they leave no depth
        // (FrameOrder.md 4); not drawn here.
        for (let i = 0; i < 4 * CONSTANTS; i++)
            if (Number.isNaN(u[i])) {
                this.skipped.nan++;
                return null;
            }
        for (let i = 0; i < 4 * Math.min(draw.psCount, 3); i++)
            u[AT_K + i] = packed(params[draw.psFirst + i]);
        if (kind === 'fixed' || kind === 'plain') {
            const f = draw.flags & DRAW_FACTOR ? draw.factor : 0xFFFFFFFF;
            u.set([(f >>> 16 & 255) / 255, (f >>> 8 & 255) / 255, (f & 255) / 255, (f >>> 24) / 255], AT_K + 12);
            u.set([0, 0, 0, 1], AT_K + 8);     // SET_TEXTURE_BORDER_COLOR 0xFF000000 (GPU.md 2.2)
        }
        u.set([PIXEL_BIAS, this.off.has('snap') ? 1 : 0, width / 2, height / 2], AT_BIAS);
        // a picture not in 4:3 (resize): the screen and the veil's target see
        // more; the disc's and the veil's quads fill them still (shaders.js).
        // Where the veil compares depth "equal" (Effects.md 4) it falls
        // beside the 4:3 frame only if it falls on its edge (EDGE, EXTEND,
        // draw()): in 4:3 logo B covers the picture and the veil falls on no
        // pixel, and so it does beside it.
        const seen = draw.color === SURF_SCREEN || draw.color === SURF_FX_A || draw.color === SURF_FX_B;
        const [kx, ky] = seen ? this.wide : [1, 1];
        // the veil alone: the logo's picture (kind 12) lies on the same quad
        const veil = draw.mesh === MESH_VEIL && draw.what === DRAW_VEIL;
        // The glow (D4, kind 15) keeps its shape and grows alike in x and y
        // until it covers the picture as it covers the 4:3 one; else its
        // quad ended in a hard edge where the 4:3 picture ends.
        const fill = seen && (draw.mesh === MESH_FX_QUAD || veil), cover = seen && draw.what === DRAW_GLOW ? 1 / Math.min(kx, ky) : 1;
        u.set([kx, ky, fill ? 1 / kx : cover, fill ? 1 / ky : cover], AT_WIDE);
        if (draw.alphaTest && draw.alphaFunc !== 0x206)
            throw new Error(`alpha function 0x${draw.alphaFunc.toString(16)}`);
        u[AT_MISC] = draw.alphaRef;
        u[AT_MISC + 1] = STAGES[draw.what] ?? (TEXTURE | BORDER);
        u[AT_MISC + 2] = draw.alphaTest;
        u[AT_MISC + 3] = draw.shadowFunc === 0x204 && !this.off.has('shadow') ? 1 : 0;
        if (draw.mesh === MESH_INLINE)
            this.device.queue.writeBuffer(this.inline.buffer, 0, params, draw.vtxFirst, draw.vtxCount);
        return { kind, mesh, slot, draw, pipeline: this.pipeline(kind, mesh, draw), textures: this.textures(kind, draw, stages) };
    }

    pass(enc, group) {
        const colour = this.views.get(group.color), [r, g, b] = group.clearColour || [0, 0, 0];
        const p = enc.beginRenderPass({
            colorAttachments: colour ? [{ view: colour, loadOp: group.clearColour ? 'clear' : 'load', storeOp: 'store', clearValue: [r, g, b, 1] }] : [],
            depthStencilAttachment: { view: this.views.get(group.zeta), depthLoadOp: group.clearDepth ? 'clear' : 'load',
                                      depthStoreOp: 'store', depthClearValue: 1 },
        });
        p.setIndexBuffer(this.indices, 'uint32');
        for (const it of group.items) {
            p.setPipeline(it.pipeline);
            p.setBindGroup(0, this.drawBind, [it.slot * SLOT]);
            if (it.textures)
                p.setBindGroup(1, it.textures);
            p.setVertexBuffer(0, it.mesh.buffer);
            if (it.kind === 'smooth' || it.kind === 'relief')
                p.setVertexBuffer(1, it.mesh.attr);
            if (it.kind === 'blob')
                p.setVertexBuffer(1, this.deformed);
            p.drawIndexed(it.count, 1, it.first);
        }
        p.end();
    }

    clearLogo() {
        const enc = this.device.createCommandEncoder();
        for (const color of [SURF_LOGO_TARGET, SURF_LOGO])
            this.pass(enc, { color, zeta: SURF_LOGO_DEPTH, clearColour: [0, 0, 0], clearDepth: true, items: [] });
        this.device.queue.submit([enc.finish()]);
    }

    // Draws the frame of the core's last advance. With `only` = 'logo' just
    // the draw into the logo's texture, for the frames a run passes through
    // on its way to the one that is shown.
    //
    // With `pick` the draws on the screen write their number instead of
    // their colour (this.items[n - 1]), for pick() below.
    draw(only, pick = false) {
        const look = only ? null : this.look;
        const fr = this.core.frame(look || undefined);
        const draws = this.core.draws(fr.drawCount), params = this.core.paramValues;
        const items = [], wires = [];
        const size = (id) => { const t = this.surfaces.get(id); return [t.width, t.height]; };
        const indices = [], groups = [], begun = new Set();
        let slot = 0, group = null, logoDrawn = 0, cleared = !fr.clearDepth, extended = false;
        // Consecutive draws into one target are one pass. A target is
        // cleared where the frame first draws into it: the light views to
        // depth 1 (Chamber.md 8), the veil's target to 0xFFFF0000
        // (Effects.md 3), the logo's and the surface to black (Logo.md 6,
        // FrameOrder.md 1).
        const open = (color, zeta, clearDepth) => {
            const first = !begun.has(zeta);
            begun.add(zeta);
            group = { color, zeta, items: [], clearDepth: first || clearDepth,
                      clearColour: first && color ? (color === SURF_FX_A || color === SURF_FX_B ? [1, 0, 0]
                          : color === SURF_SCREEN && !only && this.roomy() ? ROOM
                          : color === SURF_SCREEN && this.background && !only ? this.background : [0, 0, 0]) : null };
            groups.push(group);
        };
        let triangles = 0;
        const marked = [], seen = new Set();
        for (const original of draws) {
            const draw = this.coarse ? this.coarser(original) : original;
            if (only ? draw.color !== SURF_LOGO_TARGET : draw.color === SURF_DISPLAY)
                continue;
            // the logo's texture belongs to the run, not to the camera that looks
            if (look && draw.color === SURF_LOGO_TARGET)
                continue;
            if (draw.what === DRAW_PRESENT || !this.views.has(draw.zeta))
                continue;
            // anim_draw clears depth before the logo when the core says so
            const clearNow = draw.color === SURF_SCREEN && draw.step === STEP_D3 && !cleared;
            // a picture not in 4:3: before the first draw that compares depth
            // "equal" EDGE and EXTEND (shaders.js)
            const extend = !extended && draw.color === SURF_SCREEN && draw.depthTest && draw.depthFunc === DEPTH_EQUAL
                && (this.wide[0] < 1 || this.wide[1] < 1);
            if (!group || group.color !== draw.color || group.zeta !== draw.zeta || clearNow || extend)
                open(draw.color, draw.zeta, clearNow);
            if (extend)
                group.extend = extended = true;
            cleared ||= clearNow;
            const key = keyOf(draw), onScreen = draw.color === SURF_SCREEN;
            if (onScreen)
                seen.add(key);
            if (onScreen && (this.solo ? !this.solo.has(key) : this.hide.has(key)))
                continue;
            if (onScreen && this.flat && !pick && EFFECTS.includes(key))
                continue;
            const it = this.prepare(draw, params, slot, ...size(draw.zeta));
            if (!it)
                continue;
            [it.first, it.count] = this.list(draw, it.mesh, indices);
            triangles += it.count / 3;
            group.items.push(it);
            logoDrawn += draw.color === SURF_LOGO_TARGET;
            if (onScreen) {
                items.push({ key, draw });
                const u = this.uniformData.subarray(slot * SLOT / 4, (slot + 1) * SLOT / 4);
                if (pick) {
                    const n = items.length, [how, limit] = SEEN[key] || [0, 0];
                    u.set([(n & 255) / 255, (n >> 8 & 255) / 255, 0, 1, 2, how, limit, 0], AT_TINT);
                    it.pipeline = this.pipeline(it.kind, it.mesh, draw, 'pick');
                } else {
                    this.restyle(it, key, u);
                    if (this.wire && !['veil', 'glow', 'blob_glow', 'logo_image'].includes(key))
                        wires.push({ it, key });
                    const item = { key, draw }, h = this.hover?.(item), c = this.chosen?.(item);
                    if (h || c)
                        marked.push({ it, colour: [h ? 1 : 0, c ? 1 : 0, 0, 1] });
                }
            }
            slot++;
        }
        // the wireframes: the same draws once more, as edges, with a colour per part
        for (const { it, key } of wires) {
            const u = this.uniformData.subarray(slot * SLOT / 4, (slot + 1) * SLOT / 4);
            u.set(this.uniformData.subarray(it.slot * SLOT / 4, (it.slot + 1) * SLOT / 4));
            u[AT_MISC + 2] = 0;
            u.set([...(PARTS[key]?.colour || [1, 1, 1]), 1, 1, 0, 0, 0], AT_TINT);
            const w = { ...it, slot, pipeline: this.pipeline(it.kind, it.mesh, it.draw, 'wire') };
            [w.first, w.count] = this.list(it.draw, it.mesh, indices, true);
            it.wire = w;
            slot++;
        }
        // the outline's mask: the marked draws once more, their colour alone
        const masks = marked.map(({ it, colour }) => {
            const u = this.uniformData.subarray(slot * SLOT / 4, (slot + 1) * SLOT / 4);
            u.set(this.uniformData.subarray(it.slot * SLOT / 4, (it.slot + 1) * SLOT / 4));
            u.set([...colour, 1, 0, 0, 0], AT_TINT);
            return { ...it, slot: slot++, pipeline: this.pipeline(it.kind, it.mesh, it.draw, 'mask') };
        });
        this.items = items;
        if (!only) {
            this.triangles = triangles;
            this.seen = seen;
        }
        if (!only && !begun.has(SURF_SCREEN_DEPTH))
            open(SURF_SCREEN, SURF_SCREEN_DEPTH, true);
        this.device.queue.writeBuffer(this.uniforms, 0, this.uniformData, 0, slot * SLOT / 4);
        this.device.queue.writeBuffer(this.indices, 0, new Uint32Array(indices));
        if (groups.some((g) => g.items.some((it) => it.kind === 'blob')))
            this.device.queue.writeBuffer(this.deformed, 0, this.core.blobDeformed());

        const enc = this.device.createCommandEncoder();
        for (const g of groups) {
            if (g.extend)
                this.extendDepth(enc);
            this.pass(enc, g);
            // logo_render_to_texture copies its target into the texture logo+0xD0 (Logo.md 6)
            if (g.color === SURF_LOGO_TARGET && g.items.length)
                enc.copyTextureToTexture({ texture: this.logoTarget }, { texture: this.logo }, [LOGO_SIZE, LOGO_SIZE]);
        }
        if (!only && !pick && (wires.length || this.wire === 2 || this.path || this.lines)) {
            const ws = wires.map((w) => w.it.wire);
            this.pass(enc, { color: SURF_SCREEN, zeta: SURF_SCREEN_DEPTH, items: ws, clearColour: this.wire === 2 ? this.background || [0, 0, 0] : null });
            if (this.path || this.lines)
                this.drawPath(enc, fr);
        }
        this.outlined = !only && !pick && masks.length > 0;
        if (this.outlined) {
            this.views.set('mask', this.maskView);
            this.pass(enc, { color: 'mask', zeta: SURF_SCREEN_DEPTH, items: masks, clearColour: [0, 0, 0] });
        }
        this.fx = draws.find((d) => d.step === 0x0B02)?.color ?? this.fx ?? SURF_FX_A;
        if (!only && !pick && !this.noShow)
            this.showOn(enc);
        this.device.queue.submit([enc.finish()]);
        return { ...fr, drawn: slot - wires.length, logoDrawn };
    }

    // EDGE and EXTEND (shaders.js) on the surface's depth as it is now.
    extendDepth(enc) {
        this.device.queue.writeBuffer(this.edgeOpen, 0, new Uint32Array([0]));
        const c = enc.beginComputePass();
        c.setPipeline(this.edgePipe);
        c.setBindGroup(0, this.edgeBind);
        c.dispatchWorkgroups(Math.ceil(this.edgeCount / 64));
        c.end();
        const p = enc.beginRenderPass({ colorAttachments: [], depthStencilAttachment: { view: this.views.get(SURF_SCREEN_DEPTH),
            depthLoadOp: 'load', depthStoreOp: 'store' } });
        p.setPipeline(this.extendPipe);
        p.setBindGroup(0, this.extendBind);
        p.draw(3);
        p.end();
    }

    // W3b (docs/web.md 11): a draw on the screen in the workbench's own way,
    // its values `u` already written. Nothing changes while all is off.
    restyle(it, key, u) {
        const ghost = this.ghost.has(key);
        let how = 0;
        if (this.flat && !AS_IMAGE.includes(key)) {
            u.set([...(PARTS[key]?.colour || [1, 1, 1]), 1], AT_TINT);
            how = 3;
        } else if (ghost) {
            how = 5;
        } else if (this.roomy()) {
            // the white room: the chamber white in green light, the light
            // of veil and glows as a tint, the logo (its meshes A to D with
            // the dark ground they bring, its picture) as on the wall, blob
            // and drops glowing
            how = it.kind === 'smooth' || it.kind === 'relief' ? 6 : EFFECTS.includes(key) ? 7 : ON_WALL.includes(key) ? 8
                : it.kind === 'blob' || it.kind === 'drop' ? 9 : 0;
        }
        if (!how)
            return;
        u.set([how, 0, 0, ghost ? 0.35 : 0], AT_TINT + 4);
        if (ghost)
            it.pipeline = this.pipeline(it.kind, it.mesh, it.draw, 'ghost');
        else if (how === 3)
            it.pipeline = this.pipeline(it.kind, it.mesh, it.draw, 'flat');
        else if (how === 7)
            it.pipeline = this.pipeline(it.kind, it.mesh, it.draw, 'tint');
    }

    // The white room only over the picture as the original draws it.
    roomy() {
        return this.room && !this.flat;
    }

    // A draw of the chamber in the fine level as the same draw in the
    // coarse one. Each form is drawn as two fans of `fan` indices and a
    // strip after them, or, without fans, whole (measured in the draw lists,
    // docs/web.md 11); the same place in the form of level 1.
    coarser(draw) {
        if (draw.what !== 1 || draw.detail !== 0 || draw.mesh < MESH_CHAMBER || draw.mesh >= MESH_BLOB)
            return draw;
        const [fine, coarse] = this.forms[draw.mesh - MESH_CHAMBER];
        const i = fine.findIndex((f) => draw.first >= f.firstIndex && draw.first < f.firstIndex + f.indexCount);
        if (i < 0)
            return draw;
        const f = fine[i], c = coarse[i], rel = draw.first - f.firstIndex;
        let first = c.firstIndex, count = c.indexCount;
        if (f.fan && rel === 0)
            count = c.fan;
        else if (f.fan && rel === f.fan)
            [first, count] = [c.firstIndex + c.fan, c.fan];
        else if (f.fan)
            [first, count] = [c.firstIndex + 2 * c.fan, c.indexCount - 2 * c.fan];
        return { ...draw, detail: 1, first, count };
    }

    // D5, or in its place what the workbench chose to show (this.show):
    // 'light0', 'light1' the light views, 'fx' the veil's target last drawn,
    // 'logo' the logo's texture, 'tex<n>' texture n of xb_texture.
    showOn(enc) {
        let pipe = this.present, bind = this.presentBind;
        if (this.show === 'picture' && this.grade) {
            pipe = this.gradePipe;
            bind = this.gradeBind;
            const r = this.grade.rot;
            this.device.queue.writeBuffer(this.gradeUniform, 0, new Float32Array([
                r[0], r[3], r[6], 0, r[1], r[4], r[7], 0, r[2], r[5], r[8], 0]));
        } else if (this.show !== 'picture') {
            const v = this.viewOf(this.show);
            pipe = this.viewers[v.kind];
            const entries = [{ binding: 0, resource: v.view }];
            this.device.queue.writeBuffer(this.viewUniform, 0, new Float32Array([v.grey ? 1 : 0, this.gpu.width, this.gpu.height, 0]));
            entries.push({ binding: 1, resource: { buffer: this.viewUniform } });
            if (v.kind !== 'depth')
                entries.push({ binding: 2, resource: this.samplers.clamped });
            bind = this.device.createBindGroup({ layout: pipe.getBindGroupLayout(0), entries });
        }
        const p = enc.beginRenderPass({ colorAttachments: [{ view: this.gpu.pictureView, loadOp: 'clear', storeOp: 'store' }] });
        p.setPipeline(pipe);
        p.setBindGroup(0, bind);
        p.draw(3);
        if (this.outlined && this.show === 'picture') {
            this.device.queue.writeBuffer(this.outlineUniform, 0, new Float32Array([...this.accent, 1, 1, 1, 1, 1]));
            p.setPipeline(this.outlinePipe);
            p.setBindGroup(0, this.outlineBind);
            p.draw(3);
        }
        p.end();
    }

    viewOf(name) {
        if (name === 'light0' || name === 'light1')
            return { kind: 'depth', view: this.views.get(name === 'light0' ? SURF_LIGHT0 : SURF_LIGHT1) };
        if (name === 'fx')
            return { kind: 'flat', view: this.views.get(this.fx) };
        if (name === 'logo')
            return { kind: 'flat', view: this.views.get(SURF_LOGO) };
        const id = SURF_TEX + Number(name.slice(3)), t = this.surfaces.get(id);
        if (t.depthOrArrayLayers === 6)
            return { kind: 'cube', view: this.views.get(id) };
        return { kind: 'flat', view: this.views.get(id), grey: t.format === 'r8unorm' };
    }

    // The camera's path (this.path, pairs of points in the world, a line
    // list, yellow) and the workbench's coloured lines (this.lines) seen with
    // the frame's view and projection.
    drawPath(enc, fr) {
        const parts = [];
        if (this.path) {
            const n = this.path.length / 3, a = new Float32Array(n * LINE_FLOATS);
            for (let i = 0; i < n; i++)
                a.set([this.path[3 * i], this.path[3 * i + 1], this.path[3 * i + 2], 1, 0.85, 0.2, 1], i * LINE_FLOATS);
            parts.push(a);
        }
        if (this.lines)
            parts.push(this.lines);
        const n = parts.reduce((k, a) => k + a.length, 0), data = new Float32Array(n);
        parts.reduce((at, a) => (data.set(a, at), at + a.length), 0);
        if (!n)
            return;
        if (!this.pathBuffer || this.pathBuffer.size < data.byteLength)
            this.pathBuffer = this.device.createBuffer({ size: data.byteLength, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
        this.device.queue.writeBuffer(this.pathBuffer, 0, data);
        const m = mul4(fr.view, fr.proj);
        for (let i = 0; i < 4; i++) {
            m[4 * i] *= this.wide[0];
            m[4 * i + 1] *= this.wide[1];
        }
        this.device.queue.writeBuffer(this.pathUniform, 0, new Float32Array([...m, 1, 0.85, 0.2, 1]));
        const p = enc.beginRenderPass({
            colorAttachments: [{ view: this.views.get(SURF_SCREEN), loadOp: 'load', storeOp: 'store' }],
            depthStencilAttachment: { view: this.views.get(SURF_SCREEN_DEPTH), depthLoadOp: 'load', depthStoreOp: 'store' },
        });
        p.setPipeline(this.linesThrough ? this.pathThrough : this.pathPipe);
        p.setBindGroup(0, this.pathBind);
        p.setVertexBuffer(0, this.pathBuffer);
        p.draw(n / LINE_FLOATS);
        p.end();
    }

    // The draw seen at pixel (x, y) of the picture (640 x 480 or as resized), the mean of
    // pixels (2x, y) and (2x + 1, y) of the surface (GPU.md 7): { key, draw }
    // of the first of the two that has one, or null. Veil and glow lie over
    // everything and count only where nothing else is drawn. Draws the
    // frame with numbers, once without veil and glow and if need be once
    // with, then as it was.
    async pick(x, y) {
        const hide = this.hide;
        this.hide = new Set([...hide, 'veil', 'glow']);
        let got = await this.pickOnce(x, y);
        this.hide = hide;
        if (!got)
            got = await this.pickOnce(x, y);
        this.draw();
        return got;
    }

    async pickOnce(x, y) {
        this.draw(undefined, true);
        const buffer = this.device.createBuffer({ size: 256, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        const enc = this.device.createCommandEncoder();
        enc.copyTextureToBuffer({ texture: this.screen, origin: [Math.min(2 * x, this.screen.width - 2), Math.min(y, this.screen.height - 1)] }, { buffer }, [2, 1]);
        this.device.queue.submit([enc.finish()]);
        await buffer.mapAsync(GPUMapMode.READ);
        const px = new Uint8Array(buffer.getMappedRange()).slice(0, 8);
        buffer.unmap();
        buffer.destroy();
        const n = (px[0] | px[1] << 8) || (px[4] | px[5] << 8);
        return n ? this.items[n - 1] : null;
    }

    async readTexture(texture, width, height, bytes) {
        const buffer = this.device.createBuffer({ size: width * height * bytes, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        const enc = this.device.createCommandEncoder();
        enc.copyTextureToBuffer({ texture }, { buffer, bytesPerRow: width * bytes }, [width, height]);
        this.device.queue.submit([enc.finish()]);
        await buffer.mapAsync(GPUMapMode.READ);
        const data = new Uint8Array(buffer.getMappedRange()).slice();
        buffer.unmap();
        buffer.destroy();
        return data;
    }

    // The drawing surface as drawn: 1280 * 480 * 4 bytes R G B A, and its
    // depth as 1280 * 480 float32, 1 where nothing was drawn (at the
    // original's size; the pictures on demand are never resized).
    readScreen() {
        return this.readTexture(this.screen, this.screen.width, this.screen.height, 4);
    }

    readDepth() {
        return this.readTexture(this.screenDepth, this.screenDepth.width, this.screenDepth.height, 4);
    }
}

export { WIDTH, HEIGHT, LINE_FLOATS };
