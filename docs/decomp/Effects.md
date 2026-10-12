# Veil and glow

The sub-object `fx` = `anim_object` + 0x17EC = 0x8007083C draws, over
chamber, blob and logo, a green veil of three noise layers and over it a
flat glow. The veil is a surface over the whole screen; its brightness comes
from a texture that `fx` draws itself in every frame: a disc around the
picture centre of the scene, darkened where the chamber is near.

Evidence: run `c8_main2` (`tools/xemu/probe_c8.py`), model
`tools/xemu/effects_model.py`, comparison `tools/xemu/vectors_c8.py`, vector
file `vectors/c8_gpu.json` (sections `effects` and `draws`). The programs
and combiners are in `GPU.md` 3.3, 3.6 and 4.3, the noise textures in
`Textures.md` 4.8, the quantities x20, t in `Timing.md` 4, D and the angles
of the orbit in `Camera.md`.

Computation is on the x87 FPU with a 53-bit mantissa; `f32` means: at this
place the value is stored as float32.

## 1. The object

`fx_construct` (0x80059B17) sets ten doublewords to 0, `fx_setup`
(0x8005A41D) fills them in the set-up:

| Field | Content | Evidence |
|---|---|---|
| +0x00 | vertex buffer of the veil, 4 vertices of 0x1C bytes | 0x8005A47C |
| +0x04 | vertex buffer of the quad for the target texture, 4 of 0xC bytes | 0x8005A567 |
| +0x08, +0x0C, +0x10 | three noise textures 256 × 256, A8 | 0x8005A60E, `Textures.md` 4.8 |
| +0x18, +0x14 | two target textures 1024 × 512, format 6; the first one created goes to +0x18, the second to +0x14 | 0x8005A445, 0x8005A45B |
| +0x1C | depth surface 1024 × 512, format 0x2E | 0x8005A471 |
| +0x20 | combiner block from 0x80066598 | 0x8005A5F1 |
| +0x24 | program object from 0x80066690 with the list (0x20000000, 0x40320000, 0x40220001, 0x40220002) | 0x8005A604 |

The list puts stream 0 on v0 (three float32), v1 and v2 (two float32 each;
`GPU.md` 3).

In the tear-down `fx_destroy` (0x80059B38) releases the resources +0x00 to
+0x1C (0x800409F0), then the block +0x20 (0x80040ED0) and the program +0x24
(0x80040320), and sets every field to 0. `fx_setup` calls it at its start as
well.

Vertices of the veil (+0x00), position (x, y, z), u, v, then two further
values p, q (0x8005A48F–0x8005A558):

| i | x, y, z | u, v | p, q |
|---|---|---|---|
| 0 | −1, −1, 1 | 0, 1 | f32((2v − 1) · −1.875), f32((2u − 1) · −2.5) |
| 1 | −1, 1, 1 | 0, 0 | likewise |
| 2 | 1, 1, 1 | 1, 0 | likewise |
| 3 | 1, −1, 1 | 1, 1 | likewise |

−1 at 0x80012894, 2 at 0x80010A24, −1.875 at 0x8001295C, −2.5 at
0x80012960. p comes from v, q from u. The quad +0x04 has the same four
positions without further values (0x8005A57A–0x8005A5CE).

## 2. When

`anim_update` (0x800571AD) calls `fx_update` (0x8005A618 → 0x8005A194) if
`this+0xD` and `this+0x181D` are set (0x800575D8). `this+0x181D` is set to 1
by the constructor (0x80056B8B); it was 1 in all runs. `this+0xD` is set by
the camera (`Camera.md` 5.5).

`anim_draw` (0x80056D0B) calls `fx_draw` (0x80059BCD) last before the frame
is finished, if (0x80056D75–0x80056DA8)

```
this+0xD  and  this+0x181D  and  (x20 > 0  or  t < 0.6)
```

