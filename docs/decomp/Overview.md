# Overview

How the kernel lies in memory, what all addresses in `README.md` refer to,
and where the animation lies in it (section "Map"). How the kernel comes out
of the ROM is in `Rom.md`, the time keeping in `Timing.md`.

## The unpacked kernel

`out/rom/kernel.img` from `tools/rom/unpack.py`, 0x9B500 bytes, SHA-256
`6bf7aecdbea49ed67a44214366520b56649f6b395b75ff346d7dd3d7115c407f`.
It is a PE image in memory layout: every section stands in the file at its
RVA, and the file is as long as SizeOfImage. The file offset of an address
is therefore address − 0x80010000.

Values from the PE head (read with pefile, the same in Ghidra):

| Field | Value |
|---|---|
| Machine | 0x14C |
| ImageBase | 0x80010000; the 2BL unpacks to there (2BL 0x4008F7) |
| AddressOfEntryPoint | 0x4504C, that is 0x8005504C |
| SizeOfImage | 0x9B500 |
| SizeOfHeaders | 0x2C0 |
| SectionAlignment, FileAlignment | 0x20, 0x20 |
| Subsystem | 14 |

In the running guest the image stands from physical 0x10000. An address
0x8xxxxxxx of the kernel therefore lies at physical address − 0x80000000.
Measured in `c2final_A`, frame 90 (`vectors/c2_bench.json`,
`kernel_text_in_ram`): there `.text` equals the image except for 174 of
153 136 bytes. What changes those 174 bytes is open.

## Sections

The names stand like this in the section head of the image.

| Name | Start | End (excl.) | VirtualSize | SizeOfRawData | Characteristics | Rights |
|---|---|---|---|---|---|---|
| `.text` | 0x800102C0 | 0x800358F0 | 0x25630 | 0x25640 | 0x68000020 | r-x |
| `.data` | 0x80035900 | 0x8003AECC | 0x55CC | 0x55E0 | 0xC8000040 | rw- |
| `STICKY` | 0x8003AEE0 | 0x8003C870 | 0x1990 | 0x19A0 | 0xC8000040 | rw- |
| `IDEXPRDT` | 0x8003CA00 | 0x8003CB08 | 0x108 | 0x120 | 0xC8000040 | rw- |
| `INIT` | 0x8003CB20 | 0x800AB4FA | 0x6E9DA | 0x6E9E0 | 0xE2000020 | rwx |

Ghidra creates the blocks with SizeOfRawData, that is up to the next 0x20
boundary. `tools/ghidra/import_kernel.py` checks at every import that image
base, start and rights of every section agree with the PE head.

## Ghidra project

Reproducible with

```
python tools/rom/unpack.py
python tools/ghidra/import_kernel.py
```

The project lies in `out/ghidra/` and stays local. Ghidra rejects project
paths in which a folder begins with a dot, hence not in `.tools/`.

The prescript `tools/ghidra/NoForeignNames.java` switches off the analyzers
that bring in names or types from outside the binary: Function ID, Apply
Data Archives, PDB Universal, PDB MSDIA, the TEB model for Windows x86 and
"Non-Returning Functions - Known". `tools/ghidra/headless.py` discards every
run in which the prescript has not taken effect. After the import only two
symbols carry a name that does not follow Ghidra's address scheme
(`out/ghidra/kernel_layout.json.symbols.txt`):

| Address | Name | Origin |
|---|---|---|
| 0x8005504C | `entry` | Ghidra's name for the entry point from the PE head |
| 0x80021CE8 | `__break` | Ghidra's pattern file `Processors/x86/data/patterns/x86win_patterns.xml`, line 146: a function that consists of `int 3` only |

In addition there are the jump table labels `switchD_…`/`caseD_…`, which
Ghidra forms from the address.

## Map of the animation

### What lies in `INIT`

`INIT` begins with two entries of 16 bytes each: the string `XGRAPHC` and
`DSOUND` respectively, then the doublewords 1 and 0x40010F68 (0x8003CB20,
0x8003CB30). Code follows. Up to 0x80044F40 every function begins on a
16-byte boundary (172 functions); behind that, aligned and unaligned
functions mix. `INIT` also holds the entry point of the kernel (0x8005504C),
data (such as the animation object from 0x8006F050) and sample data (8 bit,
from 0x80066AC0, see 0x80061BA1).

