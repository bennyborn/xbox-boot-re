# Order per frame

Which draw calls a frame of the animation sends to the GPU in which order,
into which target, with which program, which combiners and textures. The
steps have short names: A in the set-up, U on updating (`anim_update`), D on
drawing (`anim_draw`). Every draw call of every recorded frame is assigned
to exactly one step (section 4).

Evidence: run `c8_main2` (`tools/xemu/probe_c8.py`), comparison
`tools/xemu/vectors_c8.py` (`check_draws`, table `STEPS`), vector file
`vectors/c8_gpu.json`, section `draws`. Programs and blocks: `GPU.md` 3 and
4, combiners from the stages: `GPU.md` 5, states: `GPU.md` 6, textures:
`Textures.md`.

A draw call here is a block `SET_BEGIN_END` (kind) … `SET_BEGIN_END` 0 in
the stream. The kind: 5 triangles, 6 strip, 7 fan.

## 1. The steps

| Step | When | Caller | Target, depth | Program, block | Textures | Calls |
|---|---|---|---|---|---|---|
| A1 | set-up, `device_setup` | 0x80057B7E → 0x8003E550 | display buffer | fixed function, from stages | drawing surface | 1, kind 5 |
| A2 | set-up, `texgen_chamber_cube` | `chamber_draw` → `prim_group_draw` | cube face 0 … 5, depth surface 256 | as D1 | as D1 | 6 × 433 |
| U1 | every frame, if `this+0xD` and `this+0x181C` | `chamber_light_view` → `prim_group_draw` | no colour, light view a or b | fixed function 2, from stages | – | 221 + 178 |
| U2 | every frame, if `this+0xD` and `this+0x181D` | `fx_render_offscreen` 0x8005A373 | `fx`+0x18, `fx`+0x1C | 3.3 / depth for the veil | – | 1, kind 7 |
| U3 | likewise | `chamber_draw_depth` → `prim_group_draw` | `fx`+0x18, `fx`+0x1C | 3.3 / depth for the veil | – | 433 |
| U4 | t ≥ 6 and t ≥ 6.25, once each | `logo_render_to_texture` → `logo_draw` | target 1024 × 1024 | as D3 | as D3 | 4 or 7 |
| D1 | if `this+0xD` and x20 > 0 | `chamber_draw` → `prim_group_draw` | drawing surface | 3.1 / smooth, 3.2 / relief | 0 normal map, 1, 2 cube 128 or 64, 3 light view a or b | 433 |
| D2 | if `this+0xD` and t ≥ 0.6 | `blob_draw` | drawing surface | glow surface: fixed function 0x102, from stages; mesh 3.4; drops 3.5 | falloff; cube 64 on 0 and 1 | 1 + 1 + 8 |
| D3 | if `this+0xE` | `logo_draw` | drawing surface | A, B, C, Q1, Q2: fixed function 0x102; D 3.7; E fixed function 2; as a picture 0x202 | gradients, radial, TM, `logo`+0xD0 | 4, 7 or 1 |
| D4 | if `this+0xD`, `this+0x181D` and (x20 > 0 or t < 0.6) | `fx_draw` 0x80059EFE, 0x8005A100 | drawing surface | 3.6 / veil; glow fixed function 0x202 | `fx` target, three noise layers; falloff | 1, with glow 2 |
| D5 | every frame | `anim_draw` 0x80056DF9 → 0x8003E550 | display buffer | fixed function, from stages | drawing surface | 1, kind 5 |

The drawing surface is the surface 1280 × 480 at physical 0x3628000 with the
depth at 0x33D0000, the display buffer 640 × 480 at 0x32A4000 (`GPU.md` 7).
"Fixed function n" is the value n to 0x80040440 (handle without bit 0,
`GPU.md` 2), "from stages" the block 0 to 0x80040EF0 (`GPU.md` 5).

**Conditions.** `this+0xD` and `this+0xE` are set by the camera (`Camera.md`
5.5), x20 is in `Timing.md` 4, `this+0x181C` and `this+0x181D` are set to 1
by the constructor (0x80056B84, 0x80056B8B). For U1 to U3 see `Chamber.md` 1
and 8 and `Effects.md` 2, for U4 `Logo.md` 6, for D1 to D5 `anim_draw`
(0x80056D0B): first clearing of the drawing surface (mask 0xF3, 0xFF000000,
depth 1.0; 0x80056D2A), then D1, D2, clearing of depth and stencil where
applicable (`Effects.md` 2), D3, D4, D5.

**Parts in A2, U1, U3, D1.** One call per part of the chamber, three for
cylinder and frustum (two fans, one strip; `Chamber.md` 7). 433 are the six
groups of detail level 1; U1 draws only the groups of the places 3, 4, 2, 1
and only the parts of the respective side (`Chamber.md` 8). In D1 the vertex
format of the part chooses program 3.1 or 3.2 (`Chamber.md` 7, modes 0 and
1), the group the cubes on stages 1 and 2 (group field +0x24, `anim_object`
+ 0x934 or + 0x938), the side the light view on stage 3.

