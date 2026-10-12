// The wasm core and its memory. The core allocates nothing (docs/core.md 2);
// this file hands it state, pack and output buffers out of the wasm memory
// and reads the structs of xboot.h by the offsets of layout.json.
//
// Used by the page and, with its own instance, by the sound worklet.

export const XB_OK = 0, XB_DONE = 1;
export const XB_START_SHORT = 1, XB_START_WIDE = 2;
export const DRAWS_MAX = 1320, PARAMS_MAX = 40960;   // XB_FRAME_*_MAX
export const AUDIO_RATE = 48000;
export const CHAMBER_GROUPS = 6, LOGO_MESHES = 7, TEXTURES = 11;
export const BLOB_VERTICES = 1734, BLOB_INDICES = 4054, BLOB_SMALL_VERTICES = 150, BLOB_SMALL_INDICES = 346;

const align = (n) => (n + 15) & ~15;                 // XB_ALIGN

// Length of step n at `hz` in whole ms, as run_step_ms of hosts/native/run.c:
// 17, 17, 16 at 60 Hz.
export function stepMs(hz, n) {
    const edge = (k) => Math.floor((k * 1000 + hz - 1) / hz);
    return edge(n + 1) - edge(n);
}

export class Core {
    static async load(wasm, pack, layout) {
        const { instance } = await WebAssembly.instantiate(wasm, {});
        return new Core(instance, pack, layout);
    }

    constructor(instance, pack, layout) {
        this.x = instance.exports;
        this.L = layout;
        this.top = align(this.x.__heap_base.value);
        this.packSize = pack.byteLength;
        this.state = this.alloc(this.x.xb_state_size());
        this.pack = this.alloc(this.packSize);
        this.bytes(this.pack, this.packSize).set(new Uint8Array(pack));
        this.cfg = this.alloc(layout.xb_config.size);
        this.sound = 0;
        this.samples = 0;
        this.sampleCap = 0;
        if (layout.xb_frame_out) {
            this.out = this.alloc(layout.xb_frame_out.size);
            this.drawsAt = this.alloc(layout.xb_draw.size * DRAWS_MAX);
            this.params = this.alloc(4 * PARAMS_MAX);
            this.tm = this.alloc(layout.xb_timing.size);
        }
    }

    // Bump allocation; nothing is ever freed. Growing the memory replaces
    // its buffer, so views are made when they are used.
    alloc(size) {
        const at = this.top;
        this.top = align(at + size);
        const have = this.x.memory.buffer.byteLength;
        if (this.top > have)
            this.x.memory.grow(Math.ceil((this.top - have) / 65536));
        return at;
    }

    get dv() {
        if (!this._dv || this._dv.buffer !== this.x.memory.buffer)
            this._dv = new DataView(this.x.memory.buffer);
        return this._dv;
    }

    bytes(at, size) {
        return new Uint8Array(this.x.memory.buffer, at, size);
    }

    init(flags = 0, startTick = 0) {
        this.dv.setUint32(this.cfg + this.L.xb_config.fields.flags, flags, true);
        this.dv.setUint32(this.cfg + this.L.xb_config.fields.start_tick, startTick, true);
        const rc = this.x.xb_init(this.state, this.pack, this.packSize, this.cfg);
        if (rc !== XB_OK)
            throw new Error(`xb_init: ${rc}`);
    }

    // Hands the core its sound memory and starts the sound from the beginning.
    initSound(sampleCap = 4800) {
        const size = this.x.xb_audio_size();
        if (!this.sound) {
            this.sound = this.alloc(size);
            this.sampleCap = sampleCap;
            this.samples = this.alloc(4 * sampleCap);
        }
        const rc = this.x.xb_audio_init(this.state, this.sound, size);
        if (rc !== XB_OK)
            throw new Error(`xb_audio_init: ${rc}`);
    }

    // `frames` stereo frames, interleaved 16 bit; a view into the wasm memory
    // that the next call overwrites.
    audio(frames) {
        if (frames > this.sampleCap)
            throw new Error('more frames than the sample buffer holds');
        const rc = this.x.xb_audio(this.state, this.samples, frames);
        if (rc !== XB_OK)
            throw new Error(`xb_audio: ${rc}`);
        return new Int16Array(this.x.memory.buffer, this.samples, 2 * frames);
    }

