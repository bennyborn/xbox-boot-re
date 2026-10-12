# Graphics state

How the animation operates the GPU (NV2A): the way of the commands, the
graphics library in the kernel, the seven vertex programs, the seven blocks
for the combiners, the combiners that the library itself forms from the
texture stages, and the remaining states. Which draw call comes when with
which state is in `FrameOrder.md`, the textures in `Textures.md`, veil and
glow in `Effects.md`.

Evidence: runs `c8_main` and `c8_main2` (`tools/xemu/probe_c8.py`),
comparison `tools/xemu/vectors_c8.py`, vector file `vectors/c8_gpu.json`,
data `data/gpu/` from `tools/extract/gpu.py`. The vertex programs are
disassembled with the third-party tool nv2a_vsh 0.1.12 (`provenance.md`);
names and fields of the methods come from xemu (`nv2a_regs.h`, `pgraph.c`,
`psh.c`), meanings are those of xemu unless said otherwise.

Notation: `c[k]` is constant k of the transform unit as nv2a_vsh counts it;
`SET_TRANSFORM_CONSTANT_LOAD` 0x60 loads from c[96]. What `Chamber.md`,
`Blob.md` and `Logo.md` call "constant k" is c[96 + k].

## 1. The way of the commands

### 1.1 Device and push buffer

The library holds a device object at 0x8004AA80; 0x8004D610 points to it
(0x8003CB60). The first words:

| Offset | Content | Evidence |
|---|---|---|
| +0x00 | write pointer into the push buffer (virtual address) | 0x8003CC60, 0x800429B0 |
| +0x04 | limit from which room is made | 0x8003CC68, 0x80042710 |
| +0x10, +0x14 | start and end of the ring | 0x80042710 |
| +0x1C | running number of the segments | 0x80042580 |

Every library function writes commands as a head word and values at the
write pointer. The head word has the format that xemu reads in `pfifo.c`
(`pfifo_run_pusher`): method in bits 0–12, subchannel in bits 13–15 (always
0 here), number of values in bits 18–28; bit 30 means "the same method for
all values". A word with `(w & 3) = 1` is a jump.

The ring is 0x100000 bytes in size and is released in pieces of 0x10000
bytes: `device_setup` passes both sizes to 0x8003CB40 (0x80057A73,
0x80057A78); the defaults 0x80000 and 0x8000 at 0x8003CB69 and 0x8003CB7C
hold for 0 only. Measured, the write pointer in `c8_main2` ranges over
0xFF904 bytes (0x83880038 to 0x8397F93C). If the end is near, 0x80042710
writes a jump to the start of the ring (start + 1) and puts the write
pointer there. After every piece 0x80042580 writes
`BACK_END_WRITE_SEMAPHORE_RELEASE` with the running number and twice method
0x1D90 with 0; 0x800424D0 then releases the end to the GPU. In `c8_main2`
the wrap occurred five times, several times in the middle of a sequence of
indices; the GPU follows it, and the sequence goes on after the jump with a
new head word.

The sub-object +0x6AC of the animation object (`Overview.md`, sub-objects)
manages the device on the side of the animation: 0x80057A52 sets its two
doublewords to 0, 0x80057A5A sets the device up (section 2.2) and writes a
value other than 0 to 0x8007087C (0x80057B70; in `c9_post` the device
pointer 0x8004AA80), 0x80057B96 calls 0x8003E7D0 in the tear-down if +4 is
set and sets 0x8007087C to 0 again. As long as 0x8007087C is set, 0x800565F3
accepts an after-image (`Overview.md`, After-image).

### 1.2 Measuring the commands per call

`probe_c8.py` stops at each of the 510 call sites in the range of the
animation that jump into the graphics library 0x8003CB40–0x80044F40, and
reads the write pointer. The words between two stops belong to the previous
call. `vectors_c8.py` puts them together for every recorded frame and
compares them with the methods xemu executes (trace `nv2a_pgraph_method*`).
In `c8_main2` the set-up and all 20 frames agree method by method
(`vectors/c8_gpu.json`, `stream`); 1 140 108 methods together. With that,
every method in the stream is assigned to a call of the animation. Gaps of
the trace (`tools/xemu/README.md`, "Limits") are taken into account:
sequences of `ARRAY_ELEMENT16` are compared by word count; xemu writes a
method it does not handle twice (`pgraph.c`, label `unhandled`), the second
line does not count.

## 2. The graphics library

The animation calls 76 functions of the library directly; each has a row in
`README.md` (subsystem `GPU`). 44 of them ran in `c8_main2`. Of the other
32, 29 are called only by the after-image (`Overview.md`), three only by the
tear-down (0x8003E7D0, 0x80040320, 0x80040ED0); they are described
statically only. By their effect:

| Group | Functions | What appears in the stream |
|---|---|---|
| State directly | 0x8003CC60 (head word in ecx, value in edx) | exactly this pair of method and value; the animation passes head words such as 0x40304 (`SET_BLEND_ENABLE`) |
| State by number | 0x8003CC90 (number in ecx, value in edx) | nothing; numbers 0x52 to 0x73 noted until drawing. The library itself takes 0x8003CCB0 (number, value): < 0x52 a method from the table 0x80065470 (`data/gpu/gpu.json`, `render_states`), < 0x74 as 0x8003CC90, otherwise a function of its own from the table 0x800496E0 |
| Single states | 0x8003CD40 … 0x8003DC10 | one to three methods each (rows in `README.md`) |
| Texture stage | 0x8003CD10 (stage, number, value) | nothing; noted, at the next drawing as `SET_TEXTURE_*` and combiners (section 5) |
| Texture | 0x8003EFD0 (stage, texture) | `SET_TEXTURE_OFFSET`, `SET_TEXTURE_FORMAT` and control word of the stage |
| Data stream | 0x80040080 (stream, buffer, stride) | nothing; noted until drawing (`SET_VERTEX_DATA_ARRAY_*`) |
| Vertex program | 0x80040440 (program or format) | load and start the program (section 3), or fixed function |
| Constants | 0x80040520 (number, pointer, count) | `SET_TRANSFORM_CONSTANT_LOAD` 0x60 + number, then 4 · count values |
| Combiner block | 0x80040EF0 (block or 0) | the methods of the block (section 4) |
| Combiner constants | 0x800410B0 (0, pointer, count) | colours into the factor registers after the assignment of the block (section 4.2) |
| Factor colour | 0x8003CED0 (colour) | without a block: `SET_COMBINER_FACTOR0/1`, 16 values |
| Matrices | 0x8003E6C0 (slot, matrix) | nothing; on drawing with fixed function the matrices (`SET_MODEL_VIEW_MATRIX` …) |
| Target | 0x8003E920 (colour, depth) | `SET_SURFACE_*`, viewport, depth test |
| Clear | 0x80040AB0 (…, mask, colour, depth, …) | `SET_COLOR_CLEAR_VALUE`, `CLEAR_SURFACE` |
| Draw | 0x8003F620 (kind, start, count), 0x8003F6C0 (kind, count, indices) | first everything noted, then `SET_BEGIN_END`, `DRAW_ARRAYS` or `ARRAY_ELEMENT16/32`, `SET_BEGIN_END` 0 |
| Finish the frame | 0x8003E550 | section 7 |
| Create, release | 0x8003E630, 0x8003E660, 0x8003E690, 0x8003E6B0, 0x8003F570, 0x8003F5D0, 0x800409F0, 0x80040320, 0x80040ED0 | memory and resources (`Textures.md` 2) |

The kinds on drawing are those of `SET_BEGIN_END`: 5 triangles, 6 strip, 7
fan (xemu `NV097_SET_BEGIN_END_OP_*`).

### 2.2 The remaining calls

What the groups above do not name, with the call sites of the animation
(call graph from `tools/ghidra/export_graph.py`, disassembly at the sites):

| Function | Call sites | Effect |
|---|---|---|
| 0x8003D340 border colour (stage, colour) | logo 0x8005BE21, 0x8005BEAD: stage 0, 0xFF000000; chamber 0x8005F96C: stage 3, 0xFFFFFFFF | `SET_TEXTURE_BORDER_COLOR` of the stage; in the stream of `c8_main2` 26 times stage 0 = 0xFF000000, 29 times stage 3 = 0xFFFFFFFF, otherwise once each 0 for the stages 0 to 3 |
| 0x8003D910 depth test | blob, veil, logo, chamber, light view | `SET_DEPTH_TEST_ENABLE`; values per kind of drawing in section 6 |
| 0x8003EE70 viewport (x, y, w, h, zmin, zmax) | light view 0x8005FFAF with (0, 0, 0x200, 0x200, 0, 1); otherwise from the library itself (0x8003E920, 0x8003DEC0) | limits the rectangle to the size of the target (0x8003EEA3–0x8003EF6B), notes it in device+0xB00 … +0xB14 and writes viewport, scale and offset anew; in the stream `SET_WINDOW_CLIP_HORIZONTAL` 0x1FF0000 for the light view |
| 0x8003EE20, 0x8003EE40 | veil 0x8005A194 | deliver colour and depth target, to restore them afterwards (`Effects.md` 3) |
| 0x8003EBC0 copy a surface | logo 0x8005C45F | draws one surface into another (`Logo.md` 6) |
| 0x8003F130 index buffer (buffer, base) | blob 0x80058912, 0x80058A18; chamber 0x8005EE28; base always 0 | notes the buffer for the next draw calls with indices (0x8003F6C0); in the stream the indices appear as `ARRAY_ELEMENT16` |
| 0x8003F530 create a buffer with data | blob 0x80058E69, chamber 0x8005F559 | resource 0x01010001, data directly behind it; no stream (`Textures.md` 2) |
| 0x800405C0, 0x800405D0, 0x80040620, 0x80040650, 0x80041340 | texture generators, veil | description, surface and address of a level or cube face; no stream (`Textures.md` 2) |
| 0x80040770 | set-up 0x80057A5A | delivers 1 |
| 0x8003E810 | set-up 0x80057B86, four times in a row | sets device+0x24F4 to 0 and waits, through the kernel function 0x80019CDC, for the object device+0x24F0; what it triggers is not determined |

