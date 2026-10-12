// The vertex programs and combiners of docs/decomp/GPU.md 3 to 5 in WGSL.
//
// Each vertex shader follows the listing of its program in
// data/gpu/programs.txt; each fragment shader the stages of its block in
// data/gpu/gpu.json ("decoded") with the rules by which xemu reads them
// (GPU.md 4.1): a stage's result is clamped to [-1, 1], an input is taken
// signed, as max(x, 0), as 2 max(x, 0) - 1 or as 1 - clamp(x, 0, 1). A colour
// from the vertex stage is clamped to [0, 1], registers start at 0, 0, 0, 1.
//
// d.c[k] is vertex constant k of the core, c[96 + k] of GPU.md. d.k[0..2]
// are the colours of 0x800410B0 after packing to bytes (GPU.md 4.2), d.k[3]
// the factor colour of 0x8003CED0.
//
// d.wide is the host's own, for a picture in the shape of the window
// (docs/web.md 11.7): x and y scale the place on the screen before the
// rasteriser, z and w stretch the quads that fill the screen by as much the
// other way. At 640 x 480 it is (1, 1, 1, 1) and changes nothing.
export const CONSTANTS = 20;
// the wall of the white room (docs/web.md 11.5), also its clear colour
export const ROOM = [0.95, 0.95, 0.94];
const ROOM_WGSL = `vec3f(${ROOM.join(', ')})`;

