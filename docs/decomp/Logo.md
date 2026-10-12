# Logo, lettering and trade marks

The final picture of the animation: a three-dimensional X, below it the
lettering "XBOX" and twice the mark "TM". Everything belongs to the
sub-object at `anim_object` + 0x57C = **0x8006F5CC** (`logo` in what
follows). The time t is in `Timing.md`, the camera and the frame F in
`Camera.md` 5.3.

Evidence: runs `c7_main` and `c7_b` (`tools/xemu/probe_c7.py`), model
`tools/xemu/logo_model.py`, comparison `tools/xemu/vectors_c7.py`, vector
file `vectors/c7_logo.json`, data `data/logo/` from `tools/extract/logo.py`.
The model is written after this text and gets from the guest only t of every
frame, for the matrix of mesh D also frame, view and projection. In both
runs all seven meshes in guest memory agree bit-exactly with `data/logo/`,
the state of the logo in all 688 frames and each of the 119 draw calls of
the logo in the command stream of the GPU: primitive, vertex format, number
of indices, for mesh D all nine constants (section 8).

Coordinates and matrices as in `Chamber.md`: z up, row vectors, translation
in row 3.

## 1. Sequence

| When | Function | What |
|---|---|---|
| set-up (0x8005768C) | 0x8005BBEE `logo_setup` | unpack (0x8005B4D1), buffers for the meshes A–D (0x8005B712), E (0x8005B934), Q1 and Q2 (0x8005B9A8), two GPU objects |
| every frame, if `this+0xE` | 0x8005C4A5 `logo_update` (t, dt) | position of the lettering, opacity of the TM marks; from t = 6 the texture (0x8005C366) |
| every frame, if `this+0xE` | 0x8005BC63 `logo_draw` (`this+0x1774`, 0) | meshes A–D in space or as a texture, plus lettering and TM |
| tear-down (0x80056E03) | 0x8005BACE `logo_destroy` | twelve GPU resources, 14 memory blocks, two GPU objects |

`this+0xE` is set by the camera as soon as the path reaches its own keys
(`Camera.md` 5.5); for path 0 at t ≈ 5.225. `anim_update` calls
`logo_update` with t and dt (0x80057602–0x80057617, check of `this+0xE` at
0x800575FB); the function reads only t (`ret 8`, only `[ebp+8]`).
`anim_draw` calls `logo_draw` after chamber and blob and before the veil
(0x80056DAD–0x80056DDA). Before that it clears a second time with mask 3 and
colour 0xFF000000 (0x80056DBB–0x80056DC7) if `this+0xD` or the switch of the
veil is set; mask 3 means depth and stencil (`GPU.md` 2.1).

`this+0x1774` is `campath_object` + 0x1E0, the frame F from `Camera.md` 5.3
with `d · ρ` as row 3 (0x8005A9CD–0x8005AA0F). It arises once when the path
is chosen and stays fixed after that.

## 2. Data in the ROM

### 2.1 Position

All data of the logo lies without gaps at 0x80071008–0x800743CF, directly
behind the path table (`Camera.md` 5.1) and before the tracks of the chamber
(`Chamber.md` 2). Between the lists stand only zero bytes for alignment (2
to 7 bytes, in `logo.json` under `padding`). The extractor checks that every
other byte belongs to a list.

| Mesh | Vertices | Count | Indices | Count | Triangles | What can be seen |
|---|---|---|---|---|---|---|
| A | 0x80072AA0 | 0xD2 (210) | 0x800732D8 | 0x150 (336) | 112 | band along the outline of the X, 4.6 deep in y |
| B | 0x800735F0 | 0x4D (77) | 0x800738F8 | 0xE1 (225) | 75 | plate in the plane of the band with an X-shaped opening |
| C | 0x80073428 | 0x22 (34) | 0x80073580 | 0x69 (105) | 35 | triangle of the same plane above the middle (z 8 to 84) |
| D | 0x80071008 | 0x1F5 (501) | 0x800723A0 | 0x3F0 (1008) | 336 | the X, extruded from y = −127.5 backwards to y = 180 and widening on the way |
| Q1 | 0x800739DC | 4 | 0x80073A04 | 6 | 2 | square 3.55 × 3.55 at the top right of the X, plane y = −132.22 |
| Q2 | 0x80073A0C | 4 | 0x80073A34 | 6 | 2 | square 3.0 × 3.0 at the bottom right under the X, beside the destination of the lettering, plane y = −132.22 |
| E | 0x80073A40 | 0x9C (156) | 0x80073DE8 | 0x1CE (462) | 154 | lettering "XBOX" in the plane z = −0.003 |