### The delimited range

**0x80055E75 to 0x800626DF**, 222 functions. Each has a row in `README.md`;
`tools/ghidra/apply_names.py` sets the names and reports every function in
the range that has no row (result: none). Found like this:

1. **Samples.** `tools/xemu/sample_eip.py` stops the guest every 2–6 ms and
   reads EIP and stack (run `c3_eip`, 1295 samples). Assigned with
   `tools/xemu/eipmap.py`. Outside the idle loop 0x8001B025 most of the time
   lay in the chain 0x800579F4 → 0x800579B8 → 0x80057949 → 0x80056D0B →
   0x8003E550 → 0x800422A0.
2. **Reachability.** From the entry point 0x800579F4, 690 functions are
   reachable. 310 of them are reached by no other root of the call graph;
   the largest connected block is 0x80056ACA–0x800600F5.
3. **Edges.**
   - At the front the kernel side of the animation begins with 0x80055E75;
     0x80055E75, 0x80055EE4 and 0x80055FBA are called from 0x8001576F.
     Before it lies other kernel code that shares nothing with the
     animation.
   - At the back the sound ends at 0x80062638 (end 0x800626DF). 0x800626E0
     copies tables from `INIT` to `STICKY` (0x800626F2–0x8006277A) and does
     not belong to it.
4. **Code through pointers only.** In the range lay 1960 bytes and smaller
   pieces without a function. Their entry points stand in pointer tables at
   0x800669D8–0x80066A6C (commands of the sound), 0x80066A70–0x80066AA0
   (events of the sound) and 0x8006E910–0x8006EB14 (channel entries of the
   sound) and as immediates at 0x80055FCB (entry point of the thread) and
   0x80061688 (callback of the sound timer). With these 34 entry points no
   code without a function remains in the range, only filler bytes
   (0x80057C73, 0x80057D4E, 0x800616B6). Ghidra creates no function for them
   at import; `apply_names.py` does so for every row of the table.

Within the range some functions are also called by the rest of the kernel:
0x80055F8C (floating point to 64-bit integer), 0x80055FF8 (get memory and
zero it), 0x800565F3 (hand over the after-image), 0x80056AC0, 0x80056AC5 and
0x8006013C.

### Structure of the range

The functions lie together by subsystem. The boundaries follow from the rows
in `README.md`:

| From | To | Subsystem | Content |
|---|---|---|---|
| 0x80055E75 | 0x80055FF8 | Kernel side | start the thread, wait, memory helpers |
| 0x8005602B | 0x80056657 | After-image | a picture the kernel can hand over after the animation (below) |
| 0x8005667A | 0x800582C0 | Main object, arithmetic | constructor, set-up, update, draw, tear-down; matrices, vectors, trigonometric functions |
| 0x800582E7 | 0x800598E6 | Blob (+0xB5C) | mesh, bulges, drops, drawing |
| 0x80059A2E | 0x80059ABB | Arithmetic | vector times matrix, scaling |
| 0x80059B17 | 0x8005A618 | Veil (+0x17EC) | textures, drawing into a target texture |
| 0x8005A620 | 0x8005A662 | Arithmetic | generator, cross product |
| 0x8005A69F | 0x8005B367 | Camera (+0x1594, +0x6E8) | paths, view and projection |
| 0x8005B398 | 0x8005C4A5 | Logo (+0x57C) | unpack, meshes, drawing |
| 0x8005C665 | 0x8005C940 | Arithmetic | quaternions, transforms |
| 0x8005C968 | 0x800600F5 | Chamber (+0x968) | create, place, move and draw primitives |
| 0x8006013C | 0x8006033C | Sound | event ring, track management, sequence |
| 0x8006036C | 0x800614E9 | Textures | texture generators, strip indices, cube texture of the chamber |
| 0x8006165E | 0x80062638 | Sound | start, voices, tables, commands of the tracks |

### Call graph, top level