    advance(tick) {
        const rc = this.x.xb_advance(this.state, tick >>> 0);
        if (rc < 0)
            throw new Error(`xb_advance: ${rc}`);
        return rc;
    }

    timing() {
        const rc = this.x.xb_get_timing(this.state, this.tm);
        if (rc !== XB_OK)
            throw new Error(`xb_get_timing: ${rc}`);
        const f = this.L.xb_timing.fields, dv = this.dv, at = this.tm;
        return {
            t: dv.getFloat32(at + f.t, true), tBits: dv.getUint32(at + f.t, true),
            x18: dv.getFloat32(at + f.x18, true), x1c: dv.getFloat32(at + f.x1c, true),
            x20: dv.getFloat32(at + f.x20, true), x24: dv.getFloat32(at + f.x24, true),
            lastTick: dv.getUint32(at + f.last_tick, true),
        };
    }

    call(name, ...args) {
        const rc = this.x[name](...args);
        if (rc !== XB_OK)
            throw new Error(`${name}: ${rc}`);
    }

    f32(at, n) {
        return new Float32Array(this.x.memory.buffer, at, n).slice();
    }

    u16(at, n) {
        return new Uint16Array(this.x.memory.buffer, at, n).slice();
    }

    // Everything a renderer loads once (docs/core.md 2, "A host from start
    // to finish"), copied out of the wasm memory: per chamber group and detail
    // level the positions and indices, the blob's two meshes, the logo's
    // seven, the veil's quad and the quad for its target. The chamber's
    // attributes are (u, v,) a, b, n per vertex: 11 floats in format 1, else 9.
    meshes() {
        const L = this.L, dv = () => this.dv;
        const info = this.alloc(L.xb_mesh_info.size), mi = L.xb_mesh_info.fields;
        const chamber = [];
        for (let group = 0; group < CHAMBER_GROUPS; group++) {
            chamber.push([]);
            for (let detail = 0; detail < 2; detail++) {
                this.call('xb_chamber_mesh_info', this.state, group, detail, info);
                const nv = dv().getUint32(info + mi.vertices, true), ni = dv().getUint32(info + mi.indices, true);
                const pos = this.alloc(12 * nv), idx = this.alloc(2 * ni);
                const attr = this.alloc(4 * nv * dv().getUint32(info + mi.attr_floats, true));
                const forms = this.alloc(L.xb_form.size * dv().getUint32(info + mi.forms, true));
                this.call('xb_chamber_mesh', this.state, group, detail, pos, attr, idx, forms);
                const af = dv().getUint32(info + mi.attr_floats, true);
                const nf = dv().getUint32(info + mi.forms, true);
                const fw = new Uint32Array(this.x.memory.buffer, forms, 6 * nf).slice();
                chamber[group].push({ floats: 3, vertices: this.f32(pos, 3 * nv), indices: this.u16(idx, ni),
                                      attrFloats: af, attributes: this.f32(attr, af * nv),
                                      prim: dv().getUint32(info + mi.prim, true),
                                      forms: Array.from({ length: nf }, (_, i) => ({
                                          firstVertex: fw[6 * i], vertexCount: fw[6 * i + 1], firstIndex: fw[6 * i + 2],
                                          primCount: fw[6 * i + 3], indexCount: fw[6 * i + 4], fan: fw[6 * i + 5] })) });
            }
        }
        const blob = [[BLOB_VERTICES, BLOB_INDICES], [BLOB_SMALL_VERTICES, BLOB_SMALL_INDICES]].map(([nv, ni], small) => {
            const unit = this.alloc(12 * nv), idx = this.alloc(2 * ni);
            this.call('xb_blob_mesh', this.state, small, unit, idx);
            return { floats: 3, vertices: this.f32(unit, 3 * nv), indices: this.u16(idx, ni) };
        });
        const desc = this.alloc(L.xb_logo_mesh_desc.size), ld = L.xb_logo_mesh_desc.fields;
        const logo = [];
        for (let m = 0; m < LOGO_MESHES; m++) {
            this.call('xb_logo_mesh_info', this.state, m, desc);
            const nv = dv().getUint32(desc + ld.vertices, true), ni = dv().getUint32(desc + ld.indices, true);
            const floats = dv().getUint32(desc + ld.floats, true);
            const v = this.alloc(4 * floats * nv), idx = this.alloc(2 * ni);
            this.call('xb_logo_mesh', this.state, m, v, idx);
            logo.push({ floats, vertices: this.f32(v, floats * nv), indices: this.u16(idx, ni) });
        }
        const veil = this.alloc(4 * 7 * 4), quad = this.alloc(4 * 3 * 4);
        this.call('xb_fx_quads', veil, quad);
        return { chamber, blob: blob[0], drop: blob[1], logo, veil: { floats: 7, vertices: this.f32(veil, 28) },
                 quad: { floats: 3, vertices: this.f32(quad, 12) } };
    }

