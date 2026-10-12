// The shaders of the "next gen" picture (docs/web.md 12). Nothing here is in
// the original: it is the host's own remaster of the same scene. What it
// takes from the core and the specification is named in nextgen.js.
//
// Conventions: matrices come from JavaScript as 16 floats. The core's
// matrices (W of a part, view, projection) are row-vector matrices; read as
// a WGSL mat4x4f, M * v is then v . M, so they are used as they are. The
// matrices of the light's six faces are made column-major in nextgen.js.

export const MAX_DROPS = 8;

// group 0, shared by every pass that sees the scene
export const COMMON = `
struct Frame {
    vp: mat4x4f,         // world -> clip of the screen, the picture's shape in x and y
    inv_vp: mat4x4f,
    eye: vec4f,          // xyz the eye, w t
    light: vec4f,        // xyz the light (the blob's centre), w the blob's radius k
    lcol: vec4f,         // rgb the light's colour times m = 2 x20, w m
    atten: vec4f,        // the attenuation 1, a1, a2; w the fog's density
    blob: vec4f,         // rgb the blob's colour c0, w 1 while the blob is drawn
    size: vec4f,         // width, height, 1 / width, 1 / height of the render
    flags: vec4f,        // x reflections, y god rays, z particles, w exposure
    pulse: vec4f,        // x20, x1c, the flash, number of drops
    right: vec4f,        // the camera's axes, for particles; w the picture's width
    up: vec4f,           // w the picture's height
    shadow: vec4f,       // near, far, size of a face, 1 while the chamber is drawn
    misc: vec4f,         // x bloom, y the legacy surface's scale, z particles' count, w fog steps
    grade0: vec4f, grade1: vec4f, grade2: vec4f,   // the playground's turn; grade0.w 1 while the bounce's history is valid
    lens: vec4f,         // depth of field: focus distance, strength (0 off), largest radius in pixels
    drops: array<vec4f, ${3 * MAX_DROPS}>,   // per drop: (pos, prev x), (dir, prev y), (s, z, 1, prev z)
    faces: array<mat4x4f, 6>,
    taa: vec4f,          // the jitter of this frame in clip units, the frame's number, 1 with the bounce (GI)
    taa2: vec4f,         // how much of this frame enters the history, 1 while it is valid, the same where the original draws;
                         // w the bounce's rays a pixel
    prev_vp: mat4x4f,    // vp of the frame before, without jitter
}
struct Inst { w: mat4x4f, inv: mat4x4f, root: u32, on: u32, look: f32, rough: f32 }
@group(0) @binding(0) var<uniform> F: Frame;
@group(0) @binding(1) var<storage, read> inst: array<Inst>;
@group(0) @binding(7) var<storage, read> prev_inst: array<Inst>;
@group(0) @binding(2) var smap: texture_depth_2d_array;
@group(0) @binding(3) var cmp: sampler_comparison;
@group(0) @binding(4) var lin: sampler;
@group(0) @binding(5) var relief: texture_2d<f32>;
@group(0) @binding(6) var rep: sampler;

const PI = 3.14159265;

// light as an HDR target can hold it: the targets are 16-bit floats, where
// anything above 65504 becomes infinite and TAA's tone map makes NaN of it,
// which the history then carries along; NaN itself becomes black
fn safe(c: vec3f) -> vec3f { return select(clamp(c, vec3f(0), vec3f(3e4)), vec3f(0), c != c); }
fn ign(p: vec2f) -> f32 { return fract(52.9829189 * fract(dot(p, vec2f(0.06711056, 0.00583715)))); }
fn hash(n: u32) -> f32 {
    var x = n * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    x = (x >> 22u) ^ x;
    return f32(x) / 4294967295.0;
}
fn h3(p: vec3f) -> f32 { return fract(sin(dot(p, vec3f(127.1, 311.7, 74.7))) * 43758.5453); }
fn noise3(p: vec3f) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u = f * f * (3 - 2 * f);
    return mix(mix(mix(h3(i), h3(i + vec3f(1, 0, 0)), u.x), mix(h3(i + vec3f(0, 1, 0)), h3(i + vec3f(1, 1, 0)), u.x), u.y),
               mix(mix(h3(i + vec3f(0, 0, 1)), h3(i + vec3f(1, 0, 1)), u.x), mix(h3(i + vec3f(0, 1, 1)), h3(i + vec3f(1, 1, 1)), u.x), u.y), u.z);
}
fn fbm(p: vec3f) -> f32 { return 0.55 * noise3(p) + 0.3 * noise3(p * 2.03 + 7.1) + 0.15 * noise3(p * 4.1 + 3.3); }
// noise that changes from frame to frame, for TAA to average
fn ign_t(p: vec2f) -> f32 { return ign(p + vec2f(5.588238, 3.142) * f32(u32(F.taa.z) % 64u)); }
fn rand2(p: vec2u) -> vec2f {
    let s = p.x * 1973u + p.y * 9277u + u32(F.taa.z) * 26699u;
    return vec2f(hash(s), hash(s ^ 0x9E3779B9u));
}
// the place without this frame's jitter; and how far a point moved on the
// screen since the frame before, in parts of the picture (velocity)
fn unjitter(c: vec4f) -> vec4f { return vec4f(c.xy - F.taa.xy * c.w, c.zw); }
fn velocity(cur: vec4f, prev: vec4f) -> vec2f {
    if (prev.w <= 1e-6 || cur.w <= 1e-6) { return vec2f(0); }
    return (cur.xy / cur.w - prev.xy / prev.w) * vec2f(0.5, -0.5);
}
// The blob's light at p with normal N seen from E: c0 of Blob.md 7.2,
// brighter inside, with a plasma of noise.
fn blob_glow(p: vec3f, N: vec3f, E: vec3f) -> vec3f {
    let c = clamp(abs(dot(N, E)), 0, 1);
    let r = (1 - c) * (1 - c);
    let t = F.eye.w;
    let q = (p - F.light.xyz) / max(F.light.w, 0.5);
    let plasma = fbm(q * 1.7 + vec3f(0, 0, t * 0.9)) * 0.9 + fbm(q * 4.0 - vec3f(t * 0.6, 0, 0)) * 0.35;
    let core = F.blob.rgb * (1 - r) * (0.55 + plasma);
    let rim = F.blob.rgb * pow(1 - c, 3) * 0.6;
    let hot = vec3f(1, 1, 0.85) * pow(max(1 - r, 0), 6) * length(F.blob.rgb) * 0.25;
    return (core + rim + hot) * 0.55;
}

// the light's radiance arriving at distance d (Chamber.md 7: 1 / (1 + a1 d + a2 d^2))
fn radiance(d: f32) -> vec3f { return F.lcol.rgb / dot(F.atten.xyz, vec3f(1, d, d * d)); }

// the ray through pixel uv (0 .. 1, y down) from the eye
fn ray_dir(uv: vec2f) -> vec3f {
    let far = F.inv_vp * vec4f(uv.x * 2 - 1, 1 - uv.y * 2, 1, 1);
    return normalize(far.xyz / far.w - F.eye.xyz);
}

// The six faces of the light: the one whose axis is largest, as nextgen.js
// makes them (+x, -x, +y, -y, +z, -z).
fn face_of(v: vec3f) -> u32 {
    let a = abs(v);
    if (a.x >= a.y && a.x >= a.z) { return select(1u, 0u, v.x > 0); }
    if (a.y >= a.z) { return select(3u, 2u, v.y > 0); }
    return select(5u, 4u, v.z > 0);
}
const DISC = array<vec2f, 12>(vec2f(-0.326, -0.406), vec2f(-0.840, -0.074), vec2f(-0.696, 0.457), vec2f(-0.203, 0.621),
    vec2f(0.962, -0.195), vec2f(0.473, -0.480), vec2f(0.519, 0.767), vec2f(0.185, -0.893), vec2f(0.507, 0.064),
    vec2f(0.896, 0.412), vec2f(-0.322, -0.933), vec2f(-0.792, -0.598));
// How much of the light reaches p (0 .. 1). soft > 0 filters over the
// penumbra of a light as large as the blob, about k / d wide; seed turns
// the disc per pixel.
fn shadow_at(p: vec3f, n: vec3f, soft: f32, seed: f32) -> f32 {
    if (F.shadow.w == 0) { return 1; }
    let d = distance(p, F.light.xyz);
    let q = p + n * (0.03 + 0.006 * d);
    let f = face_of(q - F.light.xyz);
    let c = F.faces[f] * vec4f(q, 1);
    let uv = c.xy / c.w * vec2f(0.5, -0.5) + 0.5;
    let z = c.z / c.w;
    if (soft == 0) { return textureSampleCompareLevel(smap, cmp, uv, f, z); }
    let r = clamp(soft * F.light.w / max(d, 1.0) * 0.35, 1.5 / F.shadow.z, 0.04);
    let a = seed * 6.2831853;
    let rot = mat2x2f(cos(a), sin(a), -sin(a), cos(a));
    var s = 0.0;
    for (var i = 0; i < 12; i++) { s += textureSampleCompareLevel(smap, cmp, uv + rot * DISC[i] * r, f, z); }
    return s / 12;
}

// GGX with the light widened to the blob's size (a sphere light)
fn shade(p: vec3f, N: vec3f, E: vec3f, albedo: vec3f, metal: f32, rough_in: f32, soft: f32, seed: f32) -> vec3f {
    let lv = F.light.xyz - p;
    let d = length(lv);
    let L = lv / max(d, 1e-4);
    let rad = radiance(d);
    let nl = max(dot(N, L), 0);
    let nv = max(dot(N, E), 1e-3);
    // what lights the chamber besides the light: its own walls, roughly;
    // with the traced bounce (GI) only what that leaves out, more bounces
    let bounce = albedo * rad * (0.035 + 0.025 * max(dot(N, -normalize(p)), 0)) * (1 - 0.85 * F.taa.w);
    if (nl <= 0) { return bounce; }
    let sh = shadow_at(p, N, soft, seed);
    let rough = clamp(rough_in + F.light.w / (2 * max(d, 1.0)), 0.05, 1);
    let a2 = pow(rough, 4);
    let H = normalize(L + E);
    let nh = max(dot(N, H), 0);
    let D = a2 / (PI * pow(nh * nh * (a2 - 1) + 1, 2));
    let k = (rough + 1) * (rough + 1) / 8;
    let G = nl / (nl * (1 - k) + k) * nv / (nv * (1 - k) + k);
    let F0 = mix(vec3f(0.04), albedo, metal);
    let Fr = F0 + (1 - F0) * pow(1 - max(dot(H, E), 0), 5);
    // bounded: at grazing angles and near the light it would outgrow 16-bit floats
    let spec = min(D * G / (4 * nl * nv), 200.0) * Fr;
    let diff = (1 - Fr) * (1 - metal) * albedo / PI;
    return (diff + spec) * rad * nl * sh * PI + bounce;
}
`;