### 2.3 Not needed

On the side of the animation, 29 functions of the library are called only by
the after-image (0x800561D1), which draws nothing at start-up
(`Overview.md`, "After-image", run `c9_post`). In `README.md` they carry the
note "not needed". Three of them (0x8003D200, 0x8003D2E0, 0x8003D380) are
also called by the library itself from 0x8003D3C0; what they write there
belongs to the result in the stream (section 5).

### 2.1 Fixed function, matrices, clearing, back faces

Measured in `c8_main2` over all calls (`out/` local only, numbers here):

- **Format value to 0x80040440** (even, hence no program): bit 1 means
  position from three float32 on field 0, n · 0x100 means n pairs of float32
  as texture coordinates on the fields 9, 10, … (field numbers after xemu,
  `NV2A_VERTEX_ATTR_TEXTURE0` = 9). In the stream: 0x2 → field 0 with stride
  0xC (4393 draw calls), 0x102 → fields 0 and 9 with stride 0x14 (35), 0x202
  → fields 0, 9, 10 with stride 0x1C (14). The animation uses 0x2, 0x102,
  0x202 and 0.
- **Slots to 0x8003E6C0.** The animation puts the world matrix in slot 6,
  the view in 0 and the projection in 1 (`Logo.md` 5.1, `Effects.md` 5). On
  drawing with fixed function the library writes
  `SET_MODEL_VIEW_MATRIX` = (slot 6 · slot 0)ᵀ and
  `SET_COMPOSITE_MATRIX` = (slot 6 · slot 0 · slot 1 · B)ᵀ with the screen
  mapping B = scale (640, −240, 16777215), offset (640, 240, 0) for the
  drawing surface 1280 × 480. Evidenced in two cases with known slots: "logo
  as a picture" (all three identity: model-view = identity, composite = Bᵀ)
  and the glow (slot 6 = diag(3.0303, 3.0303, 0) with [3][2] = 1: model-view
  = this matrix, composite = it times B). The model-view is
  recomputed value by value (`core/test/check_k6.py`): in all 4442 draw
  calls with fixed function whose slots the core knows (light views, glow
  surface of the blob, logo in space and as a picture, lettering, glow) it
  equals, bit for bit, the product slot 6 · slot 0 in float32, the four
  terms of every element summed in the order k = 0, 1, 2, 3 and rounded
  after every step. The library therefore computes differently here from
  0x800566CF (`Chamber.md` 11). The composite matrix is not recomputed.
- **Clear mask to 0x80040AB0** goes unchanged as `CLEAR_SURFACE`: bit 0
  depth, bit 1 stencil, 0xF0 the four colour channels (xemu
  `NV097_CLEAR_SURFACE_*`). The animation uses 0xF3 (everything), 3 (depth
  and stencil) and 1 (depth only, light views with depth 0xFFFF).
- **0x8003CDF0** with 0x900 writes `SET_CULL_FACE_ENABLE` 1 and
  `SET_CULL_FACE` 0x404 (front), with 0x901 1 and 0x405 (back), 14 times
  each.

The library notes every state in a copy from 0x8004DA80 (number · 4); the
animation reads values there to restore them later, e.g. `blob_draw` at
0x8004DB6C (number 0x3B, blending on).

## 3. Vertex programs

Seven programs lie in the ROM as halfword 0x2078, halfword n and n
instructions of four words. `gpu_create_with_decl` (0x80056EC1 → 0x8003FF40)
creates an object from a program and an attribute list; 0x80040440 loads it
with `SET_TRANSFORM_PROGRAM` in pieces of 32 instructions from slot 0 and
starts it. Measured: each of the 93 loads in `c8_main2` sends exactly the
words of one of the seven programs from `data/gpu/gpu.json` (`objects`).

The attribute list (`gpu.json`, `attributes`) chooses the data stream s with
0x2000000s and puts type TT on register vRR with 0x40TT00RR: type 0x32 is
three float32, 0x22 two, 0x42 four (field layout as in
`SET_VERTEX_DATA_ARRAY_FORMAT`: type in bits 0–3, size in bits 4–7).

All seven end with the same two instructions:

```
MUL oPos.xyz, R12, c[58]  +  RCC R1.x, R12.w
MAD oPos.xyz, R12, R1.x, c[59]
```