    // The eleven textures the set-up computes (Textures.md 1), each as the
    // core writes it: bytes in the layout of the guest's memory, swizzled.
    textures() {
        const desc = this.alloc(this.L.xb_texture_desc.size), f = this.L.xb_texture_desc.fields;
        const out = [];
        for (let tex = 0; tex < TEXTURES; tex++) {
            this.call('xb_texture_info', this.state, tex, desc);
            const get = (name) => this.dv.getUint32(desc + f[name], true);
            const t = { width: get('width'), height: get('height'), levels: get('levels'), faces: get('faces'),
                        format: get('format'), texelBytes: get('texel_bytes'), size: get('bytes') };
            const at = this.alloc(t.size);
            this.call('xb_texture', this.state, tex, at, t.size);
            t.bytes = this.bytes(at, t.size).slice();
            out.push(t);
        }
        return out;
    }

    // The deformed mesh of the blob as the last advance left it: four float32
    // per vertex, a view into the wasm memory that the next call overwrites.
    blobDeformed() {
        if (!this.deformed)
            this.deformed = this.alloc(16 * BLOB_VERTICES);
        this.call('xb_blob_deformed', this.state, this.deformed);
        return new Float32Array(this.x.memory.buffer, this.deformed, 4 * BLOB_VERTICES);
    }

    // Whether the last advance drew the logo into its texture (U4).
    logoDrawnToTexture() {
        if (!this.logo)
            this.logo = this.alloc(this.L.xb_logo.size);
        this.call('xb_get_logo', this.state, this.logo);
        return this.dv.getUint32(this.logo + this.L.xb_logo.fields.rtt, true) !== 0;
    }

    // The draws of the last frame() as objects, and the values they point into.
    draws(count) {
        const f = this.L.xb_draw.fields, size = this.L.xb_draw.size, dv = this.dv;
        const out = [];
        for (let i = 0; i < count; i++) {
            const at = this.drawsAt + i * size;
            const run = (k) => ({ base: dv.getUint16(at + f[`vs[${k}].base`], true),
                                  count: dv.getUint16(at + f[`vs[${k}].count`], true),
                                  first: dv.getUint32(at + f[`vs[${k}].first`], true) });
            out.push({
                step: dv.getUint16(at + f.step, true), what: dv.getUint16(at + f.what, true),
                mesh: dv.getUint16(at + f.mesh, true), part: dv.getUint16(at + f.part, true),
                prim: dv.getUint8(at + f.prim), detail: dv.getUint8(at + f.detail),
                color: dv.getUint8(at + f.color), zeta: dv.getUint8(at + f.zeta),
                tex: [0, 1, 2, 3].map((s) => dv.getUint8(at + f.tex + s)),
                flags: dv.getUint32(at + f.flags, true), first: dv.getUint32(at + f.first, true),
                count: dv.getUint32(at + f.count, true), program: dv.getUint32(at + f.program, true),
                combiners: dv.getUint32(at + f.combiners, true), factor: dv.getUint32(at + f.factor, true),
                alphaTest: dv.getUint8(at + f['state.alpha_test']), alphaFunc: dv.getUint16(at + f['state.alpha_func'], true),
                alphaRef: dv.getUint32(at + f['state.alpha_ref'], true),
                blend: dv.getUint8(at + f['state.blend']), blendSrc: dv.getUint16(at + f['state.blend_src'], true),
                blendDst: dv.getUint16(at + f['state.blend_dst'], true),
                cull: dv.getUint8(at + f['state.cull']), cullFace: dv.getUint16(at + f['state.cull_face'], true),
                shadowFunc: dv.getUint16(at + f['state.shadow_func'], true),
                colourMask: dv.getUint32(at + f['state.colour_mask'], true),
                psFirst: dv.getUint32(at + f.ps_first, true), psCount: dv.getUint32(at + f.ps_count, true),
                vtxFirst: dv.getUint32(at + f.vtx_first, true), vtxCount: dv.getUint32(at + f.vtx_count, true),
                depthTest: dv.getUint8(at + f['state.depth_test']), depthWrite: dv.getUint8(at + f['state.depth_write']),
                depthFunc: dv.getUint16(at + f['state.depth_func'], true),
                world: dv.getUint32(at + f.world, true), view: dv.getUint32(at + f.view, true),
                proj: dv.getUint32(at + f.proj, true), vs: [run(0), run(1)],
            });
        }
        return out;
    }