**Blob in D2** (`Blob.md` 7, 8): glow surface as a fan with 4 vertices, the
mesh as one strip with 4054 indices, per drop one strip with 2768 indices;
always 8 drops in the recorded frames.

**Logo in D3 and U4** (`Logo.md` 5, 6): meshes A, B, C, D as triangles with
336, 225, 105 and 1008 indices; Q1 and Q2 6 each, lettering E 462; the logo
as a picture a fan with 4 vertices from the quad of the veil. Which of them
come in a frame depends on the phase:

| t | D3 |
|---|---|
| 5.225 ≤ t < 6 (phase 3D) | A, B, C, D |
| 6 ≤ t < 6.25 | logo as a picture (phase 0: X), Q1, Q2, E |
| from 6.25 | logo as a picture (phase 1: X, TM, lettering) |

In the first frame with t ≥ 6, A, B, C, D come before that in U4 into the
target 1024 × 1024; in the first frame with t ≥ 6.25 A, B, C, D, Q1, Q2, E.

## 2. Set-up

In the set-up (frame 0 of the measurement) only A1 and A2 come. A1 is the
finishing of the frame that `device_setup` calls after clearing to black; it
shows a black drawing surface. A2 are the six faces of the cube texture
`anim_object` + 0x93C (`Textures.md` 5): per face the 433 calls of the
chamber with the same textures as D1 and the light views, which are not yet
drawn at this time.

## 3. A frame

```
anim_update:
    if this+0xD:
        chamber_update, blob_update
        if this+0x181C:  U1   (two light views)
        if this+0x181D:  U2, U3   (target texture of the veil)
    if this+0xE:  logo_update  →  U4 in the first frame with t ≥ 6 and with t ≥ 6.25
anim_draw:
    clear the drawing surface
    if this+0xD:
        if x20 > 0:  D1
        if t ≥ 0.6:  D2
    v = this+0xD and this+0x181D and (x20 > 0 or t < 0.6)
    if this+0xE:
        if v or this+0xD:  clear depth and stencil
        D3
    if v:  D4  (veil; glow, if its alpha ≠ 0, Effects.md 5)
    D5
```

`anim_update` 0x800575C8–0x80057617, `anim_draw` 0x80056D0B–0x80056DF9.

## 4. Measurement

`check_draws` goes through the stream call by call for every recorded frame,
remembers target, program, block, textures and states and assigns every
block `SET_BEGIN_END` to a step by (phase, calling function, call site).
Within `prim_group_draw` the function that called it counts. It recognises
program and block by the calls of 0x80040440 and 0x80040EF0 and in addition
by their methods in the stream; in the set-up the measurement does not stop
at these calls in `prim_group_draw` (`probe_c8.py`). So in A2, 3.1 with the
smooth block is measured at 2394 calls and 3.2 with the block for relief at
204 calls, in the same ratio as in D1 (399 and 34). Result in `c8_main2`: 21
frames, no call without a step (`draws.unmapped` = 0), 266 different
combinations of step, call site and states (`kinds`).

| Frame | t | Steps | Calls |
|---|---|---|---|
| 0 | set-up | A1 A2 | 1 + 2598 |
| 1, 35 | 0.001, 0.316 | U1 U2 U3 D4 D5 | 399 + 1 + 433 + 2 + 1 |
| 96 | 1.019 | U1 U2 U3 D2 D5 | 399 + 1 + 433 + 10 + 1 |
| 161, 228, 285, 323 | 2.025 … 4.633 | U1 U2 U3 D1 D2 D4 D5 | 399 + 1 + 433 + 433 + 10 + 1 + 1 |
| 342 | 5.021 | U1 U2 U3 D1 D2 D4 D5 | as above, D4 with glow: 2 |
| 346, 350, 354 | 5.246 … 5.754 | U1 U2 U3 D1 D2 D3 D4 D5 | … + 4 (A–D) + 2 + 1 |
| 358 | 5.985 | D3 D5 | 4 + 1 |
| 359 | 6.023 | U4 D3 D5 | 4 + 4 + 1 |
| 360, 361 | 6.158, 6.225 | D3 D5 | 4 + 1 |
| 362 | 6.291 | U4 D3 D5 | 7 + 1 + 1 |
| 363, 364, 428, 537 | 6.416 … 7.916 | D3 D5 | 1 + 1 |

At t = 1.019, x20 = 0: no chamber, no veil, but its target texture (U2, U3).
At t < 0.6 the veil comes without the blob; its green is 0 then (x20 = 0),
only the glow is visible (`Effects.md` 5). From t = 5.985 on `this+0xD` is
cleared.

The states per combination (blending, alpha test, depth, back faces, colour
mask, combiners, stages) are in `kinds`, `state`; the overview in `GPU.md`
6. What the rasteriser makes of the parts with NaN matrices (`Chamber.md` 6,
until t ≈ 1.17) the stream does not show: the calls are there and assigned.
For the depth their result has been measured (run `w1_t0800`,
t = 0.802, `vectors/w1_frames.json`): the depth surface from xemu coincides
there with a rebuild that leaves these parts out, except for 147 of 384 392
pixels, all on the outline. In xemu they therefore write no depth. What the
console makes of them is not said by that.