const HEAD = `
struct Draw { c: array<vec4f, ${CONSTANTS}>, k: array<vec4f, 4>, bias: vec4f, misc: vec4f, tint: vec4f, show: vec4f, wide: vec4f }
@group(0) @binding(0) var<uniform> d: Draw;
struct V {
    @builtin(position) p: vec4f, @location(0) d0: vec4f,
    @location(1) t0: vec4f, @location(2) t1: vec4f, @location(3) t2: vec4f, @location(4) t3: vec4f,
}
// Every position lies 0.53125 pixels right and down, and the rasteriser
// takes it in sixteenths of a pixel, cut off towards zero (GPU.md 3).
// d.bias.zw is half the size of the target; d.bias.y switches the cut off.
fn place(q: vec4f) -> vec4f {
    let p = vec4f(q.xy * d.wide.xy, q.zw);
    if (abs(p.w) < 1e-12) { return p; }
    let half = d.bias.zw;
    let screen = p.xy / p.w * vec2f(half.x, -half.y) + half + d.bias.x;
    let cut = select(trunc(screen * 16) / 16, screen, d.bias.y != 0);
    return vec4f((cut - half) / vec2f(half.x, -half.y) * p.w, p.zw);
}
fn mul(v: vec4f, k: i32) -> vec4f { return vec4f(dot(v, d.c[k]), dot(v, d.c[k + 1]), dot(v, d.c[k + 2]), dot(v, d.c[k + 3])); }
fn none() -> V { return V(vec4f(0), vec4f(0, 0, 0, 1), vec4f(0, 0, 0, 1), vec4f(0, 0, 0, 1), vec4f(0, 0, 0, 1), vec4f(0, 0, 0, 1)); }
fn rsq(x: f32) -> f32 { return inverseSqrt(abs(x)); }
fn sat(x: vec3f) -> vec3f { return clamp(x, vec3f(0), vec3f(1)); }
fn st(x: vec3f) -> vec3f { return clamp(x, vec3f(-1), vec3f(1)); }
fn ex(x: vec3f) -> vec3f { return 2 * max(x, vec3f(0)) - 1; }
// SET_ALPHA_FUNC 0x206: kept when round(a * 255) >= SET_ALPHA_REF
fn alpha_test(a: f32) { if (d.misc.z != 0 && i32(round(a * 255)) < i32(d.misc.x)) { discard; } }
// What a draw on the screen writes. d.show.x = 0: its colour, the picture of
// the original. For the workbench (docs/web.md 10, 11), 1: the colour d.tint
// alone, for wireframes; 2: d.tint, the number of the draw, where the draw
// can be seen, for picking. d.show.y says by what it is seen: 0 everywhere,
// 1 its alpha, 2 alpha times brightness, 3 brightness; d.show.z is the limit.
// 3: d.tint shaded by the slope of the surface towards the eye ("original
// shaders off"); 5: its colour with the alpha d.show.w, where that is not 0
// (a surface one can see through). The white room (docs/web.md 11.5): 6 the
// colour as it is, the chamber's own (chamberFragment); 7 light added on
// black as the tint it gives a white wall, for a blend that multiplies;
// 8 a picture on black as on the wall: lightness turned over, colour kept;
// 9 a body that glows: its colour at full strength, a little shading kept.
// Nothing of 3 to 8 and w is in the original.
const ROOM = ${ROOM_WGSL};
fn fin(p: vec4f, c: vec4f) -> vec4f {
    let half = d.bias.zw;
    let z = 1 / max(p.w, 1e-6);
    let ndc = (p.xy - half) / half;
    let q = vec3f(ndc.x * z * 0.5523, -ndc.y * z * 0.4142, z);
    let n = normalize(cross(dpdx(q), dpdy(q)));
    if (d.show.x == 0 || d.show.x == 6) { return c; }
    if (d.show.x == 7) {
        let e = c.rgb * c.a;
        return vec4f(clamp(1 - (max(e.r, max(e.g, e.b)) - e), vec3f(0), vec3f(1)), 1);
    }
    if (d.show.x == 9) {
        let m = max(c.r, max(c.g, c.b));
        // without colour of its own (its rim) the blob's green (Blob.md 2)
        let hue = mix(vec3f(0.25, 1.0, 0.15), c.rgb / max(m, 1e-3), smoothstep(0.0, 0.05, m));
        return vec4f(hue * (0.6 + 0.4 * m), c.a);
    }
    if (d.show.x == 8) {
        let l = dot(c.rgb, vec3f(0.299, 0.587, 0.114));
        return vec4f(clamp(c.rgb - l + (1 - l) * ROOM, vec3f(0), vec3f(1)), c.a);
    }
    if (d.show.x == 2) {
        let bright = max(c.r, max(c.g, c.b));
        var seen = 1.0;
        if (d.show.y == 1) { seen = c.a; }
        if (d.show.y == 2) { seen = c.a * bright; }
        if (d.show.y == 3) { seen = bright; }
        if (seen < d.show.z) { discard; }
    }
    var a = c.a;
    if (d.show.w != 0) { a = d.show.w; }
    if (d.show.x == 3) {
        let lit = 0.28 + 0.72 * abs(n.z);
        return vec4f(d.tint.rgb * lit, select(1.0, a, d.show.w != 0));
    }
    if (d.show.x == 5) { return vec4f(c.rgb, a); }
    return d.tint;
}
`;

// GPU.md 3.1 and 3.2. oT3 is the place in the light view, oD0 the
// attenuation, oT1 and oT2 light and half vector along the part's axes.
const chamberVertex = (bump) => `
@vertex fn vs(@location(0) v0: vec3f, ${bump ? '@location(4) uv: vec2f, ' : ''}@location(1) a: vec3f, @location(2) b: vec3f, @location(3) n: vec3f) -> V {
    let p = vec4f(v0, 1);
    var o = none();
    o.p = place(mul(p, 0));
    o.t3 = vec4f(dot(p, d.c[8]), dot(p, d.c[9]), dot(p, d.c[10]), max(dot(p, d.c[11]), 0));
    let q = v0 * d.c[6].xyz;
    let l = d.c[4].xyz - q;
    let e = d.c[5].xyz - q;
    let l2 = dot(l, l);
    let rl = rsq(l2);
    let L = rl * l;
    let H = rsq(dot(e, e)) * e + L;
    o.d0 = vec4f(sat(vec3f(1 / dot(vec3f(1, l2 * rl, l2), d.c[7].xyz))), 1);
    o.t1 = vec4f(dot(L, a), dot(L, b), dot(L, n), 1);
    o.t2 = vec4f(dot(H, a), dot(H, b), dot(H, n), 1);
    ${bump ? 'o.t0 = vec4f(uv, 0, 1);' : ''}
    return o;
}`;