    get paramValues() {
        return new Float32Array(this.x.memory.buffer, this.params, PARAMS_MAX);
    }

    // Scratch memory for the workbench's queries, made on first use.
    room(name, size) {
        this.rooms ??= {};
        return this.rooms[name] ??= this.alloc(size);
    }

    vec(at, n = 3) {
        return Array.from(new Float32Array(this.x.memory.buffer, at, n));
    }

    // The camera of the path at time t (Camera.md 2 to 5).
    camera(t) {
        const f = this.L.xb_camera.fields, at = this.room('camera', this.L.xb_camera.size);
        this.call('xb_camera_at', this.state, t, at);
        const dv = this.dv;
        return { t: dv.getFloat32(at + f.t, true), s: dv.getFloat32(at + f.s, true), key: dv.getInt32(at + f.key, true),
                 path: dv.getUint32(at + f.path, true), chamberOn: dv.getUint8(at + f.chamber_on), logoOn: dv.getUint8(at + f.logo_on),
                 eye: this.vec(at + f.eye), target: this.vec(at + f.target), up: this.vec(at + f.up) };
    }

    // The track values anim_update keeps of the eye (Camera.md 6).
    track() {
        const f = this.L.xb_track.fields, at = this.room('track', this.L.xb_track.size);
        this.call('xb_get_track', this.state, at);
        const dv = this.dv;
        return { dist: dv.getFloat32(at + f.dist, true), azimuth: dv.getFloat32(at + f.azimuth, true),
                 elevation: dv.getFloat32(at + f.elevation, true), blobDist: dv.getFloat32(at + f.blob_dist, true),
                 fxDraws: dv.getUint32(at + f.fx_draws, true) };
    }

    // The blob's state without its bulges and drops (Blob.md 2).
    blob() {
        const f = this.L.xb_blob.fields, at = this.room('blob', this.L.xb_blob.size);
        this.call('xb_get_blob', this.state, at);
        const dv = this.dv;
        return { rng: dv.getUint32(at + f.rng, true), draws: dv.getUint32(at + f.draws, true),
                 bulges: dv.getUint32(at + f.n_bulges, true), drops: dv.getUint32(at + f.n_drops, true),
                 flat: dv.getUint32(at + f.flat, true), updates: dv.getUint32(at + f.updates, true),
                 center: this.vec(at + f.center), radius: dv.getFloat32(at + f.radius, true), colour: this.vec(at + f.colour, 4) };
    }

    // The logo's state (Logo.md 3.1, 4, 6).
    logoState() {
        const f = this.L.xb_logo.fields, at = this.room('logo', this.L.xb_logo.size);
        this.call('xb_get_logo', this.state, at);
        const dv = this.dv;
        return { tm: dv.getUint32(at + f.tm, true), texture: dv.getUint32(at + f.texture, true), phase: dv.getUint32(at + f.phase, true),
                 rttCount: dv.getUint32(at + f.rtt_count, true), pos: this.vec(at + f.pos), alpha: dv.getFloat32(at + f.alpha, true) };
    }

