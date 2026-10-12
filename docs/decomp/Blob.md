# Glowing blob

The green glowing blob in the middle of the chamber: a sphere mesh of six
cube faces that 32 bulges deform, plus eight drops that swing around the
blob, and a glow surface behind it. It is the sub-object at `anim_object` +
0xB5C = **0x8006FBAC** (`blob` in what follows). The time t and the
quantities x1c, x20 are in `Timing.md`, the camera in `Camera.md`.

Evidence: runs `c6_main`, `c6_tail` and `c6_plain`
(`tools/xemu/probe_c6.py`), model `tools/xemu/blob_model.py`, comparison
`tools/xemu/vectors_c6.py`, vector file `vectors/c6_blob.json`. The model is
written after this text and gets from the guest only the two arguments
(t, dt) of every call of `blob_update`. In five runs it meets the whole
state of the blob and the state of its generator before every update
bit-exactly, 2623 frames up to t = 5.926 and across two restarts. At 16
times the deformed mesh, everything `blob_draw` gives to the GPU, and both
outputs of the weighted centre agree as well (section 10).

Coordinates and matrices as in `Chamber.md`: z up, row vectors, translation
in row 3. The blob sits at the origin.

## 1. Sequence

| When | Function | What |
|---|---|---|
| constructor (0x80056ACA) | 0x80059270 `blob_construct` | preset the fields, generator 0x800708BC to 0x76543210 |
| set-up (0x8005768C) | 0x800598E6 `blob_setup` | two sphere meshes, two buffers for the deformed mesh, `blob_restart`, four GPU objects |
| restart at t ≥ 8 (0x800572E9) | 0x80059868 `blob_restart` | all bulges anew, both buffers undeformed; only if `this+0xA` is set, otherwise the animation ends (`Timing.md`); measured with the byte set in `c6_loop` |
| every frame, if `this+0xD` | 0x80059789 `blob_update` (t, dt) | drops, bulges, deformation (0x8005758D–0x800575C3) |
| every frame, if `this+0xD` and t ≥ 0.6 | 0x800583BF `blob_draw` | glow surface, mesh, drops (0x80056D5A–0x80056D70, limit 0x80065E60 = 0.6) |
| tear-down | 0x8005832F `blob_destroy` | release buffers, memory and GPU objects |

dt is the argument of `anim_update`, set to 0.001 if it is above 1
(0x800571B8–0x800571CB). `this+0xD` is set by the camera (`Camera.md` 5.5);
after the last key it falls, measured after t = 5.926 in `c6_plain`.

## 2. The object

0xA38 bytes from 0x8006FBAC. In the image the range is zero. The presets
come from `blob_construct` (0x80059270), the buffers from `blob_setup`.

| Offset | Content | set in |
|---|---|---|
| +0x00, +0x04 | vertex and index buffer of the small mesh (n = 8) | 0x80059917 |
| +0x08 | buffer of the glow surface, 0x50 bytes (four vertices of 0x14) | 0x80059902 |
| +0x0C, +0x18 | vertex and index buffer of the large mesh (n = 32) | 0x80059932 |
| +0x10, +0x14 | two buffers for the deformed mesh, 16 bytes per vertex each; `blob_draw` swaps them | 0x80059944, 0x80059956 |
| +0x1C | copy of the unit vectors of the large mesh, 12 bytes per vertex | 0x80058E7C |
| +0x20, +0x24 | vertex count large 1734, small 150 | 0x80058E2F |
| +0x28, +0x2C | index count large 4054, small 346 | 0x80058E46 |
| +0x30 | vertices per cube face, 1734 / 6 = 289 | 0x80059966 |
| +0x34, +0x38 | GPU objects for mesh and drops from the data 0x800662E8 and 0x80066470 and a list (below) | 0x800599EC, 0x80059A21 |
| +0x3C, +0x40 | two GPU objects from the data 0x800660F8 and 0x800661F0 | 0x8005998A, 0x800599A9 |
| +0x44 | colour (0.25, 1.0, 0.15, 1.0); 0.15 = 0x3E19999A, immediate at 0x800592F5 | 0x80059270 |
| +0x54 | centre (0, 0, 0) | 0x80059270 |
| +0x60 | scale (1, 1, 1) | 0x80059270 |
| +0x6C | radius R = 2.3 (0x40133333, immediate at 0x8005931B) | 0x80059270 |
| +0x70 | 32 bulges of 0x40 bytes (section 2.1) | |
| +0x870 | number of bulges | 0x80059868 |
| +0x874 | 8 drops of 0x38 bytes (section 2.2) | |
| +0xA34 | number of drops | 0x80059868, 0x80059789 |