// GPU.md 4.4: the depth of the light view, filtered, against oT3.z / oT3.w.
// The texture is addressed in texels and has a white border.
const SHADOW = `
fn light_tap(p: vec2f) -> f32 {
    let q = vec2i(p);
    if (any(q < vec2i(0)) || any(q >= vec2i(512))) { return 1; }
    return textureLoad(light, q, 0);
}
fn shadow(t: vec4f) -> f32 {
    if (d.misc.w == 0) { return 1; }
    // oT3.w is max(w, 0): behind the light it is 0, the place lies outside
    // the texture, whose border is the far depth, and z / w is the nearest
    // or the farthest depth by the sign of z
    if (t.w <= 0) { return select(0.0, 1.0, t.z < 0); }
    let p = t.xy / t.w - 0.5;
    let i = floor(p);
    let f = p - i;
    let stored = mix(mix(light_tap(i), light_tap(i + vec2f(1, 0)), f.x),
                     mix(light_tap(i + vec2f(0, 1)), light_tap(i + vec2f(1, 1)), f.x), f.y) * 65535;
    return select(0.0, 1.0, stored > clamp(t.z / t.w, 0, 65535));
}`;

// GPU.md 4.3, the two blocks of the chamber: nl and nh from the cube of
// directions, nh to the power 32 (16 with relief), then
// colour0 * (Kd nl + Ks s) + Ka, once with K4 and once with the shadow.
const chamberFragment = (bump) => `
@fragment fn fs(v: V) -> @location(0) vec4f {
    let t1 = ex(textureSampleLevel(cube, clamped, v.t1.xyz, 0).rgb);
    let t2 = ex(textureSampleLevel(cube, clamped, v.t2.xyz, 0).rgb);
    let normal = ${bump ? 'ex(textureSampleLevel(relief, repeated, v.t0.xy / v.t0.w, 0).rgb)' : 'vec3f(0, 0, 1)'};
    let nl = clamp(dot(normal, t1), -1, 1);
    var s = max(clamp(dot(normal, t2), -1, 1), 0);
    for (var i = 0; i < ${bump ? 4 : 5}; i++) { s = s * s; }
    let lit = st(d.k[1].rgb * max(nl, 0) + d.k[2].rgb * s);
    let r0 = max(st(v.d0.rgb * lit + d.k[0].rgb), vec3f(0));
    if (d.show.x == 6) { return fin(v.p, room(v)); }
    return fin(v.p, vec4f(r0 * (63.0 / 255.0) + r0 * shadow(v.t3), 1));
}
// The white room (docs/web.md 11.5): the part white and without relief,
// lit by the same green light as above (place, attenuation, Kd, Ks and the
// shadow), stronger, and by a white light from the eye. Where the green is
// strong it outshines the white. Far away it fades into the wall, so that
// the chamber has no end: from 45 to 140 units from the eye (the chamber
// reaches to about 185, measured in the depth of t = 2.3 to 5.1). Not in
// the original.
fn room(v: V) -> vec4f {
    let l = normalize(v.t1.xyz);
    let h = normalize(v.t2.xyz);
    let eye = normalize(v.t2.xyz - v.t1.xyz);
    var s = max(h.z, 0);
    for (var i = 0; i < 5; i++) { s = s * s; }
    let green = v.d0.rgb * (d.k[1].rgb * max(l.z, 0) + d.k[2].rgb * s) * (63.0 / 255.0 + shadow(v.t3));
    let g = 1 - exp(-green * 3.0);
    let k = max(g.r, max(g.g, g.b));
    let fill = ROOM * (0.62 + 0.38 * abs(eye.z));
    let near = clamp(fill * (1 - 0.6 * k) + g, vec3f(0), vec3f(1));
    return vec4f(mix(near, ROOM, smoothstep(45.0, 140.0, 1 / max(v.p.w, 1e-6))), 1);
}`;