with 0x80012880 = 0 and 0x80065E60 = 0.6. Before that, if `this+0xE` is set,
`anim_draw` clears depth and stencil (mask 3, 0x80056DC7) in case the veil
is drawn or `this+0xD` is set (0x80056DB2–0x80056DB9; the veil presupposes
`this+0xD`, so only `this+0xD` counts), and draws the logo. `fx_draw` gets
`this+0xE` as its argument.

Measured (`c8_main2`): at t = 1.019, x20 = 0; the quad into the target
texture is drawn, the veil is not. From t = 5.985 on `this+0xD` is cleared,
and there is neither the one nor the other.

## 3. The target texture (0x8005A194)

`fx_render_offscreen` draws into the target texture `fx`+0x18 with the depth
surface `fx`+0x1C. It holds for the whole frame; `fx_draw` then swaps +0x14
and +0x18 and reads the one just drawn. The swap happens only if `fx_draw`
runs (section 2): in frames without the veil the target stays the same.
Which of the two textures a frame has as its target therefore depends on the
number of calls of `fx_draw` so far: even, the one created at 0x8005A445,
odd, the other. Measured in all eleven frames of `c8_main2` with a target
texture (`core/test/test_k6.c`). The vector file calls the texture created
at 0x8005A445 "fx_target_14" and the other "fx_target_18"; the names
are the wrong way round with respect to the fields in which the
textures lie at first.

```
remember the old colour and depth target (0x8003EE20, 0x8003EE40)
target = surface of fx+0x18, depth = fx+0x1C              (0x800405D0, 0x8003E920)
clear: mask 0xF3, colour 0xFFFF0000, depth 1.0            (0x8005A1E3)
alpha test on, comparison 0x206 (greater or equal), reference 1
blending on, source 1, destination 0
program anim+0x960, block anim+0x950                      (GPU.md 3.3, 4.3)
```

Then the constants. With D = `anim_object` + 0x6A8 (distance of the eye from
the centre of the blob), A = `camera` + 0x4C (aspect factor) and 0.005 at
0x8006674C:

```
kx = f32(D · 0.005),   ky = f32((A · D) · 0.005)
(px, py) = picture position of the origin:  (0, 0, 0, 1) · camera_view_proj,  times f32(1 / w)
c[112] = (1, 1, 1, 1)
c[113] = (0, 0, 0, 1)
c[114] = (kx, ky, 0, 1)
c[115] = (f32(0.5 − px · kx), f32(0.5 − py · ky), 0.5, 0)        (0x8005A33F)
c[96..99] = rows (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 1, 0)   (0x8005A35E)
```

`camera_view_proj` is 0x8005B06A, `vec4_mul_mat4` 0x80059A2E, `vec3_scale`
0x80058196. The quad +0x04 goes as a fan with 4 vertices (0x8005A373, step
U2 in `FrameOrder.md`). With program 3.3, r = (x, y, 1, 1) then, the colour
is 1 and the texture coordinate

```
t0 = (0.5 + (x − px) · kx,  0.5 + (y − py) · ky,  0.5)
```

The block turns that (`GPU.md` 4.3), with t̂ = 2t − 1, into

```
d   = (2kx (x − px))² + (2ky (y − py))²
rgb = (1 − d)²,   d clamped to [0, 1] through the combiners' mapping 1 − x
```

that is a disc around the picture position of the origin whose radius
shrinks with 1/D. The clamping is that of xemu (`psh.c`, mapping
"unsigned invert").

After that into the depth (0x8005A378–0x8005A3CF):

```
depth write on (0x8003CC60 with SET_DEPTH_MASK 1), depth test on (0x8003D910)
c[112] = (k, k, k, 1),   k = f32(1 / f32(40 + 40))                 (40 at 0x80066750)
c[113] = (o, o, o, 1),   o = f32(40 − D)
chamber_draw_depth (0x8005FDD6)
```

