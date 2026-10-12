# Textures

All textures of the animation: six generators compute ten textures on the
CPU, to which comes the texture "TM" from the ROM (`Logo.md` 2.4), eleven
together (section 1; 4.1 and 4.6 are helpers of the generators). Six further
textures and two depth surfaces arise on the GPU, by the animation drawing
into them (section 6). The time t is in `Timing.md`, the ways of the
commands to the GPU in `GPU.md`, when what is drawn in `FrameOrder.md`.

Evidence: run `c8_main2` (`tools/xemu/probe_c8.py`), model
`tools/xemu/texture_model.py`, comparison `tools/xemu/vectors_c8.py`, vector
file `vectors/c8_gpu.json` (section `textures`). At the end of the set-up
the measurement reads every texture whose creation it has seen, with all mip
levels and cube faces, from guest memory. The model is written after this
text; each of the eleven CPU textures is bit-identical with the dump
(section 7). They were equal already in the run `c8_main`.

Computation is on the x87 FPU with a 53-bit mantissa (`Timing.md` 4.2).
`f32` means: at this place the value is stored as float32. `trunc` is
`cvttss2si` on a float32, that is truncation towards zero.

## 1. Overview

| Field | Made by (call) | Size | Format word | Purpose (`FrameOrder.md`) |
|---|---|---|---|---|
| `anim`+0x938 | `texgen_normal_cube(128)` (0x800576D3) | cube 128 | 0x0771072E | chamber |
| `anim`+0x934 | `texgen_normal_cube(64)` (0x800576E0) | cube 64 | 0x0661072E | chamber |
| `anim`+0x940 | `texgen_fractal(…)` (0x8005770F) | 128 × 128 | 0x0771062A | chamber |
| `anim`+0x944 | `texgen_falloff_mips(256, 256, 0xFFFFFFFF, 0, 0x3039)` (0x80057729) | 256 × 256, 8 levels | 0x0888062A | glow surface of the blob, glow over the veil |
| `logo`+0x10 | `texgen_gradient(16, 128, 0xFF000100, 0xFF4B9B4B)` | 16 × 128 | 0x0741062A | logo, mesh A |
| `logo`+0x24 | `texgen_radial(256, 6, 0, 0.5, 0.5)` | 256 × 256 | 0x0881062A | logo, mesh B |
| `logo`+0x38 | `texgen_gradient(16, 128, 0xFF000000, 0xFFFFFFFF)` | 16 × 128 | 0x0741062A | logo, mesh C |
| `logo`+0xC0 | copy of the texels at 0x80073FD0 (`Logo.md` 2.4) | 16 × 16 | 0x0441062A | TM marks |
| `fx`+0x08, +0x0C, +0x10 | `texgen_fractal_multi` (0x8005A18B) | 256 × 256 each | 0x0881192A | veil |
| `anim`+0x93C | `texgen_chamber_cube(256)`, on the GPU | cube 256 | 0x0881062E | chamber |
| `chamber`+0x160, +0x15C | 0x8003E630 in 0x8005FD43, 0x8005FD57, on the GPU | 512 × 512, depth | 0x0001302A | shadows of the chamber |
| `fx`+0x14, +0x18 | 0x8003E630 in 0x8005A445, 0x8005A45B, on the GPU | 1024 × 512 | 0x09A1062A | veil |
| `fx`+0x1C | 0x8003E6B0 in 0x8005A471 | 1024 × 512, depth | 0x00012E2A | depth surface of the veil |
| `logo`+0xD0 | 0x8003E630 in 0x8005C3EE, on the GPU | 1024 × 1024 | 0x0AA1062A | final picture (`Logo.md` 6) |

`fx` is the sub-object `anim_object` + 0x17EC = 0x8007083C (`Effects.md`),
`chamber` = `anim_object` + 0x968 = 0x8006F9B8. The format word is the word
+0x0C of the resource (section 2) and goes into the command stream unchanged
as `SET_TEXTURE_FORMAT`.

### 1.1 Arguments of the calls

Where every argument of the CPU generators comes from. `imm8` is sign
extended; the address is that of the instruction.