export const SHADERS = {
    smooth: {
        textures: ['cube', 'clamped', 'light'],
        code: HEAD + `
@group(1) @binding(0) var cube: texture_cube<f32>;
@group(1) @binding(1) var clamped: sampler;
@group(1) @binding(2) var light: texture_depth_2d;` + SHADOW + chamberVertex(false) + chamberFragment(false),
    },
    relief: {
        textures: ['2d', 'repeated', 'cube', 'clamped', 'light'],
        code: HEAD + `
@group(1) @binding(0) var relief: texture_2d<f32>;
@group(1) @binding(1) var repeated: sampler;
@group(1) @binding(2) var cube: texture_cube<f32>;
@group(1) @binding(3) var clamped: sampler;
@group(1) @binding(4) var light: texture_depth_2d;` + SHADOW + chamberVertex(true) + chamberFragment(true),
    },
    // the light views (U1): fixed function, depth alone
    lightdepth: {
        textures: [], depthOnly: true,
        code: HEAD + `
@vertex fn vs(@location(0) v0: vec3f) -> @builtin(position) vec4f { return place(mul(vec4f(v0, 1), 0)); }`,
    },
    // GPU.md 3.3 and the block "Depth for the veil": the disc around the
    // picture of the origin, darker where the chamber is near (Effects.md 3)
    depth: {
        textures: [],
        code: HEAD + `
@vertex fn vs(@location(0) v0: vec3f) -> V {
    let r = mul(vec4f(v0 * vec3f(d.wide.zw, 1), 1), 0);
    var o = none();
    o.p = place(r);
    o.d0 = vec4f(sat(max(d.c[16].xyz * (vec3f(r.z) + d.c[17].xyz), vec3f(0))), 1);
    o.t0 = vec4f(r.xyz * (1 / r.w) * d.c[18].xyz + d.c[19].xyz, 1);
    return o;
}
@fragment fn fs(v: V) -> @location(0) vec4f {
    let e = ex(v.t0.rgb);
    let m = 1 - clamp(dot(e, e), 0, 1);
    alpha_test(1);
    return vec4f(max(st(v.d0.rgb * (m * m)), vec3f(0)), 1);
}`,
    },
    // GPU.md 3.4, 3.5 and the two blocks of the blob: the rim from normal
    // and direction to the eye, both through the cube of directions
    blob: {
        textures: ['cube', 'clamped'],
        code: HEAD + `
@group(1) @binding(0) var cube: texture_cube<f32>;
@group(1) @binding(1) var clamped: sampler;
@vertex fn vs(@location(0) v0: vec3f, @location(1) v1: vec4f) -> V {
    let dir = v0 * d.c[11].xyz;
    let q = vec4f(v0 * d.c[10].xyz + d.c[12].xyz + rsq(dot(dir, dir)) * dir * v1.w, d.c[10].w + d.c[12].w + d.c[8].x * d.c[8].y);
    let eye = d.c[9].xyz - q.xyz;
    let n = v1.xyz * d.c[11].xyz;
    var o = none();
    o.p = place(mul(q, 4));
    o.t1 = vec4f(rsq(dot(eye, eye)) * eye, d.c[8].y);
    o.t0 = vec4f(rsq(dot(n, n)) * n, d.c[8].y);
    return o;
}
@fragment fn fs(v: V) -> @location(0) vec4f {
    let c = clamp(dot(ex(textureSampleLevel(cube, clamped, v.t0.xyz, 0).rgb), ex(textureSampleLevel(cube, clamped, v.t1.xyz, 0).rgb)), -1, 1);
    let r = (1 - clamp(c, 0, 1)) * (1 - clamp(c, 0, 1));
    return fin(v.p, vec4f(st(st((1 - r) * d.k[0].rgb) + d.k[1].rgb), d.k[0].a));
}`,
    },
    drop: {
        textures: ['cube', 'clamped'],
        code: HEAD + `
@group(1) @binding(0) var cube: texture_cube<f32>;
@group(1) @binding(1) var clamped: sampler;
@vertex fn vs(@location(0) v0: vec3f) -> V {
    let q = vec4f(d.c[12].xyz * v0 + dot(v0, d.c[11].xyz) * d.c[13].xyz + d.c[10].xyz, d.c[8].y);
    let eye = d.c[9].xyz - q.xyz;
    var o = none();
    o.p = place(mul(q, 4));
    o.t0 = vec4f(v0, 1);
    o.t1 = vec4f(rsq(dot(eye, eye)) * eye, d.c[8].y);
    return o;
}
@fragment fn fs(v: V) -> @location(0) vec4f {
    let c = clamp(dot(ex(textureSampleLevel(cube, clamped, v.t0.xyz, 0).rgb), ex(textureSampleLevel(cube, clamped, v.t1.xyz, 0).rgb)), -1, 1);
    let r = (1 - clamp(c, 0, 1)) * (1 - clamp(c, 0, 1));
    return fin(v.p, vec4f(st(st((1 - r) * d.k[0].rgb) + d.k[1].rgb), clamp((1 - r) * d.k[0].a, -1, 1) * d.k[2].a));
}`,
    },
    // GPU.md 3.6 and the block of the veil: target * (noise * K0) + K1
    veil: {
        textures: ['2d', 'clamped', '2d', '2d', '2d', 'repeated'],
        code: HEAD + `
@group(1) @binding(0) var aimed: texture_2d<f32>;
@group(1) @binding(1) var clamped: sampler;
@group(1) @binding(2) var noise1: texture_2d<f32>;
@group(1) @binding(3) var noise2: texture_2d<f32>;
@group(1) @binding(4) var noise3: texture_2d<f32>;
@group(1) @binding(5) var repeated: sampler;
@vertex fn vs(@location(0) v0: vec3f, @location(1) v1: vec2f, @location(2) v2: vec2f) -> V {
    // the noise's place is (1.875 y, -2.5 x) of the quad: stretched with it
    let s = vec4f(v2 * d.wide.wz, 0, 1);
    var o = none();
    o.p = place(vec4f(v0 * vec3f(d.wide.zw, 1), 1));
    o.t0 = vec4f(v1, 0, 1);
    o.t1 = s * d.c[1] + d.c[0];
    o.t2 = s * d.c[3] + d.c[2];
    o.t3 = s * d.c[5] + d.c[4];
    return o;
}
@fragment fn fs(v: V) -> @location(0) vec4f {
    let t0 = textureSampleLevel(aimed, clamped, v.t0.xy / v.t0.w, 0).rgb;
    var n = min(textureSampleLevel(noise1, repeated, v.t1.xy / v.t1.w, 0).r + textureSampleLevel(noise2, repeated, v.t2.xy / v.t2.w, 0).r, 1);
    n = min(n + textureSampleLevel(noise3, repeated, v.t3.xy / v.t3.w, 0).r, 1);
    alpha_test(1);
    return fin(v.p, vec4f(st(st(t0 * (n * d.k[0].rgb)) + d.k[1].rgb), 1));
}`,
    },
    // GPU.md 3.7 and the block of logo D: the colour of the vertex
    logod: {
        textures: [],
        code: HEAD + `
@vertex fn vs(@location(0) v0: vec3f, @location(1) v1: vec2f) -> V {
    let a = d.c[5].xyz + (d.c[4].xyz - d.c[5].xyz) * v1.y;
    let b = d.c[7].xyz + (d.c[6].xyz - d.c[7].xyz) * v1.y;
    var o = none();
    o.p = place(mul(vec4f(v0, 1), 0));
    o.d0 = vec4f(sat(a * d.c[8].xyz + b - b * d.c[8].xyz), 1);
    return o;
}
@fragment fn fs(v: V) -> @location(0) vec4f { return fin(v.p, v.d0); }`,
    },
    // Fixed function with combiners from the stages (GPU.md 2.1, 5). The
    // rows of world * view * projection stand in d.c[0..3]. d.misc.y: 1 the
    // colour takes the texture, 2 the factor colour, 4 alpha takes the
    // texture's, 8 the texture has a border (colour d.k[2]) instead of
    // repeating its edge. Filtered between four texels, level 0 alone.
    fixed: {
        textures: ['2d'],
        code: HEAD + `
@group(1) @binding(0) var image: texture_2d<f32>;
fn tap(p: vec2f) -> vec4f {
    let size = vec2i(textureDimensions(image, 0));
    let q = vec2i(p);
    if ((u32(d.misc.y) & 8u) != 0 && (any(q < vec2i(0)) || any(q >= size))) { return d.k[2]; }
    return textureLoad(image, clamp(q, vec2i(0), size - 1), 0);
}
@vertex fn vs(@location(0) v0: vec3f, @location(1) uv: vec2f) -> V {
    var o = none();
    o.p = place(mul(vec4f(v0 * vec3f(d.wide.zw, 1), 1), 0));
    o.t0 = vec4f(uv, 0, 1);
    return o;
}
@fragment fn fs(v: V) -> @location(0) vec4f {
    let mode = u32(d.misc.y);
    let p = v.t0.xy / v.t0.w * vec2f(textureDimensions(image, 0)) - 0.5;
    let i = floor(p);
    let f = p - i;
    let t0 = mix(mix(tap(i), tap(i + vec2f(1, 0)), f.x), mix(tap(i + vec2f(0, 1)), tap(i + vec2f(1, 1)), f.x), f.y);
    var c = vec4f(1, 1, 1, d.k[3].a);
    if ((mode & 1u) != 0) { c = vec4f(c.rgb * t0.rgb, c.a); }
    if ((mode & 2u) != 0) { c = vec4f(c.rgb * d.k[3].rgb, c.a); }
    if ((mode & 4u) != 0) { c.a = c.a * t0.a; }
    alpha_test(c.a);
    return fin(v.p, c);
}`,
    },
    // the lettering: the factor colour alone
    plain: {
        textures: [],
        code: HEAD + `
@vertex fn vs(@location(0) v0: vec3f) -> V {
    var o = none();
    o.p = place(mul(vec4f(v0, 1), 0));
    return o;
}
@fragment fn fs(v: V) -> @location(0) vec4f { return fin(v.p, d.k[3]); }`,
    },
};