Addresses and counts are immediates in `logo_decode` (0x8005B4D2–
0x8005B700); the counts are repeated by `logo_build_meshes` (0x8005B734,
0x8005B73B, 0x8005B7A9, 0x8005B7B0, 0x8005B832, 0x8005B839, 0x8005B8EB,
0x8005B8F2), `logo_build_mesh_e` (0x8005B952, 0x8005B959) and
`logo_build_quads` (0x8005B9C9, 0x8005B9D3). What can be seen is how it
looks in the view of the meshes from `data/logo/logo.json` and in the final
picture of the runs; the meshes A to D together form the X. In every list
the largest index is the vertex count − 1.

### 2.2 Vertices

The meshes A–D, Q1 and Q2 have five int16 per vertex, unpacked by
`decode_vertex5` (0x8005B3D4) with four constants:

```
x, y, z = f32(i · 0.009876 + 161.71536)     // 0x80012984 = 0x3C21CEF2, 0x80012988 = 0x4321B722
u, v    = f32(i · 0.000058 + 0.947661)      // 0x8001298C = 0x38734507, 0x80012990 = 0x3F7299E9
```

Mesh E has three int16 per vertex, unpacked by `decode_i16_scaled`
(0x8005B398):

```
x, y, z = f32(i · 0.002508 + 41.06537)      // 0x80066974 = 0x3B245D42, 0x80066978 = 0x422442F0
```

The computation uses `fild`, `fmul m32`, `fadd m32`, `fstp m32` at a 53-bit
mantissa (`Timing.md` 4.2): the product is exact, the sum is rounded once to
double and then to float32. Factor and offset hold for all three axes
together.

### 2.3 Indices

`decode_delta16_tilde` (0x8005B473) turns a byte sequence into n 16-bit
indices:

```
idx[0] = sign_extend(b[0])
for i = 1 .. n−1:
    if b[p] = 0x7E:   idx[i] = idx[i−1] + (b[p+1] << 8 | b[p+2]);  p += 3
    otherwise:        idx[i] = idx[i−1] + sign_extend(b[p]);       p += 1
```

with overflow at 16 bits (0x8005B486–0x8005B4C9). The first byte is not
checked for 0x7E. The number of bytes read is in `logo.json`
(`index_bytes`); only mesh D needs 2-byte steps (1790 bytes for 1008
indices).

### 2.4 Keys and texture

- **0x80073FB8**: two points of three float32, the position of the lettering
  at the start and at the end of its motion (section 4):
  (−0.229403, −267.65042, −103.04042) and (−0.229403, −141.05376, −54.43962).
- **0x80073FD0**: 16 × 16 texels of 4 bytes, up to 0x800743CF. Read as B, G,
  R, A it shows "TM" in green on a transparent ground (alpha only 0 and 255;
  `data/logo/quad_texture.png`). In the guest the same texels lie in Z
  order: texel (x, y) at place `interleave(x, y)`, bit i of x on bit 2i, bit
  i of y on bit 2i + 1 (measured, section 8). It is copied by 0x800AAB46
  with 16, 16 and 4 (0x8005BAA9–0x8005BAB9) into a texture 16 × 16 that
  0x8003E630 creates with format 6 (A8R8G8B8) (0x8005BA83–0x8005BA95); both
  functions and the format in `Textures.md` 2, 3 and 4.9.

## 3. Set-up

### 3.1 The object

0x110 bytes from 0x8006F5CC. The meshes occupy sets of vertex buffer, index
list, vertex count, index count:

| Offset | Content |
|---|---|
| +0x00 … +0x0C | mesh A: buffer, indices, 210, 336 |
| +0x10 | texture of A: `texgen_gradient(16, 128, 0xFF000100, 0xFF4B9B4B)` (0x8005B771–0x8005B789) |
| +0x14 … +0x20 | mesh B: buffer, indices, 77, 225 |
| +0x24 | texture of B: `texgen_radial(256, 6, 0, 0.5, 0.5)` (0x8005B7CD–0x8005B80C) |
| +0x28 … +0x34 | mesh C: buffer, indices, 34, 105 |
| +0x38 | texture of C: `texgen_gradient(16, 128, 0xFF000000, 0xFFFFFFFF)` (0x8005B8BF–0x8005B8CD) |
| +0x3C … +0x48 | mesh D: buffer, indices, 501, 1008 |
| +0x4C | byte: draw TM and lettering (section 4) |
| +0x50 … +0x5C | mesh E: buffer, indices, 156, 462 |
| +0x60 | matrix M of the lettering, the identity matrix in the set-up (0x8005B997); row 3 (+0x90) is its position |
| +0xA0 … +0xAC | Q1: buffer, indices, 4, 6 |
| +0xB0 … +0xBC | Q2: buffer, indices, 4, 6 |
| +0xC0 | texture "TM" 16 × 16 |
| +0xC4 | opacity of the TM marks, 0 … 255 |
| +0xC8 | GPU object from the data at 0x80066770 (`gpu_create_from_data`, 0x8005BC1A) |
| +0xCC | GPU object from the data at 0x80066868 with the list (0x20000000, 0x40320000, 0x40220001, 0xFFFFFFFF) (`gpu_create_with_decl`, 0x8005BC34) |
| +0xD0 | texture 1024 × 1024 with the drawn logo, 0 until the first drawing (section 6) |
| +0xD4 | byte: phase with which the texture was drawn last |
| +0xD8 … +0x10C | the unpacked lists: indices and vertices of A, B, C, D, then Q2 (+0xF8/+0xFC), Q1 (+0x100/+0x104), E (+0x108/+0x10C) |

The fields +0xC8 and +0xCC are described by `GPU.md` (3.7, 4.3), the texture
generators by `Textures.md` (4.3, 4.4). Here they are only set with the
arguments named. +0x4C is set to 0 by `logo_build_mesh_e` (0x8005B99F);
+0xC4 is set to 0 by `logo_build_quads` (0x8005BAC1). +0xD0 and +0xD4 are
zero until t = 6 (measured in every frame, section 8).

### 3.2 Buffers

Every mesh gets a vertex buffer (0x8003F570, then 0x8003F5D0 for the
pointer) into which the unpacked vertices are copied unchanged
(`rep movsd`). Two deviations:

- **Mesh C, texture coordinate v.** After the copying, `logo_build_meshes`
  sets every v with `|f32(v − 1)| ≤ 0.01` to −1 (0x8005B87F–0x8005B8BD;
  constants 0x8001288C = 1, 0x80012980 = 0.01, 0x80012894 = −1; `fabs32`
  0x80057F89, comparison `test ah, 0x41` / `jp`, hence also on equality).
  That hits 10 of the 34 vertices, all with v = 1.0000930.
- **Size of the buffer of C.** What is requested is the vertex count of B
  times 0x14 (0x8005B81D–0x8005B831: `[ebx+0x1C]`), that is 0x604 bytes
  instead of 0x2A8. Only the 34 vertices of C are written.

The format word is 0x102 for buffers with five values per vertex and 2 for
mesh E with three. Both go to 0x80040440: fixed function with position and
one pair of texture coordinates, or position only (`GPU.md` 2.1). The index
lists stay in the memory of the object and go along on drawing.

## 4. Updating

`logo_update` (0x8005C4A5) with T = f32(8 − 2) = 6 (0x80066964,
0x80066968), S = 0.25 (0x80066970):

```
if t < T:   tm = 0;  alpha = 0                                         // +0x4C, +0xC4
otherwise:
    tm = 1
    inv = 1 / S                                     (register; stored f32)
    F = f32((t − T) · inv);  k = cvttss2si(F)
    if F ≤ 0:   pos = key 0
    if F ≥ 1:   pos = key 1
    otherwise:  f = F − k;  pos = f32((1 − f) · key[k] + f · key[k+1])   per axis
    alpha = f32(clamp(f32((t − T) · f32(inv)), 0, 1) · 255)            // 0x80012908
    logo_render_to_texture()
```