| Call | Argument | Value | Origin |
|---|---|---|---|
| 0x800576D3 | n | 0x80 | `mov edi, 0x80` 0x800576CD; edi also serves as n of 0x8005770F |
| 0x800576E0 | n | 0x40 | `push 0x40` 0x800576D8 |
| 0x8005770F | n, normals, S | 0x80, 1, 1/512 | edi; `push 1` 0x8005770C; `fld` 0x800128E4, 0x800576E5 |
| | amp, seed, mask | 0x200, 0, 0xFFFFFF | `push 0x200` 0x80057703; ebx = 0 from `xor ebx, ebx` 0x800576F9; `push 0xFFFFFF` 0x800576FD |
| | start, use_start, max, thr | 0xFF, 0, 0xFF, 50 | `mov eax, 0xFF` 0x800576F3; ebx; eax; `push 0x32` 0x800576EB |
| 0x80057729 | w, h, –, noise, seed | 0x100, 0x100, −1, 0, 0x3039 | `mov ebx, 0x100` 0x8005771C; `push -1` 0x8005771A; ebx = 0 at 0x80057719; `push 0x3039` 0x80057714 |
| 0x8005B789 | w, h, c0, c1 | 16, 128, 0xFF000100, 0xFF4B9B4B | `push` 0x8005B785, 0x8005B77D, 0x8005B776, 0x8005B771 |
| 0x8005B80C | n, p, a_flag, A, B | 256, 6, 0, 0.5, 0.5 | `push` 0x8005B807, 0x8005B805; edi from `xor edi, edi` 0x8005B7F5; A and B from 0x80010A18 (0x8005B7CD, 0x8005B7FB) |
| 0x8005B8CD | w, h, c0, c1 | 16, 128, 0xFF000000, 0xFFFFFFFF | `push` 0x8005B8CB, 0x8005B8C6, 0x8005B8C1, 0x8005B8BF (`push -1`) |
| 0x8005A18B | targets, k, n, amp | fx+8, 3, 256, 0x1A9 | esi; `push` 0x8005A188, 0x8005A183, 0x8005A17E |
| | seed, start, max | draw, 0x3F00, 0x55 | eax from 0x8005A178; `push 0x3F00` 0x8005A16E; `push 0x55` 0x8005A16C |

0x80060DDC reads the initial value 0x3F00 as a byte only
(`mov al, [ebp+0x1C]`, 0x80060E76) and writes it at 0x80060E80; in effect it
is therefore 0.

`fx_noise_textures` has a second caller: the restart on repetition in
`anim_update` (0x800572F4, after `blob_restart`). There the noise arises
anew with the next draw of the generator. Without intervention the
repetition does not occur (`Timing.md` 3); only `c6_loop` has forced it
(`Blob.md` 10), without reading the textures.

## 2. Creation

A texture is a resource of 0x14 bytes (0x800406B0): word 0 counter and kind
(0x01040001), word 1 the physical address of the texels, word 3 the format
word, word 4, for linear surfaces, width − 1 and height − 1 in bits 0–11 and
12–23. Measured on all 19 resources of `c8_main` and `c8_main2`. The texels
lie in memory that 0x8001DC6C fetches with the highest address 0x3FFB000 and
alignment 0x80 (0x800406FC).

| Function | Arguments | What |
|---|---|---|
| 0x8003E630 | width, height, levels, –, format, –, destination | flat texture through 0x800406B0 |
| 0x8003E660 | edge, levels, –, format, –, destination | cube texture, six equal faces |
| 0x8003E690, 0x8003E6B0 | width, height, format, kind, destination | surface to draw into through 0x800413F0 |

The format word is put together by 0x80043270. In all measured words there
are, after `nv2a_regs.h` (`NV097_SET_TEXTURE_FORMAT_*`, `provenance.md`):
context DMA 2 in bits 0–1, cube in bit 2, border colour in bit 3, dimension
2 in bits 4–7, the colour format in bits 8–15, the number of levels in bits
16–19 and log2 of width and height in bits 20–23 and 24–27. The colour
format is the argument "format" itself: 6 (A8R8G8B8, swizzled), 7 (X8R8G8B8,
swizzled), 0x19 (A8, swizzled), 0x2E (depth 24 bits, linear), 0x30 (depth 16
bits, linear).

Layout in memory, confirmed by the length of every dump: the levels of a
texture one after the other, each half as wide and high as the one before;
for a cube texture six faces in the order 0 to 5, each with its levels and
rounded up to 128 bytes (xemu `pgraph_get_texture_length`,
`NV2A_CUBEMAP_FACE_ALIGNMENT`).