R12 is the position just written. The GPU fills c[58] and c[59] from
`SET_VIEWPORT_SCALE` and `SET_VIEWPORT_OFFSET` (xemu `pgraph.c`,
`NV_IGRAPH_XF_XFCTX_VPSCL` = 0x3A, `VPOFF` = 0x3B); 0x80040440 sends both
(0x8003D4C0). With that the position goes into screen coordinates. In what
follows this ending is left out. Full listings: `data/gpu/programs.txt`.

The values, measured in the stream (run `c2final_A`, frame 150,
`methods.txt`): for the drawing surface 1280 × 480 the library sends, with a
program, `SET_VIEWPORT_SCALE` = (640, −240, 16777215, 0) (0x44200000,
0xC3700000, 0x4B7FFFFF) and `SET_VIEWPORT_OFFSET` = (640.53125, 240.53125,
0, 0) (0x44202200, 0x43708800). With fixed function the mapping is in the
composite matrix (2.1), and `SET_VIEWPORT_OFFSET` is (0.53125, 0.53125, 0,
0) (0x3F080000). In both cases the position therefore lies 0.53125 pixels
further right and further down than scale and half size alone give.
`SET_CLIP_MAX` is 16777215 (0x4B7FFFFF) here: the depth fills the 24 bits of
the depth surface (format 2 in `surface_shape.zeta_format` of the snapshot).
That the offset acts in the picture is shown in `docs/web.md` 8:
without it the depth surface from xemu coincides worse with a rebuild.

**Grid of the position in xemu.** xemu truncates the position on the screen,
after the offset, to sixteenths of a pixel, towards zero (`glsl/vsh.c`,
`roundScreenCoords`: `trunc(pos * 16) / 16`, applied to oPos.xy in
`glsl/vsh-prog.c`; the comment there names four fractional bits as the
behaviour of the hardware). 0.53125 becomes 0.5 that way: a surface over the
whole screen then also covers the pixel centres of the first column and row.
Measured in `docs/web.md` 9: with this truncation the depth surface
of a rebuild coincides pixel for pixel with that of xemu in twelve frames,
without it the veil lacks column 0 and row 0. That is behaviour of xemu; it
is not measured on the console.

### 3.1 Chamber, smooth (0x80065B60, `anim`+0x958)

Attributes: stream 0 → v0 (position), stream 1 → v1, v2, v3 (three axes of
the part). Constants from `prim_group_draw` (`Chamber.md` 7): c[96..99]
W·view·projection, c[100] light in part space, c[101] eye in part space,
c[102] scale, c[103] three coefficients of the light, c[104..107] W·T of the
light view.

```
oPos  = v0 · c[96..99]
oT3   = (v0 · c[104..106],  max(v0 · c[107], 0))          coordinate in the light view
p     = v0 * c[102]
l     = c[100] − p,   e = c[101] − p
oD0   = 1 / (c[103] · (1, |l|, |l|²))                      attenuation (RCC)
L     = l / |l|,   H = e / |e| + L
oT1   = (L·v1, L·v2, L·v3)                                  light direction in the part
oT2   = (H·v1, H·v2, H·v3)                                  half vector in the part
```

`DST` and `RCC` as in nv2a_vsh. The textures for it (`FrameOrder.md`):
stages 1 and 2 the cube of directions (64 or 128), stage 3 the light view.
The cubes turn oT1 and oT2 into normalised directions as a colour
(`Textures.md` 4.2).

### 3.2 Chamber with relief (0x80065998, `anim`+0x95C)

As 3.1, but stream 1 → v1 (two float32, texture coordinate), v2, v3, v4
(axes); plus `oT0 = v1`. Stage 0 gets the normal map (`Textures.md` 4.7).

### 3.3 Depth for the veil (0x80065D28, `anim`+0x960)

Attribute: stream 0 → v0.

```
r     = v0 · c[96..99]                    (as oPos)
oD0   = (max(c[112] * (r.z + c[113]), 0) in xyz,  1)
oT0   = (r.xyz / r.w * c[114] + c[115],  1)
```

c[112..115] are set by `fx_render_offscreen` (`Effects.md` 2).

### 3.4 Mesh of the blob (0x800662E8, `blob`+0x34)

Attributes: stream 0 → v0 (direction, three float32), stream 1 → v1 (four
float32). Constants from `blob_draw` (`Blob.md` 7): c[100..103], c[104] (two
auxiliary values), c[105] eye, c[106] centre, c[107] radius per axis, c[108]
translation.

```
d    = normalize(v0 * c[107])
q    = v0 * c[106] + c[108] + d * v1.w            (w from c[104])
oPos = q · c[100..103]
oT1  = normalize(c[105] − q)                       direction to the eye
oT0  = normalize(v1.xyz * c[107])                  normal
```

The order of the lines differs in the program; the formulas follow the
registers.

### 3.5 Drops (0x80066470, `blob`+0x38)