// ---- the light's six faces: depth alone ---------------------------------------
export const SHADOW = COMMON + `
@group(1) @binding(0) var<uniform> face: vec4u;
@vertex fn vs(@location(0) p: vec3f, @builtin(instance_index) i: u32) -> @builtin(position) vec4f {
    let w = inst[i].w * vec4f(p, 1);
    return F.faces[face.x] * w;
}`;

// ---- the chamber, blob, drops and particles into the HDR target ------------------
// colour and reflectivity; normal and distance; velocity, albedo, 1 + roughness (> 0.5: drawn here)
const OUT = `
struct Out { @location(0) c: vec4f, @location(1) g: vec4f, @location(2) m: vec4f }
`;

export const CHAMBER = COMMON + OUT + `
struct V {
    @builtin(position) p: vec4f, @location(0) w: vec3f, @location(1) n: vec3f, @location(2) a: vec3f,
    @location(3) b: vec3f, @location(4) uv: vec3f, @location(5) @interpolate(flat) part: u32,
    @location(6) cur: vec4f, @location(7) prev: vec4f,
}
@vertex fn vs(@location(0) p: vec3f, @location(1) a: vec3f, @location(2) b: vec3f, @location(3) n: vec3f,
              @location(4) uv: vec3f, @builtin(instance_index) i: u32) -> V {
    let I = inst[i];
    let w = (I.w * vec4f(p, 1)).xyz;
    let m = mat3x3f(I.inv[0].xyz, I.inv[1].xyz, I.inv[2].xyz);
    let clip = F.vp * vec4f(w, 1);
    // where the part stood in the frame before, with its W then
    let P = prev_inst[i];
    var prev = vec4f(0);
    if (P.on != 0u) { prev = F.prev_vp * (P.w * vec4f(p, 1)); }
    return V(clip, w, transpose(m) * n, (I.w * vec4f(a, 0)).xyz, (I.w * vec4f(b, 0)).xyz, uv, i, unjitter(clip), prev);
}
@fragment fn fs(v: V, @builtin(front_facing) front: bool) -> Out {
    var N = normalize(v.n);
    let E = normalize(F.eye.xyz - v.w);
    let I = inst[v.part];
    var rough = I.rough;
    // the relief (GPU.md 3.2) along the part's axes a, b; its levels
    // averaged, so that a short normal widens the highlight (Toksvig)
    let t = textureSample(relief, rep, v.uv.xy).xyz * 2 - 1;
    if (v.uv.z > 0.5) {
        let len = max(length(t), 1e-3);
        let ft = len / (len + 1.0 * (1 - len) + 1e-4);
        rough = clamp(sqrt(rough * rough + (1 - ft) / max(ft, 1e-3) * 0.12), rough, 1);
        // where a or b lies along the normal there is no plane for the relief
        let Ta = v.a - N * dot(v.a, N);
        let Bb = v.b - N * dot(v.b, N);
        if (dot(Ta, Ta) > 1e-10 && dot(Bb, Bb) > 1e-10) {
            let n2 = Ta * (t.x / length(Ta)) + Bb * (t.y / length(Bb)) + N * t.z * 1.6;
            if (dot(n2, n2) > 1e-10) { N = normalize(n2); }
        }
    }
    if (any(N != N)) { N = vec3f(0, 0, 1); }
    if (dot(N, E) < 0 && !front) { N = -N; }
    let albedo = vec3f(I.look);
    let seed = ign_t(v.p.xy);
    let lit = shade(v.w, N, E, albedo, 0.75, rough, 1, seed);
    let F0 = 0.04 + 0.71 * I.look;
    let fr = (F0 + (1 - F0) * pow(1 - max(dot(N, E), 0), 5)) * (1 - rough * 0.85);
    return Out(vec4f(safe(lit), fr), vec4f(N, distance(v.w, F.eye.xyz)), vec4f(velocity(v.cur, v.prev), I.look, 1 + rough));
}`;