`chamber_draw_depth` draws the chamber through `prim_group_draw`, which
overwrites c[96..99] per part with its matrix (`Chamber.md` 7; step U3).
c[114] and c[115] stay; so every pixel of the chamber carries the colour

```
rgb = max((r.z + 40 − D) / 80, 0) · (1 − d)²
```

with r.z the depth after the projection. Finally program and block 0, clear
depth and stencil (mask 3), old target back, release the three remembered
surfaces (0x800409F0).

## 4. The veil (0x80059BCD to 0x80059F03)

```
program fx+0x24, block fx+0x20
swap fx+0x14 and fx+0x18;  stage 0 = new fx+0x14  (the one just drawn)
stage 0: states 0xE = 2, 0xD = 2, 0xF = 0, 0xA = 3, 0xB = 3     (0x8003CD10)
stage i = 1, 2, 3: noise texture fx+4+4i, states 0xE = 2, 0xD = 2, 0xF = 0, 0xA = 1, 0xB = 1
alpha test on, reference 1;  depth comparison 0x207 (always), with argument ≠ 0 0x202 (equal)
blending on, source 0x302 (source alpha), destination 1;  no depth write
```

The states of the stages go into the stream as `SET_TEXTURE_*` on drawing;
the values per stage are in `vectors/c8_gpu.json` (`kinds`, `state`).
`fx_draw` does not set the comparison of the alpha test; it stays 0x206 from
section 3 (measured).

The two colours to 0x800410B0 (0x80059DF0), constant 0 to K0, 1 to K1
(`GPU.md` 4.2):

```
g  = x20 · 0.7 − 0.1,  below 0 → 0                       (0x80012958, 0x80012888)
K0 = (0, f32(g), 0, 1)                                    (0 at 0x80012880)
T  = ((8 − 2) − 0.8) − 0.5                                (0x8006673C, 0x80066740, 0x80066744, 0x80010A18)
f  = clamp((t − (T + 0.3)) · 4, 0, 1) · 0.75              (0x800128FC, 0x80012900, 0x80012898)
K1 = (f32(0.625 f), f32(f), f32(0.4 f), 0)                (0x80012954, 0x800128AC)
```

T is 4.7 in x87 format. K1 therefore rises from t = 5.0 to 5.25 from 0 to
(0.47, 0.75, 0.3): a bright flash at the end of the chamber. The clamp takes
1 for values above 1 and for NaN, 0 for values below 0
(0x80059D9A–0x80059DC5).

The texture coordinates of the three noise layers, c[96..101] (0x80059EE8),
with az = `anim_object` + 0x698 and el = + 0x6A0 (angles of the orbit,
`Camera.md` 6):

```
X = (D · 0.005) · 0.5,   Y = (A · D) · 0.005
c6c = f32(py · Y · 1.875),   c70 = f32(px · X · 2.5)          (0x80012950, 0x8001294C)
for i = 2, 1, 0:
    k_i = (i · 1/3 − 0.2) · 0.6                                (0x80012948, 0x800128B0, 0x80012930)
    offset  (f32(c6c − el · k_i), f32(az · k_i − c70), 0, 0)
    scale   (f32(−Y), f32(−X), 1, 1)
```

Pair i = 2 goes to c[96], c[97], i = 1 to c[98], c[99], i = 0 to c[100],
c[101]. Program 3.6 computes `oT1 = (p, q) * c[97] + c[96]` and so on: stage
1 gets the layer with k = 0.28, stage 2 0.08, stage 3 −0.12. The layers are
of equal size but glide at different speeds with the orbit of the camera.
Because all three textures contain layer 0 (`Textures.md` 4.8), they differ
in these coordinates only.

Drawn is the quad +0x00, stride 0x1C, a fan with 4 vertices (0x80059EFE,
step D4). Program 3.6 passes the position through; t0 = (u, v) reads the
target texture at the same place on the screen. The block:

```
n   = t1.a + t2.a + t3.a
rgb = t0 · (n · K0) + K1,   a = 1
```

With source alpha 1 and destination 1 that adds to the picture. With the
comparison "equal" (during the logo) the surface with z = 1 hits only pixels
whose depth still stands at the cleared value 1, hence not the logo.

After that block, program and the textures of all four stages to 0
(0x80059F03–0x80059F21).

## 5. The glow (0x80059F23 to 0x8005A100)

```
G = clamp((t − T) · 3.3333, 0, 1)                         (0x80012944)
if t < 0.6:                                               (0x80066748)
    h = 0.6 · 0.2
    G = t / h,  if t < h;  otherwise 1 − (t − h) / 0.6     (1 at 0x8001288C)
G = f32(G)
n = trunc(G · 255) (0x80055F8C), to [0, 255];  a = min(2n, 0xC4)
if a = 0: no glow
```

Up to t = 0.12 G rises to 1, then falls to 0.2 by t = 0.6; from 0.6 on it is
0 until t = 4.7 and rises to 1 again by t = 5.0. a is at most 0xC4.

```
factor colour = a · 2²⁴ + 0xA0FF60                        (0x8003CED0)
stage 0: states 2 = 3, 3 = 2, 0 = 4, 6 = 3, 7 = 2, 4 = 4;  stage 1: 0 = 1
s = f32(0.33 / G);  for t < 0.6:  s = f32((D · 0.6) / (t · 8))   (0x80012940, 0x8001293C)
slot 6 = diag(1/s, 1/s, 0) with element [3][2] = 1;  slots 0, 1 = identity matrix   (0x8003E6C0)
stage 0 = texture anim+0x944 (falloff, Textures.md 4.5);  fixed function 0x202
fan with 4 vertices from the quad +0x00 (0x8005A100)
```

The library forms the combiners from the stages: colour K0 · t0, alpha
K0.a · t0.a (`GPU.md` 5). In the stream 1/s stands at
`SET_MODEL_VIEW_MATRIX` (methods 0x480 and 0x494, measured): the quad is
stretched around the picture centre by the factor 1/s, from t = 5.0 on
therefore by 3.03. The remaining states are those of the veil.

Finally (0x8005A105–0x8005A140): depth write on, destination of the blending
0x303, depth comparison 0x203.

## 6. Measurement

`vectors_c8.py` (`check_effects`) computes, in every recorded frame, the
values after this text from t, x20, D, A, az, el and the model of the camera
(`chamber_model.camera`) and compares them with the stream:

| Check | Call | Frames | equal |
|---|---|---|---|
| c[112..115] for the quad | 0x8005A33F | 11 | 11 |
| c[96..99] for the quad | 0x8005A35E | 11 | 11 |
| c[112], c[113] for the depth | 0x8005A3C5 | 11 | 11 |
| K0, K1 packed and `SET_EYE_VECTOR` | 0x80059DF0 | 10 | 10 |
| c[96..101] of the noise layers | 0x80059EE8 | 10 | 10 |
| factor colour of the glow | 0x80059FF9 | 6 | 6 |
| 1/s in the stream | 0x8005A100 | 6 | 6 |

Together 65 of 65, word for word each. The glow is missing in exactly the
frames in which the model delivers a = 0 (t = 2.0 to 4.6). The states of the
steps U2, U3 and D4 are in `vectors/c8_gpu.json`, `kinds`, and as an
overview in `FrameOrder.md` 5.

The core computes D, the angles and the camera itself
(`Camera.md` 6) and meets the same values with them: in
`core/test/check_k6.py` all 592 constants of the steps U2 and D4, the 20
packed colours, the six factor colours and the six matrices of the glow in
the stream, bit for bit. Two places of this text are not evidenced by the
runs in this, only by the disassembly: s from the rounded G, and c[115] from
the unrounded products. With the other reading in each case, no value
changes in the recorded frames.