// D5 (GPU.md 7): the library's triangle reads the surface 1280 x 480 at
// (2 x, y) of the picture's pixel (x, y), filtered: the mean of two pixels.
export const SHOW = `
@group(0) @binding(0) var screen: texture_2d<f32>;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    var p = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
    return vec4f(p[i], 0, 1);
}
@fragment fn fs(@builtin(position) p: vec4f) -> @location(0) vec4f {
    let at = vec2u(u32(p.x) * 2, u32(p.y));
    return (textureLoad(screen, at, 0) + textureLoad(screen, at + vec2u(1, 0), 0)) * 0.5;
}`;

// A picture not in 4:3 (docs/web.md 11.7), before the first draw that
// compares depth "equal" (veil and glow, Effects.md 4, 5): such a draw falls
// where nothing was drawn since the clear (depth 1). EDGE asks whether one
// pixel on the edge of the 4:3 frame is such; e.xy and e.zw are the first
// and last pixel of the frame in the surface. If none is, the draw falls on
// no pixel of the edge, and EXTEND keeps it off the empty pixels beside the
// frame as well: there it writes the depth just below 1. Seen at t = 5.8 to
// 5.98, where logo B covers the 4:3 picture. Not in the original.
export const EDGE = `
@group(0) @binding(0) var depth: texture_depth_2d;
@group(0) @binding(1) var<uniform> e: vec4f;
@group(0) @binding(2) var<storage, read_write> open: atomic<u32>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3u) {
    let lo = vec2i(e.xy);
    let hi = vec2i(e.zw);
    let w = hi.x - lo.x + 1;
    let h = hi.y - lo.y + 1;
    let i = i32(id.x);
    var q: vec2i;
    if (i < w) { q = vec2i(lo.x + i, lo.y); }
    else if (i < 2 * w) { q = vec2i(lo.x + i - w, hi.y); }
    else if (i < 2 * w + h) { q = vec2i(lo.x, lo.y + i - 2 * w); }
    else if (i < 2 * w + 2 * h) { q = vec2i(hi.x, lo.y + i - 2 * w - h); }
    else { return; }
    if (textureLoad(depth, q, 0) >= 1.0) { atomicStore(&open, 1u); }
}`;