// The blob: its deformed mesh (Blob.md 6.4) as curved PN triangles, cut
// into n * n triangles each in the vertex shader. Its glow from the
// colour c0 of Blob.md 7.2, brighter inside, with a plasma of noise.
export const BLOB = COMMON + OUT + `
@group(1) @binding(0) var<storage, read> ctrl: array<vec4f>;     // position, normal per vertex
@group(1) @binding(1) var<storage, read> tris: array<u32>;
@group(1) @binding(2) var<storage, read> bary: array<vec2f>;
@group(1) @binding(3) var<storage, read> prev_ctrl: array<vec4f>; // the same, a frame before
struct V { @builtin(position) p: vec4f, @location(0) w: vec3f, @location(1) n: vec3f, @location(2) cur: vec4f, @location(3) prev: vec4f }
// the point (u, v) of the curved triangle over P1, P2, P3 with normals N1, N2, N3
fn pn(P1: vec3f, P2: vec3f, P3: vec3f, N1: vec3f, N2: vec3f, N3: vec3f, u: f32, v: f32) -> vec3f {
    let w = 1 - u - v;
    let b210 = (2 * P1 + P2 - dot(P2 - P1, N1) * N1) / 3;
    let b120 = (2 * P2 + P1 - dot(P1 - P2, N2) * N2) / 3;
    let b021 = (2 * P2 + P3 - dot(P3 - P2, N2) * N2) / 3;
    let b012 = (2 * P3 + P2 - dot(P2 - P3, N3) * N3) / 3;
    let b102 = (2 * P3 + P1 - dot(P1 - P3, N3) * N3) / 3;
    let b201 = (2 * P1 + P3 - dot(P3 - P1, N1) * N1) / 3;
    let E = (b210 + b120 + b021 + b012 + b102 + b201) / 6;
    let b111 = E + (E - (P1 + P2 + P3) / 3) / 2;
    return P1 * w * w * w + P2 * u * u * u + P3 * v * v * v
          + 3 * (b210 * w * w * u + b120 * w * u * u + b201 * w * w * v + b021 * u * u * v + b102 * w * v * v + b012 * u * v * v)
          + 6 * b111 * w * u * v;
}
@vertex fn vs(@builtin(vertex_index) vi: u32) -> V {
    let per = arrayLength(&bary);
    let t = vi / per;
    let b = bary[vi % per];
    let i0 = tris[3 * t]; let i1 = tris[3 * t + 1]; let i2 = tris[3 * t + 2];
    let p = pn(ctrl[2 * i0].xyz, ctrl[2 * i1].xyz, ctrl[2 * i2].xyz, ctrl[2 * i0 + 1].xyz, ctrl[2 * i1 + 1].xyz, ctrl[2 * i2 + 1].xyz, b.x, b.y);
    let q = pn(prev_ctrl[2 * i0].xyz, prev_ctrl[2 * i1].xyz, prev_ctrl[2 * i2].xyz,
               prev_ctrl[2 * i0 + 1].xyz, prev_ctrl[2 * i1 + 1].xyz, prev_ctrl[2 * i2 + 1].xyz, b.x, b.y);
    let n = normalize((1 - b.x - b.y) * ctrl[2 * i0 + 1].xyz + b.x * ctrl[2 * i1 + 1].xyz + b.y * ctrl[2 * i2 + 1].xyz);
    let clip = F.vp * vec4f(p, 1);
    // a control point with w = 0: no blob in the frame before
    let prev = select(vec4f(0), F.prev_vp * vec4f(q, 1), prev_ctrl[2 * i0].w > 0);
    return V(clip, p, n, unjitter(clip), prev);
}
@fragment fn fs(v: V) -> Out {
    let N = normalize(v.n);
    let E = normalize(F.eye.xyz - v.w);
    return Out(vec4f(safe(blob_glow(v.w, N, E)), 0), vec4f(N, distance(v.w, F.eye.xyz)), vec4f(velocity(v.cur, v.prev), 0, 1));
}`;

// The drops (Blob.md 7.3): spheres of radius s stretched by z along their
// direction, as a finely cut sphere instead of the original's n = 8.
export const DROP = COMMON + OUT + `
struct V { @builtin(position) p: vec4f, @location(0) w: vec3f, @location(1) n: vec3f, @location(2) @interpolate(flat) i: u32,
           @location(3) cur: vec4f, @location(4) prev: vec4f }
@vertex fn vs(@location(0) p: vec3f, @builtin(instance_index) i: u32) -> V {
    let at = F.drops[3 * i].xyz;
    let dir = F.drops[3 * i + 1].xyz;
    let s = F.drops[3 * i + 2].x;
    let z = F.drops[3 * i + 2].y;
    let along = dot(p, dir);
    let w = at + s * p + along * z * dir;
    let n = normalize(p / s - dir * along * z / (s * (s + z)));
    let clip = F.vp * vec4f(w, 1);
    // moved by as much as its centre since the frame before
    let was = vec3f(F.drops[3 * i].w, F.drops[3 * i + 1].w, F.drops[3 * i + 2].w);
    return V(clip, w, n, i, unjitter(clip), F.prev_vp * vec4f(w - at + was, 1));
}
@fragment fn fs(v: V) -> Out {
    let N = normalize(v.n);
    let E = normalize(F.eye.xyz - v.w);
    let c = clamp(abs(dot(N, E)), 0, 1);
    let r = (1 - c) * (1 - c);
    let k = F.drops[3 * v.i + 2].z;
    let col = F.blob.rgb / max(length(F.blob.rgb), 1e-3) * (F.pulse.x * 0.3 * (1 - r) + 0.2 + pow(1 - c, 3) * 0.5) * k;
    return Out(vec4f(safe(col * 3.0), 0), vec4f(N, distance(v.w, F.eye.xyz)), vec4f(velocity(v.cur, v.prev), 0, 1));
}`;

// Particles without state: each a function of t and its number, so that a
// run can be cut anywhere. Sparks round the blob, dust in the chamber lit
// by the light and the god rays, and a burst at the flash (Effects.md 4).
// They are drawn into a target of their own, added after DOF and TAA, and
// blurred by themselves: each sprite grows by its circle of confusion into
// a soft disc, its light spread over the larger area.
export const PARTICLES = COMMON + `
struct V { @builtin(position) p: vec4f, @location(0) c: vec3f, @location(1) q: vec2f, @location(2) blur: f32 }
fn rot(v: vec3f, axis: vec3f, a: f32) -> vec3f { return v * cos(a) + cross(axis, v) * sin(a) + axis * dot(axis, v) * (1 - cos(a)); }
fn sphere(i: u32) -> vec3f {
    let z = hash(i) * 2 - 1;
    let a = hash(i + 7919u) * 6.2831853;
    let r = sqrt(max(1 - z * z, 0));
    return vec3f(r * cos(a), r * sin(a), z);
}
@vertex fn vs(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> V {
    let corners = array<vec2f, 6>(vec2f(-1, -1), vec2f(1, -1), vec2f(1, 1), vec2f(-1, -1), vec2f(1, 1), vec2f(-1, 1));
    let q = corners[vi];
    let t = F.eye.w;
    let n = u32(F.misc.z);
    let kind = ii % 3u;
    let seed = ii * 9781u + 17u;
    let L = F.light.xyz;
    let k = F.light.w;
    let tint = F.blob.rgb / max(max(F.blob.r, max(F.blob.g, F.blob.b)), 1e-3);
    var at = vec3f(0);
    var size = 0.0;
    var col = vec3f(0);
    if (kind == 0u) {
        // sparks: born on the blob, carried out on a turning path
        let life = 1.6 + hash(seed + 3u) * 2.4;
        let age = fract(t / life + hash(seed + 5u));
        let axis = normalize(sphere(seed + 11u));
        let d = rot(sphere(seed), axis, age * (1.5 + hash(seed + 13u) * 2.5));
        at = L + d * (k * 1.02 + age * age * (8 + 22 * hash(seed + 17u)));
        at += 0.6 * vec3f(sin(t * 1.3 + f32(ii)), cos(t * 1.1 + f32(ii) * 0.7), sin(t * 0.9 + f32(ii) * 1.3)) * age;
        let fade = sin(age * PI);
        size = (0.02 + 0.05 * hash(seed + 19u)) * (1.2 - age * 0.6);
        col = mix(vec3f(1, 1, 0.9), tint, age) * fade * fade * length(F.blob.rgb) * 0.35 * F.blob.w;
    } else if (kind == 1u) {
        // dust: drifting in the chamber, seen where the light falls on it
        let base = sphere(seed) * pow(hash(seed + 23u), 0.45) * 90.0;
        at = base + 1.5 * vec3f(sin(t * 0.21 + f32(ii)), sin(t * 0.17 + f32(ii) * 1.7), sin(t * 0.13 + f32(ii) * 2.3));
        let d = distance(at, L);
        let vis = shadow_at(at, vec3f(0), 0, 0);
        let twinkle = 0.6 + 0.4 * sin(t * (2 + 3 * hash(seed + 29u)) + f32(ii));
        size = 0.06 + 0.06 * hash(seed + 31u);
        col = radiance(d) * vis * twinkle * 0.35 * F.shadow.w;
    } else {
        // the burst of the flash: a shell that keeps growing faster while the
        // camera pulls back out of the chamber (from t = 5.3, 65 to 490 from
        // the blob by 6.0), so that it overtakes the camera; gone with the
        // chamber (Camera.md 5.5, t = 5.936)
        let age = t - 5.0 - hash(seed + 37u) * 0.12;
        if (age > 0 && t < 5.936) {
            let speed = 18 + 50 * hash(seed + 41u);
            at = L + sphere(seed) * (k + speed * (1.2 * age + 16 * age * age * age));
            size = 0.08 + 0.18 * hash(seed + 43u);
            let fade = 1 - smoothstep(5.82, 5.93, t);
            col = mix(vec3f(1, 1, 0.92), tint, clamp(age * 1.4, 0, 1)) * (0.3 + 0.7 * exp(-age * 3)) * 8 * fade;
        }
    }
    if (size == 0 || dot(col, col) < 1e-8 || F.flags.z == 0) {
        return V(vec4f(0, 0, -2, 1), vec3f(0), q, 0);
    }
    // the sprite's radius on the screen, and its circle of confusion as in COMPOSE
    // added after TAA: without this frame's jitter
    let c = unjitter(F.vp * vec4f(at, 1));
    // at or behind the eye a sprite has no size on the screen; near it, it fades
    if (c.w < 0.5) {
        return V(vec4f(0, 0, -2, 1), vec3f(0), q, 0);
    }
    col *= smoothstep(0.5, 4.0, c.w);
    let e = unjitter(F.vp * vec4f(at + F.up.xyz * size, 1));
    let px = clamp(length((e.xy / e.w - c.xy / c.w) * F.size.xy * 0.5), 0.5, F.size.y * 0.03);
    let d = distance(at, F.eye.xyz);
    let coc = min(F.lens.y * abs(1 - F.lens.x / max(d, 0.1)) * F.size.y / 1080 * 22, F.lens.z);
    let grown = px + coc;
    var p = c;
    p = vec4f(p.xy + q * grown * 2 / F.size.xy * p.w, p.zw);
    return V(p, col * (px * px) / (grown * grown), q, coc / grown);
}
@fragment fn fs(v: V) -> @location(0) vec4f {
    let r = dot(v.q, v.q);
    if (r > 1) { discard; }
    // sharp: a soft point; blurred: a disc with a soft edge, as a lens makes it
    let point = exp(-r * 4.5) * 1.3;
    let disc = (1 - smoothstep(0.7, 1.0, r)) * (0.85 + 0.15 * r) * 0.75;
    return vec4f(v.c * mix(point, disc, clamp(v.blur * 1.5, 0, 1)), 0);
}`;