    // Part i of the chamber as the ROM places it (Chamber.md 5).
    part(i) {
        const f = this.L.xb_part.fields, at = this.room('part', this.L.xb_part.size);
        this.call('xb_chamber_part', this.state, i, at);
        const dv = this.dv;
        return { group: dv.getUint8(at + f.group), type: dv.getUint8(at + f.type), side: dv.getUint8(at + f.side),
                 index: dv.getUint16(at + f.index, true), form: dv.getInt16(at + f.form, true),
                 posTrack: dv.getInt16(at + f.pos_track, true), rotTrack: dv.getInt16(at + f.rot_track, true),
                 scale: this.vec(at + f.scale, 4) };
    }

    // The whole blob: its state with the 32 bulges and 8 drops (Blob.md 2).
    blobFull() {
        const L = this.L.xb_blob, at = this.room('blob', L.size), dv = this.dv;
        this.call('xb_get_blob', this.state, at);
        const b = this.blob();
        const f32 = (o) => dv.getFloat32(o, true), v3 = (o) => [f32(o), f32(o + 4), f32(o + 8)];
        b.bulge = Array.from({ length: b.bulges }, (_, i) => {
            const o = at + L.fields.bulge + 0x40 * i;
            return { size: f32(o), h: f32(o + 0x0C), c: v3(o + 0x10), mask: dv.getUint32(o + 0x1C, true), dir: v3(o + 0x20),
                     t0: f32(o + 0x2C), freq: f32(o + 0x30), amp: f32(o + 0x34), drop: dv.getInt32(o + 0x38, true) };
        });
        b.drop = Array.from({ length: b.drops }, (_, i) => {
            const o = at + L.fields.drop + 0x38 * i;
            return { size: f32(o), pos: v3(o + 4), dir: v3(o + 0x14), t0: f32(o + 0x20), freq: f32(o + 0x24),
                     amp: f32(o + 0x28), disp: f32(o + 0x2C), scale: f32(o + 0x30), rate: f32(o + 0x34) };
        });
        return b;
    }

    // For the workbench: the camera follows path p of the table
    // (xb_select_path, Camera.md 5.2). False when there is no such path.
    selectPath(p) {
        return this.x.xb_select_path(this.state, p >>> 0) === XB_OK;
    }

    // The chosen path's keys: time and eye of each, the table's first, then
    // the eight own ones, and the point B the target turns to (Camera.md 5).
    campath() {
        const L = this.L.xb_campath, at = this.room('campath', L.size), dv = this.dv, f = L.fields;
        this.call('xb_get_campath', this.state, at);
        const n = dv.getUint32(at + f.n_keys, true);
        return { path: dv.getUint32(at + f.path, true), nTable: dv.getUint32(at + f.n_table, true),
                 keys: Array.from({ length: n }, (_, i) => ({ t: dv.getFloat32(at + f.key + 60 * i, true), eye: this.vec(at + f.key + 60 * i + 4) })),
                 look: this.vec(at + f.look) };
    }

    // World matrix W of every part of the chamber at time t (Chamber.md 6,
    // 7): 271 times 16 values, row vectors. NaN where the ROM makes it so.
    chamberWorld(t) {
        const pose = this.room('pose', this.L.xb_chamber_pose.size), w = this.room('world', 64 * 271);
        this.call('xb_chamber_at', this.state, t, pose);
        this.call('xb_chamber_world', this.state, pose, 0, 271, w);
        return { world: this.f32(w, 16 * 271), detail: this.dv.getUint32(pose + this.L.xb_chamber_pose.fields.detail, true) };
    }