The comparisons in detail: t < T also holds for unordered values
(`test ah, 1`, 0x8005C4C3–0x8005C4C8). F ≤ 0 tests `test ah, 0x41` with `jp`
(0x8005C4F5–0x8005C503); a NaN would fall through there to the test F < 1
(`test ah, 1`, 0x8005C537–0x8005C545). Key 0 is read by 0x8005C508–
0x8005C52C, key 1 by 0x8005C54A–0x8005C565, the blend reads both through the
index 12k (0x8005C56D–0x8005C5C9). The clamping range [0, 1] of the opacity
(0x8005C5E6–0x8005C623) and of p and q in section 5.1 (0x8005BFAE–
0x8005C03D) lets a NaN through unchanged. For finite t these cases change
nothing; they stand here because the rebuild adopts them like this.

pos is in +0x90 … +0x98, that is in row 3 of M. Before t = 6, M is the
identity matrix from the set-up, pos therefore (0, 0, 0); after that the
lettering glides from key 0 to key 1 in 0.25 s and stays there. Evidence:
0x8005C4AB–0x8005C64A; the sums of the blend run in the register
(0x8005C583–0x8005C5C9), only the result is stored.

## 5. Drawing

`logo_draw` (0x8005BC63) gets a world matrix W (always F, `this+0x1774` or
0x800707C4, the same address) and a byte r (1 only when drawing into the
texture). At its start it gives 0x102 to 0x80040440 and 0 to 0x80040EF0
(0x8005BC71–0x8005BC81); 0x102 is the same value as when creating the
buffers with five values per vertex: fixed function with position and one
pair of texture coordinates, combiners from the stages (`GPU.md` 2.1, 5).

### 5.1 The X in space (r = 1 or t < 6)

Condition 0x8005BC86–0x8005BCA6: r ≠ 0, or the unrounded 8 − 2 is greater
than t from 0x8006F060 (`fcomp`, `test ah, 0x41`, `je`); for NaN `logo_draw`
draws the picture. Matrices to the graphics code: slot 6 = W, slot 0 = view,
slot 1 = projection (0x8006F834, 0x8006F874; what becomes of them in the
stream: `GPU.md` 2.1). Then one after the other, each with primitive 5
(triangles) and its own index list:

1. A with texture +0x10,
2. B with texture +0x24,
3. C with texture +0x38,
4. D with the GPU objects +0xCC and +0xC8 and the vertex constants 0 to 8.

Constants 0–3 are `transpose(W · (view · projection))` (0x8005C05C–
0x8005C087; `camera_view_proj` 0x8005B06A, `mat4_mul` 0x800566CF,
`mat4_transpose` 0x8005808F). Constants 4–8 depend on t
(0x8005BF6D–0x8005C16C):

```
t6 = 8 − 2;  t0 = (t6 − 0.8) − 0.5 = 4.7            // 0x8006696C, 0x80010A18
s  = (t − t0) · (1 / (t6 − t0))
l  = (s + s) − 1                                     // register; f32(l) for the comparison only
p  = f32(clamp(−l, 0, 1))
q  = f32(clamp((1 − p) − clamp(l, 0, 1), 0, 1))
if f32(l) < 0:   c4 = c5 = (a, 1, b, 1);  c6 = c7 = (a, 1, c, 1);  c8 = (p, p, p, 1)
otherwise:       c4 = c5 = (a, 1, c, 1);  c6 = d;  c7 = e;          c8 = (q, q, q, 1)
```

with a = 0.81568 (0x8001299C), b = 0.5921 (0x80012998), c = 0.5294
(0x80012994), d = (0x3F4BC6A8, 0x3F5FDF3B, 0x3B7F9724, 1) and
e = (0x3E04816F, 0x3ED566CF, 0x3DB88659, 1) as immediates
(0x8005C101–0x8005C131; the six words are at 0x8005C104, 0x8005C10E,
0x8005C117, 0x8005C121, 0x8005C12A and 0x8005C134). The choice of the branch
compares f32(l) from [ebp+0x60] with 0 from 0x80012880 (`test ah, 5`, `jp`,
0x8005C08C–0x8005C0A8): only l < 0 takes the first branch, NaN the second.
c8 therefore falls from 1 to 0 from t = 4.7 to 5.35, then jumps to 1 and
falls to 0 again by t = 6; with the jump c4 to c7 change. From t = 5.225 on
the logo can be seen, c8 begins at 0.19 there. Program +0xCC mixes with v
between c5 and c4 and between c7 and c6, and with c8 between both results
(`GPU.md` 3.7); block +0xC8 outputs the colour unchanged (`GPU.md` 4.3).

### 5.2 The X as a picture (r = 0 and t ≥ 6)