The lists for +0x34 and +0x38 (0x800599AE–0x80059A04) read 0x20000000,
0x40320000, 0x20000001, 0x40420001, 0xFFFFFFFF and 0x20000000, 0x40320000,
0xFFFFFFFF respectively. That fits the data streams on drawing: stream 0
with 12 bytes (three float32) and stream 1 with 16 bytes (four float32).
+0x34 and +0x38 are vertex programs with this assignment of streams to
registers (`GPU.md` 3, 3.4 and 3.5; measured).

Nobody changes the fields +0x44 to +0x6C after the constructor; in all 1283
measured states they have these values. 0x800708B8 holds a pointer to `blob`
(0x800598FC), through which drops and bulges read centre and radius.

### 2.1 Bulge (0x40 bytes)

| Offset | Name | Meaning |
|---|---|---|
| +0x00 | `size` | size s, radius of the bulge on the unit sphere |
| +0x04, +0x08 | `size2`, `inv2` | s² and 1/s² |
| +0x0C | `h` | height now |
| +0x10 | `c` | direction the deformation computes with (copy of `dir`) |
| +0x1C | `mask` | cube faces the bulge reaches, bit f for face f |
| +0x20 | `dir` | direction, approximately length 1 |
| +0x2C, +0x30, +0x34 | `t0`, `freq`, `amp` | start, angular frequency and height of the pulse |
| +0x38 | `drop` | pointer to a drop, or 0 |
| +0x3C | `flag` | byte, "drop near enough again" (section 6.3) |

### 2.2 Drop (0x38 bytes)

| Offset | Name | Meaning |
|---|---|---|
| +0x00 | `size` | size |
| +0x04 | `pos` | position, centre + `dir` · `disp` |
| +0x10 | `neg` | byte, `disp` < 0 |
| +0x14 | `dir` | direction, taken over from the bulge |
| +0x20, +0x24, +0x28 | `t0`, `freq`, `amp` | start, angular frequency and width of the oscillation |
| +0x2C | `disp` | displacement now |
| +0x30, +0x34 | `scale`, `rate` | stretch (0.5 … 2) and its rate of change |

`blob_construct` sets `scale` = 1 and `rate` = 0 for all eight and `drop` = 0
for all 32 bulges (0x80059273–0x8005929A).

## 3. Meshes

`cube_sphere_build` (0x80058DF2) with n = 8 for the small and n = 32 for the
large mesh:

```
m    = max(n / 2, 1)                     // integer: 4 and 16
step = f32(2.0 / m)                      // 0x80010A24
for face f = 0..5, row j = 0..m, column i = 0..m:
    a = (i == m) ? 1 : f32(i · step − 1)
    b = (j == m) ? 1 : f32(j · step − 1)
    v = face 0: (−1, −a, b)   1: (b, −1, −a)   2: (−a, b, −1)
        face 3: ( 1,  a, b)   4: (b,  1,  a)   5: ( a, b,  1)
    vertex = normalize(v)                // 0x8005816D
indices per face f: strip_index_build(m, m, lead = f > 0, trail = f < 5,
                                      start = f · (m+1)²)   // 0x800612D3
```

Face f therefore lies on the cube face −x, −y, −z, +x, +y, +z. Vertex count
6 (m+1)², index count 6 · `strip_index_count`(m, m) + 10; the ten are the
connecting indices between the faces. The large mesh also writes its vectors
to +0x1C (0x80058F71–0x80058F7F). Measured: both vertex buffers, both index
buffers and the copy equal the model bit-exactly.

## 4. Generator and auxiliary quantities

**Generator.** Its own state at 0x800708BC, step 0x8005A645 as with the main
object (`Timing.md` 4.1), initial value 0x76543210 (instruction 0x8005929C,
immediate at 0x800592A2). Of every step the lower 16 bits k count:

```
unit()   = k · 2⁻¹⁶          in [0, 1)     0x800582E7, 0x800708C0
signed() = k · 2⁻¹⁵ − 1      in [−1, 1)    0x80058308, 0x800708C4
```

`blob_restart` in the set-up needs 208 steps. Up to t = 5.806 there are 1822
in `c6_main`.

**fade(t)**, the same sequence in 0x800590A6 (0x80059150–0x8005917F) and
0x80059320 (0x80059393–0x800593C2):

```
k       = 1 / (((8 − 2) − 0.8) − (0.6 + 0.5))      // fdivr with the 1 from 0x8001288C
fade(t) = f32(max((t − 0.6) · k, 0))
        ≈ max((t − 0.6) / 4.1, 0)
```

The code does not divide by 4.1, it forms the reciprocal and multiplies; in
the register k stays unrounded. Constants 0x8006657C (8), 0x80066580 (2),
0x80066584 (0.8), 0x80066588 (0.6), 0x80010A18 (0.5), 0x8001288C (1). At
t = 5.936 fade = 1.30. Measured: with a division instead of the
reciprocal no value changes in any of the five runs or any of the four
synthetic sequences of `core/test/check_k3.py`; the order nevertheless
stands as the code computes.

**Approximate normalisation** `normalize_approx` (0x80058290): with the
absolute values of the components, sorted into large L, middle M, small S,

```
len ≈ (L + 0.34375 M + 0.25 S) · 1.0433885        // 0x8005820E: 0x800128F4, 0x8001289C, 0x800128F0
if len ≥ 1e−6 (0x800128F8):  v = v · f32(1 / len)
```

The vectors do not become exactly length 1 by this.

**Cube faces of a bulge** (0x80058FFC), with k = 0.57735 (0x80012914):

```
bit 0, 1, 2:  dir.x − s < −k,  dir.y − s < −k,  dir.z − s < −k     (faces −x, −y, −z)
bit 3, 4, 5:  s + dir.x >  k,  s + dir.y >  k,  s + dir.z >  k     (faces +x, +y, +z)
```

## 5. Creation

### 5.1 All bulges anew

`blob_restart` (0x80059868):

```
drops = 0;  bulges = 0
while bulges < 32:
    slot = drops < 8 ? drops : none
    b    = bulge[bulges];  bulges += 1             // counted before the creation
    if spawn(b, −0.3, slot): drops += 1              // 0x8001292C
fill both buffers +0x10, +0x14 with the undeformed mesh, w = 1 (0x80058B2C)
```

The first eight bulges thus get a drop each, the other 24 none. There are
never more than eight drops, and a drop stays with its bulge until the next
`blob_restart`.

### 5.2 Creating a bulge

`spawn(b, t, slot)` (0x80059320), returns whether the bulge has a drop:

```
if t < 0: b.drop = none
b.dir = (signed(), signed(), signed())
if |b.dir|² < 0.001 (0x800128A0): b.dir = (signed(), signed(), 1)
b.c   = 0
b.dir = normalize_approx(b.dir)
fd    = fade(t)
q     = unit()
s     = (q + 1) · 0.4                                       // 0x800128AC
b.size = f32(s);  b.size2 = f32(s²);  b.inv2 = f32(1 / s²);  b.h = 0
mask(b)
b.t0  = f32(unit() · 0.4 + t)
b.amp = f32(((1 − q) · 0.5 + 0.2) · (fd + 1) · 0.5)          // 0x800128B0
if b.drop == none:
    b.drop = slot
    if slot == none:                                        // bulge without a drop
        b.freq = f32((fd · 0.2 + 0.8) / ((unit() + b.amp) · 0.3) · π)
        if t < −1: b.t0 = f32(−(unit() · π / b.freq))
        return: no
drop d = slot:
    d.size  = f32((unit() + 1) · b.size · R · 0.25)
    d.dir   = b.dir
    r       = signed()
    d.amp   = f32(f32((r + r + 5) · R) · 0.6)                // 0x80012934, 0x80012930
    d.t0    = t < −1 ? f32(unit() · −0.3) : t
    p       = (unit() · 0.3 + 0.8) · 1.6666666              // 0x800128FC, 0x800128D0, 0x80012928
    d.freq  = f32(2 π / p)
    d.scale = 1.2 (0x3F99999A);  d.rate = 0
    b.flag  = (t − b.t0 < 0.4 p)
    drop_update(d, t, 0);  step(b, t, 0, none)
    return: yes
```