    // The next block of the sound in detail (xb_audio_block): the calls into
    // the voice layer, the gain of each channel and the loudness of its voice.
    audioBlock() {
        const L = this.L.xb_audio_out, C = this.L.xb_audio_call, at = this.room('audioblock', L.size), dv = this.dv, f = L.fields;
        this.call('xb_audio_block', this.state, at);
        const calls = [];
        for (let i = 0, n = dv.getUint32(at + f.n_calls, true); i < n; i++) {
            const o = at + f.call + C.size * i;
            calls.push({ a0: dv.getInt32(o + C.fields.a0, true), a1: dv.getInt32(o + C.fields.a1, true),
                         channel: dv.getUint8(o + C.fields.channel), fn: dv.getUint8(o + C.fields.fn), effect: dv.getUint8(o + C.fields.effect) });
        }
        const voice = new Float32Array(this.x.memory.buffer, at + f.voice, 16 * 32), gain = new Float32Array(this.x.memory.buffer, at + f.gain, 16 * 3);
        return { block: dv.getUint32(at + f.block, true), tick: dv.getUint32(at + f.tick, true), calls, voice, gain };
    }

    // The twelve pulses of the run: t0, w, a (Timing.md 4).
    pulses() {
        this.timing();
        const at = this.tm + this.L.xb_timing.fields.pulse;
        return Array.from({ length: 12 }, (_, i) => this.vec(at + 12 * i));
    }

    // The frame of the last advance as a list of draws (FrameOrder.md). With
    // `look` = { eye, target, up } the same frame from that camera
    // (xb_frame_from), for the workbench.
    frame(look) {
        const f = this.L.xb_frame_out.fields, dv = this.dv, at = this.out;
        this.bytes(at, this.L.xb_frame_out.size).fill(0);
        dv.setUint32(at + f.draws, this.drawsAt, true);
        dv.setUint32(at + f.draw_cap, DRAWS_MAX, true);
        dv.setUint32(at + f.params, this.params, true);
        dv.setUint32(at + f.param_cap, PARAMS_MAX, true);
        let rc;
        if (look) {
            const v = this.room('look', 36);
            new Float32Array(this.x.memory.buffer, v, 9).set([...look.eye, ...look.target, ...look.up]);
            rc = this.x.xb_frame_from(this.state, v, v + 12, v + 24, at);
        } else {
            rc = this.x.xb_frame(this.state, at);
        }
        if (rc !== XB_OK)
            throw new Error(`xb_frame: ${rc}`);
        return {
            view: this.f32(at + f.view, 16), proj: this.f32(at + f.proj, 16),
            drawCount: dv.getUint32(at + f.draw_count, true),
            paramCount: dv.getUint32(at + f.param_count, true),
            clearDepth: dv.getUint32(at + f.clear_depth, true),
        };
    }
}

// One run of the animation: the core and the ticks it has been given. The
// blob and the logo depend on the whole sequence of steps (docs/core.md 2),
// so going back means starting again and giving the same ticks.
export class Run {
    constructor(core, flags = 0) {
        this.core = core;
        this.flags = flags;
        this.path = null;            // the path xb_init draws, or one the workbench chose
        this.restart();
    }

    restart() {
        this.core.init(this.flags, 0);
        if (this.path !== null && !this.core.selectPath(this.path))
            throw new Error(`no camera path ${this.path}`);
        this.ticks = [];
        this.rc = XB_OK;
        this.onRestart?.(this);
    }

    get tick() {
        return this.ticks.length ? this.ticks[this.ticks.length - 1] : 0;
    }

    get done() {
        return this.rc === XB_DONE;
    }

    advance(tick) {
        if (this.done)
            return this.rc;
        this.rc = this.core.advance(tick);
        this.ticks.push(tick);
        this.onAdvance?.(this);
        return this.rc;
    }

    // The next frame of a run at `hz`, counted from the frames made so far.
    step(hz = 60) {
        return this.advance(this.tick + stepMs(hz, this.ticks.length));
    }

    replay(ticks) {
        this.restart();
        for (const tick of ticks)
            this.advance(tick);
    }

    back() {
        this.replay(this.ticks.slice(0, -1));
    }

    // The run at `hz` from the start up to its first frame with tick >= ms.
    seek(ms, hz = 60) {
        this.restart();
        while (this.tick < ms && !this.done)
            this.step(hz);
    }
}