0x8005BCAC–0x8005BE46: set stage states, all three slots to the identity
matrix, set the texture +0xD0 and draw the quad of the veil: vertex buffer
`*0x8007083C` (field 0 of the veil object `anim_object` + 0x17EC), value
0x202 to 0x80040440, stride 0x1C, primitive 7 (fan) with four vertices
(0x8005BD92–0x8005BDAF). After that W, view and projection into the slots
again.

### 5.3 Lettering and TM (tm = 1 and phase 0)

If +0x4C is set and +0xD4 is zero (0x8005C181–0x8005C193), that is when
drawing to the screen for 6 ≤ t < 6.25 and when drawing into the texture
from t = 6.25 (section 6):

1. State 0x40304 = 1, 0x40344 = 0x302, 0x40348 = 0x303 to 0x8003CC60
   (0x8005C1C6–0x8005C20A; in the stream `SET_BLEND_ENABLE` 1,
   `SET_BLEND_FUNC_SFACTOR` 0x302, `SET_BLEND_FUNC_DFACTOR` 0x303), colour
   `cvttss2si(alpha) << 24` to 0x8003CED0 (0x8005C1B2–0x8005C20A; in the
   stream `SET_COMBINER_FACTOR0/1`), texture +0xC0, then Q1 and Q2 each with
   primitive 5. The TM marks fade in like this with the opacity from
   section 4.
2. State 0x40304 = 0 (in the stream `SET_BLEND_ENABLE` 0), colour 0xFF62CA13
   (`push` at 0x8005C2C3, the word at 0x8005C2C4; in the stream
   `SET_COMBINER_FACTOR0/1`), slot 6 = `rot_x(f32(π · 0.5)) · M · W`
   (0x8005C2DF–0x8005C317; `mat4_rot_x` 0x800570A3 with `fsincos`, π from
   0x8006695C), value 2 to 0x80040440, mesh E with primitive 5.

`rot_x(φ)` has the rows (1, 0, 0, 0), (0, cos φ, sin φ, 0),
(0, −sin φ, cos φ, 0), (0, 0, 0, 1). `fsincos` stores cos and sin as
float32, −sin is the sign of the stored sin (`fchs`,
0x800570C1–0x80057102). With φ = π/2 it stands the lettering upright out of
the plane z ≈ 0: its y axis points to z afterwards, that is up. The position
in M then moves it from y = −267.65 to −141.05 forwards towards the X (Q1
and Q2 lie at y = −132.22) and from z = −103.04 to −54.44 upwards.

At the end 0x901 goes to 0x8003CDF0 (0x8005C352), at the start of the 3D
branch 0x900 (0x8005BEDD): 0x900 leaves out the front faces, 0x901 the back
faces (`GPU.md` 2.1).

## 6. Drawing into the texture

`logo_render_to_texture` (0x8005C366) runs from `logo_update` for t ≥ 6:

```
phase = (6 + 0.25 ≤ t)                                   // 0x8005C36C–0x8005C395
if +0xD0 = 0 or phase ≠ +0xD4:
    create the target 1024 × 1024 (0x8003E690, 0x8003E6B0), texture +0xD0 the first time (0x8003E630)
    clear to black (mask 0xF3, 0xFF000000, 1.0)
    old = +0x4C;  +0x4C = phase;  logo_draw(0x800707C4, 1);  +0x4C = old
    copy into +0xD0 (0x8003EBC0);  +0xD4 = phase;  old target back
```

That happens exactly twice: in the first frame with t ≥ 6 (phase 0: the X
only) and in the first frame with t ≥ 6.25 (phase 1: X, TM and lettering,
opacity 255). In between `logo_draw` draws the picture of the texture and
over it TM and lettering with rising opacity; after that only the picture.
From t = 6.25 on the final picture is therefore a single textured quad.
Accordingly, frame 210 of `c2final_A` contains only one fan with four
vertices through `DRAW_ARRAYS`, plus the block with inline vertices that
stands at the start of every frame (the finishing of the frame, `GPU.md` 7).

## 7. Schedule for path 0

| t | What |
|---|---|
| ≈ 5.225 | `this+0xE` on: the X appears in space, c8 = 0.19 |
| 5.35 | c8 reaches 0, jumps to 1, other constants c4–c7 |
| ≈ 5.936 | chamber and blob end (`Camera.md` 5.5) |
| 6.0 | X into the texture; picture of the texture, over it TM and lettering, opacity and position rise |
| 6.25 | lettering at its destination, opacity 255; X, TM and lettering into the texture; from now on only the picture |
| 8.0 | end (`Timing.md`) |