For filling, 0x80040620 (level) or 0x80041340 after 0x80040650 (face) gives
the address of the level or face; 0x800405C0 delivers a description of the
level which the generators do not use further.

## 3. Reordering on copying (0x800AAB46)

Every generator computes its picture row by row into a buffer and then
copies it into the texture with 0x800AAB46 (source, 0, 0, destination, width
w, height h, 0, bytes per texel). If w < 3 or h < 2, the copy is unchanged
(0x800AAB8C–0x800AABB9). Otherwise texel (x, y) lands at the place whose
bits are interleaved like this:

```
from bit 0 upwards: one bit of x, as long as x still has bits (2^k < w),
                    then one bit of y, as long as y still has bits (2^k < h),
                    k = 0, 1, 2, …
```

For w = h that is the Z order from `Logo.md` 2.4. For 16 × 128 (the
gradients) the bits 0–7 lie alternately with x and y, the bits 8–10 with y
only. 0x800AAB46 (`copy_swizzled`) branches by size and texel width into
fast sub-routines (0x800AA652 for 4 bytes, 0x800AA554 for 2, 0x800AA454 for
1); the result is this reordering for all measured textures. The routine
takes the fast ways only if the second, third and seventh argument are 0
(row length, cut-out, destination point; 0x800AAB9A–0x800AABAE), and only
from 8 × 8 texels on at 4 bytes (0x800AABB0–0x800AABBC) or 16 × 8 at 2 and 1
bytes (0x800AABD2–0x800AABFB); smaller pictures, such as the level 4 × 4 of
the falloff, go through the general way from 0x800AAC11. The unchanged case,
to which the level 2 × 2 also belongs, copies w · h · bytes with `rep movsd`
and `rep movsb` (0x800AB270–0x800AB28B). xemu describes the same masks in
`hw/xbox/nv2a/pgraph/swizzle.c` (`generate_swizzle_masks`).

## 4. The generators

### 4.1 Direction as colour (0x8006036C)

```
normal_to_argb(v) = 0xFF000000 | trunc(f32((v.x + 1) · 127.5)) << 16
                               | trunc(f32((v.y + 1) · 127.5)) << 8
                               | trunc(f32((v.z + 1) · 127.5))
```

with 127.5 at 0x800129C0 and the 1 at 0x8001288C. Sum and product run in the
register, rounding happens once on storing (0x8006037B–0x800603A3). Alpha is
`cvttss2si` of the immediate 0x437F0000 = 255.0 (0x80060384). The four parts
are ORed without a mask.

### 4.2 Cube of directions (0x800610DD)

`texgen_normal_cube(n)`, format 7. For every face f = 0 … 5 and every texel
(u, v) of the face, u column, v row:

```
d  = f32(n − 1)
fx = f32(2 · (u / d) − 1),   fy = f32(2 · (v / d) − 1)
f = 0: ( 1, −fy, −fx)        f = 1: (−1, −fy,  fx)
f = 2: (fx,   1,  fy)        f = 3: (fx,  −1, −fy)
f = 4: (fx, −fy,   1)        f = 5: (−fx, −fy, −1)
texel = normal_to_argb(normalize(vector))
```

`normalize` is `vec3_normalize` (0x8005816D): length through `sqrt_sse`,
then the components multiplied by the reciprocal in the register (as in
`Chamber.md` 11). Case distinction 0x8006118B–0x80061205. Called with
n = 128 and n = 64.

The 1 in fx and fy comes from 0x8001288C (0x80061162, 0x80061185), the
doubling as `fadd st(0), st(0)`. fy is stored per row, fx stays in the
register and is stored only with the vector. The component +1 is loaded by
`fld1`, the component −1 is the immediate 0xBF800000 (0x8006119E for f = 5,
0x800611C4 for f = 3, 0x800611E3 for f = 1).

### 4.3 Colour gradient (0x800607A1)

`texgen_gradient(w, h, c0, c1)`, format 6. Every row has one colour; from c0
at the top to c1 at the bottom:

```
k = 1/255 (0x800129CC)
a_i = f32(byte i of c0 · k),  b_i = f32(byte i of c1 · k)      i = A, R, G, B
inv = f32(1 / (h − 1))
row y:    s = y · inv,  r = 1 − s                       (register)
          channel i = trunc(f32((r · a_i + s · b_i) · 255))   // 255 at 0x80012908
```

Evidence 0x800607DC–0x800608DF (bytes, `inv`) and 0x800608EE–0x80060972
(row). The sum begins with the share of c0. The 1 in `inv` comes from
0x8001288C (`fdivr`, 0x800608D6), that in r from `fld1` (0x80060907).

### 4.4 Radial brightness (0x800603D8)

`texgen_radial(n, p, a_flag, A, B)`, format 6; the logo calls
(256, 6, 0, 0.5, 0.5). It is the only one to fetch its buffer through
0x80055F5E (n · n · 4 bytes, alignment 0x80; 0x80060435–0x80060446) and
releases it after the copying through 0x80055F7A (0x800605C3). First a table
with 256 steps (0x80060468–0x800604D1):

```
k = 1/256 (0x800129C8)
for i = 0 … 255:
    φ = f32(i · k);  c = f32(cos φ)          // fsincos
    p times: c = f32(c · c)
    tab[i] = trunc(f32(((256 − i) · k · A + c · B) · 255)) & 0xFF
```

Then the texels (0x800604D3–0x80060591):

```
inv = f32(1 / (n/2));  m = (n − 1)/2 (integer)
for row y = 0 … n−1:
    for x = 0 … y−1:                        // only y texels!
        ρ = sqrt_sse(f32((x − m)² + (y − m)²)) · inv
        ρ ≥ 1:      texel = 0
        otherwise:  L = tab[trunc(f32(ρ · 256))]          // 256 at 0x800129C4
                    texel = (a_flag ? L : 0xFF) << 24 | L << 16 | L << 8 | L
```

The 256 of the table is the immediate in `mov edx, 0x100` (0x80060463),
loop bound and minuend at once. The 1 in `inv` comes from 0x8001288C
(0x8006042C), as does the 1 of the comparison (`fcom`, 0x8006052E): after
`test ah, 5` (0x80060536) ρ ≥ 1 and an unordered comparison lead to texel 0.

The inner loop counts with the row number (0x800604EB:
`cmp [ebp+0x14], ecx` with [ebp+0x14] = y, counter [ebp−4] = y). In row y
only the first y texels are therefore written, the rest of the buffer keeps
what stood in memory. In the guest 0 stood there: the texture shows the disc
only in the lower left triangle x < y, otherwise black with alpha 0
(measured, section 7).

### 4.5 Falloff with mip levels (0x800605D2)

`texgen_falloff_mips(w, h, –, noise, seed)`, format 6. Purely integer. The
number of levels is the bit length of w/2 or h/2, the larger
(0x800605DE–0x8006061B); for 256 there are 8 (256 to 2). For every level
with W = w ≫ s, H = h ≫ s:

```
sh = 1 + log2(0x1000 / W);  cx = (W−1)/2,  cy = (H−1)/2
for every texel (x, y):
    d = ((x−cx) ≪ sh)² + ((y−cy) ≪ sh)²            (32 bits)
    v = d ≤ 2^24 ? 2^24 − d : 0
    v = v − noise (below), below 0 → 0
    b = (v ≥ 2^24) ? 0xFF : (v ≫ 16) & 0xFF
    q = (b⁴ ≫ 16)² ≫ 24                            (32 bits each)
    texel = q · 0x01010101
```

The noise is `((v · noise) ≫ 32) · s' ≫ 32 ≪ 15` with a generator of its own
(0x800606FC–0x8006070B: `rcl 13` with the carry of the preceding
multiplication, then minus (s − 11)). The set-up passes noise = 0; the noise
is then always 0 and the texture a pure falloff. The generator nevertheless
advances one step per texel. At `b` the value 2^24 (the centre only) ends up
at 0xFF through `rcl ebx, 8; sbb ebx, 0` (0x8006071F).

"Below 0 → 0" is more precisely `sub ebx, edx; jge` (0x80060713): a signed
comparison of v and noise, not the sign of the result. The two differ only
on an overflow, that is only with noise ≠ 0. Immediates: 0x1000
(0x8006066A), 2^24 (0x800606E8), 13 (0x80060703), 11 (0x80060706), 15
(0x80060710), the mask 0x1FF0000 (0x80060719) and 0xFF00 (0x80060736).