```
0x8001576F (kernel)
  0x80055FBA  starts the thread, entry point 0x80055F7F
    0x800579F4  entry point of the animation, this = 0x8006F050
      0x80056AAA(0xB, 1)
      0x80053302                  (outside, not examined)
      0x80056ACA  constructor
      0x8005768C(640, 480)  set-up; on success:
        0x8004E13A, 0x80056AAA(0xB, 0)
        0x800579B8  loop until t >= 8.0
          [0x8006166D  start the sound, long version only]
          0x80057949  one frame
            0x800571AD  update (t, dt)
            0x80056D0B  draw
          [0x800616A7  end the sound, long version only]
        0x80056E03  tear-down, in it 0x80056657 (after-image)
    0x80021B78(0)                 (end of the thread)
```

The main object has sub-objects at fixed offsets, called from 0x80056ACA
(constructor), 0x8005768C (set-up), 0x800571AD (update), 0x80056D0B (draw)
and 0x80056E03 (tear-down):

| Offset | Constructor, set-up | Update | Draw | Tear-down | Role |
|---|---|---|---|---|---|
| +0x57C | 0x8005BBEE | 0x8005C4A5 | 0x8005BC63 | 0x8005BACE | logo and lettering |
| +0x968 | 0x8005FC65 | 0x8005F9F1, 0x800600F5 | 0x8005F897 | 0x8005DF9B | the chamber around the blob |
| +0xB5C | 0x80059270, 0x800598E6 | 0x80059789 | 0x800583BF | 0x8005832F | the glowing blob |
| +0x17EC | 0x80059B17, 0x8005A41D | 0x8005A618 | 0x80059BCD | 0x80059B38 | green veil and flat glow |
| +0x6E8 | 0x8005B061, 0x8005B318 | 0x8005B2C0 | – | – | camera |
| +0x1594 | 0x8005AFB6 | 0x8005AC32 | – | – | camera path |
| +0x6AC | 0x80057A52, 0x80057A5A | – | – | 0x80057B96 | set up and tear down the graphics |