// ---- ray tracing: a BVH of two levels over all parts of the chamber -------------
const TRACE = `
struct Node { lo: vec3f, a: u32, hi: vec3f, n: u32 }
struct Tri { p0: vec4f, p1: vec4f, p2: vec4f }
@group(1) @binding(0) var<storage, read> blas: array<Node>;
@group(1) @binding(1) var<storage, read> tlas: array<Node>;
@group(1) @binding(2) var<storage, read> tris: array<Tri>;
@group(1) @binding(3) var<storage, read> tnorm: array<Tri>;
@group(1) @binding(4) var<storage, read> torder: array<u32>;
// the deformed blob as a table over the faces of a cube, 17 x 17 a face:
// its normal and its distance from the centre (nextgen.js, blobGrid)
@group(1) @binding(5) var<storage, read> grid: array<vec4f>;
struct Hit { t: f32, kind: u32, part: u32, tri: u32, u: f32, v: f32 }

// The table at direction d: the face by the largest axis, as the vertices
// lie on it, and between the four nearest points.
fn blob_at(d: vec3f) -> vec4f {
    let a = abs(d);
    var f = 0u;
    var st = vec2f(0);
    if (a.x >= a.y && a.x >= a.z) { f = select(1u, 0u, d.x > 0); st = d.yz / a.x; }
    else if (a.y >= a.z) { f = select(3u, 2u, d.y > 0); st = d.xz / a.y; }
    else { f = select(5u, 4u, d.z > 0); st = d.xy / a.z; }
    let g = clamp((st + 1) * 8, vec2f(0), vec2f(16));
    let i = min(vec2u(floor(g)), vec2u(15));
    let w = g - vec2f(i);
    let at = f * 289u + i.y * 17u + i.x;
    return mix(mix(grid[at], grid[at + 1u], w.x), mix(grid[at + 17u], grid[at + 18u], w.x), w.y);
}
// The ray against the blob: through its bounding sphere in steps, where the
// ray first goes inside the surface, then halved down to the crossing.
fn blob_hit(o: vec3f, d: vec3f, tmax: f32) -> f32 {
    let c = F.light.xyz;
    let oc = o - c;
    let b = dot(oc, d);
    let r = F.pulse.z;
    let h = b * b - dot(oc, oc) + r * r;
    if (h < 0) { return 1e30; }
    let t0 = max(-b - sqrt(h), 1e-3);
    let t1 = min(-b + sqrt(h), tmax);
    if (t1 <= t0) { return 1e30; }
    let n = 40;
    let dt = (t1 - t0) / f32(n);
    var lo = t0;
    var hi = -1.0;
    for (var i = 1; i <= n; i++) {
        let t = t0 + dt * f32(i);
        let p = o + d * t - c;
        if (length(p) < blob_at(normalize(p)).w) { hi = t; lo = t - dt; break; }
    }
    if (hi < 0) { return 1e30; }
    for (var i = 0; i < 6; i++) {
        let t = (lo + hi) * 0.5;
        let p = o + d * t - c;
        if (length(p) < blob_at(normalize(p)).w) { hi = t; } else { lo = t; }
    }
    return hi;
}

fn box(o: vec3f, inv: vec3f, lo: vec3f, hi: vec3f, tmax: f32) -> f32 {
    let t1 = (lo - o) * inv;
    let t2 = (hi - o) * inv;
    let a = min(t1, t2);
    let b = max(t1, t2);
    let tn = max(max(a.x, a.y), max(a.z, 0.0));
    let tf = min(min(b.x, b.y), b.z);
    return select(1e30, tn, tn <= tf && tn < tmax);
}
fn safe_inv(d: vec3f) -> vec3f { return 1 / select(d, vec3f(1e-12), abs(d) < vec3f(1e-12)); }
fn sphere_hit(o: vec3f, d: vec3f, c: vec3f, r: f32) -> f32 {
    let oc = o - c;
    let b = dot(oc, d);
    let h = b * b - dot(oc, oc) + r * r;
    if (h < 0) { return 1e30; }
    let t = -b - sqrt(h);
    return select(1e30, t, t > 1e-3);
}
fn blas_trace(o: vec3f, d: vec3f, root: u32, part: u32, h: ptr<function, Hit>) {
    let inv = safe_inv(d);
    var stack: array<u32, 32>;
    var sp = 0u;
    var node = root;
    if (box(o, inv, blas[node].lo, blas[node].hi, (*h).t) >= 1e30) { return; }
    loop {
        let N = blas[node];
        if (N.n > 0u) {
            for (var k = 0u; k < N.n; k++) {
                let T = tris[N.a + k];
                let e1 = T.p1.xyz - T.p0.xyz;
                let e2 = T.p2.xyz - T.p0.xyz;
                let pv = cross(d, e2);
                let det = dot(e1, pv);
                if (abs(det) < 1e-12) { continue; }
                let id = 1 / det;
                let tv = o - T.p0.xyz;
                let u = dot(tv, pv) * id;
                if (u < 0 || u > 1) { continue; }
                let qv = cross(tv, e1);
                let v = dot(d, qv) * id;
                if (v < 0 || u + v > 1) { continue; }
                let t = dot(e2, qv) * id;
                if (t > 1e-3 && t < (*h).t) { *h = Hit(t, 1u, part, N.a + k, u, v); }
            }
            if (sp == 0u) { break; }
            sp--;
            node = stack[sp];
            continue;
        }
        let c0 = N.a;
        let c1 = N.a + 1u;
        var t0 = box(o, inv, blas[c0].lo, blas[c0].hi, (*h).t);
        var t1 = box(o, inv, blas[c1].lo, blas[c1].hi, (*h).t);
        var near = c0;
        var far = c1;
        if (t1 < t0) { near = c1; far = c0; let s = t0; t0 = t1; t1 = s; }
        if (t0 >= 1e30) {
            if (sp == 0u) { break; }
            sp--;
            node = stack[sp];
            continue;
        }
        if (t1 < 1e30 && sp < 32u) { stack[sp] = far; sp++; }
        node = near;
    }
}
fn trace(o: vec3f, d: vec3f, tmax: f32) -> Hit {
    var h = Hit(tmax, 0u, 0u, 0u, 0, 0);
    if (F.blob.w > 0) {
        let t = blob_hit(o, d, h.t);
        if (t < h.t) { h = Hit(t, 2u, 0u, 0u, 0, 0); }
    }
    for (var i = 0u; i < u32(F.pulse.w); i++) {
        let t = sphere_hit(o, d, F.drops[3 * i].xyz, F.drops[3 * i + 2].x + F.drops[3 * i + 2].y * 0.5);
        if (t < h.t) { h = Hit(t, 3u, i, 0u, 0, 0); }
    }
    if (F.shadow.w == 0 || arrayLength(&torder) == 0u) { return h; }
    let inv = safe_inv(d);
    var stack: array<u32, 24>;
    var sp = 0u;
    var node = 0u;
    if (box(o, inv, tlas[0].lo, tlas[0].hi, h.t) >= 1e30) { return h; }
    loop {
        let N = tlas[node];
        if (N.n > 0u) {
            for (var k = 0u; k < N.n; k++) {
                let part = torder[N.a + k];
                let I = inst[part];
                blas_trace((I.inv * vec4f(o, 1)).xyz, (I.inv * vec4f(d, 0)).xyz, I.root, part, &h);
            }
            if (sp == 0u) { break; }
            sp--;
            node = stack[sp];
            continue;
        }
        let c0 = N.a;
        let c1 = N.a + 1u;
        var t0 = box(o, inv, tlas[c0].lo, tlas[c0].hi, h.t);
        var t1 = box(o, inv, tlas[c1].lo, tlas[c1].hi, h.t);
        var near = c0;
        var far = c1;
        if (t1 < t0) { near = c1; far = c0; let s = t0; t0 = t1; t1 = s; }
        if (t0 >= 1e30) {
            if (sp == 0u) { break; }
            sp--;
            node = stack[sp];
            continue;
        }
        if (t1 < 1e30 && sp < 24u) { stack[sp] = far; sp++; }
        node = near;
    }
    return h;
}
`;