// Drawn with the depth test "less", so only where the depth is still 1.
export const EXTEND = `
@group(0) @binding(0) var<uniform> e: vec4f;
@group(0) @binding(1) var<storage, read> open: u32;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    var p = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
    return vec4f(p[i], 0.99999994, 1);
}
@fragment fn fs(@builtin(position) p: vec4f) {
    let q = vec2i(p.xy);
    if (open != 0u || (all(q >= vec2i(e.xy)) && all(q <= vec2i(e.zw)))) { discard; }
}`;

// D5 for the playground (docs/web.md 11.5): the same mean of two pixels, then
// turned about the grey axis by u.rot (the scene's colour). Not in the
// original; the picture of D5 above stays as it is.
export const GRADE = `
struct Grade { rot: mat3x3f }
@group(0) @binding(0) var screen: texture_2d<f32>;
@group(0) @binding(1) var<uniform> u: Grade;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    var p = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
    return vec4f(p[i], 0, 1);
}
@fragment fn fs(@builtin(position) p: vec4f) -> @location(0) vec4f {
    let at = vec2u(u32(p.x) * 2, u32(p.y));
    let c = ((textureLoad(screen, at, 0) + textureLoad(screen, at + vec2u(1, 0), 0)) * 0.5).rgb;
    return vec4f(clamp(u.rot * c, vec3f(0), vec3f(1)), 1);
}`;

