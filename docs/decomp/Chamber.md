# Chamber

The chamber around the glowing blob: 271 parts of six basic shapes, some of
which move into place and turn in the first seconds. It is the sub-object at
`anim_object` + 0x968 = **0x8006F9B8** (`ch` in what follows). The camera is
in `Camera.md`, the time t in `Timing.md`.

Evidence: run `c5_main` (`tools/xemu/probe_c5.py`), model
`tools/xemu/chamber_model.py`, comparison `tools/xemu/vectors_c5.py`, vector
file `vectors/c5_chamber.json`. The model is written after this text. It
meets every checked value in the guest bit-exactly: all placements and the
whole geometry of both detail levels, plus, at five times (t = 0.784, 2.049,
3.024, 4.500, 5.642), the track states and the world matrix of every part
drawn. In all that is 107 checks with 172 014 values. The data from the ROM
is in `data/chamber/`, made by `tools/extract/chamber.py`.

Coordinates: z points up (the up vector of the camera is (0, 0, 1),
`Camera.md`), the blob sits at the origin. Matrices are 4 × 4, stored row by
row, and act on row vectors: v' = v · M, the translation stands in row 3.
`M · N` means M first, then N (0x800566CF).

## 1. Sequence

| When | Function | What |
|---|---|---|
| set-up (0x8005768C) | 0x8005FC65 `chamber_setup` | unpack the data, build twelve groups, textures and depth buffers for the pre-pass |
| set-up, after that | 0x800614E9 `texgen_chamber_cube` | the chamber at the fixed time 5.35 into a cube texture (section 9) |
| every frame, if `this+0xD` | 0x8005F9F1 `chamber_update` | tracks at time t, choice of the detail level |
| likewise, if `this+0x181C` | 0x800600F5 `chamber_prepass` | two views from the centre of the blob (section 8) |
| every frame, if `this+0xD` and x20 > 0 | 0x8005F897 `chamber_draw` | all parts of the chosen detail level |
| tear-down | 0x8005DF9B `chamber_destroy` | release groups, buffers and textures |

`this+0xD` is set by the camera (`Camera.md` 5.5). The condition x20 > 0 is
in 0x80056D0B (comparison with 0x80012880 = 0.0); x20 comes from `Timing.md`
4. `this+0x181C` is set to 1 by the constructor (0x80056ACA); it was 1 in
all runs.

## 2. The data in the ROM

Everything lies without gaps from 0x800743D0 to 0x80078503
(`data/chamber/scene.bin`, decoded in `scene.json`):

| Address | Content | Read in |
|---|---|---|
| 0x800743D0 | 29 index lists of the rotation tracks, 0x5A bytes each | 0x8005FC65 |
| 0x80074E08 | 32 index lists of the position tracks, 0x5A bytes each | 0x8005FC65 |
| 0x80075948 | number of positions, 0x140 = 320 | 0x8005FC65 |
| 0x80075950 | 320 positions of 3 × int16 | 0x8005DEB1 |
| 0x800760D0 | number of quaternions, 0x19D = 413 | 0x8005FC65 |
| 0x800760D8 | 413 quaternions of 3 × int16 | 0x8005DDE2 |
| 0x80076A88 | 413 sign bits for the quaternions, 13 doublewords | 0x8005DDE2 |
| 0x80076AC0 | placements of the spheres, 28 of 0x10 bytes | 0x8005D99D |
| 0x80076C80 | shapes of the spheres, 2 of 1 byte | 0x8005DBB3 |
| 0x80076C88 | placements of the cylinders, 47 of 0x14 bytes | 0x8005E6E6 |
| 0x80077034 | shapes of the cylinders, 6 of 2 bytes | 0x8005DC04 |
| 0x80077040 | placements of the boxes, 72 of 0x18 bytes | 0x8005EA78 |
| 0x80077700 | placements of the rings, 84 of 0x10 bytes | 0x8005EC3E |
| 0x80077C40 | shapes of the rings, 7 of 8 bytes | 0x8005DD04 |
| 0x80077C78 | placements of the frustums, 34 of 0xC bytes | 0x8005E8B4 |
| 0x80077E10 | shapes of the frustums, 3 of 0x10 bytes | 0x8005DC6C |
| 0x80077E40 | placements of the lathes, 6 of 0xC bytes | 0x8005E8B4 |
| 0x80077E88 | shapes of the lathes, 5 of 0x14C bytes | 0x8005DD58 |

Addresses and counts of the groups are immediates in 0x8005F7B8, those of
the tracks and numbers in 0x8005FC65 (loop end 0x80074E02 and 0x80075948,
step 0x5A).

Plus constants in the text section:

| Address | Value | Purpose |
|---|---|---|
| 0x800129A8 | 3.0534e-05 (0x38001203) | factor of the quaternions |
| 0x8006699C, 0x800669A0, 0x800669A4 | 0.002755, 0.002755, 0.00244 | factors of the positions per axis |
| 0x800669A8, 0x800669AC, 0x800669B0 | −0.159046, −0.741611, 2.155624 | offset of the positions per axis |
| 0x80066984, 0x80066988, 0x8006698C | 0.004131, 0.008252, 0.004421 | factors of the placement positions per axis |
| 0x80066990, 0x80066994, 0x80066998 | −27.844984, −0.228729, 0.497086 | offset of the placement positions per axis |
| 0x800669C0, 0x8001289C, 0x800669C4 | 0.6, 0.25, 4.5 | time axis of the tracks |
| 0x800129B0 | 28.0 | sample points per time axis |
| 0x800669B4, 0x800669B8, 0x800669BC | 8.0, 2.0, 0.8 | threshold of the detail level |
| 0x800669C8 | 0.0 | threshold of the side byte |
| 0x8006697C | π | angles of the generators |

## 3. Unpacking

`chamber_setup` stores the unpacked data in `ch`:

```
ch+0x60          quats      = decode_quats(0x800760D8, bits 0x80076A88, 413)
ch+0x64 + 4k     turn_list[k] = expand(0x800743D0 + 0x5A k), k = 0..28
ch+0xD8          positions  = decode_positions(0x80075950, 320)
ch+0xDC + 4k     shift_list[k] = expand(0x80074E08 + 0x5A k), k = 0..31
```

**Index lists** (0x8005DF27, `expand`): 30 values of 16 bits. A byte b is a
signed difference from the predecessor; b = 0x7F introduces a difference
from the two following bytes, higher byte first. The first value is the
difference from 0. The longest list needs 34 of the 0x5A bytes.

**Positions** (0x8005DEB1): per axis `p = f32(i * k + o)` with factor and
offset from the table above, computed in the FPU, rounded once.

**Quaternions** (0x8005DDE2), (x, y, z, w):

```
x = f32(i0 * K);  y = f32(i1 * K);  z' = i2 * K;  z = f32(z')
w = sqrt_sse(f32(((1 - x*x) - y*y) - z'*z'))
if bit n of the sign bits is 0:  w = f32(w * -1)          // 0x80012894 = -1.0
```

For z the unrounded value z' goes into the sum; it stays in the register
after the store. `sqrt_sse` is 0x80057F42 (section 11).

**Quaternion 238 is invalid.** It is (0, 0, 32750): z' = 32750 · K is
slightly greater than 1, the argument of the root −1.1e−7, and w becomes
NaN. In the guest (0, 0, 1.0, NaN) stands there with the pattern 0xFFC00000.
The rotation tracks 11, 16 and 24 begin with index 238 (track 11 also in
second place). Consequences in section 6.

## 4. Groups and shapes

### 4.1 The six groups

Before that the constructor 0x8005FC65 fetches a group object of 0x6C bytes
(0x80055FF8, zeroed) for each of the six places and both detail levels and
calls 0x8005C968 on it, which sets the first seven doublewords to 0
(0x8005FC6F–0x8005FC96). 0x8005F7B8 `chamber_build_groups` builds every
group with 0x8005F3DB `prim_group_build`, once with detail level 0 to
`ch+0x00 … ch+0x14` and once with detail level 1 to `ch+0x18 … ch+0x2C`:

| Place | Basic shape (kind) | Shapes | Parts | Vertex format | Vertices level 0 / 1 | Indices level 0 / 1 |
|---|---|---|---|---|---|---|
| 0 | sphere (0) | 2 | 28 | 1 | 416 / 104 | 788 / 184 |
| 1 | cylinder (1) | 6 | 47 | 0 | 456 / 244 | 778 / 400 |
| 2 | box (2) | – | 72 | 0 | 24 / 24 | 36 / 36 |
| 3 | ring (3) | 7 | 84 | 0 | 1912 / 478 | 3956 / 1022 |
| 4 | frustum (4) | 3 | 34 | 0 | 159 / 86 | 246 / 128 |
| 5 | lathe (5) | 5 | 6 | 1 | 2050 / 1066 | 4176 / 2088 |

The numbers of vertices and indices are measured in the guest and agree with
the counting formulas below (`vectors/c5_chamber.json`, `static.groups`).

The group object has 0x6C bytes (0x8005FC65 fetches that much):

| Offset | Content |
|---|---|
| +0x00, +0x04, +0x08 | position buffer (12 bytes per vertex), attribute buffer, index buffer |
| +0x0C, +0x10 | shape table (0x18 bytes per shape), number of shapes |
| +0x14, +0x18 | placements (0x11C bytes per part), number of parts |
| +0x1C | primitive kind: 6 (strip), for the box 5 (triangles) |
| +0x20, +0x24 | two textures from the call: +0x20 normal map `anim_object` + 0x940 (stage 0), +0x24 cube of directions + 0x934 or + 0x938 (stages 1 and 2; `Textures.md` 4.2, 4.7). `chamber_build_groups` gives sphere, box, ring and frustum + 0x934 (edge 64), cylinder and lathe + 0x938 (edge 128); only sphere and lathe get the normal map (last two arguments of the six calls, 0x8005F7C1–0x8005F870) |
| +0x28 … +0x54 | three groups of four (0.2079, 1, 0.1, 1); 0.2079 from 0x800129AC, 0.1 from 0x80012888; factors of the three colours on drawing (section 7) |
| +0x60 | vertex format and draw mode (section 7) |
| +0x64 | kind of the basic shape |
| +0x68 | side for mode 3 |