// Reflections and the bounce (GI), half the size, one ray per pixel from
// the surface the main pass saw. Both change their rays from frame to frame;
// the blur and TAA average them.
export const REFLECT = COMMON + TRACE + `
@group(2) @binding(0) var hdr: texture_2d<f32>;
@group(2) @binding(1) var gbuf: texture_2d<f32>;
@group(2) @binding(2) var out: texture_storage_2d<rgba16float, write>;
@group(2) @binding(3) var motion: texture_2d<f32>;
// What a ray finds where it lands, lit by the light. With bounce, for the
// light the walls pass on, the blob does not count (it is the light).
fn landed(o: vec3f, r: vec3f, h: Hit, bounce: bool) -> vec3f {
    if (h.kind == 1u) {
        let I = inst[h.part];
        let T = tnorm[h.tri];
        let nl = T.p0.xyz * (1 - h.u - h.v) + T.p1.xyz * h.u + T.p2.xyz * h.v;
        let m = mat3x3f(I.inv[0].xyz, I.inv[1].xyz, I.inv[2].xyz);
        var n = normalize(transpose(m) * nl);
        if (dot(n, r) > 0) { n = -n; }
        return shade(o + r * h.t, n, -r, vec3f(I.look), select(0.75, 0.35, bounce), max(I.rough, select(0.3, 0.7, bounce)), 0, 0);
    }
    if (h.kind == 2u && !bounce) {
        let q = o + r * h.t;
        let n = normalize(blob_at(normalize(q - F.light.xyz)).xyz);
        return blob_glow(q, n, -r);
    }
    if (h.kind == 3u) { return F.blob.rgb * 0.8; }
    return vec3f(0);
}
// a frame of the pixel's normal: two axes across it
fn across(N: vec3f) -> mat3x3f {
    let up = select(vec3f(0, 0, 1), vec3f(1, 0, 0), abs(N.z) > 0.9);
    let T = normalize(cross(up, N));
    return mat3x3f(T, cross(N, T), N);
}
@compute @workgroup_size(8, 8) fn main(@builtin(global_invocation_id) id: vec3u) {
    let dims = textureDimensions(out);
    if (id.x >= dims.x || id.y >= dims.y) { return; }
    let full = min(vec2i(id.xy) * 2, vec2i(F.size.xy) - 1);
    let c = textureLoad(hdr, full, 0);
    let g = textureLoad(gbuf, full, 0);
    if (F.flags.x == 0 || c.a < 0.01 || g.w > 30000) { textureStore(out, id.xy, vec4f(0)); return; }
    let uv = (vec2f(full) + 0.5) * F.size.zw;
    let dir = ray_dir(uv);
    let p = F.eye.xyz + dir * g.w;
    let N = normalize(g.xyz);
    // glossy: the mirrored ray turned within a lobe as wide as the surface is rough
    let rough = max(textureLoad(motion, full, 0).w - 1, 0);
    let e = rand2(id.xy);
    let lobe = rough * rough * 0.6;
    let a = 6.2831853 * e.x;
    var r = reflect(dir, N);
    r = normalize(r + across(r) * vec3f(cos(a), sin(a), 0) * lobe * sqrt(e.y));
    if (dot(r, N) < 0) { r = reflect(r, N); }
    let o = p + N * (0.04 + g.w * 0.002);
    let h = trace(o, r, 600);
    var col = landed(o, r, h, false);
    // the fog takes some of it on the way
    col *= exp(-F.atten.w * min(h.t, 600) * 0.6);
    textureStore(out, id.xy, vec4f(safe(col), h.t));
}
// The bounce: light that the walls throw onto the walls. taa2.w rays (1, 2
// or 4 by quality) in directions by the cosine about the normal, stratified
// in their first number; what they find, lit by the light. Stored divided by
// the light's strength m, so that GI_ACC can average it over frames while
// the blob pulses; COMPOSE multiplies m back.
@compute @workgroup_size(8, 8) fn gi(@builtin(global_invocation_id) id: vec3u) {
    let dims = textureDimensions(out);
    if (id.x >= dims.x || id.y >= dims.y) { return; }
    let full = min(vec2i(id.xy) * 2, vec2i(F.size.xy) - 1);
    let c = textureLoad(hdr, full, 0);
    let g = textureLoad(gbuf, full, 0);
    if (F.taa.w == 0 || c.a < 0.005 || g.w > 30000) { textureStore(out, id.xy, vec4f(0)); return; }
    let uv = (vec2f(full) + 0.5) * F.size.zw;
    let p = F.eye.xyz + ray_dir(uv) * g.w;
    let N = normalize(g.xyz);
    let frame = across(N);
    let o = p + N * (0.04 + g.w * 0.002);
    let n = max(u32(F.taa2.w), 1u);
    var sum = vec3f(0);
    for (var i = 0u; i < n; i++) {
        let s = (id.x * 1973u + id.y * 9277u + u32(F.taa.z) * 26699u) ^ (i * 0x68E31DA4u + 7u);
        let ex = fract(hash(s) / f32(n) + f32(i) / f32(n));
        let ey = hash(s ^ 0x9E3779B9u);
        let rr = sqrt(ex);
        let a = 6.2831853 * ey;
        let r = normalize(frame * vec3f(rr * cos(a), rr * sin(a), sqrt(max(1 - ex, 0))));
        sum += landed(o, r, trace(o, r, 220), true);
    }
    textureStore(out, id.xy, vec4f(safe(sum / f32(n) / max(F.lcol.w, 1e-3)), 1));
}`;