Constants, as far as without an address above: 0.001 from 0x800128A0
(0x8005935D), 0.4 from 0x800128AC (s at 0x800593E6, b.t0 at 0x8005940D,
b.flag at 0x8005950F), 1 from 0x8001288C, 0.5 from 0x80010A18 and 0.2 from
0x800128B0 in b.amp (0x8005941D–0x80059433); in b.freq 0.3 from 0x800128FC,
0.2, 0.8 from 0x800128D0 and π from 0x80066574 (0x80059561–0x80059578); 0.25
from 0x8001289C in d.size (0x80059471); −1 from 0x80012894 (0x800594AB,
0x80059586); 1.2 as an immediate at 0x800594FB (instruction 0x800594F8).

No caller reaches the branches for t < −1. `spawn` is called only from
`blob_restart` (0x800598B3, t = −0.3) and from `step` (0x80059742), `step`
only from `spawn` (0x80059552, with its t) and from `blob_update`
(0x8005983F), which works only from t = 0.6 on. t is therefore always −0.3
or at least 0.6.

The branch `|dir|² < 0.001` is rare (a sphere of radius √0.001 in the cube
[−1, 1)³: about 1.7 · 10⁻⁵ per creation) and did not occur in the runs
without intervention. In `c6_poke` the generator was set to 0xF0410209 at
t = 2.279, whose next three draws give |dir|² = 0.00018 (searched with numpy
over random states). The branch was taken, and guest and model stayed equal
in all frames after that.

A bulge with a drop gets no frequency of its own at creation; at the first
creation `freq` therefore stays 0 until the first pulse from section 6.3.

## 6. Updating

### 6.1 A frame

`blob_update(t, dt)` (0x80059789):

```
if t ≥ 0.6 (0x80066588):
    for every drop i < drops:  drop_update(drop[i], t, dt)
    for every bulge j < bulges:
        slot = drops < 8 ? drops : none
        if step(bulge[j], t, dt, slot): drops += 1
    deform()
otherwise if t − dt > 0.6:
    fill both buffers undeformed and swap them (as in blob_restart)
```

The second branch takes effect only if t jumps backwards, that is on a
restart. Before t = 0.6 the blob stays unchanged; in `c6_main` the 41 frames
with t < 0.6 leave the state after the set-up as it is.

### 6.2 Drops

`drop_update(d, t, dt)` (0x800590A6). The stretch `scale` is a spring with a
dead zone, the displacement a sharpened sine:

```
v = clamp(dt · d.rate + d.scale, 0.5, 2.0)                  // 0x80010A18, 0x80010A24
d.scale = f32(v)
move = d.rate > 0 ? (v < 0.95 or v > 1.0)                   // 0x80012924
                  : (v < 1.0  or v > 1.05)                  // 0x80012920
if move: d.rate = f32(d.rate − (v − 1) · 1000 · dt)         // 0x80066590
a  = f32((t − d.t0) · d.freq · (0.1 t + 1) · 1.4)           // 0x80012888, 0x8001291C
s  = sin(a)                                                 // 0x80057F9D
u  = f32(1 − |s|)
w  = 1 − √u · u;   if s ≤ 0: w = −w                         // √ through 0x80057F42
e  = w · d.amp · fade(t)
d.disp = f32(e);  d.neg = (e < 0)
d.pos  = centre + d.dir · d.disp
return: |d.disp| + d.size < R · 0.5        (the value is used nowhere)
```

The factor (0.1 t + 1) speeds the oscillation up with time, fade(t) lets its
width grow from 0 at t = 0.6 to 1.3 times at t = 5.936. In `c6_main` the
displacements at t = 0.662 lie between −0.14 and −0.05, at t = 5.806 between
−11.97 and 8.29 (`vectors/c6_blob.json`). In this run the limit 0.5 or 2 of
`scale` was reached 737 times (recomputed).

### 6.3 Bulges

`step(b, t, dt, slot)` (0x800595B6), returns "yes" only through `spawn`:

```
if b.drop = d:
    q   = (|d.disp| + d.size) / R
    h   = clamp(q − 1, 0, 2);   b.h = f32(h)
    if b.flag:
        if h ≤ 0.8:                                         // 0x800128D0
            neg = (dot(b.dir, d.dir) < 0)
            if neg == d.neg: done
            b.dir = −b.dir;  b.c = b.dir;  mask(b);  done
        // the drop is far out: a new pulse
        b.amp  = f32(h);  b.flag = 0
        p      = 0.3 h
        b.freq = f32(2 π / p);   b.t0 = f32(t − 0.25 p)
        d.scale = f32(clamp(h − 0.5, 0.6, 0.8));  d.rate = 0
    if not b.flag and q < 0.9 (0x80012938): b.flag = 1
ph = (t − b.t0) · b.freq
if ph > π:
    without a drop: return spawn(b, t, slot)
    with a drop:    b.h = 0;  done
if ph < 0: done
b.h = f32(sin(f32(ph)) · b.amp)
b.c = b.dir;  mask(b)
```

Constants: 1 from 0x8001288C and 2 from 0x80010A24 for h (0x800595E9–
0x8005960F), 0.8 from 0x800128D0 (0x80059633), 0.3 from 0x800128FC and π
from 0x80066574 for p and b.freq (0x80059647, 0x8005964F), 0.25 from
0x8001289C (0x8005965C), 0.5 from 0x80010A18 (0x80059668), the limits 0.6
from 0x80012930 and 0.8 from 0x800128D0 for d.scale (0x8005966E–0x800596A5),
0.9 from 0x80012938 (0x80059703), π for ph (0x80059721).

A bulge without a drop is therefore a single pulse: half a sine wave of
height `amp`, after which it is created anew at another place. Because of
fade(t) the pulses grow with time (`amp` times fd + 1), and they get faster
(`freq` with fd · 0.2 + 0.8). A bulge with a drop follows its drop: if it is
more than 0.8 R out beyond the surface, a pulse with its height arises, and
the drop is squashed; if it is near and on the other side, the bulge
reverses its direction. Recomputed for `c6_main`, in `blob_update` only: 269
new creations, 64 pulses from drops, 111 reversals.

### 6.4 Deformation

`deform()` (0x80058B93) writes into the buffer +0x14, four float32 per
vertex. Per cube face f it takes the bulges with bit f in `mask`, in
ascending order, and works through them from the back:

```
for every vertex p of face f (unit vector from +0x1C):
    o = (p.x, p.y, p.z, 0)
    for each of these bulges b, last first:
        δ  = p − b.c                                      (float32 per axis)
        dd = (δz² + δy²) + δx²
        if dd < b.size2:
            g = dd · b.inv2 − 1                             // −1 in the middle, 0 at the edge
            o.w   += f32(R · b.h · g²)
            n      = normalize_approx(p + δ · f32(−4 · b.h · b.inv2 · g))   // 0x80012910
            o.xyz += n
```

o.w is the height above the sphere: every bulge contributes
R · h · (1 − dd/s²)², a smooth cap of radius s. o.xyz is a sum of
directions: the vector p itself and, for every bulge, p minus the gradient
of its height field, approximately normalised. The bulges are added, not
blended. In `c6_main` o.w lies between 0 and 0.36 at t = 0.662, between 0
and 2.8 from t = 3.0 on.

The GPU puts the vertex at centre + (k + o.w) · p and takes
normalize(o.xyz) as the normal (program +0x34, `GPU.md` 3.4; k from 7.2).

## 7. Drawing

`blob_draw` (0x800583BF) draws three things. A common quantity:

```
k = f32((√f32(x20 − x1c) · 1.3 + 1) · R)          // 0x8001290C, x1c = 0x8006F06C, x20 = 0x8006F070
```

x1c and x20 come from `Timing.md` 4; x20 − x1c is the share of the pulses.
Without a pulse k = R = 2.3 (measured at t = 1.207, 2.002, 5.005, 5.806),
with a pulse larger (4.14 at t = 0.662).

### 7.1 Glow surface

A quad in the picture plane of the camera, side axes X = row 0 and Y = row 1
of the inverse view (0x8006F7F4, 0x8006F804, `Camera.md` 1), with
kq = f32(k · 5.2) (0x80012904):