Attribute: stream 0 → v0 (direction on the small sphere).

```
s    = v0 · c[107]                                share along the axis c[107]
q    = c[108] * v0 + s * c[109] + c[106]
oPos = q · c[100..103]
oT0  = v0
oT1  = normalize(c[105] − q)
```

A sphere with radius c[108], stretched by c[109] along c[107], at c[106].
That confirms the reading from `Blob.md` 7.3, which was listed as guessed
there.

### 3.6 Veil (0x80066690, `fx`+0x24)

Attributes: stream 0 → v0 (position), v1 and v2 (two float32 each).

```
oPos = v0
oT0  = v1
oT1  = v2 * c[97] + c[96],  oT2 = v2 * c[99] + c[98],  oT3 = v2 * c[101] + c[100]
```

The position is already in picture coordinates (−1 … 1); `Effects.md` 3.

### 3.7 Logo, mesh D (0x80066868, `logo`+0xCC)

Attributes: stream 0 → v0, v1 (two float32, u and v).

```
oPos = v0 · c[96..99]
a    = c[101] + (c[100] − c[101]) * v1.y
b    = c[103] + (c[102] − c[103]) * v1.y
oD0  = a * c[104] + b − b * c[104]               = b + (a − b) · c[104]
```

With `Logo.md` 5.1: the colour changes with v between c5 and c4 and between
c7 and c6, and c8 mixes both.

## 4. Blocks for the combiners

Seven blocks lie in the ROM as the word "PSB0" (0x30425350) and 0x3C words.
`gpu_create_from_data` (0x80056EDC → 0x80040E80) copies the words into an
object; 0x80040EF0 sends them (`gpu.json`, `methods`):

| Words | Method |
|---|---|
| 0–7 | `SET_COMBINER_ALPHA_ICW[0..7]` |
| 8, 9 | `SET_COMBINER_SPECULAR_FOG_CW0/1` (last stage), only if one is ≠ 0 |
| 10–17, 18–25 | `SET_COMBINER_FACTOR0/1[0..7]` |
| 26–33 | `SET_COMBINER_ALPHA_OCW[0..7]` |
| 34–41 | `SET_COMBINER_COLOR_ICW[0..7]` |
| 42 | `SET_SHADER_CLIP_PLANE_MODE` |
| 43, 44 | `SET_SPECULAR_FOG_FACTOR[0..1]` |
| 45–52, 53 | `SET_COMBINER_COLOR_OCW[0..7]`, `SET_COMBINER_CONTROL` |
| 55, 56 | `SET_DOT_RGBMAPPING`, `SET_SHADER_OTHER_STAGE_INPUT` |
| 54 | texture modes of the four stages, 5 bits each; goes as `SET_SHADER_STAGE_PROGRAM` on drawing, adapted to the bound textures (below) |
| 57–59 | assignment of the constants (4.2) |

Measured: each of the 93 loads in `c8_main2` contains the methods of one of
the seven blocks in this order. Without a block (0x80040EF0 with 0) the
library forms the combiners itself (section 5).

**The modes in the stream.** Word 54 names mode 1 (2D) for every stage with
a texture. What stands in the stream as `SET_SHADER_STAGE_PROGRAM` (0x1E70)
on drawing is adjusted by the library to the texture of the stage: 3 (cube)
for a cube texture, 2 (3D) for the depth texture of the light view. Measured
in `c8_main2` (`vectors/c8_gpu.json`, `kinds`, `state`), 5 bits per stage
from stage 0, values after xemu (`psh_regs.h`, `PS_TEXTUREMODES_*`: 0 none,
1 2D, 2 3D, 3 cube, 4 coordinate as colour):

| Kind of drawing | Word 54 | in the stream | Stages 0 to 3 |
|---|---|---|---|
| Chamber smooth (D1, A2) | 0x8420 | 0x10C60 | –, cube, cube, 3D |
| Chamber with relief (D1, A2) | 0x8421 | 0x10C61 | 2D, cube, cube, 3D |
| Depth for the veil (U2, U3) | 0x4 | 0x4 | coordinate as colour |
| Blob, mesh and drops (D2) | 0x21 | 0x63 | cube, cube |
| Veil (D4) | 0x8421 | 0x8421 | four times 2D |
| Logo, mesh D (D3, U4) | 0x0 | 0 | – |
| from stages, with a texture | – | 0x1 | 2D |
| Light view (U1) | – | 0 | – |

With the mode 3D xemu also compares the third coordinate for a depth texture
(4.4); the cube is read with oT.xyz, without dividing.

### 4.1 How to read them