A shape entry of 0x18 bytes holds: first vertex, number of vertices, first
index, number of primitives, number of indices and the length of a cap fan
(cylinder and frustum only). `prim_group_build` first counts all shapes,
creates the buffers and then generates the shapes one after the other into
the same buffers.

**Detail level.** The generators shift the segment counts right by the
level, so level 1 halves them. The number of rings of cylinder and frustum
stays.

### 4.2 Vertex formats

The position buffer holds (x, y, z) as float32. The attribute buffer holds
three vectors a, b, n per vertex (9 float32, format 0, stride 0x24) or,
before them, two texture coordinates (u, v) as well (11 float32, format 1,
stride 0x2C). The generators mostly set b as a cross product; n is the
surface normal for the round shapes. The vertex programs take a, b, n as the
axes of the part space into which they bring light and half vector (`GPU.md`
3.1, 3.2).

### 4.3 The basic shapes

All angles are float32; `cs(a)`, `sn(a)` are cosine and sine in the FPU,
rounded to float32. 2π is π (0x8006697C) doubled. Shape data from
`scene.json`.

**Sphere** (0x8005C9DC; shape: 1 byte segments s). Unit sphere, the poles
lie on the x axis.

```
n = s >> level;  r = n / 2;  dlat = f32(π / (r - 1))
for i = 0 .. r-1:  lat = f32(i * dlat)
  for j = 0 .. n-1:  lon = f32(j * (2π / n))
    p = (cs(lat), f32(sn(lat) * cs(lon)), f32(sn(lon) * sn(lat)))
    a = (0, -sn(lon), cs(lon));  b = a × p;  n = p
    uv = (lon / 2π, f32(1/π) * lat)
```

Counting (0x8005DBB3): vertices r · n, primitives 2(r − 1)(n + 1) − 2,
indices two more. Indices: per row i = 0 … r − 2 the pairs (row i+1,
column j), (row i, column j) for j = 0 … n, the last column wrapped to j = 0
again. The rows join without filler indices.

**Cylinder** (0x8005CBC8; shape: byte rings R, byte segments s). Radius 1,
height 0 to 1.

```
n = s >> level;  for j = 0 .. n:  c_j = cs(a_j), s_j = sn(a_j), w_j = f32(a_j / 2π), a_j = f32(j * (2π/n))
top cap:    j = n-1 .. 0:  p = (c_j, s_j, 1);  a, b, n = unit vectors;  uv = (c_j + 0.5, s_j + 0.5)
bottom cap: j = 0 .. n-1:  p = (c_j, s_j, 0);  a = (1,0,0), b = (0,-1,0), n = (0,0,-1);  uv as above
side: for k = 0 .. R:  z = f32(k * f32(1/R)),  j = 0 .. n:
  p = (c_j, s_j, z);  a = (-s_j, c_j, 0);  b = (0, 0, 1);  n = (c_j, s_j, 0);  uv = (4 w_j, 32 z)
```

Indices: the n indices of the top cap, the n of the bottom cap, then a strip
over the grid of the side (0x800612D3, below). Counted are
(R + 1)(n + 1) + 2n + 2 vertices (0x8005DC04); the generator does not write
the last two.

**Frustum** (0x8005CF51; shape: r0, r1, h as float32, byte rings R, byte
segments s). Radius r0 at z = 0 at the bottom, r1 at z = h at the top.

```
A_j = (f32(c_j r0), f32(s_j r0)),  B_j = (f32(c_j r1), f32(s_j r1))     (c_j, s_j, w_j as for the cylinder)
top cap:    j = n-1 .. 0:  p = (B_j, h);  unit vectors;  uv = B_j + 0.5
bottom cap: j = 0 .. n-1:  p = (A_j, 0);  (1,0,0), (0,-1,0), (0,0,-1);  uv = A_j + 0.5
side: for k = 0 .. R:  f = f32(k * f32(1/R)),  j = 0 .. n:
  p = ((B_j - A_j) f + A_j,  f h)
  a = norm(-B_j.y, B_j.x, 0);  b = norm(B_j - A_j, h);  n = a × b;  uv = (w_j, f)
```

Indices and counting as for the cylinder (0x8005DC6C).

