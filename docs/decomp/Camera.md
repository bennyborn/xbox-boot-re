# Camera

How the animation guides its camera: a path of keys, evaluated at a path
time s that follows from t, plus view and projection matrix. Two sub-objects
of `anim_object` (0x8006F050) carry this: the camera at +0x6E8 =
**0x8006F738** (`cam`) and the camera path at +0x1594 = **0x800705E4**
(`path`). The time t is in `Timing.md`, the parts of the chamber in
`Chamber.md`.

Evidence as in `Chamber.md`: run `c5_main`, model
`tools/xemu/chamber_model.py` (class `CamPath`, functions `path_time`,
`view_toward`, `perspective`), vector file `vectors/c5_chamber.json`. At all
five times all keys of the path with their tangents, eye, target, view
matrix and projection matrix agree with the guest bit-exactly. The data of
the path is in `data/camera/`, made by `tools/extract/chamber.py`.

## 1. The camera object

| Offset | Content | Written by |
|---|---|---|
| +0x40, +0x50 | two bytes that 0x8005B2C0 and 0x8005B318 set to 0; +0x50 = 1 as soon as +0x7C is valid | 0x8005B061, 0x8005B06A |
| +0x44, +0x48, +0x4C | near, far, aspect factor | 0x8005B318 |
| +0x58, +0x64, +0x70 | eye, target, up | 0x8005B2C0 |
| +0x7C | view · projection, on demand | 0x8005B06A |
| +0xBC | inverse of the view (row 3 = position of the camera, 0x8006F824) | 0x8005B22E |
| +0xFC | view | 0x8005B093 |
| +0x13C | projection | 0x8005B18C |

`camera_view_proj` (0x8005B06A) computes the product only if +0x50 is still
0, and delivers +0x7C. The draw functions fetch it through 0x8006F738.

## 2. Projection

The set-up (0x8005768C) calls `camera_set_projection` (0x8005B318) with the
opening angle f32(π · 0.25) (0x80065E4C, 0x8001289C), aspect factor 0.75
(0x80012898) or 0.5625 (0x800128E0), near 0.4 (0x800128AC) and far 800
(0x800128DC). 0.5625 holds if the byte `anim_object` + 0x181E is set
(`Overview.md`); in our runs it was 0. `mat4_perspective` (0x8005B18C):

```
c = cos(fov/2), s = sin(fov/2)        (FPU, rounded to f32)
cot = c / s                            (unrounded in the register)
q   = far / (far - near)               (unrounded in the register)
P = | f32(cot · aspect)  0          0              0 |
    | 0                  f32(cot)   0              0 |
    | 0                  0          f32(q)         1 |
    | 0                  0          f32(-(q·near)) 0 |
```

q arises at 0x8005B1CC–0x8005B1D2 (`fld far`, `fsub near`, `fdivr far`),
goes to P[10] with `fst` (0x8005B209) and stays in the register: the product
with near (0x8005B21B, `fmul`, `fchs`, `fstp`) takes the unrounded value.
For the projection of the camera (0.4, 800) and that of the light views (1,
500) that gives the same as with a rounded q, for that of the cube texture
(0.1, 400, `Chamber.md` 9) it does not: P[14] = 0xBDCCD9E9 instead of
0xBDCCD9EA. Measured in the stream of `c8_main2`: with a rounded q, 251
constants of the 2598 draw calls of the set-up deviate by one to two units,
with an unrounded one none (`core/test/check_k6.py`).

Screen y is therefore cot, screen x cot · 0.75: the aspect factor is height
by width. Values: P[0] = 1.81066, P[5] = 2.41421, P[10] = 1.0005,
P[14] = −0.4002.

## 3. View

`camera_set_view` (0x8005B2C0) stores eye, target and up, and builds, with
`mat4_view_toward` (0x8005B093), the view to +0xFC and its inverse to +0xBC:

```
z = norm(target - eye)           // viewing direction
x = norm(z × up)
y = x × z
V = | x.x  y.x  z.x  0 |
    | x.y  y.y  z.y  0 |
    | x.z  y.z  z.z  0 |
    | -eye · (columns 0..2)    1 |
```

`norm` is 0x8005816D (length through 0x80057F42, `Chamber.md` 11), `×` is
0x8005A662. The camera therefore looks along +z of its space, x goes to the
right, y up. `anim_update` always passes (0, 0, 1) as up.

## 4. Path time

In path mode (byte `anim_object` + 0xB not 0; the constructor sets it to 1,
0x80056ACA; 1 in all runs) `anim_update` (0x800571AD) computes:

```
a = f32(t · 0.2833);  if a > π/2: a = π/2           // 0x800128CC, 0x800128C8
s = f32(sin_sse(a) · t · 0.2 + f32(t · t · 0.16667 · 0.8))   // 0x800128B0, 0x800128D4, 0x800128D0
```

s therefore runs slowly at first (both terms small) and then grows like t².
At t = 8, s = 10.13; at t = 7.5, the start of the short version, 9.0.

## 5. The camera path

### 5.1 Table in the ROM

0x80070DB8, 0x15 = 21 keys of 0x1C bytes (`data/camera/campath.bin`):

| Offset | Content |
|---|---|
| +0 | time as a byte; 0 begins a new path |
| +1, +2 | cut a and split b as signed bytes, ·0.01 (section 5.4) |
| +4 | eye, 3 × float32 |
| +0x10 | target, 3 × float32 |

`campath_construct` (0x8005AFB6) unpacks them to 0x800708C8 into keys of
0x3C bytes (15 float32): time, eye, tangent of the eye, target, tangent of
the target, a (+0x34), b (+0x38). The time becomes
`f32(byte · ((8 − 2) − 0.8) · 0.01)` (0x8006675C, 0x80066760, 0x80066764,
0x80012980). `path+0x238` = 21, `path+0x23C` = number of paths = 4 (time 0
at the keys 0, 5, 12, 17). In all keys of the ROM target, a and b are 0.

### 5.2 Selection

`campath_select` (0x8005A6CB) with −1 draws a random number from the
generator of the main object (0x8005A645 on 0x8006F6DC, `Timing.md` 4.1),
takes the lower 31 bits and of those the remainder by the number of paths.
At the first call that gives path 0 (`Overview.md`). Then:

```
path+0x240 = path;  path+0x244 = first key;  path+0x24C = number n of keys of the path
path+0x248 = n + 8
```

With an argument ≥ 0 `campath_select` draws no number: the test at
0x8005A6D4–0x8005A6DB skips the call of 0x8005A645, and the path is then the
argument, from the number of paths on its remainder
(0x8005A6EF–0x8005A6FC). Both callers pass −1 (`push -1` at 0x800572F9 in
`anim_update` and at 0x8005B053 in `campath_construct`); the branch is
therefore never taken in the original. The workbench uses it for choosing
the path (`xb_select_path`, `web.md` 11).

`campath_key` (0x8005A69F) delivers key i: for i < n the key `first + i` of
the table at 0x800708C8, otherwise the own key i − n at
`path + 0x3C (i − n)`. Path 0 has five keys with the times 0, 1.04, 2.08,
3.12, 4.68.

### 5.3 The eight own keys

To every path `campath_select` appends eight keys that lead the camera
outwards from the last point of the path:

```
time_j = f32(j · 0.8 · (1/7) + (8 − 2 − 0.8)),  j = 0..7         // 0x80012978; that is 5.2 .. 6.0
v      = (eye[n-1] − eye[n-2]) · f32(1 / (time[n-1] − time[n-2]))     (for n < 2: 0)
e      = eye[n-1] + v · f32((time_0 − time[n-1]) · 0.7)           // 0x80012958
ℓ      = |e|;  d = e · f32(1/ℓ)
ρ      = f32(ℓ · 1.2 + 95);  if ρ < 195: ρ = 195                 // 0x800128B8, 0x80012974, 0x80012970
eye_0  = d · f32(ρ − 95)                                          // 0x8001296C
m = −d;  x = norm(m × (0,0,1));  o = x × m
F = rows x, m, o;  origin U = m · f32(−132.14 − ρ)                // 0x80012968
eye_j  = (0, Y_j + 132.14, X_j) · F + U,  j = 1..7               // 0x80012964
look-at point B = (0, 132.14, 25) · F + U                         // path+0x22C
```

with the numbers on the stack of 0x8005A6CB (as immediates in the code):

| j | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| X_j | 0.322 | 1.821 | 2.323 | −11.926 | −39.973 | −60.774 | −90.795 |
| Y_j | 30.548 | −70.819 | −150.298 | −220.64 | −243.021 | −261.441 | −287.773 |

An eighth value 95.0 stands there as well but is not read. In the formula
for time_j, 8, 2 and 0.8 are at 0x8006675C, 0x80066760 and 0x80066764
(0x8005A769–0x8005A77D), in that for m the −1 at 0x80012894. The frame `F`
with `d · ρ` as row 3 is at `path+0x1E0`, U at `path+0x220`.
`campath_select` creates F with `mat4_identity`, copies x, m and o into the
rows 0 to 2 and m to row 3, which it then multiplies with `vec3_scale` and
−ρ (`fld [ebp+8]`, `fchs`): row 3 = (f32(m_i · −ρ)), that is f32(d_i · ρ)
(0x8005A9CD–0x8005AA0F). Column 3 stays (0, 0, 0, 1). Measured in `c7_main`
and `c7_b` (field `frame_matrix`, 0x800707C4): all 16 values bit-identical.
Only `logo_draw` reads row 3 (`Logo.md` 1). Target, a and b of the own keys
are 0. In addition:

```
path+0x250 = time of key n+2   (own key 2, 5.4286)
path+0x254 = 1 / (time of key n+5 − path+0x250)   (2.91666)
```

For path 0, ℓ = 65.157 and ℓ · 1.2 + 95 = 173.19, hence ρ = 195; the first
own eye lies at (28.72, −80.80, −51.45), the last at (123.01, −346.07,
−326.22) and B at (60.31, −169.68, −78.88). At the end the camera therefore
moves backwards away from the blob and raises its view to B in doing so.

### 5.4 Tangents

For all n + 8 keys, those of the table too (they are overwritten in the
process):

```
T_i = 0
i > 0:      T_i += (P_i − P_{i−1}) · f32((b_i + 1)(1 − a_i) · 0.5)
i < n+7:    T_i += (P_{i+1} − P_i) · f32((1 − b_i)(1 − a_i) · 0.5)
```

for the eye and likewise for the target (0x8005AB2B–0x8005AC07: `fld1`,
`fsub [key+0x34]`, times `[key+0x38] + 1` or `1 − [key+0x38]`, times 0.5
from 0x80010A18). a therefore cuts both chord shares equally, b shifts the
weight between the incoming and the outgoing chord. Since a and b are 0
everywhere, T_i is the mean of the two neighbouring chords (half of one at
the edge).

### 5.5 Evaluation

`campath_eval` (0x8005AC32) at the path time s:

```
if s > 8 − 2:  eye = own key 7, target = B;  this+0xD = 0, this+0xE = 1;  done
i = first index from 1 with s < time_i;  none: as above, but eye = last key
dt   = f32(time_i − time_{i−1});  if dt < 0.001: 0.001                   // 0x800128A0
prev = time_{i−1} − time_{i−2} (for i = 1: dt);  if prev < 0.001: 0.001
u  = clamp((s − time_{i−1}) / dt, 0, 1)
h  = 3u² − 2u³
u' = (s − time_{i−1}) / ((1 − h) · prev + h · dt)
with U = f32(u'), U2 = f32(u'²), U3 = f32(u'³), T2 = f32(2u'³), T3 = f32(3u'²):
  h00 = (T2 − T3) + 1;  h10 = (U3 − 2 U2) + U;  h11 = U3 − U2;  h01 = T3 − T2
eye = f32(((h01 P_i + h11 T_i) + h10 T_{i−1}) + h00 P_{i−1})           per axis
target likewise from the targets
f = clamp((s − path+0x250) · path+0x254, 0, 1)
w = f32((1 − f32(cos(f32(π f)))) · 0.5)
target = f32(target · f32(1 − w)) + B · w
this+0xD = (i < n + 7);   this+0xE = (i > n − 1)
```

u' blends the length of the previous segment into that of the current one,
so that the speed does not jump at the keys. The intermediate values stand
as the disassembly stores them (0x8005AD72–0x8005AE1F); Python `double` for
the rest.

Constants: 8 − 2 from 0x8006675C and 0x80066760 (0x8005AC38), 0.001 from
0x800128A0, the 3 in h and T3 from 0x800128A8 (0x8005ADB5, 0x8005ADF4), the
1 from 0x8001288C, π of the blend from 0x80066754 (0x8005AF20), cos(π f)
with `fsincos` (0x8005AF44), 0.5 from 0x80010A18. The switches are set by
0x8005AF87–0x8005AFAD: `this+0xE` with `setg` against `path+0x24C` − 1,
`this+0xD` with `setl` against `path+0x248` − 1.

A branch for i = 0 sets eye (0, −90, 0) (−90 from 0x8001297C) and target
(0, 0, 0) (0x8005ACAD–0x8005ACE0). It cannot be reached: the search begins
at i = 1 (0x8005AC79–0x8005AC7B), and without a match the case "none" holds.

The switches: `this+0xD` controls updating and drawing of chamber and blob
(`Chamber.md` 1), `this+0xE` the logo (`Logo.md` 1). For path 0 that means:
the logo comes as soon as s ≥ 4.68 (t ≈ 5.225), chamber and blob end as soon
as s ≥ 5.8857 (t ≈ 5.936). The targets of all keys are 0; the target is
therefore the origin until the blend to B begins from s = 5.4286
(t ≈ 5.675); from s = 5.7714 (t ≈ 5.872) it is B.

### 5.6 Values at the measuring points

From `vectors/c5_chamber.json` (guest, bit-identical with the model):

| t | s | Segment i | Eye | Target |
|---|---|---|---|---|
| 0.784 | 0.1165 | 1 | (11.535, −32.477, 32.550) | (0, 0, 0) |
| 2.049 | 0.7845 | 1 | (12.849, −36.155, 28.243) | (0, 0, 0) |
| 3.024 | 1.6763 | 2 | (14.868, −41.836, 16.103) | (0, 0, 0) |
| 4.500 | 3.5609 | 4 | (16.338, −45.953, −19.409) | (0, 0, 0) |
| 5.642 | 5.3725 | 7 | (62.001, −174.429, −109.802) | (0, 0, 0) |

At t = 5.642, f < 0 and w = 0; the target is still the origin.

## 6. Orbit values and free camera

In path mode `anim_update` keeps four orbit values up to date after every
evaluation of the path (0x80057435–0x80057587), in every frame, even if
`this+0xD` is not set. With e = eye from `campath_eval`:

```
len  = sqrt_sse(f32((e.x² + e.y²) + e.z²))            // 0x800568E5, 0x80057F42
+0x690 = len
+0x6A0 = asin_approx(f32(e.z · (1 / len)))             // reciprocal unrounded, 0x80057E2D
q    = sqrt_sse(f32(e.y² + e.x²))
c    = f32(e.x · (1 / q)),  s = f32(e.y · (1 / q))     // 1 from 0x8001288C, reciprocal unrounded
old  = +0x698
+0x698 = f32(angle_from_sin_cos(s, c))                 // 0x80056969
if |old| < 1000 and |f32(+0x698 − old)| > π:           // 0x800128C4, 0x80065E4C, absolute value 0x80057F89
    if old − π > +0x698:  v = +0x698;  repeat v = v + 2π as long as old − π > v;  +0x698 = f32(v)
    if old + π < +0x698:  v = +0x698;  repeat v = v − 2π as long as f32(old + π) < v;  +0x698 = f32(v)
+0x6A8 = sqrt_sse(f32((d.z² + d.y²) + d.x²)),  d = centre of the blob − e   // 0x80056950, 0x8005691F
```

The centre of the blob is `anim_object` + 0xBB0 (`Blob.md` 2, stays
(0, 0, 0)); + 0x6A8 is fixed before `blob_update` of this frame. The loops
compute in the register and store only at the end; the second compares with
the stored f32(old + π) (ebp + 0x5C), its entry with the unrounded one. NaN
takes none of the branches.

`angle_from_sin_cos` (0x80056969) with the constants −1 (0x80012894), 1
(0x8001288C), π (0x80065E4C), −0.5 (0x80012890), 0.5 (0x80010A18), 0
(0x80065E50, 0x80012880), 0.1 (0x80012888), −0.1 (0x80012884):

```
if s ≤ −1:  π · −0.5          if s ≥ 1:  π · 0.5
if c ≤ −1:  π                 if c ≥ 1:  0
if −0.1 < s < 0.1:
    r = asin_approx(s)
    if c < 0:  r = π − r;  if r > π:  r = r − 2π
otherwise:
    r = acos_approx(c)                                  // 0x80058039
    if s < 0:  r = −r
```

The value stays in the register; the caller stores it.

**Initial values.** `anim_construct` sets + 0x6A0 = 0, + 0x698 = π and
+ 0x690 = 90 (0x42B40000) and then swaps each of the three fields with its
neighbour + 0x6A4, + 0x69C, + 0x694 (0x80056B71–0x80056BDC). The neighbours
are 0 in the image; after the constructor + 0x690, + 0x698 and + 0x6A0
therefore stand at 0 and + 0x694 = 90, + 0x69C = π, + 0x6A4 = 0. Measured in
`c8_main2`, frame 1: + 0x694 = 90, + 0x69C = 0x40490FDB, + 0x6A4 = 0.

**Measurement.** In the 20 frames of `c8_main2` in which the measurement
read the object, all four values equal the core bit for bit
(`vectors/c8_gpu.json`, `effects`, field `track`; `core/test/test_k6.c`).
The angle lay at −1.2292 to −1.2296 in all of them; the branch with
asin_approx and the adjustment by whole turns did not occur on path 0 and
are evidenced from the disassembly only. + 0x690 and + 0x6A8 had the same
bit pattern in all frames, although their sums are bracketed differently.

If + 0xB equals 0, `anim_update` takes an orbit around the origin instead of
the path: elevation angle + 0x6A0 limited to [−π/2, π/2] (0x80010A18,
0x80012890), distance + 0x690 at least 10 (0x800128D8),
eye = (0, + 0x690, −10) · (rotation about x with + 0x6A0) · (rotation about
z with + 0x698), target (0, 0, 0). What sets + 0xB to 0 has not been found;
in the runs it was 1.

## 7. Further views

The animation also sets the camera for intermediate pictures and restores it
afterwards (copy of the 0x5F doublewords from 0x8006F738):

- two views from the blob downwards and upwards, for the pre-pass of the
  chamber (`Chamber.md` 8; recomputed bit-exactly),
- six cube faces from the origin, for the cube texture of the chamber
  (`Chamber.md` 9; face matrices 0x80061418, `Textures.md` 5).

## 8. Open

- Paths other than path 0 occur only after a repetition (`Timing.md` 3),
  which did not occur in the runs. Their keys are in
  `data/camera/campath.json`; the model computes them like path 0, only
  path 0 is checked.