// The bounce over frames, before its blur: this frame's rays mixed into the
// history where the point stood in the frame before (velocity of the main
// pass), 8 % a frame; a history at another depth (an edge, a part moved in
// front) or none counts for nothing. Alpha keeps the depth for the next frame.
export const GI_ACC = COMMON + `
@group(1) @binding(0) var cur: texture_2d<f32>;
@group(1) @binding(1) var hist: texture_2d<f32>;
@group(1) @binding(2) var motion: texture_2d<f32>;
@group(1) @binding(3) var gbuf: texture_2d<f32>;
@group(1) @binding(4) var out: texture_storage_2d<rgba16float, write>;
@compute @workgroup_size(8, 8) fn main(@builtin(global_invocation_id) id: vec3u) {
    let dims = vec2i(textureDimensions(out));
    let p = vec2i(id.xy);
    if (p.x >= dims.x || p.y >= dims.y) { return; }
    let full = min(p * 2, vec2i(F.size.xy) - 1);
    let c = textureLoad(cur, p, 0).rgb;
    let z = textureLoad(gbuf, full, 0).w;
    let prev = (vec2f(p) + 0.5) / vec2f(dims) - textureLoad(motion, full, 0).xy;
    var h = vec3f(0);
    var a = 1.0;
    if (F.grade0.w > 0 && F.taa2.y > 0 && z < 30000 && all(prev >= vec2f(0)) && all(prev <= vec2f(1))) {
        let hp = textureLoad(hist, clamp(vec2i(prev * vec2f(dims)), vec2i(0), dims - 1), 0);
        if (abs(hp.a - z) < 0.05 * z + 0.3) {
            h = hp.rgb;
            a = 0.08;
        }
    }
    textureStore(out, p, vec4f(safe(mix(h, c, a)), z));
}`;

// God rays: in-scattering along the ray from the eye to the surface, half
// the size, the light seen through the shadow map at every step, a mist of
// slowly moving noise.
export const FOG = COMMON + `
@group(2) @binding(0) var gbuf: texture_2d<f32>;
@group(2) @binding(1) var out: texture_storage_2d<rgba16float, write>;
@compute @workgroup_size(8, 8) fn main(@builtin(global_invocation_id) id: vec3u) {
    let dims = textureDimensions(out);
    if (id.x >= dims.x || id.y >= dims.y) { return; }
    let full = min(vec2i(id.xy) * 2, vec2i(F.size.xy) - 1);
    let g = textureLoad(gbuf, full, 0);
    if (F.flags.y == 0 || F.shadow.w == 0) { textureStore(out, id.xy, vec4f(0, 0, 0, 1)); return; }
    let uv = (vec2f(full) + 0.5) * F.size.zw;
    let dir = ray_dir(uv);
    let len = min(g.w, 260.0);
    let steps = i32(F.misc.w);
    let dt = len / f32(steps);
    let j = ign_t(vec2f(id.xy));
    let sigma = F.atten.w;
    let gg = 0.72;
    var T = 1.0;
    var acc = vec3f(0);
    for (var i = 0; i < steps; i++) {
        let s = (f32(i) + j) * dt;
        let x = F.eye.xyz + dir * s;
        let lv = x - F.light.xyz;
        let d = max(length(lv), F.light.w);
        let cosv = dot(lv / d, -dir);
        let phase = (1 - gg * gg) / (4 * PI * pow(1 + gg * gg - 2 * gg * cosv, 1.5)) + 0.01;
        let mist = 0.15 + 1.7 * pow(fbm(x * 0.045 + vec3f(F.eye.w * 0.05, -F.eye.w * 0.03, F.eye.w * 0.02)), 2.0);
        let dens = sigma * mist;
        let vis = shadow_at(x, vec3f(0), 0, 0);
        acc += T * dens * phase * radiance(max(d, F.light.w * 1.6)) * vis * dt;
        T *= exp(-dens * dt * 0.5);
    }
    textureStore(out, id.xy, vec4f(safe(acc), select(T, 1.0, T != T)));
}`;

// A bilateral blur along u.dir over a target of half the size: neighbours
// count less the farther their depth is from the centre's.
export const BLUR = `
struct U { dir: vec2i, radius: i32, mode: i32 }
@group(0) @binding(0) var src: texture_2d<f32>;
@group(0) @binding(1) var gbuf: texture_2d<f32>;
@group(0) @binding(2) var dst: texture_storage_2d<rgba16float, write>;
@group(0) @binding(3) var<uniform> u: U;
@compute @workgroup_size(8, 8) fn main(@builtin(global_invocation_id) id: vec3u) {
    let dims = vec2i(textureDimensions(dst));
    let p = vec2i(id.xy);
    if (p.x >= dims.x || p.y >= dims.y) { return; }
    let full = vec2i(textureDimensions(gbuf)) - 1;
    let zc = textureLoad(gbuf, min(p * 2, full), 0).w;
    var sum = vec4f(0);
    var wsum = 0.0;
    for (var i = -u.radius; i <= u.radius; i++) {
        let q = clamp(p + u.dir * i, vec2i(0), dims - 1);
        let z = textureLoad(gbuf, min(q * 2, full), 0).w;
        let x = f32(i) / f32(u.radius + 1);
        let w = exp(-x * x * 2.5) * exp(-abs(z - zc) / (0.04 * zc + 0.2));
        sum += textureLoad(src, q, 0) * w;
        wsum += w;
    }
    textureStore(dst, p, sum / max(wsum, 1e-5));
}`;