**Box** (0x8005D398). Cube with edge 1 around the origin, six faces of four
corners, two triangles each (0, 1, 2), (0, 2, 3); 24 vertices, 36 indices,
primitive kind 5 (0x8005DCD6, 0x8005F3DB). The code puts the values together
in six cases from 0.5 (0x80010A18), −0.5 (0x80012890), −1 (0x80012894), 0
and 1:

| Face | Corners (in units of 0.5) | a | b | n |
|---|---|---|---|---|
| 0 | (1,−1,1) (1,1,1) (1,1,−1) (1,−1,−1) | (0,1,0) | (0,0,1) | (1,0,0) |
| 1 | (1,1,1) (−1,1,1) (−1,1,−1) (1,1,−1) | (−1,0,0) | (0,0,1) | (0,1,0) |
| 2 | (−1,1,1) (−1,−1,1) (−1,−1,−1) (−1,1,−1) | (0,−1,0) | (0,0,1) | **(0,−1,0)** |
| 3 | (−1,−1,1) (1,−1,1) (1,−1,−1) (−1,−1,−1) | (1,0,0) | (0,0,1) | (0,−1,0) |
| 4 | (−1,1,1) (1,1,1) (1,−1,1) (−1,−1,1) | (1,0,0) | (0,1,0) | (0,0,1) |
| 5 | (−1,−1,−1) (1,−1,−1) (1,1,−1) (−1,1,−1) | (1,0,0) | (0,−1,0) | (0,0,−1) |

The face at x = −0.5 carries (0, −1, 0) as n, not (−1, 0, 0). That is how it
stands in the code and how it stands in the guest. Texture coordinates of
the four corners (format 1, not used here): (0,1), (1,1), (1,0), (0,0).

**Ring** (0x8005D74F; shape: tube radius r as float32, byte segments around
the axis s, byte segments around the tube q). Centre circle with radius 1 in
the xy plane.

```
n = s >> level;  m = q >> level
for i = 0 .. m-1:  α = f32(i * (2π/m));  ρ = f32(cs(α) r + 1)
  for j = 0 .. n-1:  β = f32(j * (2π/n))
    p = (cs(β) ρ, sn(β) ρ, sn(α) r)
    a = (-sn(β), cs(β), 0);  b = (-(cs(β) sn(α)), -(sn(β) sn(α)), cs(α));  n = a × b
    uv = (β / 2π, α / 2π)
```

Counting (0x8005DD04): vertices n · m, primitives 2m(n + 1) − 2. Indices per
tube ring i the pairs (ring i, column j), (ring i+1, column j), j = 0 … n
with wrap; the last ring joins ring 0.

**Lathe** (0x8005E210; shape of 0x14C bytes: up to 19 profile points of
(x, y, z, flags), axis A at +0x130, centre C at +0x13C, byte segments s at
+0x148, byte number of points P at +0x149). Of the flags the generator reads
only bit 0: 1 means smooth, 0 means crease.

```
for every profile point i (successor i+1, after the last 0 again):
  a crease first creates a vertex with kind 0 (normal later from the predecessor)
  then a vertex with kind 1 (crease) or 2 (smooth), with
  N_i = norm((A × (p_{i+1} - C)) × (p_{i+1} - p_i))
normal per profile vertex v: kind 0: N of v-1;  kind 1: N_v;  kind 2: norm(N_{v-1} + N_v)
                             (v-1 before the first is the last)
for k = 0 .. n (n = s >> level):  φ = f32(k * f32(2π / n));  M = rotation about A by φ (0x8005E051, 0x8005C665)
  for every profile vertex v (counter z counts only vertices with kind ≠ 0):
    d = p_v - C;  p = d · M + C
    a = norm((A × d) · M);  n = N'_v · M;  b = n × a
    uv = (10 f32(φ / 2π), 10 f32(z * f32(1 / (P - 1))))
```

Counting (0x8005DD58): vertices (P + creases)(n + 1), primitives
2n(P + 1 + creases) − 2. Indices per segment the pairs (next row, column j),
(this row, column j), with wrap. The five shapes have 19, 6, 14, 6 and 6
points, all with 24 segments.

**Strip over a grid** (0x800612D3, counting 0x8006127E). For a grid of c + 1
columns and r + 1 rows from index a (row distance c + 1):

```
[a, if lead]  a,  then for i = 1 .. c twice a + i
for row k = 0 .. r-1:  a + k(c+1),  then for i = 0 .. c:  a + k(c+1) + i,  a + (k+1)(c+1) + i
                       except in the last row: a + c + (k+1)(c+1)
[a + c + r(c+1), if trail]
```

From 15 columns on the function divides into blocks of 14 columns, with lead
and trail at the seams. Length: 2((c + 2) r + c) plus one index each for
lead and trail. Divided here are the cylinders with 16 and 20 segments in
level 0 and the frustum with 16; the indices in the guest correspond to that
exactly.

## 5. Placement

`prim_group_build` creates an entry of 0x11C bytes per part and fills it
with the function of its kind:

| Offset | Content |
|---|---|
| +0x00, +0x02, +0x04 | shape, position track, rotation track (int16, −1 = none) |
| +0x08 | L: rotation from the fixed quaternion, translation = position |
| +0x48 | L⁻¹ (0x8005B22E, rotation transposed) |
| +0x88 | S: scale |
| +0xC8 | the three scales and 1.0 |
| +0xD8 | S · L |
| +0x118 | side: 1 if the initial height lies above 0 |

The records in the ROM:

| Kind | Size | Fixed quaternion | Position | Shape, position track, rotation track | Scale (x, y, z) |
|---|---|---|---|---|---|
| sphere | 0x10 | none (L without rotation) | int16 +0, +2, +4 | bytes +6, +7, +8 | radius r (+0xC): (r, r, r) |
| cylinder | 0x14 | byte +0 | int16 +2, +4, +6 | bytes +8, +9, +0xA | radius r (+0xC), half height h (+0x10): (r, r, 2h) |
| box | 0x18 | byte +0 | int16 +2, +4, +6 | shape 0, bytes +9, +0xA | (+0x14, +0xC, +0x10) |
| ring | 0x10 | byte +0 | int16 +2, +4, +6 | bytes +8, +9, +0xA | factor (+0xC) in all axes |
| frustum, lathe | 0xC | byte +0 | int16 +2, +4, +6 | bytes +8, +9, +0xA | none: S = 1, S · L = L |

All bytes for indices are signed. The position is `f32(i * k + o)` per axis
with the placement factors from section 2. The rotation of L is the matrix
of the fixed quaternion in the form of 0x8005C790 (below), for the sphere
the identity matrix.

**Side** (+0x118): if the part has a rotation track, the position is rotated
with the matrix of the first quaternion of that track; if it has a position
track, the z component of its first position is added. If the height
obtained like this is greater than 0.0 (0x800669C8), the side is 1. 120 of
the 271 parts have side 1.

**Quaternion to matrix** (0x8005C790), q = (x, y, z, w), every line in the
FPU and rounded once:

```
m0  = ((w² + x²) - y²) - z²      m1 = 2xy - 2zw            m2  = (wy + zx) 2
m4  = (xy + zw) 2                m5 = (y² + (w² - x²)) - z²  m6  = 2zy - 2xw
m8  = 2zx - 2wy                  m9 = (zy + xw) 2          m10 = z² + ((w² - x²) - y²)
```

0x8005C665 delivers the same matrix transposed (used for the lathe).

Of the 271 parts 67 have no track, 18 only a position track and 186 both.
The extent of the positions ranges from −95.5 to 39.8 in x, from −135.4 to
134.9 in y and from −71.9 to 72.9 in z.

## 6. Motion

`chamber_update` (0x8005F9F1) computes per frame from t:

```
q = (t - (0.6 + 0.25)) / 4.5                // 0x800669C0, 0x8001289C, 0x800669C4; in the register
u = f32(q)                                  // fst, 0x8005FA12
k = f32(q * 28)                             // 0x800129B0, 0x8005FA15: q unrounded
i = int(k);  f = f32(k - i)
for every position track:  u <= 0: positions[idx[0]];   u >= 1: positions[idx[29]]
                           otherwise:  f32(f * positions[idx[i+1]] + (1 - f) * positions[idx[i]])  per axis
for every rotation track:  u <= 0: quats[idx[0]];  u >= 1: quats[idx[29]];  otherwise: slerp(quats[idx[i]], quats[idx[i+1]], f)
                           result as a matrix (0x8005C790) to ch+0x1E4 + 64 k
positions to ch+0x1EC + 12 k
detail level: ch+0x5C = (t >= (8.0 - 2.0) - 0.8)  // 0x800669B4 .. 0x800669BC
```

u serves only the comparisons with 0 and 1; k arises from the unrounded
quotient. With k = f32(u · 28), k deviates by one unit for some t, and with
it every moving track. At the five recorded times both readings give the same;
it showed up when checking the frame draw calls, in the frame t = 1.019 of `c8_main2` (1350 of 152 416
constants of the depth for the veil deviated). Measured again in the run
`k6_pose` (`probe_c5.py`, t = 0.933, 1.032, 1.130, 1.441, 1.758): all 29
rotations and 32 positions equal the model with unrounded q bit for bit;
with rounded u up to 27 tracks deviated.

The sample points therefore lie at t = 0.85 + i · 4.5/28, i = 0 … 28. The
value 29 holds only from u ≥ 1 (t ≥ 5.35); in all tracks it equals value 28.
No track moves any more after sample point 23 (t ≈ 4.55). The detail level
changes as soon as t as float32 is greater than 5.2 in double precision.

**Slerp** (0x8005E0B1), everything with the helpers from section 11:

```
d = a·b (4 components, FPU);  if d < 0: b = -b, d = -d
if |f32(f32(d) - 1)| <= 1e-5 (0x800129A0):  q = f32(b f + (1 - f) a)
otherwise:  θ = f32(acos_approx(f32(d)))
            q = f32(sin_sse(f32(θ f)) b + sin_sse(f32((1 - f) θ)) a),  then q = f32(q / sin_sse(θ))
```

**Consequences of quaternion 238.** As long as they stand on index 238, the
rotation tracks 11, 16 and 24 deliver a matrix full of NaN: until
t = 0.85 + 4.5/28 ≈ 1.011 (tracks 16, 24) and 0.85 + 2 · 4.5/28 ≈ 1.171
(track 11; the slerp between 238 and 238 gives NaN as well). 19 parts hang
on these tracks: boxes 4–7, 48, 49, 64, 65; rings 4, 5, 36–38, 60–62;
frustums 22, 30; lathe 3. Measured at t = 0.784: exactly these 19 parts have
a world matrix with NaN (`captures[0].parts_nan`). What the GPU makes of
that is not measured: the calls are in the stream and assigned
(`FrameOrder.md` 4), the measurement does not read their result in the
picture. In this period the chamber is drawn only during the first pulse
(`Timing.md` 4, last entry around 0.73 s).

## 7. Drawing

`chamber_draw` (0x8005F897) sets stage states and calls `prim_group_draw`
(0x8005EE08) for the six groups of the chosen detail level in the order of
the places 0 to 5. `anim_draw` passes the byte `this+0x181C` and 1
(0x80056D46–0x80056D55). The byte frames the groups with two states
(0x8003CD70 with 0x204 and 0x207: shadow comparison "greater" on,
afterwards "always"; `GPU.md` 4.4), the 1 is passed on by `chamber_draw` to
`prim_group_draw` as a switch.

**Colours.** Per part `prim_group_draw` sends, in the same branch as
constant 4, right after the weighted centre, three colours to 0x800410B0
(0x8005F15F–0x8005F1E6), with G1, G2, G3 the groups of four +0x28, +0x38,
+0x48 of the group and the light object L (0x8006F8B4):

```
m  = 2 · x20
K0 = (L+0x24 · G1, …) · m, alpha 1          per component, f32
K1 = (G2 · L+0x04, …) · m, alpha 1
K2 = (G3 · L+0x14, …) · m, alpha 1
```

A second branch sends the same without m once per group
(0x8005EF41–0x8005EFAB, if the argument at ebp + 0x7C and the byte at
ebp + 0x6F are 0). The block turns them into ambient, diffuse and specular
share (`GPU.md` 4.3).

**World matrix W of a part** (0x8005EE08, 0x8005F057–0x8005F0C3):

```
if no position track and no rotation track:   W = (S · L) from +0xD8
otherwise:  L' = L · R[rotation track]     (or L, without a rotation track)
            row 3 of L' += P[position track]  (f32 per axis, nothing without a position track)
            W = S · L'
```

R and P are the results from section 6. The rotation track therefore turns
the part about the origin of the world, not about its own centre, and the
position track moves it afterwards. At the five times,
`vectors/c5_chamber.json` has, for each of the 271 parts, the position (row
3 of W) and the three scaled axes (rows 0 to 2) as the guest passed them to
`prim_group_draw` (breakpoint 0x8005F0FE, W at ebp − 0x98). The model gives
every value bit-exactly.

**To the GPU** then go (0x80040520 with constant number, pointer, count),
with I = inverse of L′ (0x8005B22E; without tracks the field +0x48) and the
light object L:

```
0–3   = transposed(W · camera_view_proj)                   // 0x8005B06A, 0x8005F0FE–0x8005F121
in the modes 0 and 1 also:
4     = (light in part space, 1)
5     = (vec3_transform(row 3 of the inverse view 0x8006F824, I), 1)     // 0x8005F27E–0x8005F2A0
6     = the scale +0xC8                                                   // 0x8005F2A5
8–11  = transposed(W · T),  T = ch+0x164 for side 1, ch+0x1A4 for side 0      // 0x8005F2B5–0x8005F2FA
texture of the side on stage 3: ch+0x15C (side 1) or ch+0x160 (side 0)

with the switch (drawing to the screen), per part, 0x8005F13A–0x8005F225:
    c     = weighted_center(ref = row 3 of W)              // 0x80058D1F, section 8
    m     = 2 · x20                                         // 0x8006F070, fadd st, st
    light = vec4_mul_mat4((c, 1), I), xyz                   // 0x80059A2E
    7     = (L+0x54, f32((1 / f32(m)) · L+0x58), f32((1 / f32(m)) · L+0x5C), 1)
    colours K0, K1, K2 as above, with m
without the switch (cube texture in the set-up), 0x8005EF41–0x8005EFCF and 0x8005F243:
    light = vec3_transform(L+0x34, I)
    7     = (L+0x54, L+0x58, L+0x5C, 1), once per group
    colours without m, once per group
```