| Vertex | Position | u, v |
|---|---|---|
| 0 | (Y − X) · kq | 0, 1 |
| 1 | (Y + X) · kq | 1, 1 |
| 2 | (X − Y) · kq | 1, 0 |
| 3 | (−X − Y) · kq | 0, 0 |

Drawn as a fan (kind 7, 4 vertices, 0x8003F620) with the texture
`anim_object` + 0x944 (0x8006F994) and a colour

```
a      = min(x20, 1)
colour = (int(f32(a · 255)) << 24) | 0xA0FF40       // 0x80012908; ARGB: green 0xFF, red 0xA0, blue 0x40
```

through 0x8003CED0. At t = 0.662 it is 0x60A0FF40, at t = 5.806 0xFFA0FF40.

### 7.2 The mesh

Objects +0x34 (0x80040440) and +0x3C (0x80040EF0), texture `anim_object` +
0x934 (0x8006F984) on stages 0 and 1. Data streams: 0 = unit vectors +0x0C
(12 bytes), 1 = deformed mesh (16 bytes); before that +0x10 and +0x14 are
swapped, so that the mesh just deformed is drawn. One strip (kind 6) over
all 4054 indices. Constants (0x80040520):

| No. | Value |
|---|---|
| 0–3 | (diag(k, k, k, 1) with row 3 = (centre, 1))ᵀ |
| 4–7 | (view · projection)ᵀ, from 0x8006F834 and 0x8006F874 |
| 8 | (0, 1, 2, 0.5) |
| 9 | (eye of the camera 0x8006F790, —) |
| 10 | (k, k, k, 1) (k times scale +0x60) |
| 11 | (1/k, 1/k, 1/k, —) |
| 12 | (centre, 0) |

"—": `blob_draw` does not set the w components of 9 and 11 here; what stands
there is what the stack still holds from the last frame. The measurement
passes these two values along (`not_set_by_blob_draw`).

Colours through 0x800410B0, which clamps every vector to [0, 1], multiplies
by 255 and packs it as four bytes (0x80041100–0x80041245); they go to K0 and
K1 of the block +0x3C (`GPU.md` 4.2, 4.3):

```
br = ((x20 − x1c) · 0.8 + x1c · 1.2) · 4 + 0.3      // 0x800128D0, 0x800128B8, 0x80012900, 0x8006658C
f  = f32(min(4 t, 1) · br)
c0 = (colour.rgb · f, 1),   c1 = (0, 0, 0, 1)
```

### 7.3 The drops

Objects +0x38 and +0x40, data stream 0 = small mesh +0x00. Constants 4–7 as
above, 8 = (0, 1, 2, 0.5), 9 = (eye, 1). Colours:
c0 = (colour.rgb · f32(0.3 x20), 1), c1 = (colour.rgb · 0.2, 1),
c2 = (2, 2, 2, 2). Then per drop d one strip over 346 indices with

```
σ   = d.size / √d.scale
z   = f32(d.scale · d.size − σ)
c10 = (d.pos, 0)
c11 = (d.dir, 1)
c12 = (σ, σ, σ, 1)
c13 = (z · d.dir, 1)
```

Program +0x38 (`GPU.md` 3.5) puts the point p of the small sphere at
c10 + σ · p + (p · c11) · z · c11: a sphere of radius σ, stretched by z
along its direction, longer and thinner for `scale` > 1.

## 8. Weighted centre

0x80058D1F, called from `chamber_light_view` (0x8005FDF9) and
`prim_group_draw` (0x8005EE08) with a reference point `ref` on the stack
(`Chamber.md` 8). The centre is described there and checked bit-exactly;
its points, blob centre and drop positions, are part of the
model too. The second output (0x80058D5A–0x80058DEC):

```
w_0 = 1 / |ref − centre|²,  w_i = 1 / |ref − d_i.pos|²      (x87, unrounded)
m   = f32(x20 · w_0 · 4);   for every drop: m = f32(x20 · w_i + m)
Σ   = f32 sum of the f32(w)
second output = f32(m · (1 / Σ))
```