After xemu (`psh.c`, `psh_regs.h`, `provenance.md`): a stage computes,
separately for colour and alpha, from four inputs A, B, C, D the outputs A·B
(or the dot product A.B), C·D (or C.D) and A·B + C·D; every input byte names
a register (0, K0, K1, fog, colour 0/1, texture 0–3, R0, R1) and a mapping
(x, 1 − x, 2x − 1, …). The last stage computes `rgb = D + A·B + (1 − A)·C`
with E·F as a possible input, alpha = G. `tools/extract/gpu.py` takes every
block apart like this (`gpu.json`, `decoded`). Shorthand below:
`x̂ = 2x − 1`, `·` dot product over RGB.

### 4.2 Constants

0x800410B0 (start, pointer, count) takes colours (r, g, b, a) as float32,
clamps to [0, 1] and packs them as `trunc(f32(x · 255 + 0.5))` into
A·2²⁴ + R·2¹⁶ + G·2⁸ + B (0x8004116A–0x80041209, 0x80042A00 is `cvttss2si`).
Where constant k goes is said by the words 57–59 of the active block: nibble
i of word 57 (58) is the constant for `FACTOR0[i]` (`FACTOR1[i]`), the
nibbles 0 and 1 of word 59 those for `SPECULAR_FOG_FACTOR0/1`; 0xF means
none. For k = 0 the function also writes r, g, b as float32 to method 0x181C
(`SET_EYE_VECTOR`). Factors not sent keep the value from the block.

### 4.3 The seven blocks

**Chamber smooth (0x800657A8, `anim`+0x948)**, with program 3.1, modes
(–, 2D, 2D, 2D):

```
nl  = K3 · t̂1           (K3 = 0xFF0000FF from the block: normal (0, 0, 1))
nh  = K3 · t̂2
s   = nh^32              (stages 1–5: squared five times)
lit = Kd * nl + Ks * s   (stage 6, Kd = constant 1, Ks = constant 2)
r0  = colour0 * lit + Ka (stage 7, Ka = constant 0)
rgb = r0 * K4 + r0 * t3  (last stage, K4 = 0xFF3F3F3F from the block)
a   = alpha of K4
```

t3 is the shadow comparison of the light view (4.4). The chamber sends three
constants per part (0x8005F1E6): Ka = (0, 0, 0, 1), Kd and Ks from the light
object; packed in `c8_main2` 0xFF000000, 0xFF020901 and 0xFF0F4707.

**Chamber with relief (0x800656B0, `anim`+0x94C)**, with program 3.2, modes
all 2D: as above, but in stage 0 `nl = t̂0 · t̂1`, `nh = t̂0 · t̂2` with the
normal map on stage 0 instead of K3; hence one squaring fewer (seven stages,
`s = nh^16`).

**Depth for the veil (0x800658A0, `anim`+0x950)**, with program 3.3, mode of
stage 0 "coordinate as colour" (4):

```
d   = t̂0 · t̂0           (t0 = oT0 of the program, passed through unchanged)
m   = (1 − d)²
rgb = colour0 * m,   a = 1
```

**Blob, mesh (0x800660F8, `blob`+0x3C)** and **drops (0x800661F0,
`blob`+0x40)**, with 3.4 and 3.5, modes (2D, 2D):

```
c   = t̂0 · t̂1           (normal · direction to the eye, both through the cubes)
r   = (1 − c)²
rgb = (1 − r) * K0 + K1          mesh: alpha = K0.a        drops: alpha = (1 − r) · K0.a · K2.a
```

The constants are set by `blob_draw` (0x80058906, 0x80058A0C; `Blob.md` 8).

**Veil (0x80066598, `fx`+0x20)**, with 3.6, modes all 2D:

```
n   = t1.a + t2.a + t3.a          (three noise textures)
rgb = t0 * (n * K0) + K1,   a = 1
```

K0 and K1 are set by `fx_draw` (`Effects.md` 3).

**Logo, mesh D (0x80066770, `logo`+0xC8)**, with 3.7: `rgb, a = colour0`.

### 4.4 Shadow comparison

The light views are depth textures (format 0x30, `Textures.md` 1). If a
depth texture lies on a stage, the GPU compares its depth at
(oT3.x, oT3.y)/oT3.w with oT3.z/oT3.w after `SET_SHADOW_DEPTH_FUNC` and
delivers 1 or 0; "never" always gives 0, "always" always 1 (xemu `psh.c`,
`psh_append_shadowmap`). If the first argument of `chamber_draw` is set
(`this+0x181C`), it sets 0x204 ("greater", 0x8005F99F) before the groups and
0x207 ("always", 0x8005F9D6) after them through 0x8003CD70; otherwise
nothing (`Chamber.md` 7). For the cube texture (A2, argument 0) the value
that stood last therefore holds.