The reciprocal 1 / f32(m) stays unrounded in the register. What the programs
compute with the constants is in `GPU.md` 3.1 and 3.2: constant 4 is the
position of the light, 5 the eye, 7 the coefficients of the attenuation, 8
to 11 the matrix into the light view. In the set-up both T are still 0 (the
light views arise only in the first frame), the constants 8 to 11 therefore
0.

**The light object** `anim_object` + 0x864 = 0x8006F8B4 is filled by
`anim_construct` (0x80056BE5–0x80056C69), after which nobody writes it: 0x1A
doublewords 0, then +0x04 = (0.13, 0.13, 0.13, 1) (0x800128A4),
+0x14 = (1, 1, 1, 1), +0x24 = (0, 0, 0, 1), +0x34 = (0, 0, 0), +0x54 = 1,
+0x58 = +0x5C = 0.001 (0x800128A0). A second object of the same form at
+0x8CC (position (0, −40, 30), 0.25, 0.75; 0x80056C71–0x80056D01) follows;
who reads it is not determined. With G = (0.2079, 1, 0.1) therefore K0 = 0,
K1 = 0.13 · G · m and K2 = G · m.

**The two vector helpers.** `vec3_transform` (0x8005C8BB) and
`vec4_mul_mat4` (0x80059A2E) compute v · M in the FPU and round every
component once; the sums in the order of the code:

```
vec3_transform:  x' = ((M20 z + M10 y) + x M00) + M30
                 y' = ((M01 x + M21 z) + M11 y) + M31
                 z' = ((M02 x + M22 z) + M12 y) + M32
vec4_mul_mat4:   x' = ((M30 w + M10 y) + M20 z) + x M00
                 j = 1, 2, 3:  ((M0j x + M3j w) + M1j y) + M2j z
```

**Measurement of the constants** (`core/test/check_k6.py`, stream of
`c8_main2`): in the 3464 draw calls of the chamber to the screen (8 frames),
the 2598 of the set-up and the 4763 for the depth of the veil each of the
443 392 constants equals the core bit for bit, as do the 18 186 packed
colours; 112 values are NaN on both sides with a different bit pattern (the
seven parts on quaternion 238 at t = 1.019).

**Draw modes** (`+0x60`): 0 and 1 are the vertex formats (shaders and
textures from `anim_object` + 0x948/0x958 and +0x94C/0x95C); mode 2 position
only, for the depth (0x8005FC1C); mode 3 only parts whose side equals
`+0x68`, with W as a fixed transform (0x8003E6C0 with 6) instead of
constants (0x8005FC3C, pre-pass).

**Draw calls** (0x8003F6C0 with kind, count, indices) per part the indices
of its shape: cylinder and frustum with two fans (kind 7) of n indices each
for the caps and one strip (kind 6) over the rest; the others with the kind
from `+0x1C` and the count `a · primitives + b` from the table at 0x800655B8
(two doublewords per kind): kind 5 (3, 0), kind 6 (1, 2).

## 8. Pre-pass: two views from the blob

If `this+0x181C` is set, `anim_update` calls 0x800600F5 `chamber_prepass`
after `blob_update`. It saves the camera object (0x5F doublewords from
0x8006F738), calls 0x8005FDF9 `chamber_light_view` for side 0 and 1 and
restores the camera. Per side:

```
ref    = (0, 0, -30) for side 0, (0, 0, 40) for side 1       // 0x800669D0, 0x800669CC
c      = weighted_center(ref)                                 // 0x80058D1F, below
camera = view_toward(c, c + (0, 0, -1 | +1), up (0, +1 | -1, 0))    // side 0 downwards
projection: f32(π · 0.6667) (0x800129B8), aspect factor 1, near 1, far 500 (0x800129BC)
T      = view · projection · X,    X = diag(256, -256, 65535, 1), row 3 = (256.5, 256.5, 0, 1)
                                   // 0x477FFF00, 0x800129B4
T to ch+0x1A4 (side 0) or ch+0x164 (side 1)
drawn: groups of the places 3, 4, 2, 1 in mode 3, only parts of this side,
       into the texture ch+0x160 (side 0) or ch+0x15C (side 1), 512 × 512
```

T is the matrix that `prim_group_draw` multiplies with W as constant 8; the
texture of the side goes on stage 3. Both matrices are recomputed
bit-exactly at all five times (`light_views` in the vector file).

**Weighted centre** (0x80058D1F, called with `ecx` = blob 0x8006FBAC).
Ghidra shows two arguments; but the caller first puts a point `ref` on the
stack as three float32 (0x8005FE23–0x8005FE3D: `sub esp, 0xC` and three
`movsd`), and the function reads it at ebp + 0x10:

```
w(p) = 1 / |ref - p|²                         (FPU, sum z² + y² + x²)
points: centre of the blob (blob+0x54) and its drops (blob+0x878 + 0x38 j, number at blob+0xA34)
Σ    = f32 sum of the f32(w)
acc  = Σ f32(acc + p · f32(w))
c    = f32(acc · (1 / Σ))                     // reciprocal unrounded in the register
```