The blob counts four times as much as a drop here. **No caller reads the
value:** `chamber_light_view` passes ebp+0x1C and never reads it afterwards
(only access 0x8005FE2F), `prim_group_draw` overwrites ebp+0x5C right after
the call with 2 · x20 (0x8005F15F–0x8005F16F). It is measured nevertheless:
in `c6_loop` and `c6_poke` both outputs of all calls equal the model at
every target (at t ≈ 4.0, 273 calls each: 2 from the pre-pass, 271 parts).
At t ≈ 1.0 it is 0, because x20 is 0 there; `anim_draw` then does not draw
the chamber, and only the two calls of the pre-pass remain.

## 9. Precision of the arithmetic

- **FPU** with a 53-bit mantissa (`Timing.md` 4.2). The model computes in
  `double` and rounds to float32 where the code stores. Unrounded in the
  register stay, among others, s in `spawn` (for s² and 1/s²), v in
  `drop_update` (for the spring), h and q in `step`, g in `deform` and e
  before the comparison `e < 0`.
- **SSE.** Sine 0x80057F9D and square root 0x80057F42 as in `Chamber.md` 11;
  the root goes through `rsqrtss` and is exact in xemu, an approximation on
  hardware (instruction reference). The bit identity therefore holds for
  xemu.
- **Order.** The sums in `deform` run over the bulges from the last to the
  first; `normalize_approx` and `mask` compare with float32 constants from
  the ROM, the model reads them there (`rom_f32`).

## 10. Measurement

`probe_c6.py` stops at every call of `blob_update`, reads t and dt from the
stack, t, x1c, x20 from the main object and the whole blob. At chosen times
it also reads, in `anim_draw`, the deformed mesh, in `blob_draw` the colour
(0x80058592, eax), the glow surface (0x800586ED) and every call of
0x80040520 and 0x800410B0, and once the meshes from `blob_setup`. At the
targets it also stops at every call of the weighted centre (0x8005FE3E,
0x8005F157) and at its return. In every frame it reads the state of the
generator 0x800708BC. `--loop` sets the byte `this+0xA` (0x8006F05A) to 1 at
the first stop, so that the animation begins anew at t ≥ 8. `--rand-poke`
writes a state into the generator.

| Run | Targets (t) | Frames | Result |
|---|---|---|---|
| `c6_main` | 0.65, 1.2, 2.0, 3.0, 4.0, 5.0, 5.8 | 376 | 42 of 42 checks equal, 376 of 376 states |
| `c6_tail` | 1.0, 5.85, 5.92 | 447 | 17 of 17 equal, 447 of 447 states |
| `c6_plain` | none | 460 | 460 of 460 states, up to t = 5.926 |
| `c6_loop` | 1.0, 4.0 in every pass, `--loop`, end after 1000 frames | 1000 | 38 of 38 equal, 1000 of 1000 states and generator states; `blob_restart` in frames 391 and 776 |
| `c6_poke` | 1.5, 3.0, `--rand-poke 2.0:0xF0410209` | 340 | 20 of 20 equal, 340 of 340 states and generator states; branch `\|dir\|² < 0.001` taken |

Each check is byte by byte, without filler bytes and GPU pointers: all
fields from section 2 from +0x30 on, all 32 bulges and all eight drops. At
every target come, in addition, the deformed mesh (6936 values), colour and
quad of the glow surface, all 13 constant calls (204 values, without the two
"—") and both colour sets through 0x800410B0. The states of the blob at the
targets are in `vectors/c6_blob.json` (bulges, drops, glow surface,
constants), plus every (t, dt) sequence with which the model can be
recomputed.

**Measurement artefact.** A capture stops the guest for so long that the
next dt lies at about 0.18 instead of 0.011 (all nine captures in `c6_main`
and `c6_tail`). After a late capture t therefore jumps over 5.936, and
`blob_update` is not called any more; that is why `c6_main` and `c6_tail`
end at t = 5.806 and 5.870. The model gets the same dt and stays equal.
`c6_plain` without captures shows the regular end.

## 11. Open

- ~~The GPU objects +0x34, +0x38 and +0x3C, +0x40, the stage states and the
  textures.~~ Done: `GPU.md` 3.4, 3.5, 4.3, 5; textures `Textures.md`
  4.2, 4.5; order `FrameOrder.md` (D2).

Also done since: the second output of the weighted centre
(section 8), the branch `|dir|² < 0.001` and the unreachability of the
branches for t < −1 (section 5.2), `blob_restart` on a restart (section 10).