// The outline of what the pointer is over (red in the mask) and of what is
// chosen (green), laid over the picture: a thin line where the mask changes
// between a pixel and its eight neighbours, in the pixels of the picture
// whatever its size, and a faint wash inside. u.hover and u.chosen are their colours. Display only
// (docs/web.md 11).
export const OUTLINE = `
struct Outline { hover: vec4f, chosen: vec4f }
@group(0) @binding(0) var mask: texture_2d<f32>;
@group(0) @binding(1) var<uniform> u: Outline;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    var p = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
    return vec4f(p[i], 0, 1);
}
fn at(x: i32, y: i32) -> vec2f {
    let q = clamp(vec2i(x * 2, y), vec2i(0), vec2i(textureDimensions(mask, 0)) - vec2i(2, 1));
    return max(textureLoad(mask, q, 0).rg, textureLoad(mask, q + vec2i(1, 0), 0).rg);
}
@fragment fn fs(@builtin(position) p: vec4f) -> @location(0) vec4f {
    let x = i32(p.x);
    let y = i32(p.y);
    let here = step(vec2f(0.5), at(x, y));
    var edge = vec2f(0);
    for (var dy = -1; dy <= 1; dy++) {
        for (var dx = -1; dx <= 1; dx++) {
            edge = max(edge, abs(step(vec2f(0.5), at(x + dx, y + dy)) - here));
        }
    }
    if (edge.y > 0) { return vec4f(u.chosen.rgb, 0.9); }
    if (edge.x > 0) { return vec4f(u.hover.rgb, 0.8); }
    if (here.y > 0) { return vec4f(u.chosen.rgb, 0.08); }
    if (here.x > 0) { return vec4f(u.hover.rgb, 0.10); }
    return vec4f(0);
}`;