The blob itself is in `Blob.md`; in the recorded comparison its points come
from guest memory, but the model computes them itself (`Blob.md` 6.2).
A second output (a weight from x20 · 4 and the weights) is not needed in the
pre-pass.

## 9. Cube texture at the fixed time

`texgen_chamber_cube` (0x800614E9) is called in the set-up with edge 256. It
saves the camera, sets the projection π · 0.5 (0x80066AA4, 0x80010A18),
aspect factor 1, near 0.1 (0x80012888), far 400 (0x800129D0), and draws the
chamber from the origin for each of the six cube faces with the view from
0x80061418. The view is set by 0x8005B367 (0x800615DA): it copies the matrix
to camera +0xFC, puts its inverse to +0xBC with 0x8005B22E and sets the
bytes +0x40 and +0x50 to 0. Drawing happens after it has called
`chamber_update` with t = 0.6 + 0.25 + 4.5 = 5.35 (0x80066AAC, 0x8001289C,
0x80066AB0). At this time all tracks stand at their end (u = 1) and the
detail level is 1. The draw call has both switches at 0. The face matrices
are in `Textures.md` 5, the draw calls in `FrameOrder.md` (A2).

## 10. Tear-down

`chamber_destroy` (0x8005DF9B) releases the twelve groups (0x8005C97F:
buffers and tables), the results of the tracks (`ch+0x1E4`, `ch+0x1EC`), the
unpacked data and the two textures `ch+0x160`, `ch+0x15C`.

## 11. Precision of the arithmetic

- **FPU.** Precision control 53 bits (`Timing.md` 4.2). The model computes
  in `double` and rounds to float32 where the code stores. In six places a
  value stays unrounded in the register, and that counts for bit identity:
  z' in the quaternions (section 3), the reciprocal in 0x80058D1F, the
  intermediate values of the curve in 0x8005AC32, cot and q in 0x8005B18C
  (all `Camera.md`) and the quotient of the track time in 0x8005F9F1
  (section 6). The last two were found only once the frame draw calls were checked.
- **Matrix product.** 0x800566CF adds the four products of every element in
  an order of its own, which the model takes over from the disassembly
  (`MUL_ORDER` in `chamber_model.py`).
- **SSE.** 0x80057F42 (root: `rsqrtss` r, then x · r · (1.5 − 0.5 x r²),
  constants 0x80065EA0 = 0.5, 0x80065EB0 = 1.5), 0x80057F9D (sine:
  |x| · 2/π (0x80065EF0), integer part k, remainder f, for odd k 1 − f (1.0
  from 0x80065E80), sign from x and bit 1 of k, polynomial
  ((c3 f² + c2) f² + c1) f² + c0 with c0 … c3 = 0x80065F50 … 0x80065F5C,
  times f), 0x80057E2D (arc sine: series with the factors from 0x80066030
  backwards from entry 0x30 to entry 1, stopping as soon as a term falls
  below 1e−6 (0x80065FB8); the 0x31 factors are in
  `data/overview/asin_factors.bin`, made by `tools/extract/overview.py`; for
  positive x from 0.7071 on through x/√(1 − x²) and 0x80057BA9), 0x80057BA9
  (arc tangent as a continued fraction with `rcpss`, constants 0x80065F84 …
  0x80065FB4) and 0x80058039 (arc cosine as π/2 minus arc sine). These
  functions compare the bit pattern of their argument unsigned. For negative
  values 0x80057E2D therefore never checks the limits ±1 and 0.7071 and
  always takes the series; 0x80057BA9 takes the branch for x ≥ 1 for
  negative values above −1. The model reproduces both.
- **`rsqrtss` and `rcpss`.** xemu computes them as exact 1/√x and 1/x in
  float32 (`target/i386/ops_sse.h`, `helper_rsqrtss`, `helper_rcpss`); with
  that the model is bit-identical. According to the instruction reference
  the instructions deliver only an approximation there, with |relative
  error| ≤ 1.5 · 2⁻¹² (RCPSS, RSQRTSS). On real hardware the values that go
  through these functions therefore deviate in the last places. That is a
  limit of the measurement in xemu; nothing is measured on hardware.
- **`cvttss2si`** gives the value 0x80000000 for NaN (instruction reference,
  CVTTSS2SI); so does the model, otherwise it aborts at quaternion 238.

## 12. Open

- ~~Meaning of the constants 4 to 8, of the three groups of four and of the
  textures on drawing; face matrices of the cube texture.~~ Done
  (sections 4.1, 7, 9; `GPU.md` 3.1, 3.2, 4.3).
- What the rasteriser makes of the NaN matrices (section 6): not measurable
  from guest memory; the calls are assigned (`FrameOrder.md` 4).
- ~~Motion and drops of the blob, from which the weighted centre computes.~~
  Done in `Blob.md`; the second output there in section 8.