## 5. States per kind of drawing

What holds at the draw call, not only what the step itself sets. The
animation sets only a part of the states per step; the rest stands as an
earlier step left it. The source is `kinds`, `state` in
`vectors/c8_gpu.json` (method → value sent last), over all 21 frames;
`GPU.md` 6 has the meaning of the values.

| Kind of drawing | Alpha test | Blending (source, destination) | Depth test | Depth write | Back faces | Colour mask |
|---|---|---|---|---|---|---|
| Finish the frame (A1, D5) | off | off | off | on | off | all |
| Cube texture (A2) | off | off | on | on, *guessed* | 0x405 | all |
| Light view (U1) | off | off | on | on | 0x405 | 0 |
| Veil: quad, chamber (U2, U3) | ≥ 1 | 1, 0 | on | on | 0x405 | all |
| Chamber (D1) | off | off | on | on | 0x405 | all |
| Blob: glow surface (D2) | inherited | 0x302, 1 | on | off | 0x405 | all |
| Blob: mesh, drops (D2) | inherited | 0x302, 0x303 | on | on | 0x405 | all |
| Logo A, B, C, D (D3, U4) | off | off | on | on | 0x404 | all |
| TM marks Q1, Q2 (D3, U4) | off | 0x302, 0x303 | off | on | 0x404 | all |
| Lettering E (D3, U4) | off | off | off | on | 0x404 | all |
| Logo as a picture (D3) | off | 1, 0 | on | off | 0x405 | all |
| Veil, glow (D4) | ≥ 1 | 0x302, 1 | on, comparison 0x207, with the logo 0x202 | off | 0x405 | all |

"≥ 1" is the comparison 0x206 with reference 1. In addition:

- **Alpha test of the blob.** `fx_render_offscreen` switches it on in every
  frame with the chamber (`Effects.md` 3), `chamber_draw` switches it off
  (0x8005F8AB–0x8005F8B8), `blob_draw` does not set it. The blob is
  therefore drawn without the alpha test if D1 ran before, and with it if
  not (x20 = 0 and t ≥ 0.6). Measured: frame 96 with, the eight frames with
  D1 without.
- **Back faces.** 0x405 has stood since the set-up. `logo_draw` sets 0x404
  for its meshes and 0x405 again at the end (`Logo.md` 5.3); the logo as a
  picture comes before the change. The finishing of the frame switches off
  for its triangle; before the first call of the next frame it stands at on
  with 0x405 again (frames 359 to 364, the only consecutive ones).
- **Depth comparison.** Only `fx_draw` sets it: 0x207 or 0x202 for veil and
  glow, 0x203 at the end (`Effects.md` 5). For all other draw calls 0x203
  from the previous frame holds. Before the first `fx_draw`, that is in the
  set-up and for U1 to U3 of the first frame, no call has sent it; that it
  is 0x203 there already is *guessed*. Likewise no call has sent the depth
  write in the set-up.
- **Shadow comparison** (D1): `chamber_draw` sets 0x204, afterwards 0x207
  (`GPU.md` 4.4); in the stream `SET_SHADOW_DEPTH_FUNC` stands with 4 and 7,
  the value without 0x200.
- **In the snapshot after copying into the texture of the logo**
  (0x8003EBC0, frames 359 and 362) method 0x308 has the value 0x04001000. It
  belongs to another object of the channel, not to the switch of the back
  faces; the vector file does not separate the objects.
- **Textures and programs that stay bound.** On stages that a draw call does
  not read, the stream shows what was bound last: the normal map on stage 0
  of the smooth groups, the cube 128 on stage 2 of the blob and of the logo,
  the gradient of mesh C under mesh D. For the finishing of the frame the
  vector file names the program that the animation set last; what the
  library sets there itself is not assigned. None of this belongs to the
  kind of drawing.

The core outputs these states with every draw call (`core.md` 2);
`core/test/test_k6.c` compares them for all 15 396 draw calls of the vector
file, as far as the frame has sent the value.

## 6. The run frame by frame

`vectors/c8_gpu.json`, section `timeline`, holds, for every stop of the
measurement at 0x80057949, the step in whole milliseconds and t after it as
a bit pattern. From this the run can be reproduced without the recording; t
equals the guest after each of the 540 steps.

Four stops (3, 131, 162, 265) are not frames: the breakpoint stopped twice
at the same call (a measurement incident). The following stop has the same t and the
same write pointer of the push buffer, although every frame at least clears
and finishes. They are in `repeated_stops`. If one counts them as frames
with step 0, the target of the veil is no longer right in the frames 35, 96
and 228 (`Effects.md` 3), because every frame with the veil swaps the
targets; in t and in the constants of the blob it does not show.