// The scene put together, at the size of the render: the main pass, its
// reflections, the fog, and the original's own draws that this picture
// keeps (logo, veil, glow; drawn by renderer.js on a larger surface).
export const COMPOSE = COMMON + `
@group(1) @binding(0) var hdr: texture_2d<f32>;
@group(1) @binding(1) var gbuf: texture_2d<f32>;
@group(1) @binding(2) var refl: texture_2d<f32>;
@group(1) @binding(3) var fog: texture_2d<f32>;
@group(1) @binding(4) var legacy: texture_2d<f32>;
@group(1) @binding(5) var ldepth: texture_depth_2d;
@group(1) @binding(6) var motion: texture_2d<f32>;
@group(1) @binding(7) var gi_light: texture_2d<f32>;
// the scene with its circle of confusion; for TAA the velocity, how much
// of the original's draws is here (they move without velocity), and in w
// how much of the lettering, plus 2 where the logo lies in front
struct Composed { @location(0) c: vec4f, @location(1) m: vec4f }
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    var p = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
    return vec4f(p[i], 0, 1);
}
// ACES as fitted by Narkowicz, and its inverse: the original's picture is
// already tone mapped, so it comes back as light that maps to itself.
fn aces_inv(y: vec3f) -> vec3f {
    let c = clamp(y, vec3f(0), vec3f(0.985));
    let a = 2.43 * c - 2.51;
    let b = 0.59 * c - 0.03;
    let k = 0.14 * c;
    return max((-b - sqrt(max(b * b - 4 * a * k, vec3f(0)))) / (2 * a), vec3f(0));
}
@fragment fn fs(@builtin(position) pos: vec4f) -> Composed {
    let px = vec2i(pos.xy);
    let uv = pos.xy * F.size.zw;
    let c = textureLoad(hdr, px, 0);
    let g = textureLoad(gbuf, px, 0);
    let mo = textureLoad(motion, px, 0);
    let covered = g.w < 30000;
    var sharp = !covered;
    var col = c.rgb + textureSampleLevel(refl, lin, uv, 0).rgb * c.a;
    // the bounce on the walls: what arrives, times their albedo
    col += textureSampleLevel(gi_light, lin, uv, 0).rgb * F.lcol.w * mo.z * 1.8 * F.taa.w;
    var orig = 0.0;
    let fg = textureSampleLevel(fog, lin, uv, 0);
    col = col * fg.a + fg.rgb;
    // the original's draws, averaged over the pixel's footprint on their surface
    let o = F.size.zw * 0.25;
    let l = (textureSampleLevel(legacy, lin, uv + vec2f(-o.x, -o.y), 0) + textureSampleLevel(legacy, lin, uv + vec2f(o.x, -o.y), 0)
           + textureSampleLevel(legacy, lin, uv + vec2f(-o.x, o.y), 0) + textureSampleLevel(legacy, lin, uv + vec2f(o.x, o.y), 0)) * 0.25;
    let lc = aces_inv(pow(l.rgb, vec3f(2.2))) / max(F.flags.w, 1e-3);
    let ls = vec2f(textureDimensions(ldepth));
    let lz = textureLoad(ldepth, min(vec2i(uv * ls), vec2i(ls) - 1), 0);
    // anim_draw clears the depth before the logo (FrameOrder.md 3): where
    // the logo left depth it lies over the chamber, whatever is nearer
    if (lz < 1) {
        col = lc;
        sharp = true;
        orig = 1.0;
    } else if (!covered) {
        col += lc;
        orig = clamp(max(lc.r, max(lc.g, lc.b)) * 4, 0, 1);
    }
    // misc.y = 1: what the light's shadow map lets through, on the surfaces;
    // 2: the bounce alone
    if (F.misc.y == 1 && covered) {
        let p = F.eye.xyz + ray_dir(uv) * g.w;
        col = vec3f(shadow_at(p, normalize(g.xyz), 0, 0)) * 0.6;
    }
    if (F.misc.y == 2) { col = textureSampleLevel(gi_light, lin, uv, 0).rgb * F.lcol.w * mo.z * 1.8 * 4; }
    // the circle of confusion in pixels, for DOF
    let coc = select(F.lens.y * abs(1 - F.lens.x / max(g.w, 0.1)) * F.size.y / 1080 * 22, 0.0, sharp);
    // the lettering: the original draws it in the one colour 0xFF62CA13
    // (Logo.md 5.3), on the screen and into the logo's picture; found by
    // that hue, which edges against black keep
    let key = vec2f(l.r, l.b) / max(l.g, 1e-3) - vec2f(98.0, 19.0) / 202.0;
    let text = (1 - smoothstep(0.03, 0.06, length(key))) * smoothstep(0.02, 0.08, l.g);
    // single fireflies (a highlight in one pixel) would grow into white
    // discs in DOF and bloom: no pixel brighter than 48, its hue kept
    col = safe(col);
    col *= min(1.0, 48.0 / max(max(col.r, max(col.g, col.b)), 1e-6));
    return Composed(vec4f(col, min(coc, F.lens.z)), vec4f(select(vec2f(0), mo.xy, mo.w > 0.5 && lz >= 1), orig, text + select(0.0, 2.0, lz < 1)));
}`;

// Depth of field: each pixel gathers from a spiral as wide as the largest
// circle of confusion; a neighbour counts where its own circle reaches this
// pixel, and one behind a sharper pixel only as far as that pixel's circle,
// so that the background does not bleed over a sharp foreground.
export const DOF = COMMON + `
@group(1) @binding(0) var scene: texture_2d<f32>;
@group(1) @binding(1) var gbuf: texture_2d<f32>;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    var p = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
    return vec4f(p[i], 0, 1);
}
@fragment fn fs(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    let size = vec2f(textureDimensions(scene));
    let px = vec2i(pos.xy);
    let c = textureLoad(scene, px, 0);
    let zc = textureLoad(gbuf, px, 0).w;
    let reach = 18.0 * size.y / 1080;
    if (reach < 0.5 || F.lens.y == 0) { return c; }
    var sum = c.rgb;
    var wsum = 1.0;
    let turn = 2.39996323;
    for (var i = 0; i < 48; i++) {
        let rr = sqrt((f32(i) + 0.5) / 48) * reach;
        // the spiral turned per pixel and frame: no copies of its pattern round bright points
        let a = f32(i) * turn + ign_t(pos.xy) * 6.2831853;
        let off = vec2f(cos(a), sin(a)) * rr;
        let q = clamp(pos.xy + off, vec2f(0.5), size - 0.5);
        let s = textureSampleLevel(scene, lin, q / size, 0);
        let zs = textureLoad(gbuf, vec2i(q), 0).w;
        var w = clamp(s.a - rr + 1, 0, 1);
        if (zs > zc) { w *= clamp(c.a - rr + 1, 0, 1); }
        sum += s.rgb * w;
        wsum += w;
    }
    return vec4f(sum / wsum, c.a);
}`;

// TAA: this frame and the history of the frames before, met where each
// point stood then (velocity of the nearest point among the 3 x 3, so that
// edges keep their own). The history is clipped to the colours around the
// pixel (variance in YCoCg) and read with Catmull-Rom; the mean is weighted
// by 1 / (1 + luma) against flicker. Where the original's draws lie (no
// velocity) this frame weighs more. Then the particles, not where the logo
// lies in front; the shown picture carries the lettering in alpha for bloom.
export const TAA = COMMON + `
@group(1) @binding(0) var cur: texture_2d<f32>;
@group(1) @binding(1) var hist: texture_2d<f32>;
@group(1) @binding(2) var mot: texture_2d<f32>;
@group(1) @binding(3) var gbuf: texture_2d<f32>;
@group(1) @binding(4) var parts: texture_2d<f32>;
struct Two { @location(0) history: vec4f, @location(1) shown: vec4f }
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    var p = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
    return vec4f(p[i], 0, 1);
}
fn ycocg(c: vec3f) -> vec3f { return vec3f(dot(c, vec3f(0.25, 0.5, 0.25)), dot(c, vec3f(0.5, 0, -0.5)), dot(c, vec3f(-0.25, 0.5, -0.25))); }
fn rgb(c: vec3f) -> vec3f { return vec3f(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
// tone mapped for the neighbourhood, so that one bright pixel does not widen it
fn tm(c: vec3f) -> vec3f { return c / (1 + max(c.r, max(c.g, c.b))); }
fn itm(c: vec3f) -> vec3f { return c / max(1 - max(c.r, max(c.g, c.b)), 1e-3); }
fn at(uv: vec2f) -> vec3f { return textureSampleLevel(hist, lin, uv, 0).rgb; }
fn catmull(uv: vec2f) -> vec3f {
    let size = vec2f(textureDimensions(hist));
    let p = uv * size;
    let c = floor(p - 0.5) + 0.5;
    let f = p - c;
    let w0 = f * (-0.5 + f * (1 - 0.5 * f));
    let w1 = 1 + f * f * (-2.5 + 1.5 * f);
    let w2 = f * (0.5 + f * (2 - 1.5 * f));
    let w3 = f * f * (-0.5 + 0.5 * f);
    let w12 = w1 + w2;
    let t0 = (c - 1) / size;
    let t3 = (c + 2) / size;
    let t12 = (c + w2 / w12) / size;
    let r = at(vec2f(t12.x, t0.y)) * w12.x * w0.y + at(vec2f(t0.x, t12.y)) * w0.x * w12.y + at(t12) * w12.x * w12.y
          + at(vec2f(t3.x, t12.y)) * w3.x * w12.y + at(vec2f(t12.x, t3.y)) * w12.x * w3.y;
    let w = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(r / w, vec3f(0));
}
@fragment fn fs(@builtin(position) pos: vec4f) -> Two {
    let px = vec2i(pos.xy);
    let dims = vec2i(textureDimensions(cur));
    let uv = pos.xy / vec2f(dims);
    let c = textureLoad(cur, px, 0);
    let mw = textureLoad(mot, px, 0).w;
    let logo = mw >= 2;
    let text = mw - select(0.0, 2.0, logo);
    let p = textureLoad(parts, px, 0).rgb;
    let extra = select(min(p, vec3f(48)), vec3f(0), logo || any(p != p));
    var m1 = vec3f(0);
    var m2 = vec3f(0);
    var near = 1e9;
    var v = vec2f(0);
    for (var y = -1; y <= 1; y++) {
        for (var x = -1; x <= 1; x++) {
            let q = clamp(px + vec2i(x, y), vec2i(0), dims - 1);
            let s = ycocg(tm(safe(textureLoad(cur, q, 0).rgb)));
            m1 += s;
            m2 += s * s;
            let z = textureLoad(gbuf, q, 0).w;
            if (z < near) { near = z; v = textureLoad(mot, q, 0).xy; }
        }
    }
    let legacy = textureLoad(mot, px, 0).z;
    let prev = uv - v;
    var out = safe(c.rgb);
    if (F.taa2.y > 0 && all(prev >= vec2f(0)) && all(prev <= vec2f(1))) {
        let mean = m1 / 9;
        let sigma = sqrt(max(m2 / 9 - mean * mean, vec3f(0)));
        let gamma = select(1.0, 1.6, F.taa2.x < 0.1 && length(v) == 0);
        let lo = mean - gamma * sigma;
        let hi = mean + gamma * sigma;
        // the history clipped towards the mean into the box lo .. hi
        let h = ycocg(tm(safe(catmull(prev))));
        let d = h - mean;
        let e = max(abs(d) / max(hi - mean, vec3f(1e-4)), vec3f(1));
        let hc = itm(rgb(mean + d / max(e.x, max(e.y, e.z))));
        let a = max(F.taa2.x, legacy * F.taa2.z);
        let wc = a / (1 + dot(out, vec3f(0.2126, 0.7152, 0.0722)));
        let wh = (1 - a) / (1 + dot(hc, vec3f(0.2126, 0.7152, 0.0722)));
        out = safe((out * wc + hc * wh) / max(wc + wh, 1e-5));
    }
    return Two(vec4f(out, c.a), vec4f(out + extra, text));
}`;