### 4.6 Random generator of the generators

0x8005A620 (`rand_below(&s, n)`) makes a step like 0x8005A645
(`Timing.md` 4.1) on a state of its own and delivers `(s' · n) ≫ 32`
(unsigned, 32 × 32 → 64 bits).

### 4.7 Height field by subdivision (0x800609D1)

`texgen_fractal(n, normals, S, amp, seed, mask, start, use_start, max, thr)`,
format 6. The set-up calls (128, 1, 1/512, 0x200, 0, 0xFFFFFF, 0xFF, 0,
0xFF, 50); 1/512 at 0x800128E4.

The height field H (n × n values, initial value 0) arises like this:

```
s = seed;  H[0,0] = use_start ? start : rand_below(&s, max)
a = amp / 2;  half = n/2;  step = n
while half > 0:
    centres: for y = half, half+step, …;  x = half, half+step, …:
             H[x,y] = perturb(mean of the four corners (x±half, y±half))
    edges:   for y = 0, step, …;  x = half, half+step, …
             then y = half, half+step, …;  x = 0, step, …:
             H[x,y] = perturb(mean of the four neighbours (x±half, y), (x, y±half))
    step = half;  half = half/2;  a = a/2
```

Coordinates are taken modulo n, the mean is the sum of the four values
divided by 4 as an integer. One perturbation (0x80060B01–0x80060B33):

```
perturb(v):  if rand_below(&s, 100) > thr:   v = v + rand_below(&s, a), at most max
             otherwise:                      v = v − rand_below(&s, a), below 0 → 0
```

The 100 is `push 0x64` (0x80060AC8), the comparison with thr is signed
(`jle`, 0x80060B15). "At most max" compares unsigned (`jbe`, 0x80060B21).
"Below 0 → 0" is `cmp edi, esi; jbe` with esi = 0xFF (`mov esi, 0xFF`,
0x80060A82): a value above 0xFF unsigned, that is a borrow below 0, becomes
0. With the same 0xFF, byte 0 of every word is taken on averaging. a halves
with every level (`sar [ebp−0x10], 1`, 0x80060C2D).

Every value stands in the buffer as v · 0x010101 at first; after that every
word becomes `(w & mask) | (w ≪ 24)` (0x80060C77–0x80060C90), with mask
0xFFFFFF therefore v in all four bytes.

With `normals` ≠ 0 that becomes a normal map, in the same buffer
(0x80060C9C–0x80060DB4). For row r and column x, with h(·) the byte 2 of a
word:

```
hc = h(texel (x, r)),  hr = h(next word),  hb = h(word one row below)
A = hc · S (register),  B = f32(hr · S),  C = f32(hb · S)
v1 = (f32((x + 0.1) − x), 0, f32(B − A))           // 0.1 at 0x80012888
v2 = (0, f32(f32(r + 0.1) − r), f32(C − f32(A)))
texel (x, r) = normal_to_argb(normalize(cross(v1, v2)))
```

`cross` is 0x8005A662 (`Chamber.md`). "Next word" means: at the end of a row
it is the first texel of the next row. For the last row the loop reads hb
from the n words behind the buffer (the address is only limited to the end
of the buffer, 0x80060CB7–0x80060CBC, then + 2). In the guest 0 stood there;
with hb = 0 the model meets all texels.

### 4.8 Three noise layers (0x80060DDC)

`texgen_fractal_multi(targets, k, n, amp, seed, start, max)`, format 0x19
(A8). `fx_noise_textures` (0x8005A14F) calls (fx+8, 3, 256, 0x1A9, seed, 0,
0x55); seed is the next draw of the shared generator at `anim`+0x68C
(0x8005A178), 0xC12E9847 in the guest, the 38th draw after 0x76543210
(`Timing.md` 4.1).

The same subdivision as 4.7, but for k byte fields in one buffer (layer i
from i · n²) and with a different step (0x80060F35–0x80060F5E):

```
for every point, for every layer i = 0 … k−1 in turn:
    v = mean of the four values of layer i + rand_below(&s, 2a) − a
    H_i[x,y] = min(max(v, 0), max)
```