**Evidence for the role of the first four rows:** in the runs `c3_ko_*` one
draw function each returns at once (`bench.py --patch`, waited for the
kernel's entry point, bytes read back). Compared with the base run, exactly
what the column names is missing (contact sheet:
`tools/xemu/contact_sheet.py`). At flip 420 the picture without 0x8005BC63
is black; in the base run and the three others it is bit-identical (SHA-256
`f7cbe18b…`, `vectors/c3_knockout.json`).

**Evidence for the other three,** static:

- **Camera +0x6E8.** 0x8005B2C0 gets three vectors and builds from them,
  with 0x8005B093, a view matrix to +0xFC: the first is the eye (the
  translation of the matrix is its negative, rotated), the second the target
  (the third axis is the normalised difference), the third the up direction
  (cross products). 0x800571AD passes (0, 0, 1) as the third. 0x8005B318
  builds, with 0x8005B18C, a projection matrix to +0x13C from opening angle,
  aspect factor, near and far plane; the set-up 0x8005768C passes π · 0.25
  (0x80065E4C, 0x8001289C), 0.75 or 0.5625 (0x80012898, 0x800128E0), 0.4
  (0x800128AC) and 800 (0x800128DC). 0.5625 holds if bit 16 is set in the
  value that 0x8002FFDE delivers with identifier 6 (0x80056ACA to
  this+0x181E). 0x8005B06A delivers the product of both matrices, which the
  draw functions of the sub-objects fetch through 0x8006F738 (= this+0x6E8).
- **Camera path +0x1594.** 0x8005AFB6 reads 0x15 keys of 0x1C bytes from
  0x80070DB8: byte 0 is a time, then six float32 whose first three are a
  point. A time 0 begins a new path; the table contains four paths.
  0x8005A6CB chooses one of them, with argument −1 at random through the
  generator at 0x8006F6DC (remainder of the random number by the number of
  paths), and appends eight keys of its own. 0x8005AC32 evaluates the path
  at time s as a Hermite curve and delivers eye and target, which 0x800571AD
  passes on to 0x8005B2C0. With the fixed initial value 0x76543210 the first
  draw is 0x1A2F809C, hence path 0 (recomputed; the state after 38 steps is
  measured in the guest in `Timing.md` 4.1). Other paths are possible only
  after a repetition, which did not occur in our runs.
- **Graphics +0x6AC.** 0x80057A5A gets 640 and 480, calls 0x80040770 and
  0x8003CB40, sets stage states, clears to black, calls 0x8003E550 as at the
  end of every frame and sets 0x8007087C. 0x80057B96 clears 0x8007087C
  again. The functions called lie in the graphics code before the range;
  they are in `GPU.md` 2 and in the main table.

### After-image

The kernel can hand a small picture to the animation after its end.
0x800565F3 (called from 0x8002EDC9 and 0x80055523) copies up to 0x1000 bytes
to 0x8006F01C, only if the doubleword 0x8006F030 is 0 and 0x8007087C is set,
and sets the event 0x8006F02C. In the tear-down 0x80056657 sets the event
0x8006F03C, for which the kernel also waits in 0x80055F0C, itself waits for
0x8006F02C and then draws with 0x800561D1: run-length data into a texture
with 100 pixels per line (0x800560C6), value 0 black, otherwise grey from 4
bits (0x8005607F), onto a quad from 0x80065618.

**At start-up the after-image draws nothing.** Run `c9_post`
(`tools/xemu/probe.py --name c9_post --at 0x800565F3 --at 0x80056657
--at 0x800561D1 --at 0x80056E03 --read 0x8006F01C:0x18 --read
0x8007087C:4 --idle 40`): 0x800565F3 is called once, 63 ms after the start
and before the first frame, when 0x8007087C is still 0; so it copies
nothing. At flip 256 follow the tear-down (0x80056E03), 0x80056657 and
0x800561D1, all with 0x8006F01C = 0. With 0, 0x800561D1 jumps to the end at
once (0x800561DF–0x800561E5 → 0x800565ED): no buffer, no texture, no state,
no call of 0x8003F410. A rebuild of the animation therefore does not need
the functions that only the after-image reaches (`README.md`, "not needed").
There were no further calls of 0x800565F3 in the run, not in the 40 s after
the tear-down either. A later hand-over would not be drawn any more anyway,
because 0x80056657 runs only once, in the tear-down.

### Sound and textures

The sound is a sequencer. 0x8006166D starts a timer with the callback
0x8006165E; every callback counts a time up and, in 0x80060252, first works
off the events from a ring of 16 bytes (0x800A7912, table 0x80066A70), then
the tracks whose waiting time has run out (commands through the table
0x800669D8). The commands stand as bytecode in `INIT`; they set values of
the channels, wait, jump, call subroutines and loops. Before that 0x80061AC1
makes the tables: noise with a linear congruential generator (0x80061A9E:
factor 0x343FD, addend 0x269EC3, initial value 0x3EB at 0x80061AC7), sine, a
sound from nested sine functions with 0x8000 values, and converts 8-bit
samples from 0x80066AC0 (0x5540 of them), 0x8006C008 and 0x8006CEC8 to 16
bits. In detail in `Audio.md`.

Between the two parts of the sound lie the texture generators
(0x8006036C–0x800614E9). The set-up 0x8005768C makes five textures with them
to this+0x934 up to this+0x944, three cube textures among them; for one of
them 0x800614E9 draws the chamber six times at a fixed time. In detail in
`Textures.md`.

### Small helpers in the range

Vectors, matrices, quaternions and trigonometric functions each have a row
in `README.md` (subsystem `Overview`). Conventions, from the disassembly of
the functions below:

- Vectors and matrices are float32. A matrix M has 16 values row by row,
  M[i][j] at 4 · (4i + j). Vectors are row vectors: v · M, the translation
  stands in row 3 (as in `Chamber.md`, introduction).
- Computation is on the x87 FPU with 53 bits (`Timing.md` 4.2). `f32(…)`
  means: at this place the value is stored as float32. A result "in st0"
  goes to the caller unrounded.
- Arguments on the stack, in the order of the table; the callee cleans up.
- Sums are formed in the order given, (a + b) + c means: a + b first. For
  bit identity the order counts.

| Address | Call | Computation |
|---|---|---|
| 0x80055F8C | (st0) | 64-bit integer in edx:eax, truncated (rounding mode 3 for `fistp` only) |
| 0x8005667A | (v, M, out) | out[j] = f32(Σ v[i] · M[i][j], i = 0..2); order j = 0: (v2 M20 + v1 M10) + v0 M00; j = 1, 2: (v0 M0j + v2 M2j) + v1 M1j |
| 0x800566CF | (A, B, out) | out = A · B, order `Chamber.md` 11 |
| 0x800568E5 | (v) | st0 = (x² + y²) + z² |
| 0x8005690A | (v) | 0x80057F42(f32(0x800568E5(v))) |
| 0x8005691F | (a, b) | d = a − b unrounded; st0 = (dz² + dy²) + dx² |
| 0x80056950 | (a, b) | 0x80057F42(f32(0x8005691F(a, b))) |
| 0x800570A3 | (α, out) | s, c = f32(sin α), f32(cos α) with `fsincos`; out = rows (1,0,0,0), (0,c,s,0), (0,−s,c,0), (0,0,0,1) |
| 0x80057127 | (α, out) | as above; out = rows (c,s,0,0), (−s,c,0,0), (0,0,1,0), (0,0,0,1) |
| 0x80057BA9, 0x80057E2D, 0x80057F42, 0x80057F9D, 0x80058039 | (x) | arc tangent, arc sine, square root, sine, arc cosine: `Chamber.md` 11; the factors of the series of the arc sine at 0x80066030 (0x31 float32) are in `data/overview/asin_factors.bin`, decoded in `asin_factors.json` (`tools/extract/overview.py`) |
| 0x80057F89 | (x) | st0 = x with the sign bit cleared |
| 0x8005808F | (M, out) | out = Mᵀ, copies bit patterns |
| 0x800580F8 | (out) | identity matrix |
| 0x8005814E | (a, b) | st0 = (a2 b2 + a1 b1) + a0 b0 |
| 0x8005816D | (v) | r = 1.0 (0x8001288C) / 0x8005690A(v); v[i] = f32(r · v[i]) |
| 0x80058196 | (v, s) | v[i] = f32(s · v[i]) |
| 0x800581B9 | (a, b, out) | out[i] = f32(a[i] − b[i]) |
| 0x800581E0 | (a, b, s) | a[i] = f32(s · b[i] + a[i]) |
| 0x8005820E, 0x80058290 | (v) | approximate length, approximate normalising from length 1e−6 (0x800128F8): `Blob.md` 4 |
| 0x800582C0 | (a, b, out) | out[i] = f32(a[i] + b[i]) |
| 0x80059A2E | (v, M, out) | out = v · M with four components; j = 0: ((v3 M30 + v1 M10) + v2 M20) + v0 M00; j = 1..3: ((v0 M0j + v3 M3j) + v1 M1j) + v2 M2j; each f32 |
| 0x80059ABB | (a, b, c, out) | diag(a, b, c, 1) |
| 0x8005A620 | (n) | generator step, (s′ · n) >> 32: `Timing.md` 4.1 |
| 0x8005A662 | (a, b, out) | out = a × b: f32(a1 b2 − a2 b1), f32(a2 b0 − a0 b2), f32(a0 b1 − a1 b0) |
| 0x8005B22E | (M, out) | inverse of a matrix of rotation and translation: `Chamber.md` 5 |
| 0x8005C665, 0x8005C790 | (q, out) | rotation matrix from a quaternion and its transpose: `Chamber.md` 5 |
| 0x8005C8BB | (p, M, out) | out = p · M + row 3; j = 0: ((p2 M20 + p1 M10) + p0 M00) + M30; j = 1, 2: ((p0 M0j + p2 M2j) + p1 M1j) + M3j; each f32 |
| 0x8005C919 | (a, b) | st0 = ((a3 b3 + a2 b2) + a1 b1) + a0 b0 |
| 0x8005C940 | (a, b) | 1 if \|f32(a − b)\| ≤ 1e−5 (0x800129A0), otherwise 0, also for NaN |
| 0x8005E051, 0x8005E0B1 | | quaternion from axis and angle, slerp: `Chamber.md` 4.3 and 6 |
| 0x8006127E, 0x800612D3 | | strip indices over a grid: `Chamber.md` 4.3 |

Evidenced by the disassembly at the addresses named. The models
`tools/xemu/chamber_model.py`, `blob_model.py` and `logo_model.py` compute
with these rules and meet the values from the guest bit-exactly (`Chamber.md`
11, `Blob.md` 9, `Logo.md` 8). Whether `fsincos` computes the same in the
last place in xemu and on the hardware is not checked; only xemu is measured.

In addition the two pseudo-random generators with the same step 0x8005A645
(`Timing.md` 4.1): that of the main object at this+0x68C and that of the
blob at 0x800708BC, both with the initial value 0x76543210.