More precisely, after xemu (`psh_append_shadowmap` with `compare_z`, format
0x30): the depth is read at (oT3.x, oT3.y)/oT3.w, counted in texels,
filtered between four texels and multiplied by 65535; the result is 1 if it
is greater than oT3.z/oT3.w, limited to [0, 65535]. Outside the texture the
border colour 0xFFFFFFFF of stage 3 holds (2.2), that is the farthest depth.
Program 3.1 sets oT3.w to max(w, 0). Behind the plane of the light w is
therefore 0: the place then lies outside the texture, and z/w is the nearest
or the farthest depth, depending on the sign of z. Surfaces behind the light
with z < 0 are therefore lit, the others dark. Measured in `docs/web.md`
9 (run `w1_t4400`): the shell above the blob, which lies behind the light
view looking down, is bright in xemu.

## 5. Combiners without a block

Without a block the library writes, on drawing, combiners that it forms from
the noted texture stages (0x80043960, called through 0x80044BE0). For that
the animation sets states of the stages with 0x8003CD10. Specified here is
the result in the stream, per kind of drawing (`vectors/c8_gpu.json`,
`draws`, `kinds`); K0 is `FACTOR0[0]`, set with 0x8003CED0.

| Kind of drawing (`FrameOrder.md`) | Colour | Alpha | Modes |
|---|---|---|---|
| Light view (U1) | K0 | colour0.a or K0.a | – (colour not written, mask 0) |
| Blob, glow surface (D2) | t0 · K0 | K0.a | 2D |
| Logo A, B, C (D3, U4) | t0 | K0.a, in the target t0.a · K0.a | 2D |
| Logo as a picture (D3) | t0 | K0.a | 2D |
| TM marks (D3, U4) | t0 | t0.a · K0.a | 2D |
| Lettering (D3, U4) | K0 | t0.a · K0.a (without effect without blending) | 2D |
| Glow over the veil (D4) | K0 · t0 | K0.a · t0.a | 2D |
| Finish the frame (D5) | t0 | colour0.a | 2D |

## 6. Further states

Per kind of drawing the effective values of blending, alpha test, depth
test, depth write, back faces and colour mask are in `vectors/c8_gpu.json`
(`kinds`, `state`, method number → value). The values of the factors for
blending: 0 zero, 1 one, 0x302 source alpha, 0x303 one minus source alpha
(`NV097_SET_BLEND_FUNC_SFACTOR_V_*`); the comparisons for depth and alpha
0x200 + n with n = 0 never, 1 less, 2 equal, 3 less or equal, 4 greater, 5
not equal, 6 greater or equal, 7 always (`NV_PGRAPH_CONTROL_0_ZFUNC_*`; xemu
takes the lower four bits, `pgraph.c`, `SET_DEPTH_FUNC`, `SET_ALPHA_FUNC`).
Overview:

| Kind of drawing | Blending | Depth | Other |
|---|---|---|---|
| Light view (U1) | off | test on, write | colour locked (mask 0) |
| Veil: quad, chamber (U2, U3) | one, zero | test on, write | alpha test "greater or equal 1/255" |
| Chamber (D1) | off | test on, write | |
| Blob: glow surface (D2) | source alpha, one | test on, no write | |
| Blob: mesh, drops (D2) | source alpha, 1 − source alpha | test on, write | |
| Logo A–D (D3) | off | test on, write | back faces removed |
| Logo as a picture (D3) | one, zero | no write | |
| TM marks (D3) | source alpha, 1 − source alpha | test off | `Logo.md` 5.3 |
| Veil, glow (D4) | source alpha, one | comparison "always", during the logo (`this+0xE`) "equal"; no write | alpha test "greater or equal 1/255" |

### 6.1 States of the texture stages

Measured in `c8_main2` (`kinds`, `state`, methods 0x1B00 + 0x40 · stage),
fields after xemu (`nv2a_regs.h`, `NV_PGRAPH_TEXFILTER0_*`,
`NV_PGRAPH_TEXADDRESS0_*`, `NV_PGRAPH_TEXCTL0_0_*`):

- **Filter.** `SET_TEXTURE_FILTER` is 0x02022000 on every stage of every
  draw call: minification 2 and magnification 2, that is averaged between
  four texels and from level 0 of the texture only. The seven smaller levels
  of the falloff (`Textures.md` 4.5) are therefore read by no draw call.
- **Addressing** (`SET_TEXTURE_ADDRESS`, u in bits 0–3, v in bits 8–11;
  1 repeat, 3 clamp to the edge, 4 border colour):

| Stage and texture | Value | u, v | Border colour |
|---|---|---|---|
| 0, normal map (D1) | 0x00010101 | repeat | |
| 1, 2, cube of directions (D1, D2) | 0x00030303 | clamp | |
| 3, light view (D1) | 0x00030404 | border colour | 0xFFFFFFFF |
| 0, target of the veil (D4) | 0x00010303, 0x00030303 | clamp | |
| 1, 2, 3, noise (D4) | 0x00030101 | repeat | |
| 0, falloff (D2 glow surface, D4 glow) | 0x00030303, 0x00010303 | clamp | |
| 0, gradients, radial, TM, picture of the logo (D3, U4) | 0x00030404 | border colour | 0xFF000000 |
| 0, drawing surface (A1, D5) | 0x00030303, 0x00010303 | clamp | |

  The border colour is set by 0x8003D340 (2.2). With it the v = −1 of mesh C
  (`Logo.md` 3.2) becomes black.