## 8. Measurement

`probe_c7.py` stops in every frame at `anim_draw` (0x80056D0B) and reads t,
`this+0xE`, the whole logo, F, view and projection; from a selectable t on
it switches on the trace of the GPU methods. A second breakpoint at
0x8005C43F records every drawing into the texture. Once it reads all buffers
of the logo through their physical address (word 1 of the resource).

| Run | Command | Frames | Frames with the logo in space | into the texture |
|---|---|---|---|---|
| `c7_main` | `probe_c7.py --name c7_main --trace-from 5.0` | 354 | 7 (t = 5.261 … 5.951) | t = 6.136, 6.253 |
| `c7_b` | `probe_c7.py --name c7_b --trace-from 5.2` | 334 | 6 (t = 5.276 … 5.930) | t = 6.115, 6.339 |

The breakpoints and the trace slow the guest down; the intervals of t are
therefore large (0.1 to 0.18 around t = 5.5).

`vectors_c7.py` compares:

1. **Buffers**: vertex buffer and index list of every mesh in the guest
   equal `logo_model.vertex_buffer` and `index_buffer` from
   `data/logo/logo.json`, counts equal; the texture "TM" equals the texels
   from the ROM in Z order. Both runs: everything equal.
2. **State**: +0x4C, +0x90 … +0x98, +0xC4, +0xD4 and whether +0xD0 is set,
   in every frame against the model over the same sequence of t; the frames
   with drawing into the texture against those of the model. 354 of 354 and
   334 of 334 equal, texture frames equal.
3. **Command stream**: every block `SET_BEGIN_END` … `END` whose vertex
   array 0 lies on a buffer of the logo.

| Mesh | Format in the stream (stride, size, type) | Indices in the stream | Indices in `data/` | Calls `c7_main` | Calls `c7_b` |
|---|---|---|---|---|---|
| A | 0x14, 3, float | 336 | 336 | 9 | 8 |
| B | 0x14, 3, float | 225 | 225 | 9 | 8 |
| C | 0x14, 3, float | 105 | 105 | 9 | 8 |
| D | 0x14, 3, float | 1008 | 1008 | 9 | 8 |
| Q1 | 0x14, 3, float | 6 | 6 | 15 | 2 |
| Q2 | 0x14, 3, float | 6 | 6 | 15 | 2 |
| E | 0xC, 3, float | 462 | 462 | 15 | 2 |

Every call has primitive 5 and reads the vertices through array 0 at the
physical address of the buffer whose content point 1 has checked. The stream
counts the indices like this: every word of `ARRAY_ELEMENT16` carries two,
every `ARRAY_ELEMENT32` one. xemu writes a sequence of `ARRAY_ELEMENT16` as
one line `* n` with n = words − 1 (`pgraph.c`, `pgraph_method_log`: the
counter begins with 0 at the first word). With an odd number the last index
comes as `ARRAY_ELEMENT32` (B, C). The fields of the format after
`nv2a_regs.h` (type bits 0–3, size 4–7, stride from bit 8), the primitives 5
and 7 after `NV097_SET_BEGIN_END_OP_*` (`provenance.md`).

In addition, at every call of D, the constants 0–8 (loaded from
`SET_TRANSFORM_CONSTANT_LOAD` 0x60 or 0x64) bit-identical with the model for
exactly one measured frame or drawing into the texture, in the order of the
frames, both branches of the formula included. Before Q1 and Q2
`SET_COMBINER_FACTOR0` equals the opacity of the model (in `c7_main` from
0x8A000000 to 0xFF000000), before E 0xFF62CA13. 119 of 119 calls equal.

## 9. Open (done)

All points are answered:

- GPU objects +0xC8, +0xCC: `GPU.md` 3.7 and 4.3, above in 5.1.
- Texture generators, texture "TM", target texture 1024 × 1024:
  `Textures.md` 1, 4.3, 4.4, 4.9 and 6.
- Slots, matrices in the stream, clear masks, format values, 0x900 and
  0x901: `GPU.md` 2.1; stage states: `GPU.md` 5 and `vectors/c8_gpu.json`.
- The quad of the veil: `Effects.md` 1.