// Bloom: a chain of halved targets down (13 taps), then up again, each
// level added to the one above with a tent of nine. Alpha of the shown
// picture is the logo's lettering, which does not bloom; the levels keep 0.
export const BLOOM = `
@group(0) @binding(0) var src: texture_2d<f32>;
@group(0) @binding(1) var lin: sampler;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    var p = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
    return vec4f(p[i], 0, 1);
}
fn at(uv: vec2f) -> vec3f {
    let s = textureSampleLevel(src, lin, uv, 0);
    let c = min(s.rgb, vec3f(64)) * (1 - clamp(s.a, 0, 1));
    return select(c, vec3f(0), c != c);
}
@fragment fn down(@builtin(position) p: vec4f) -> @location(0) vec4f {
    let s = 1 / vec2f(textureDimensions(src));
    let uv = p.xy * 2 * s;
    let a = at(uv + s * vec2f(-2, -2)); let b = at(uv + s * vec2f(0, -2)); let c = at(uv + s * vec2f(2, -2));
    let d = at(uv + s * vec2f(-2, 0)); let e = at(uv); let f = at(uv + s * vec2f(2, 0));
    let g = at(uv + s * vec2f(-2, 2)); let h = at(uv + s * vec2f(0, 2)); let i = at(uv + s * vec2f(2, 2));
    let j = at(uv + s * vec2f(-1, -1)); let k = at(uv + s * vec2f(1, -1)); let l = at(uv + s * vec2f(-1, 1)); let m = at(uv + s * vec2f(1, 1));
    let r = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    return vec4f(r, 0);
}
@fragment fn up(@builtin(position) p: vec4f) -> @location(0) vec4f {
    let s = 1 / vec2f(textureDimensions(src));
    let uv = p.xy * 0.5 * s;
    var r = at(uv) * 4;
    r += (at(uv + s * vec2f(-1, 0)) + at(uv + s * vec2f(1, 0)) + at(uv + s * vec2f(0, -1)) + at(uv + s * vec2f(0, 1))) * 2;
    r += at(uv + s * vec2f(-1, -1)) + at(uv + s * vec2f(1, -1)) + at(uv + s * vec2f(-1, 1)) + at(uv + s * vec2f(1, 1));
    return vec4f(r / 16, 0);
}`;

// The last step into the picture: bloom added, exposure, ACES, the
// playground's colour, a little chromatic aberration, vignette and grain.
export const FINISH = COMMON + `
@group(1) @binding(0) var scene: texture_2d<f32>;
@group(1) @binding(1) var bloom: texture_2d<f32>;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    var p = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
    return vec4f(p[i], 0, 1);
}
fn aces(x: vec3f) -> vec3f { return clamp(x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14), vec3f(0), vec3f(1)); }
@fragment fn fs(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    let size = vec2f(F.right.w, F.up.w);
    let uv = pos.xy / size;
    let s = F.size.zw * 0.35;
    let ctr = uv - 0.5;
    let ca = ctr * dot(ctr, ctr) * 0.006;
    var c = vec3f(
        textureSampleLevel(scene, lin, uv - ca, 0).r,
        (textureSampleLevel(scene, lin, uv + vec2f(s.x, s.y), 0).g + textureSampleLevel(scene, lin, uv - vec2f(s.x, s.y), 0).g
         + textureSampleLevel(scene, lin, uv + vec2f(s.x, -s.y), 0).g + textureSampleLevel(scene, lin, uv - vec2f(s.x, -s.y), 0).g) * 0.25,
        textureSampleLevel(scene, lin, uv + ca, 0).b);
    c += textureSampleLevel(bloom, lin, uv, 0).rgb * F.misc.x;
    c = aces(c * F.flags.w);
    // a cooler shadow and a warmer light, as a grade would
    let lum = dot(c, vec3f(0.2126, 0.7152, 0.0722));
    c += (vec3f(-0.003, 0.0, 0.008) * smoothstep(0.0, 0.08, lum) * (1 - lum) + vec3f(0.05, 0.02, -0.03) * lum * lum) * F.lens.w;
    c = clamp(mat3x3f(F.grade0.xyz, F.grade1.xyz, F.grade2.xyz) * c, vec3f(0), vec3f(1));
    let vig = 1 - 0.32 * pow(length(ctr * vec2f(1, size.y / size.x) * 1.35), 2.4);
    c *= vig;
    c = pow(c, vec3f(1 / 2.2));
    c += (ign(pos.xy + fract(F.eye.w * 13.7) * 64) - 0.5) / 255 * 1.5;
    return vec4f(c, 1);
}`;

// The relief's levels: each the mean of four texels of the one above. The
// mean of unit normals is shorter than 1 where they differ (Toksvig).
export const MIP = `
@group(0) @binding(0) var src: texture_2d<f32>;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    var p = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
    return vec4f(p[i], 0, 1);
}
@fragment fn fs(@builtin(position) p: vec4f) -> @location(0) vec4f {
    let s = vec2i(textureDimensions(src));
    let o = vec2i(textureDimensions(src)) / vec2i(max(vec2u(textureDimensions(src)) / 2u, vec2u(1)));
    let q = vec2i(p.xy) * o;
    var n = vec3f(0);
    for (var y = 0; y < o.y; y++) { for (var x = 0; x < o.x; x++) {
        n += textureLoad(src, min(q + vec2i(x, y), s - 1), 0).xyz * 2 - 1;
    } }
    n /= f32(o.x * o.y);
    return vec4f(n * 0.5 + 0.5, 1);
}
@fragment fn copy(@builtin(position) p: vec4f) -> @location(0) vec4f { return textureLoad(src, vec2i(p.xy), 0); }`;