- **Control word** (`SET_TEXTURE_CONTROL0`): 0x4003FFC0 on every stage read
  (bit 30 on, levels 0 to 0xFFF allowed), for the normal map 0x4003FFF0
  (plus the bits 4 and 5, `MAX_ANISOTROPY` in xemu).

### 6.2 Depth offset of the light views

`chamber_light_view` switches the depth offset on for its draw calls (U1):
`SET_POLY_OFFSET_FILL_ENABLE` 1 (0x8005FFDD), `SET_POLYGON_OFFSET_BIAS` from
`chamber`+0x54 (0x8005FFF5) and `SET_POLYGON_OFFSET_SCALE_FACTOR` from
`chamber`+0x58 (0x8006000F), afterwards 0 again (0x800600D8). The
constructor sets +0x54 = 10.0 (0x8005FD94) and +0x58 = 4.0 (0x8005FD9B). In
the stream (run `c2final_A`, frame 150, `methods.txt`): 0x41200000 and
0x40800000, the target with `SET_SURFACE_FORMAT` 0x113 (depth 16 bits). xemu
adds to the depth the value 10 and four times the largest slope of the depth
per pixel (`glsl/psh.c`, `depthOffset`, `depthFactor`; `glsl/geom.c`,
`calc_triz`), in steps of the depth surface.

### 6.3 Front face

No recorded frame sends `SET_FRONT_FACE`; 0x8003CE60 is called only by the
after-image (`README.md`). Which winding counts as the front face is
therefore measured on the picture (`docs/web.md` 9): with
`SET_CULL_FACE` 0x405 the surfaces whose corners run clockwise on the screen
remain, among them the quad of the veil (`Effects.md` 1: (−1, −1), (−1, 1),
(1, 1), (1, −1)). With the other assumption the picture of a rebuild stays
black.

## 7. Finishing the frame

Drawing goes into a surface 1280 × 480 (pitch 5120, physical 0x3628000,
`SET_SURFACE_COLOR_OFFSET` in the stream), that is twice as wide as the
picture. 0x8003E550 finishes every frame like this (through 0x8003E280 if
0x8004DC98 & 0x3000; always in the runs):

1. the target is the display buffer 640 × 480 (0x32A4000 in all frames),
2. texture 0 is the surface 1280 × 480,
3. a triangle with position and texture coordinate directly in the stream
   (`INLINE_ARRAY`) covers the whole screen,
4. `FLIP_STALL`, `FLIP_INCREMENT_WRITE`.

That is the draw call D5 of every frame and A1 in the set-up. The values of
the corner points are in the stream (`c8_main2`, every finish equal).

In detail (run `c2final_A`, frame 150, `methods.txt`): for this the library
loads a program of its own with twelve instructions that does not lie in the
range of the animation. Disassembled with nv2a_vsh it computes
`oPos = v0 * c[0] + c[1]` and passes v9 through as oT0; it does not divide
and does not end with the ending from section 3. With it, it sends
c[0] = (1, 1, 16777215, 1) and c[1] = (0.53125, 0.53125, 0, 0). The three
corners, two float32 of position (field 0) and two of texture coordinate
(field 9) each:

| Position | Texture coordinate |
|---|---|
| (−0.53125, −0.53125) | (0, 0) |
| (2559.46875, −0.53125) | (5120, 0) |
| (−0.53125, 1919.46875) | (0, 1920) |

The position is therefore already in pixels of the display buffer, and with
c[1] the corners lie at (0, 0), (2560, 0) and (0, 1920). The texture is the
drawing surface with format 0x0001122A (colour format 0x12, addressed in
texels, `SET_TEXTURE_IMAGE_RECT` 0x050001E0 = 1280 × 480), filter as in 6.1.
At pixel (x, y) with the centre (x + 0.5, y + 0.5) the coordinate is
(2x + 1, y + 0.5): in v the middle of row y, in u exactly the boundary
between the texels 2x and 2x + 1. The picture is thus the mean of two
horizontally neighbouring pixels of the drawing surface each.

## 8. Measurement

| Check | Result (`c8_main2`) |
|---|---|
| Commands per call = methods executed | set-up and 20 frames equal, 1 140 108 methods |
| Program loads = ROM | 93 of 93 |
| Combiner loads = ROM | 93 of 93 |
| Draw calls with a step | all, 0 without a step, 266 combinations (`FrameOrder.md` 4) |
| CPU textures = model | 12 of 12 bit-identical (`Textures.md` 7) |
| Constants and colours of veil and glow = model | 65 of 65 (`Effects.md` 6) |

`c8_main` (without the return chain and without reading objects) gave the
same results for stream, textures and objects.