Only layer 0 gets the initial value (0x80060E80; for 0x3F00 see 1.1);
a = amp/2 = 212 and halves with every level (`sar ebx, 1`, 0x80061072). The
clamp compares signed (`jl`, `jge`, 0x80060F48–0x80060F5A). The draws do not
depend on the values: every layer draws one number at every point, also the
layers that are discarded later. At the end the loop 0x800610B3–0x800610CC
copies each of the k textures with 0x800AAB46 but always gives the start of
the buffer as the source (`push [ebp−0xC]`) and never layer i. All three
textures therefore contain layer 0; the layers 1 and 2 are computed and
discarded. Measured: the three dumps are equal, and equal to layer 0 of the
model.

### 4.9 TM marks

`logo_build_quads` copies the 16 × 16 texels at 0x80073FD0 with 0x800AAB46
(16, 16, 4) into a texture with format 6 (`Logo.md` 2.4). At 16 × 16 the
reordering from section 3 is the Z order.

## 5. The cube texture of the chamber (0x800614E9)

`texgen_chamber_cube(256)` at the end of the set-up (0x80057927) draws the
chamber six times on the GPU:

```
cube texture 256, format 6, one level (0x8003E660);  depth surface 256 × 256,
format 0x2E (0x8003E6B0);  save the camera;
projection: opening f32(c(0x80066AA4)) · 0.5, aspect ratio 1, near 0.1, far c(0x800129D0)
for face f = 0 … 5:
    target = face f, depth = depth surface (0x8003E920)
    clear: mask 0xF3, colour 0xFF000000, depth 1.0 (0x80040AB0)
    view = cube_face_view(f, origin)  (0x80061418)
    chamber_update(5.35);  chamber_draw(…, 0, 0)        (Chamber.md 9)
old target back, camera back
```

`cube_face_view(f)` looks from the origin along +x, −x, +y, −y, +z, −z (f = 0
… 5) with the up direction (0, 1, 0) for f = 0, 1, 4, 5, (0, 0, −1) for
f = 2 and (0, 0, 1) for f = 3, through `mat4_view_toward` (0x8005B093). What
the command stream draws in the process is in `FrameOrder.md` 3 (set-up).

## 6. Further targets on the GPU

| Target | When drawn | What |
|---|---|---|
| light views `chamber`+0x160, +0x15C, 512 × 512, depth 16 bits | every frame with the chamber, `chamber_prepass` (0x800600F5) | the chamber from the weighted centre of the blob downwards and upwards (`Chamber.md`), depth only |
| `fx`+0x18 (then swapped with +0x14), 1024 × 512 | every frame, `fx_update` (0x8005A618) | quad with the depth surface `fx`+0x1C, then the chamber in depth (`Effects.md` 2) |
| `logo`+0xD0, 1024 × 1024 | twice, t ≥ 6 and t ≥ 6.25 | the logo (`Logo.md` 6) |
| cube `anim`+0x93C | once in the set-up | section 5 |

These contents arise in the rasteriser of the GPU and lie in OpenGL textures
only in xemu; they stand in guest memory only after a write-back
(`tools/xemu/README.md`, "Why savevm"). What is specified is therefore what
is drawn into them (`FrameOrder.md`), not their content texel by texel.

## 7. Measurement

At the end of the set-up (first stop in `anim_frame`) `probe_c8.py` reads
every resource whose creation it has seen: head and data in the length from
the format word (`tools/xemu/nv2a.py`, `texture_length`). `vectors_c8.py`
compares the eleven CPU textures with the model.

| Field | Bytes | different |
|---|---|---|
| `anim`+0x938, cube 128 | 393 216 | 0 |
| `anim`+0x934, cube 64 | 98 304 | 0 |
| `anim`+0x940, normal map 128 | 65 536 | 0 |
| `anim`+0x944, falloff, 8 levels | 349 520 | 0 |
| `logo`+0x10, gradient green | 8 192 | 0 |
| `logo`+0x24, radial | 262 144 | 0 |
| `logo`+0x38, gradient grey | 8 192 | 0 |
| `logo`+0xC0, TM | 1 024 | 0 |
| `fx`+0x08, +0x0C, +0x10, noise | 65 536 each | 0 |

The numbers are in `vectors/c8_gpu.json`, `textures`, with the
SHA-256 of every dump (`sha256`). The core checks against these sums
(`core/test/test_k5.c`).