// The workbench (docs/web.md 10): a target or texture in place of the
// picture, fitted into 640 x 480. Display only, nothing of the original.
// u.yz is the size of the picture; here() its pixel scaled to 640 x 480.
const FULL = `
@group(0) @binding(1) var<uniform> u: vec4f;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    var p = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
    return vec4f(p[i], 0, 1);
}
fn here(p: vec4f) -> vec2f { return p.xy * vec2f(640, 480) / u.yz; }`;
export const VIEWS = {
    // a 2D texture; u.x = 1: one channel, shown grey
    flat: FULL + `
@group(0) @binding(0) var image: texture_2d<f32>;
@group(0) @binding(2) var linear: sampler;
@fragment fn fs(@builtin(position) p: vec4f) -> @location(0) vec4f {
    let size = vec2f(textureDimensions(image, 0));
    let k = min(640 / size.x, 480 / size.y);
    let at = (here(p) - (vec2f(640, 480) - size * k) * 0.5) / (size * k);
    if (any(at < vec2f(0)) || any(at > vec2f(1))) { return vec4f(0.1, 0.1, 0.1, 1); }
    let c = textureSampleLevel(image, linear, at, 0);
    return select(vec4f(c.rgb, 1), vec4f(c.rrr, 1), u.x != 0);
}`,
    // a light view 512 x 512: depth as distance, near light, 1 to 500 (Chamber.md 8)
    depth: FULL + `
@group(0) @binding(0) var image: texture_depth_2d;
@fragment fn fs(@builtin(position) p: vec4f) -> @location(0) vec4f {
    let h = here(p);
    let at = min(vec2i((h - vec2f(80, 0)) * (512.0 / 480.0)), vec2i(511));
    if (h.x < 80 || h.x >= 560) { return vec4f(0.1, 0.1, 0.1, 1); }
    let z = textureLoad(image, at, 0);
    let dist = 500.0 / (500.0 - z * 499.0);
    return vec4f(vec3f(clamp(1 - dist / 120, 0, 1)), 1);
}`,
    // a cube: faces +x, -x, +y in the upper row, -y, +z, -z in the lower
    cube: FULL + `
@group(0) @binding(0) var image: texture_cube<f32>;
@group(0) @binding(2) var linear: sampler;
@fragment fn fs(@builtin(position) p: vec4f) -> @location(0) vec4f {
    let q = here(p) - vec2f(20, 40);
    if (any(q < vec2f(0)) || q.x >= 600 || q.y >= 400) { return vec4f(0.1, 0.1, 0.1, 1); }
    let f = u32(q.y / 200) * 3 + u32(q.x / 200);
    let st = (q - floor(q / 200) * 200) / 100 - 1;
    var dir = vec3f(1, -st.y, -st.x);
    if (f == 1) { dir = vec3f(-1, -st.y, st.x); }
    if (f == 2) { dir = vec3f(st.x, 1, st.y); }
    if (f == 3) { dir = vec3f(st.x, -1, -st.y); }
    if (f == 4) { dir = vec3f(st.x, -st.y, 1); }
    if (f == 5) { dir = vec3f(-st.x, -st.y, -1); }
    return vec4f(textureSampleLevel(image, linear, dir, 0).rgb, 1);
}`,
};

// Lines in the world, each vertex with its colour: the camera's path, its
// keys, markers on the blob. m is view * projection in rows (row vectors).
export const PATH = `
struct Path { m: mat4x4f, colour: vec4f }
@group(0) @binding(0) var<uniform> u: Path;
struct L { @builtin(position) p: vec4f, @location(0) c: vec4f }
@vertex fn vs(@location(0) p: vec3f, @location(1) c: vec4f) -> L { return L(u.m * vec4f(p, 1), c); }
@fragment fn fs(l: L) -> @location(0) vec4f { return l.c; }`;
