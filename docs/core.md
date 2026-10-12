# The core

Interface, data pack, build, numerics and costs of the portable C core. The
core is complete. Time keeping, camera, chamber, blob, logo,
textures, effects, the frame description and the sound are finished, a whole
run is checked in both builds and its costs are measured (section 6). The
animation itself is described by `docs/decomp/`; this text describes only
how the core offers it.

What the core does not rebuild is stated per subsystem in section 2 and once
more together at its end.

## 1. Layout

```
core/
  include/xboot.h     public interface
  src/core.c          entry points, checking of the arguments
  src/pack.c          reading the data pack
  src/Timing.c        time keeping (Timing.md): initial values, pulse table,
                      frame step, ramp, pulses, derived quantities
  src/Overview.c      small helpers (Overview.md): vectors, matrices,
                      quaternions in the x87 manner; root, sine, arc sine,
                      arc tangent, arc cosine in the SSE manner (Chamber.md 11)
  src/Camera.c        camera and camera path (Camera.md)
  src/Chamber.c       chamber (Chamber.md): unpacking, placement, tracks,
                      world matrix, light view, geometry
  src/Blob.c          blob (Blob.md): meshes, generator, bulges and drops,
                      deformation, values on drawing, weighted centre
  src/Logo.c          logo (Logo.md): seven meshes from the ROM, update,
                      values on drawing
  src/Textures.c      textures (Textures.md): six generators and TM,
                      reordered into buffers of the host
  src/Effects.c       veil and glow (Effects.md): constants, colours,
                      matrix, the two quads
  src/FrameOrder.c    a frame as a sequence of draw calls (FrameOrder.md),
                      set-up and every frame
  src/Audio.c         sound (Audio.md): tables, sequencer, voice layer,
                      voices after xemu's VP, frame routine of the GP program
  src/x87.c           substitute for x87 instructions without a library (fsincos)
  src/core_internal.h state, generator, read helpers, not public
  test/               host tests per milestone (test_k*.c native,
                      wasm_k*.mjs), check_k1.py to check_k7.py, sweep_k1.c,
                      gen_trig.py, a small JSON reader, SHA-256;
                      cost_k8.c and cost_k8.mjs measure the costs
  build.py            build of both targets and test run
hosts/native/
  run.h, run.c        a whole run through the interface, without files
  xboot_run.c         command line: writes the run into files
tools/pack/pack.py    data/ → out/pack/xboot.pak
```

Per subsystem one file is added in `src/`, named like the narrative in
`docs/decomp/` (`Blob.c`, `Logo.c`, …). Arithmetic helpers that several
subsystems need and that `Overview.md` lists under "Small helpers" are in
`Overview.c`, reproductions of instructions in `x87.c`, the generator step
and the storing as float32 as `static inline` in `core_internal.h`.

## 2. Interface

| Call | Purpose |
|---|---|
| `xb_state_size()` | bytes for the state; fixed per build |
| `xb_init(s, pack, size, cfg)` | create the state, check the pack, set the initial values |
| `xb_advance(s, tick)` | one frame step after `Timing.md` 2, `tick` is the millisecond counter of the host; `XB_OK`, from t ≥ 8.0 `XB_DONE` |
| `xb_advance_free(s, dt)` | free time in seconds, without the quantisation to whole ms |
| `xb_time(s)`, `xb_last_tick(s)` | t (`this+0x10`, float32) and the last tick (`this+0x28`) |
| `xb_get_timing(s, out)` | t, `x18` … `x24`, both ticks, generator, pulse table (`Timing.md` 4) |
| `xb_camera_at(s, t, out)` | camera at time t: s, segment, switches `this+0xD`/`+0xE`, eye, target, up, view, its inverse, projection (`Camera.md` 2–5) |
| `xb_get_campath(s, out)` | chosen path, its keys with tangents, frame, U, B, blend (`Camera.md` 5.2–5.4) |
| `xb_light_view(center, side, out)` | matrix T of the light view from a point (`Chamber.md` 8) |
| `xb_chamber_part(s, i, out)` | group, kind, shape, tracks, side, scale of part i (`Chamber.md` 5) |
| `xb_chamber_at(s, t, out)` | tracks at time t: 29 rotations, 32 positions, detail level (`Chamber.md` 6) |
| `xb_chamber_world(s, pose, first, n, out)` | world matrix W of the parts (`Chamber.md` 7) |
| `xb_chamber_mesh_info`, `xb_chamber_mesh` | counts and geometry of a group in a detail level, into buffers of the host (`Chamber.md` 4) |
| `xb_get_blob(s, out)` | state of the blob: generator and its draws, number of bulges and drops, all 32 bulges and 8 drops, centre, radius, colour (`Blob.md` 2) |
| `xb_blob_mesh(s, small, unit, idx)` | large or small sphere mesh into buffers of the host (`Blob.md` 3) |
| `xb_blob_deformed(s, out)` | deformed mesh, four float32 per vertex (`Blob.md` 6.4) |
| `xb_blob_draw(s, cam, t, x1c, x20, out)` | what `blob_draw` gives to the GPU: k, glow surface, constants, colours (`Blob.md` 7) |
| `xb_blob_center(s, ref, x20, out, second)` | weighted centre with both outputs (`Blob.md` 8) |
| `xb_blob_light_view(s, side, out)` | light view T from the weighted centre for the reference point of the pre-pass (`Chamber.md` 8) |
| `xb_blob_update`, `xb_blob_restart`, `xb_blob_set_rng` | interventions as in the measurement (`Blob.md` 10), for tests and workbench |
| `xb_logo_mesh_info`, `xb_logo_mesh` | counts and buffers of one of the seven meshes, as `logo_setup` creates them (`Logo.md` 2, 3) |
| `xb_get_logo(s, out)` | state of the logo: +0x4C, position of the lettering, opacity, texture and phase, whether the last update drew into the texture (`Logo.md` 3.1, 4, 6) |
| `xb_logo_draw(s, cam, t, to_texture, out)` | what `logo_draw` gives to the GPU: which parts, W, constants 0–8 of D, colours of TM and lettering, world matrix of E (`Logo.md` 5) |
| `xb_logo_update(s, t)` | intervention: `logo_update` without looking at the camera, for tests and workbench |
| `xb_texture_info(s, tex, out)` | size, levels, faces, colour format, format word and length of one of the eleven textures of the CPU (`Textures.md` 1, 2) |
| `xb_texture(s, tex, buf, size)` | the texels of this texture in the layout the GPU reads (`Textures.md` 2–4) |
| `xb_get_track(s, out)` | orbit values `anim_object` + 0x690, + 0x698, + 0x6A0, + 0x6A8 and the number of calls of `fx_draw` (`Camera.md` 6, `Effects.md` 3) |
| `xb_chamber_draw(s, cam, pose, x20, light, lit, part, out)` | what `prim_group_draw` gives to the GPU for a part: W, constants 0–11, three colours, with or without the switch (`Chamber.md` 7) |
| `xb_blob_light_camera(s, side, view, proj)` | view and projection with which the pre-pass draws a side (`Chamber.md` 8) |
| `xb_fx_draw(s, cam, t, x20, track, out)` | what `fx_render_offscreen` and `fx_draw` give to the GPU: constants of the quad and of the depth, colours and coordinates of the veil, alpha, factor colour, s and matrix of the glow (`Effects.md` 3–5) |
| `xb_fx_quads(veil, quad)` | the two vertex buffers of `fx_setup` (`Effects.md` 1) |
| `xb_frame(s, out)` | the frame of the last step as a sequence of draw calls in the order of `FrameOrder.md`, steps U1–U4 and D1–D5 |
| `xb_frame_from(s, eye, target, up, out)` | the same frame from a camera of the host, for the workbench; not in the original |
| `xb_select_path(s, path)` | the camera follows path `path` of the table instead of the drawn one, like `campath_select` with an argument ≥ 0 (`Camera.md` 5.2); for the workbench, not taken at the start of the original |
| `xb_frame_setup(s, pass, out)` | the draw calls of the set-up: pass 0 is A1, 1 to 6 one face of the cube texture each (A2) |
| `xb_audio_size()`, `xb_audio_init(s, mem, size)` | bytes for the sound; hand over memory and start the sound from the beginning (`Audio.md` 1) |
| `xb_audio(s, buf, n)` | n stereo frames, 16 bits, interleaved, 48 kHz: the output of the GP (`Audio.md` 6.5) |
| `xb_audio_block(s, out)` | one block of 32 samples in detail: commands and calls of the beat, every voice before the gain, its gain into the bins 0, 1 and 11, the three bins and both sides as 24-bit values |
| `xb_audio_mute(s, mask)` | take channels out of the mix, for the workbench |
| `xb_get_audio(s, out)` | time of the sequencer, number of tracks, per voice the values of the methods and whether it is running (`Audio.md` 5) |
| `xb_audio_gp(s, bin0, bin1, bin11, left, right)` | the frame routine of the GP program alone on one block (`Audio.md` 6.2, 6.6) |
| `xb_audio_table(s, table, out, n)` | one of the eight sample tables as 16-bit values (`Audio.md` 2) |

Rules that hold for every call:

- **No memory from the core.** The host provides state, pack and all output
  buffers. State and pack must be aligned to `XB_ALIGN` = 16 bytes;
  otherwise `XB_ERR_ALIGN`.
- **No files, no library.** The core includes only `<stddef.h>` and
  `<stdint.h>`. The wasm build has not a single import (checked in
  `core/build.py`).
- **The pack stays with the host.** The core reads it in place and keeps
  pointers into it. It must not change as long as the state lives.
- **Errors are negative** (`XB_ERR_*`). After a failed `xb_init` every
  further call rejects the state. `XB_ERR_SPACE` means: a buffer of the host
  was too small; `xb_frame` then says how much it needs.

**Start.** `xb_config.flags` with `XB_START_SHORT` chooses the short
version: t begins at 7.5 instead of 0 (`Timing.md` 3; in the original the
byte at 0x8003AEB8, set from bit 4 of the doubleword at 0x8003B138).
`xb_config.start_tick` is the tick that the set-up writes into `this+0x28`
and `this+0x2C` at its end (`Timing.md` 2). When the host takes it is its
own business; in the original there lies, between power-on and the first
frame, a time that `Timing.md` 6 still calls open.

`XB_START_WIDE` chooses the aspect factor 0.5625 instead of 0.75
(`Camera.md` 2; in the original the byte `anim_object` + 0x181E). It was 0
in all runs.

**Time.** `xb_advance` is one call of 0x80057949: step
d = |last tick − tick| as a signed 32-bit difference, hence right across an
overflow of the counter too; dt = f32(d · 0.001); a step above 1 s counts
1 ms; then t, `x1c`, `x20`, `x24`, `x18` after `Timing.md` 4. If t is
already ≥ 8.0, the call changes nothing and delivers `XB_DONE`, like the
loop 0x800579B8, which then calls no more. The host gives the tick as it
likes: from its clock for real time, from a list for playback, in fixed
steps for pictures on demand.

`xb_advance_free(s, dt)` skips only the quantisation: dt is rounded to
float32 and passes through the same update, including the rule for steps
above 1 s; the last tick stays where it is. The core rejects negative and
NaN steps. The time line stays the same, only the sampling is finer; t,
`x18` … `x24` depend in the last place on the sequence of the steps, as in
the original.

Not rebuilt, because set in no run and by no writer found (`Timing.md` 2, 3,
4): the repetition through `this+0xA`, the halting of the derived quantities
through `this+0xC`, `factor` ≠ 1 through `this+0x1818` and `this+0x14` ≠ 0.
The repetition needs the restarts of the sub-objects.

**Camera and chamber.** In the original both are functions of t alone:
the camera evaluates the path at the path time s(t) (`Camera.md` 4, 5.5),
the chamber its tracks at t (`Chamber.md` 6). The core therefore offers them
as queries at arbitrary t that do not change the state: `xb_camera_at`,
`xb_chamber_at`, `xb_chamber_world`. So a host can sample the time line
without repeating the run. That the original calls `chamber_update` only as
long as `this+0xD` is set changes nothing in the values: when the switch
falls (t ≈ 5.936), all tracks have stood at their end value since t = 5.35
and the detail level at 1 since t = 5.2. Whether drawing happens is reported
by `xb_camera.chamber_on`.

What the set-up fixes is in the state: projection, the chosen path with its
keys, unpacked quaternions, positions, index lists and the 271 placements.
The choice of the path needs, as in the original, the first draw of the
generator (0x8005A6CB); after it follow the 37 draws of the time keeping
(`Timing.md` 4.1).

The geometry of the twelve groups is generated by the core only on request
into buffers of the host, once per group and detail level; the host loads it
onto the GPU once. Position as three float32, attributes as (u, v,) a, b, n
with 11 or 9 float32 (`Chamber.md` 4.2), indices 16 bits, plus the shape
table (first vertex, number of vertices, first index, primitives, indices,
fan length). Cylinders and frustums count two vertices per shape that the
original does not write (`Chamber.md` 4.3). The core writes 0 there, in the
guest what memory held before stands there.

`xb_light_view` computes the matrix T of the pre-pass from an arbitrary
point. In the original that is the weighted centre of the blob and its drops
(0x80058D1F); `xb_blob_light_view` computes both together, with the
reference points (0, 0, −30) and (0, 0, 40) from 0x800669D0 and 0x800669CC.
The orbit values `anim_object` + 0x690 … 0x6A8 (`Camera.md` 6) are needed
only by `Effects.md`. For + 0x6A8 the centre of the blob
suffices, which stays (0, 0, 0) after the constructor (`Blob.md` 2).

**Blob.** Unlike camera and chamber the blob has a state of its own: 32
bulges and 8 drops, driven by a second generator (`Blob.md` 2, 4). It
depends on the whole sequence of the steps, not only on t. Therefore
`xb_advance` and `xb_advance_free` update it as `anim_update` does: after
the time step the camera at the new t, and if it sets `this+0xD`,
`blob_update(t, dt)` with the argument after the rule for steps above 1 s
(`FrameOrder.md` 3, `Timing.md` 2). A call that does nothing more at t ≥ 8
does not update the blob either. The core still does not rebuild the
repetition. In the original it also chooses a new camera path (measured in
`c6_loop`: after the restart the eye no longer lies on path 0).

The deformed mesh is not in the state. It depends only on the bulges, and
`xb_blob_deformed` computes it on request, with the same sequence of
roundings as `deform` (0x80058B93). A flag records whether the original
would have the undeformed mesh with w = 1 in the buffer just now: after the
set-up, after `blob_restart` and after the branch for t − dt > 0.6
(`Blob.md` 6.1). `xb_blob_draw` takes camera and t, x1c, x20 from the
caller, normally from `xb_camera_at` and `xb_get_timing`; so it can also be
called with the values of a recording. The w values of 9 and 11, which the
original does not set (`Blob.md` 7.2), are 0 in the core.

The three interventions do what the measurement did in the guest (`Blob.md`
10): `xb_blob_update` calls `blob_update` with arbitrary arguments, without
looking at the camera; `xb_blob_restart` is `blob_restart`;
`xb_blob_set_rng` writes the generator. With them the tests play `c6_loop`
and `c6_poke`.

**Logo.** Like the geometry of the chamber, the seven meshes arise only
on request in buffers of the host, the way `logo_setup` writes them into the
vertex buffers: five float32 per vertex (x, y, z, u, v), three for the
lettering E, in mesh C every v near 1 as −1 (`Logo.md` 3.2); indices 16 bits
as a triangle list. In the state is only what `logo_update` writes: +0x4C,
the position of the lettering (row 3 of M), the opacity, whether the texture
exists and with which phase it was drawn. `xb_advance` and `xb_advance_free`
call `logo_update(t)` after the blob if the camera sets `this+0xE` at the
new t (`FrameOrder.md` 3). As with the blob the state depends on the
sequence of the steps: the texture arises in the first frame with t ≥ 6 and
once more in the first with t ≥ 6.25, depending on which t the host hits.
`xb_logo.rtt` reports that the last update drew into the texture (U4 in
`FrameOrder.md`); `xb_logo_draw` with `to_texture` = 1 then delivers the
values of that drawing, with the phase as +0x4C and the old +0xD4, as
`logo_render_to_texture` does (`Logo.md` 6). With 0 it delivers the values
on the screen.

W is the frame of the camera path (`Camera.md` 5.3) with `d · ρ` as row 3.
Before the logo needed it, the core held 0 there, because the camera reads only the rows 0 to
2; now row 3 stands as in the guest, and `xb_campath.frame` delivers it
too. What `xb_logo_draw` does not deliver: the picture of the logo as the
quad of the veil (5.2, comes with `Effects.md`). Gradients, radial
texture and TM are delivered by `xb_texture`; the target
1024 × 1024 is drawn by the GPU (`Textures.md` 6). Fields for parts that are
not drawn are 0.

**Textures.** The set-up computes ten textures on the CPU and copies TM
from the ROM, eleven together (`Textures.md` 1, 4). All depend only on the
ROM and on the initial value of the noise, not on the time. `xb_texture`
makes one of them on request into a buffer of the host, byte for byte as it
lies in guest memory: the levels one after the other, for the two cubes six
faces aligned to 128 bytes, the texels reordered as by 0x800AAB46 and
little-endian (`Textures.md` 2, 3). A host loads the buffer unchanged with
the format word from `xb_texture_info`, or it orders the texels back itself.
The order of `XB_TEX_*` is that of the set-up and that of the vector file.

The original computes every picture row by row into a buffer and then copies
it reordered. The core writes every texel straight to its reordered place
and so needs no buffer: the height fields from 4.7 and 4.8 too already lie
in the buffer of the host in the process. As in the original, the normal map
reads only texels that come later in row order and are still heights. Where
the original does not write, the core writes 0: in the radial texture to the
right of the diagonal (`Textures.md` 4.4) and behind the buffer that the
last row of the normal map reads (4.7). In the guest 0 stood there in all
runs. Of the three layers of the noise the core computes only layer 0,
because the original copies only that. The layers 1 and 2 draw their numbers
nevertheless, so that layer 0 gets the same ones (4.8). The initial value of
the noise is the last draw of the set-up (0x8005A178), kept in the state as
`noise_seed`; `xb_timing.rng` shows it after `xb_init`.

Not rebuilt: the new noise on a restart (0x800572F4), because the core does
not build the repetition; the textures that arise on the GPU (cube of the
chamber, light views, targets of the veil, target of the logo, `Textures.md`
5, 6). Their content is the business of the host, the sequence of draw calls
for them is delivered by `xb_frame`. Internally the generators
take all arguments of the original, also the noise of
`texgen_falloff_mips`, which the set-up switches off with 0. Reachable and
checked are only the calls of the set-up.

**Orbit values and effects.** After every time step `xb_advance` and
`xb_advance_free` keep the four orbit values up to date as `anim_update`
does: from the eye at the new t and the centre of the blob, before
`blob_update` (`Camera.md` 6). Through the adjustment to the previous value
the angle about z depends on the sequence of the steps; on path 0 it never
takes effect. In the same step the core counts whether this frame calls
`fx_draw`, because every call swaps the two target textures of the veil
(`Effects.md` 3). The free camera (`anim_object` + 0xB = 0) stays out, as
with camera and chamber.

`xb_fx_draw` takes camera, t, x20 and the orbit values from the caller, like
`xb_blob_draw` and `xb_logo_draw`; so it can be called with the values of a
recording. It computes all values even if the frame does not draw the veil;
`veil` and `glow` say whether. The colours come as four float32 per colour,
the way the animation passes them to 0x800410B0; packing them into bytes is
the business of the host (`GPU.md` 4.2).

**Frame description.** `xb_frame` describes the frame that the last
`xb_advance` has made as a list of `xb_draw` in a buffer of the host, in the
order of the command stream: first what `anim_update` draws (U1 both light
views, U2 and U3 the target of the veil, U4 the logo into its texture), then
`anim_draw` (D1 chamber, D2 blob, D3 logo, D4 veil and glow, D5 finishing of
the frame), with the conditions from `FrameOrder.md` 3. Before the first
step there are no steps U. `xb_frame` does not change the state; called
twice it delivers the same. `clear_depth` reports the clearing of depth and
stencil before D3; that `anim_draw` clears the whole drawing surface before
and the steps U their targets is in `FrameOrder.md` 1 and `Effects.md` 3 and
always holds.

An entry names:

- **Step and kind** (`XB_STEP_*`, `XB_DRAW_*`), for the chamber the part,
  for the drops their index.
- **Mesh** (`XB_MESH_*`) with primitive, first index or vertex and count.
  The meshes are those of the earlier milestones: `xb_chamber_mesh` with
  group and detail level, `xb_blob_mesh` and `xb_blob_deformed`,
  `xb_logo_mesh`, `xb_fx_quads`. The glow surface of the blob changes with
  the camera and is therefore in the field `params` (20 values). The
  triangle of the finishing of the frame is written into the stream by the
  library itself; the core names only three vertices.
- **Program and combiners** as an address in the ROM (`GPU.md` 3, 4,
  `data/gpu/gpu.json`), or the format value of the fixed function (0x2,
  0x102, 0x202) and 0 for combiners from the stages (`GPU.md` 5). The host
  has to translate them.
- **Textures** per stage and **targets** as `XB_SURF_*`: the eleven textures
  from `xb_texture` and the surfaces that arise on the GPU (light views, two
  targets of the veil with depth, picture of the logo and its target, six
  cube faces, drawing surface, display). Named are only the stages that the
  draw call reads; in the stream of the original what was bound before stays
  on other stages (`FrameOrder.md` 5).
- **States** as they hold at the call (`xb_render_state`): alpha test,
  blending, depth test with comparison, depth write, back faces, colour
  mask, shadow comparison, as values of the methods of the NV2A
  (`FrameOrder.md` 5, `GPU.md` 6). These are the states that the animation
  sets or inherits; stage states, border colours, depth offset of the light
  view and viewport belong to program, block and target and are in `GPU.md`
  and in the vector file.
- **Constants** in at most two runs (`vs`), four values per constant in the
  field `params`; constant k is c[96 + k] from `GPU.md`. Named is what holds
  at the call, even if an earlier call has sent it (the constants 18 and 19
  in U3 come from U2).
- **Colours** for 0x800410B0 (`ps_first`, `ps_count`) or the factor colour
  for 0x8003CED0 (`factor`).
- **Matrices** with fixed function: slot 6, 0 and 1 of 0x8003E6C0 as world,
  view and projection (`GPU.md` 2.1).

**Another camera.** `xb_frame_from` describes the same frame as
`xb_frame`, but with eye, target and up of the host instead of those of the
path. From them it computes view and inverse like `camera_set_view`
(`Camera.md` 3); the projection stays. Every draw call that reads the camera
takes this one: the chamber on the screen and in the target of the veil,
blob, logo, veil and glow; the veil with the orbit values of this eye
(`Camera.md` 6), whose angle is adjusted to the stored one of the animation.
The same stay t, the switches `this+0xD` and `this+0xE` from the path, the
light views and the state, which the call does not change. With eye, target
and up from `xb_camera_at(xb_time())` the result is byte for byte that of
`xb_frame` (`hosts/web/test_w3.mjs`). If the three give no view (no
distance, or the view along up), the call delivers `XB_ERR_ARG`. The
original knows no such camera; its orbit around the origin at
`this+0xB` = 0 (`Camera.md` 6) is something else and stays out.

**Another path.** `xb_select_path` chooses path `path` of the table at
0x80070DB8, the way `campath_select` would with an argument ≥ 0: keys of the
path, eight own keys, tangents (`Camera.md` 5.2 to 5.4). Both callers in the
ROM pass −1; the branch is therefore unused there. Unlike `campath_select`
the call does not change the generator: it stays as `xb_init` left it after
its draw, so that pulses, noise and blob stay the same and only the camera
changes. It belongs before the first `xb_advance`. The table has four paths;
for `path` ≥ 4 the call delivers `XB_ERR_ARG`. With path 0, which `xb_init`
draws, every seventh frame of a run at 60 Hz equals the run without the
call: the draw calls byte for byte, the values with NaN as NaN; path 2 keeps
the 36 values of the pulses (`hosts/web/test_w3.mjs`). Without the call the
core stays as it was: `core/build.py` passes, the digest of the draw lists
is still `F6EFF3BBA045238C`.

WebAssembly does not fix the sign of a NaN that a computation produces. In
the values of the parts with NaN (until t ≈ 1.17, `Chamber.md` 6) it changed
between two runs in the same process of node (0x7FC00000 against
0xFFC00000), presumably with the tier in which the browser compiled the core
(*guessed*). Whoever compares values of the wasm core byte for byte
therefore takes NaN as NaN.

`xb_frame_setup` delivers the same for the set-up, in seven passes, so that
no buffer has to hold all 2599 draw calls at once: pass 0 the finishing of
the frame after the clearing (A1), 1 to 6 the chamber at the fixed time 5.35
into one cube face each (A2), with view and projection of the face in `view`
and `proj`. The matrices of the light views are still 0 there, as in the
original.

`XB_FRAME_DRAWS_MAX` (1320) and `XB_FRAME_PARAMS_MAX` (40 960) are enough
for every frame and every pass; the largest frame of a run at 60 Hz has 1283
draw calls and 25 060 values. A host with less memory gives smaller buffers
and gets `XB_ERR_SPACE` with the sizes needed.

The peculiarities of the GPU (vertex programs, combiners, raster rules) stay
in the hosts; the data for them is in `data/gpu/` and does not go into the
pack.

Not rebuilt: the repetition, as before; the composite matrix that the
library forms from the slots, and the values of the triangle in the
finishing of the frame (`GPU.md` 2.1, 7); what the rasteriser makes of parts
with NaN matrices. For these parts the core delivers the draw calls with NaN
in the constants, like the original.

**Sound.** The sound has its own clock and does not depend on
`xb_advance`: it runs in blocks of 32 samples at 48 kHz, the frame of the
sound hardware. Every ninth block begins with a beat of the sequencer (6 ms,
`Audio.md` 1, 4); beat n therefore lies at sample 288 · (n − 2). The host
calls `xb_audio` as its sound device needs samples and stops when the
animation ends; in the original the sound breaks off there (`Audio.md` 1).
In the short version the original starts no sound; the core then delivers
silence.

The sound needs memory of its own, 294 424 bytes, 274 048 of them for the
twelve delay lines of the reverb and 16 384 for the noise table. The host
gives it with `xb_audio_init`; without it `xb_audio` delivers silence and
costs nothing. So a host without sound does not pay for it, and a port with
little memory can do without the sound without changing the core.

A block passes through four stages:

1. **Sequencer** (`Audio.md` 3): ring of the events, list of the tracks,
   channel records of layer 0, one command per beat for every track that is
   due. It reads the data from the pack, as the BIOS does from the ROM.
2. **Voice layer** (`Audio.md` 5): every call of the sequencer becomes the
   values that the library sends to the hardware (attenuations, envelopes,
   filter, pitch, format). `xb_get_audio` shows them.
3. **Voices:** from these values samples, the way xemu's VP computes them
   (`Audio.md` 7, 8): two envelopes per block, advance 2^(pitch/4096),
   low-pass, attenuation, headroom 1. The result is the mix bins 0, 1 and 11
   as 24-bit values.
4. **GP stage** (`Audio.md` 6.2, 6.6): the frame routine of the program in
   24-bit fixed point. Bin 0 becomes left, bin 1 right, bin 11 the reverb on
   both sides. `xb_audio` gives the bits 8 to 23 of that.

The stages 1, 2 and 4 are bit-exact against the guest and against the
program. Stage 3 is an approximation, and a double one: how the hardware
turns the registers into samples is not in the BIOS but stands here after
xemu only (`Audio.md` 8); and the core does not recompute xemu bit-exactly
either. Section 4 says what deviates from what.

`xb_audio_block` delivers one block in detail: what the beat did, each of
the 16 voices before the gain and its gain into the three bins, the bins and
both sides as 24-bit values. The tests live on that, the workbench (events
on the time line, single channels) and a host that mixes itself, spatially
for example. `xb_audio_gp` calls the GP stage alone; it runs on the lines of
the sound memory, hence not together with `xb_audio` on the same state. The
tables are available on request into buffers of the host, like the textures;
in the memory of the sound lies only the noise, the other samples the core
reads or computes in their place.

Not rebuilt: layer 1 of the sequencer and the commands that no track uses
(`Audio.md` 3.2, 3.4; they halt their track like 0x80061D71); the duration
as a byte with bit 7 of the pitch set; the repetition, in which 0x800571AD
puts event 1 in anew; the two dead lines L1 and L2 and the branch for the
mode 4 of the GP program; the state machine P:0x013D, which does nothing
here; the tear-down. The core does not keep handle numbers of the voices and
the addresses of their buffers; they change nothing in the sound.

**A host from start to end.** This is how a host calls the core, and
how `hosts/native/run.c` does it:

```
state  = memory(xb_state_size()), aligned to 16 bytes
xb_init(state, pack, pack_size, cfg)
for the sound: xb_audio_init(state, memory(xb_audio_size()), size)

once:  make the meshes (xb_chamber_mesh per group and detail level,
       xb_blob_mesh, xb_logo_mesh, xb_fx_quads) and textures (xb_texture)
       and load them onto the GPU
once:  xb_frame_setup(state, 0 … 6, out) and draw (A1, A2)

per frame:
    rc = xb_advance(state, tick)            or xb_advance_free(state, dt)
    xb_frame(state, out)                    the draw calls of this frame
    if a draw call names XB_MESH_BLOB: xb_blob_deformed(state, mesh)
    draw what out names
    until rc = XB_DONE

independently of that, at the pace of the sound device:
    xb_audio(state, buffer, n)
```

Picture and sound do not touch each other: the draw lists of a run are the
same with and without sound, and the samples are those of `xb_audio` alone
(`test_k8`). Whoever wants to keep both together fetches, like the native
host, as many samples per frame as have passed since the last frame: 48 per
millisecond. A run at 60 Hz has 480 frames; the last, with t = 8.000012,
delivers `XB_DONE`, and 384 000 samples have been played by then. All
queries (`xb_frame`, `xb_get_*`, `xb_*_draw`, `xb_blob_deformed`,
`xb_texture`, `xb_frame_setup`) change no byte of the state and of the sound
memory; only `xb_init`, `xb_advance`, `xb_advance_free`, the interventions
and the calls of the sound write.

**What the core does not rebuild, together.** Every point is above at its
subsystem with the reason:

- the repetition (`this+0xA`) with everything that depends on it: restart of
  the sub-objects, new camera path, new noise, event 1 of the sound;
- the halting of the quantities (`this+0xC`), `factor` ≠ 1, `this+0x14` ≠ 0
  and the free camera (`this+0xB` = 0), for which no writer is known;
- camera paths other than path 0: computed like path 0, only it is checked;
- what the graphics library computes: the composite matrix, the triangle of
  the finishing of the frame, the packing of the colours, stage states,
  border colours, depth offset and viewport of the light view;
- the textures that arise on the GPU, and everything vertex programs,
  combiners and rasteriser do: that is the business of the host (`GPU.md`);
- in the sound layer 1 of the sequencer, the unused commands, the mode 4 of
  the GP program, the dead lines and the tear-down;
- the after-image after the end of the animation, which draws nothing at
  start-up (`Overview.md`, "After-image").

The only approximation in the core is stage 3 of the sound, the voices;
everything else is bit-exact against the guest (section 4).

## 3. Data pack

The pack contains the ranges of the unpacked kernel from `data/` unchanged.
The core finds them by start address and length, as the BIOS finds them
through its pointers. The decoded `.json` files in `data/` are for people
and for the Python models; they do not go into the pack.

Layout, all words 32 bits little-endian:

| Offset | Content |
|---|---|
| 0 | `XBPK` |
| 4 | format version, 1 |
| 8 | number of entries |
| 12 | total size in bytes |
| 16 | entries of 32 bytes: start address, length, offset in the pack (multiple of 16), 0, file name without extension on 16 bytes with zeros |
| … | data, every range at its offset, gaps with 0 |

The entries are sorted by address and do not overlap. The core checks that,
likewise that every range lies in the pack and that every range it needs is
present with exactly this length (`XB_ERR_PACK_*`). It reads words byte by
byte, so that it runs unchanged on devices with a different byte order too.

| File | Range | Length | Narrative |
|---|---|---|---|
| `audio/gp_program.bin` | 0x80053568 | 0x1894 | `Audio.md` 6 |
| `overview/asin_factors.bin` | 0x80066030 | 0xC4 | `Chamber.md` 11, `Overview.md` |
| `audio/pcm_80066ac0.raw` | 0x80066AC0 | 0x5540 | `Audio.md` 5 |
| `audio/pcm_8006c008.raw` | 0x8006C008 | 0xEB8 | `Audio.md` 5 |
| `audio/pcm_8006cec8.raw` | 0x8006CEC8 | 0x1A3F | `Audio.md` 5 |
| `audio/sequence.bin` | 0x8006E908 | 0x6F8 | `Audio.md` 3 |
| `camera/campath.bin` | 0x80070DB8 | 0x24C | `Camera.md` |
| `logo/logo.bin` | 0x80071008 | 0x33C8 | `Logo.md` |
| `chamber/scene.bin` | 0x800743D0 | 0x4134 | `Chamber.md` |
| `audio/envblocks.bin` | 0x800A7980 | 0x230 | `Audio.md` 5 |
| `audio/seq_pointers.bin` | 0x800A7BB0 | 0x64 | `Audio.md` 3.1 |

The list stands twice, in `tools/pack/pack.py` and `core/src/pack.c`.
`pack.py` checks every file against `data/*/SHA256SUMS` and against the
range that the corresponding `.json` names under `source`.

**Reproducible.** The pack depends only on the content of `data/`, not on
time, locale or order in the file system. The expected sum is in
`tools/pack/xboot.pak.sha256`; `pack.py --check` compares: 72 224
bytes, SHA-256
`f6cd4aece95ce8aa3f02f90a556eb2d0111ccf806b684e66b3bfbc2832e90ef8`.

## 4. Numerics

- **The number type of the interface** is `xb_real` = `float`. A port
  without an FPU can replace it by fixed point in its P0.
- **The build** uses `-std=c99 -O2 -ffp-contract=off -fno-fast-math`. No
  contraction to FMA, no reordering.
- **Precision as in the original.** The animation computes on the x87 with
  the control word 0x027F, that is with a 53-bit mantissa, and rounds to
  float32 only on storing (`Timing.md` 4.2). The core therefore computes
  such intermediate values in `double` and rounds to `float` where the
  original stores. Where the code computes with SSE (`rcpss`, `rsqrtss`,
  `Chamber.md` 11), the core computes in `float`, one rounding per
  instruction. Which place needs which rule is in the narrative; the core
  takes it over from there.
- **`rsqrtss` and `rcpss`** the core takes, like xemu, as exact 1/√x and 1/x
  in float32 (`Chamber.md` 11). The root is `__builtin_sqrtf`, on x86-64 the
  instruction `sqrtss`, in wasm `f32.sqrt`, both correctly rounded after
  IEEE 754. The build uses `-fno-math-errno`, so that no call of the library
  arises. On real hardware the two instructions deliver only an
  approximation. The core therefore meets xemu, not the console; that is the
  same limit as in the measurement.
- **NaN.** Quaternion 238 in the ROM gives NaN (`Chamber.md` 3). The core
  sets the pattern 0xFFC00000 of the guest there, because C does not fix the
  sign of a NaN after a multiplication. Which values are NaN after that does
  not depend on it, their bit pattern does. Comparisons and the digests in
  `test_k2.c` and `test_k6.c` treat every NaN alike: the native and the wasm
  build deliver different NaN patterns for the constants of the parts on
  quaternion 238, and both a different one from the guest. The core
  reproduces `cvttss2si` with the rule from `Chamber.md` 11
  (NaN → 0x80000000).
- **Bit-exact or bound.** Where the specification has hit bit-exactly, the
  core has to hit bit-exactly. Where not, the measured bound stands here.
  Time keeping: bit-exact, no bound needed. Camera, chamber, blob, logo,
  textures, orbit values, effects and all constants of the frame
  description: bit-exact against xemu.
- **Order of the computation.** Where the narrative names a formula but not
  the order of the operations, that of the model holds which the narrative
  names as its executable form. For the time keeping that is
  `tools/xemu/vectors_c3.py` (pulse table, ramp, pulses, `x24`, `x18`), for
  camera and chamber `tools/xemu/chamber_model.py`. If narrative and model
  name different orders, the disassembly decides, and the deviation is a gap
  (0x8005667A).
- **C computes `float` with `float` in `float`.** A constant `K_*` is
  `float`; `fd + K_ONE` with `float fd` is therefore a computation in
  float32, where the x87 computes in 53 bits. Every computation that runs on
  the x87 in the original therefore begins with a value in `double`. In the
  blob, exactly this once falsified all runs.
- **No library for mathematics.** The core brings functions such as the
  cosine along itself. `x87_fsincos` (`core/src/x87.c`) computes in
  `double`: reduction by the nearest multiple of π/2 with π/2 in three
  parts, then Taylor series up to x²¹ on |r| ≤ π/4. π comes from Machin's
  formula (`core/test/gen_trig.py`, also checks the constants in `x87.c`).
  Valid for |x| < 1.6 · 10⁶, NaN outside; the animation stays far below.
- **Measured, time keeping:** every caller of the animation stores sin and cos as
  float32. Over all 1 070 141 409 float32 arguments from 0 to 0x3FC90FE0
  (slightly above π/2; the pulses pass at most f32(π/2)) the core delivers,
  after storing, the same as `sin`/`cos` of the C library in `double` (the
  way of xemu, `Timing.md` 4.2) and the same as the instruction `fsincos` of
  this CPU with `fstp m32`: 0 deviations, for sin and cos
  (`core/test/sweep_k1.c`, native, 48.5 s). The wasm build computes the same
  `double` operations after IEEE 754 without contraction; it is checked
  against the vectors, not over the whole range. For arguments beyond π/2,
  which later subsystems need, the subsystem that introduces them measures.
- **Measured, camera and chamber:** the chamber needs sin and cos up to 2π (angles of the
  generators, `Chamber.md` 4.3), the camera up to π (blend, half opening
  angle); all arguments are float32. Over all 1 086 918 625 float32
  arguments from 0 to 0x40C90FE0 (slightly above f32(2π) = 0x40C90FDB) the
  core equals, after storing as float32, the C library and the instruction
  `fsincos`: 0 deviations for sin and cos (`sweep_k1.exe 0x40C90FE0`,
  49.4 s). Camera and chamber are bit-exact against the guest and against the
  model; a bound is not needed.
- **The blob** needs no `fsincos`: it computes sine and root with the SSE
  helpers (`Blob.md` 9), normalisations through 0x8005816D and 0x80058290.
  Nothing new to measure.
- **The logo** needs `fsincos` only for `rot_x(f32(π · 0.5))`, argument
  0x3FC90FDB; that lies in the range already measured for time keeping. The core unpacks the
  meshes with `i · factor + offset` in `double` and one rounding to float32;
  the product of an int16 with a float32 is exact in `double`, as on the x87
  (`Logo.md` 2.2).
- **The textures** need `fsincos` only in the table of the radial texture, for
  φ = f32(i / 256) < 1; that lies in the range already measured for time keeping. Falloff,
  subdivision and noise compute in integers with 32 bits like the code,
  including the signed and unsigned comparisons that `Textures.md` 4.5, 4.7
  and 4.8 name. Cubes, normal map, gradients and the radial texture compute
  in the x87 manner and store like the code; root and normalisation go
  through `sse_sqrt` and `v3_norm`. All eleven textures are bit-exact
  against the guest; a bound is not needed.
- **Effects and the frame description** need no new `fsincos`. The orbit values go through root, arc sine
  and arc cosine of the SSE helpers; effects and the constants of the
  chamber compute in the x87 manner. Two places at which a value stays in
  the register were found only once the draw-call stream was checked, because the stream shows them and the
  five recorded times do not: the quotient of the track time in
  `chamber_update` and far / (far − near) in the projection (`Chamber.md` 6,
  `Camera.md` 2). Both change values in the camera and chamber code; the recorded vector file
  still matches, the digest of the run in `test_k2` is a different one. All 447 530
  constant values, 18 449 colours and 4442 matrices of the recorded frames
  are bit-exact against the stream; a bound is not needed. The library
  multiplies the matrices of the fixed function in float32 (`GPU.md` 2.1);
  that belongs to the host.
- **Sound, bit-exact:** the tables compute like 0x80061AC1 in `double` with
  `x87_fsincos` and truncate; the FM table needs arguments up to 6434, far
  above the 2π already measured for camera and chamber. All 32 896 values of the sine and the FM
  table equal guest memory; a bound is not needed. Sequencer and voice layer
  compute in integers. The GP stage computes with 24-bit words in `int32_t`
  and the accumulator in `int64_t`, cut to 56 bits, with the rules from
  `Audio.md` 6.6; it equals the program on xemu's DSP core word for word.
- **Sound, approximation:** the core computes the voices in `double`, in the
  steps of `tools/xemu/audio_render.py`. Three things deviate from xemu,
  each on purpose. First the number type: xemu computes in `float`. Second
  the resampling: xemu takes libsamplerate, the core a sinc with 16 taps
  under a cosine window, cutoff min(1, 1/advance); that is a choice of its
  own and not a finding. Third the end of a voice: in the core the library
  learns of it by the next beat (`Audio.md` 5.5, marked as guessed there).
  Measured are therefore two things. Against the render: 385 920 samples of
  22 sounding pairs of channel and bin, largest deviation 8.8 · 10⁻⁹ at a
  peak of 0.162; that is the size of the rounding to float32 at the
  interface. The three bins that the core mixes from the values in `double`
  never lie further than half a 24-bit step (6.0 · 10⁻⁸) from the sum of the
  render. Against the recordings from xemu: the measures in `Audio.md`
  7. The core has no error against the
  hardware that one could state, because there is no measurement for it.
- **Ways of computing of its own in `Audio.c`:** e^x through x = k ln 2 + r
  and the series up to r¹³ (for 2^x, 10^x, the decay and the release of the
  envelopes); sine and cosine of the sinc through the addition theorems, two
  `x87_fsincos` per sample instead of 32; rounding to the even number
  through the addition of 1.5 · 2⁵². The root is `__builtin_sqrt`. The
  native and the wasm build compute the same `double` operations and deliver
  the same digest.

## 5. Build and tests

```
python core/build.py
```

1. `tools/pack/pack.py --check` builds the pack and compares the sum.
2. Native: clang for `x86_64-pc-windows-msvc`, core and one test executable
   each, `out/core/native/test_k0.exe` to `test_k7.exe`.
3. wasm32: clang with `--target=wasm32 -ffreestanding -nostdlib`, linked
   with `wasm-ld --no-entry`, without Emscripten. Exported are the calls
   from section 2, `memory` and `__heap_base`. The host puts state and pack
   into the linear memory from `__heap_base`. `out/core/wasm/xboot.wasm`,
   93 612 bytes, 0 imports. `build.py` also checks that every call that
   `xboot.h` declares is exported (46).
4. Both builds run against `docs/vectors/c3_timing.json`: natively
   `test_k0.exe` and `test_k1.exe`, in wasm `node core/test/wasm_k0.mjs` and
   `wasm_k1.mjs`. `test_k2.exe` and `wasm_k2.mjs` run against
   `docs/vectors/c5_chamber.json`, `test_k3.exe` and `wasm_k3.mjs` against
   `docs/vectors/c6_blob.json`, `test_k4.exe` and `wasm_k4.mjs` against
   `docs/vectors/c7_logo.json`, `test_k5.exe` and `wasm_k5.mjs` against
   `docs/vectors/c8_gpu.json`, likewise `test_k6.exe` and `wasm_k6.mjs`,
   `test_k7.exe` and `wasm_k7.mjs` against `docs/vectors/c4_audio.json` and
   `c4_gp.json` beside it. `test_k2.exe`, `test_k3.exe`, `test_k4.exe`,
   `test_k6.exe` and `test_k7.exe` each output the digest of a
   run over 8 s, `test_k5.exe` that of all textures; `build.py` aborts if the two
   builds differ. `test_k8.exe` and `wasm_k8.mjs` need no vector file: they
   check the whole run, and their digest must also be delivered by the
   native host `xboot_run.exe --digest`.
5. `core/test/check_k1.py`: the constants from `Timing.c` against
   `out/rom/kernel.img`, those from `x87.c` against `gen_trig.py`, and the
   core against the model `tools/xemu/vectors_c3.py` on synthetic sequences
   of ticks. `core/test/check_k2.py`: the constants from `Overview.c`,
   `Camera.c`, `Chamber.c` against the image, the geometry against model and
   guest memory, camera and chamber over the time line against the model.
   `core/test/check_k3.py`: the constants and immediates from `Blob.c`
   against the image, the meshes against model and guest, the five recorded
   runs frame by frame against guest memory, synthetic sequences against the
   model. `core/test/check_k4.py`: the constants and immediates from
   `Logo.c` against the image, the meshes against model and guest, both
   recorded runs frame by frame against guest memory and every draw call of
   the logo in the command stream, synthetic sequences against the model.
   `core/test/check_k5.py`: the constants and immediates from `Textures.c`
   against the image, all textures byte for byte against the dumps of the
   runs `c8_main` and `c8_main2` and against the model.
   `core/test/check_k6.py`: the constants and immediates from `Effects.c`
   and `FrameOrder.c` against the image, the addresses of the programs and
   blocks against `data/gpu/gpu.json`, and every constant, colour and matrix
   of the recorded frames against the words of the push buffer from the run
   `c8_main2`. `core/test/check_k7.py`: the constants from `Audio.c` against
   the image and against their derivation, the parameters and taps of the GP
   stage against the X memory of the program, the tables against model and
   guest memory, sequencer and voice layer beat by beat against the model,
   the voices against the render, the GP stage against the program on xemu's
   DSP core, the output against the recordings.
6. With `--sweep`: `sweep_k1.exe` up to 2π, the measurement from section 4.
7. With `--cost`: `cost_k8.exe` and `cost_k8.mjs`, the measurement from
   section 6, to `out/core/k8/cost.json`. With `--run`: the native host
   writes a whole run to `out/run/`.

**The native host.** `hosts/native/xboot_run.exe <pack> <dir>` writes a
whole run into files, from calls of `xboot.h` only; `<dir>` and
`<dir>/assets` must exist. With `--hz N` (default 60, steps 17, 17, 16 ms),
`--short`, `--wide`, `--start-tick N`, `--no-sound`, `--no-assets`. All
files are little-endian, as the values stand in memory.

| File | Content |
|---|---|
| `run.json` | settings, sizes of state and sound memory, number of frames, draw calls, values and samples, the largest frame, the digest of the run |
| `states.csv` | one line per frame: tick, return value, t as bits and as a number, `x18` to `x24`, path time and segment, both switches, eye, target, the four orbit values, calls of `fx_draw`, bulges, drops and draws of the blob, state of the logo, number of draw calls, values and samples |
| `frames.bin` | head: `XBRN`, version 1, size of an `xb_draw` (112), `XB_API_VERSION`. Then records: identifier `SETP` (seven passes of the set-up) or `FRAM`, number, tick, bits of t, number of draw calls, number of values, `clear_depth`, switch for the mesh of the blob; `view` and `proj` (16 float32 each); the `xb_draw`; the values as float32; with the switch the deformed mesh, 1734 times four float32 |
| `sound.wav` | 48 kHz, 16 bits, stereo, as long as the run |
| `assets.json`, `assets/` | what a renderer loads once, per file with the call that made it: the meshes of the chamber in both detail levels (positions, attributes, indices, shape table), both sphere meshes, the seven meshes of the logo, the two quads of the effects, the eleven textures; 79 files, 1 831 900 bytes |

A run at 60 Hz writes 480 frames with 433 105 draw calls and 8 064 980
values, before them the set-up with 2599 draw calls and 97 560 values;
`frames.bin` then has 90 MB, because the mesh of the blob goes along in 320
frames. The digest folds what the host writes: per frame tick, return value,
time values, orbit values, logo, blob, draw list, mesh and samples
(`run_whole` in `run.c`). For 60 Hz with sound it is `255E09F97DDD2114`.

**The set-up check** compares the initial value of t of both runs (`checks.start.t`) and
checks that the core rejects damaged packs.

**Time keeping** checks in both builds, for both runs, the pulse table (against the
guest and the model), the generator after the set-up (where read) and every
frame step: the core gets the tick that the guest stored in `this+0x28` and
has to meet t, `x18`, `x1c`, `x20`, `x24` bit-exactly, plus `XB_DONE`
exactly at the last step, t after the loop and the number of ticks. Natively
rules are added that the runs do not touch: step above 1 s, exactly 1000 ms,
tick backwards, overflow of the counter, free time. `check_k1.py` drives the
core through `test_k1 --dump` with six sequences (every ms over the whole
time, long and short; 60 Hz; random frame lengths; jumps above 1 s and
backwards) and compares every step with the model.

**Camera and chamber** are checked in both builds: the geometry of all twelve groups (counts,
shape tables, SHA-256 of the indices, plus of the positions and attributes
where the guest has written every vertex) and, at the five recorded times, path
time, segment, eye, target, up, view, projection, detail level, the world
matrix of each of the 271 parts (NaN where the guest has NaN) and both light
views from the point the guest used. Then the core runs twice from the start
over 8 s at 60 Hz (steps 17, 16, 17 ms), folds t, camera, tracks and all
world matrices per frame into an FNV-1a sum and has to deliver the same both
times. `wasm_k2.mjs` computes the same sum, and it has to equal the native
one. `check_k2.py` drives the core through `test_k2 --mesh` and `--dump`:
the geometry value by value against the model and, if the recorded run
`c5_main` is present locally, against guest memory; the camera at every
millisecond from 0 to 8 s; tracks and all world matrices every 25 ms and one
float32 step around every sample point of the tracks and every change of the
detail level.

**The blob** plays, in both builds, each of the five runs from
`docs/vectors/c6_blob.json`: with the (t, dt) of the guest through
`xb_blob_update`, with `blob_restart` where t jumps back, and with the
generator value from `c6_poke`. At every capture all bulges and drops, k,
colour and quad of the glow surface, the constants of the vector file and
the height range of the mesh have to be equal, at the end the number of
draws and of drops. The quad depends on the camera and is not compared after
a restart, because the original follows another path there. The four runs
without a restart then run once more through `xb_advance_free` with the dt
of the guest: t after every step, the frames with `blob_update`, x1c and x20
at the captures have to equal the guest, and the values on drawing then come
from the core itself. A run over 8 s at 60 Hz folds state, mesh, values on
drawing and both light views into a digest, the same natively and in wasm.
`check_k3.py` drives the core through `test_k3 --replay`: the whole object
before each of the 2623 calls byte for byte against guest memory, at the 16
captures the whole deformed mesh, all values on drawing with the camera of
the guest and 1098 calls of the weighted centre; plus four synthetic
sequences against the model (every millisecond, 60 Hz, random steps with
restarts, jumps back below 0.6).

**The logo** checks in both builds the counts of the seven meshes against
`docs/vectors/c7_logo.json` and every draw call of mesh D in both runs: the
t with which the vector file assigned it to a frame or to a drawing into the
texture goes through `xb_logo_update` in the order of the stream, then
`xb_logo_draw` with the camera of the core has to deliver the constants 0–8
bit-exactly; a drawing into the texture has to be one in the core too, with
the t of the vector file. The colour before E has to equal that of the core;
that before Q1 and Q2 has to be a value that the core delivers for a float32
t in [6, 6.25], and must not fall in the sequence, because the vector file
has no t for these calls. A run at 60 Hz through `xb_advance` checks the
beginning, both drawings into the texture and the final state, a run over
8 s the digest in both builds. `check_k4.py` drives the core through
`test_k4 --replay` with the t of every frame of the runs `c7_main` and
`c7_b`: after each of the 688 frames the fields that `logo_update` writes
against guest memory, the frames with drawing into the texture, then each of
the 119 draw calls of the logo in `trace.log` in its order against the
sequence that the core draws for these frames: mesh, constants of D with the
camera of the guest and with its own, colours before Q1, Q2 and E. The
assignment is by order, because the positions in the trace that the
measurement records at every stop lag behind the stops (xemu writes the
trace buffered). Plus three synthetic sequences against the model (every
millisecond from 5.2 to 8.05, 60 Hz, random steps with jumps back across 6
and 6.25).

**The textures** are checked in both builds, each of the eleven textures against
`docs/vectors/c8_gpu.json`, section `textures`: format word and length equal
to the resource in the guest, the SHA-256 of the texels equal to that of the
dump from guest memory at the end of the set-up (run `c8_main2`;
`vectors_c8.py` has the sums in the vector file). Plus the initial value of
the noise against `fx_seed`, the rejection of a buffer that is too small and
a digest over all textures, twice from a fresh state, the same natively and
in wasm. `check_k5.py` drives the core through `test_k5 --dump` and compares
byte for byte with the dumps of every local run that has all textures
(`c8_main`, `c8_main2`), and with the model `texture_model.py`.

**The frame description** plays, in both builds, the run `c8_main2`: the vector file
holds the step of every stop in whole milliseconds and t after it
(`timeline`); the core gets the ticks through `xb_advance` and has to meet t
bit-exactly after each of the 540 steps. Four stops of the measurement are
not frames and are skipped (`FrameOrder.md` 6). For the set-up
(`xb_frame_setup`, seven passes) and at each of the 20 recorded frames
(`xb_frame`) the list of the core has to equal that of the vector file: as
many draw calls in the same sequence, per call step, primitive, count,
program, block, the textures of the stages read, both targets and the
states. Excepted is what the stream shows without the animation setting it:
program and block of the finishing of the frame, textures on stages not
read, targets without a name in the vector file, states that nobody sent in
the frame; comparisons and factors count only as long as their switch is on.
Surfaces without a name in the vector file (five cube faces, depths, the
target of the logo) the test learns by their address and demands that every
surface of the core has exactly one address. At the 20 frames with an object
dump the four orbit values have to equal the guest bit-exactly, the two
colours of the veil packed, and alpha, factor colour, s and 1/s of the glow
the vector file. Plus `XB_ERR_SPACE` for buffers that are too small and a
digest over the set-up and all frames of a run at 60 Hz, twice, the same
natively and in wasm. `check_k6.py` drives the core through
`test_k6 --dump` and compares every recorded frame with the words of the
push buffer: the vertex constants that hold at the draw call with those the
core names; the colours, packed and distributed after the words 57 to 59 of
the block, with the factors in the stream; the factor colour; with fixed
function the model-view with world times view in float32.

**The sound** is checked in both builds: the eight tables against the SHA-256 of their
dump from guest memory (in the vector file), then the run `c4_seq`:
the core runs block by block, and each of the 900 measured commands has to
agree in beat, channel, address and number, each of the 620 calls into the
voice layer with its arguments, and after the first sample call of every
channel the values of 14 methods with those the guest sent. Whether the core
starts the voice at a note where the guest did, the test only prints: the
run stops at 2243 places, its voices end earlier (`Audio.md` 5.5). The GP
stage gets an impulse of 0.5 in bin 0, 1 and 11; the responses over 4 s have
to have the SHA-256 from `c4_gp.json` as float32, the other sides have to be
silent. Plus the rules of the interface (silence without sound memory and in
the short version, `xb_audio` in pieces equal to the blocks, muting) and a
digest over 12 000 blocks, twice, the same natively and in wasm.
`check_k7.py` drives the core through `test_k7 --tables`, `--run` and
`--gp`: the tables value by value against model and guest memory; 1340 beats
against `audio_model.py`, the voice layer from `vectors_c4.py` and the
render, with all registers of all voices after every beat; every voice in
every bin sample by sample against the render; 3000 frames of noise through
the GP stage against `out/dsp/gp.exe`, up to the limit; and the measures
against the recordings `c2audio`, `c4_dsp_out`, `c4_dsp_solo1` and
`c4_dsp_solo9`. With DSP the yardstick is the core's own output per channel,
every channel alone through the GP stage.

**The whole run** checks what only a whole run shows. `test_k8.c` drives the core
through `run_whole`, like the native host: set-up, every frame with draw
list, deformed mesh and the samples that fall into the frame. The run at
60 Hz has to have 480 frames, deliver `XB_DONE` exactly in the last and
384 000 samples; twice from the start it has to give the same digest. The
draw lists have to be the same with and without sound and have the digest
of `test_k6`, the samples those of `xb_audio` alone in one call. At six
frames (with the chamber, with the deformed blob, at both drawings into the
texture of the logo, at the end) 14 queries and the seven passes of the
set-up must not change a byte of state and sound memory. Runs at 30, 50 and
1000 Hz, the short version and the wide picture have to end at most one step
after t = 8; the short version has 30 frames and only silence. Behind state,
sound memory, pack and every buffer lie 64 guard bytes that have to be
untouched after all runs, and the pack has to have the same SHA-256 as
before. `wasm_k8.mjs` computes the same run call by call and has to deliver
the same digest, plus guard bytes and pack. This checks nothing new against
measurements: the per-subsystem tests do that, and this establishes that
their parts together give the same.

## 6. Costs

Measured with `core/test/cost_k8.c` and `cost_k8.mjs`
(`python core/build.py --cost`, output in `out/core/k8/cost.json`), on a run
at 60 Hz with sound, as the native host makes it. Machine: AMD Ryzen 7
3700X, Windows 10; built with clang 23.1.3 and the switches from section 4
for x86-64 without AVX. The numbers are meant to let a port calculate in
advance; they do not say how a port should build it.

### 6.1 Memory

| What | Bytes | Who provides it |
|---|---|---|
| state | 82 112 native (64 bits), 82 056 in wasm32 | host, once |
| sound memory | 294 424 | host, only with sound |
| data pack | 72 224 | host, stays unchanged and may lie in ROM |
| draw list for every frame | 147 840 (1320 `xb_draw` of 112) | host; less gives `XB_ERR_SPACE` |
| values for every frame | 163 840 (40 960 float32) | host |
| of which needed in the largest frame | 143 696 + 100 240 (1283 draw calls, 25 060 values) | |
| deformed mesh of the blob | 27 744, anew every frame | host |
| one block of sound in detail (`xb_audio_out`) | 3280 | host, only for `xb_audio_block` |
| geometry, once | 393 676 chamber, 31 408 blob, 22 768 logo, 160 quads | host or GPU |
| textures, once | 1 382 736 | host or GPU |
| program `xboot.wasm` | 93 612 | |
| stack, deepest point | 8536 in `xb_frame`, 7024 in the sound, 6792 in the set-up, 2872 in `xb_texture`, 1784 in `xb_init`, 792 in `xb_advance`, 456 in `xb_blob_deformed` | measured natively, with 8 bytes per pointer |

The core fetches no memory itself. `test_k8` puts guard bytes behind each of
these blocks and finds them untouched after all runs. The stack depth is the
smallest stack pointer that single-stepping (6.3) has seen below the caller.

### 6.2 Time on the PC

Around every call the time stamp counter of the CPU (3600 counts per µs,
measured against the system clock), per frame the smallest time from 20
runs. Microseconds per frame, mean over the frames of the section, the
largest frame behind it:

| Section | Frames | Draw calls | `xb_advance` | `xb_frame` | `xb_blob_deformed` | `xb_audio` (768 or 816 samples) | together |
|---|---|---|---|---|---|---|---|
| t < 0.6: chamber fine, blob round | 36 | 836 | 0.3 | 87 (91) | – | 461 (492) | 549 (579) |
| 0.6 ≤ t < 5.2: chamber fine, blob deformed | 275 | 1254 (1279) | 1.2 | 204 (212) | 129 (153) | 998 (1173) | 1331 (1517) |
| 5.2 ≤ t < 5.94: chamber coarse, blob, logo | 45 | 1283 | 1.3 | 210 (210) | 131 (159) | 1141 (1317) | 1483 (1667) |
| t ≥ 5.94: only logo and veil | 124 | 2.5 (9) | 0.2 | 0.3 (1.0) | – | 895 (1448) | 895 (1448) |
| the whole run | 480 | 902 | 0.9 | 143 | 86 | 944 | 1174 |

The whole run costs 564 ms of the core's computing time natively for 8 s of
animation with sound, 7 % of one processor core; four fifths of that go into
the sound. A frame without sound costs at most 0.37 ms. The wasm build in
node 22.18.0 needs 727 ms for the same run: per frame on average 164 µs for
`xb_frame`, 120 for the mesh, 1230 for the sound.

Once per run: `xb_init` 87 µs, `xb_audio_init` 10 µs, one pass of the set-up
with 433 draw calls 103 µs (six of them), the eleven textures together
7.9 ms (cubes 4.06 and 1.01, normal map 0.98, falloff 0.31, radial 0.22,
noise three times 0.42 ms).

### 6.3 Instructions

For eleven frames of the run and for the one-off calls, `cost_k8.c` counts
the instructions a call executes: it sets the single-step bit of the
processor, gets an exception after every instruction, counts, and reads from
the instruction byte what it computes. The share of the measuring frame
itself is determined with an empty call and subtracted. Cross-check in the
program: a loop of `mulss`, `dec`, `jnz` with 3000 instead of 1000
iterations counts exactly 6000 instructions and 2000 multiplications more.

The numbers hold for this build and this compiler. Another processor needs
other instructions; what the core demands in computation stays the same.
That is why the arithmetic operations stand separately: "float" and "double"
are scalar additions, subtractions, multiplications, divisions and roots in
this width, "conversions" the transitions between float, double and integer.

The largest frame, t = 5.234 (1283 draw calls):

| Call | Instructions | float | double | of which × | of which ÷ | Conversions | integer × | ÷ |
|---|---|---|---|---|---|---|---|---|
| `xb_advance` | 6 635 | 496 | 697 | 292 | 18 | 641 | 11 | 0 |
| `xb_frame` | 2 799 103 | 4 834 | 331 171 | 183 990 | 3 023 | 422 916 | 3 881 | 0 |
| `xb_blob_deformed` | 1 017 470 | 31 003 | 139 133 | 66 483 | 3 548 | 121 393 | 0 | 0 |
| `xb_audio`, 816 samples | 9 313 173 | 0 | 2 316 075 | 1 343 600 | 96 350 | 237 225 | 43 657 | 85 775 |

Over the run:

| t | `xb_advance` | `xb_frame` | `xb_blob_deformed` | `xb_audio` (samples) |
|---|---|---|---|---|
| 0.350 | 1 636 | 1 261 430 (836 draw calls) | – | 3 407 357 (768) |
| 2.017 | 6 114 | 2 801 444 (1278) | 1 059 661 | 8 518 187 (816) |
| 4.017 | 6 519 | 2 798 042 (1278) | 1 014 014 | 9 679 934 (816) |
| 5.200 | 6 473 | 2 796 686 (1279) | 1 020 379 | 8 939 570 (768) |
| 5.850 | 6 867 | 2 795 422 (1283) | 1 083 696 | 10 038 544 (768) |
| 5.967 | 1 682 | 5 082 (5) | – | 11 987 258 (816) |
| 6.017 | 1 341 | 4 534 (5) | – | 12 132 489 (816) |
| 6.267 | 1 301 | 2 512 (2) | – | 12 130 761 (816) |
| 7.017 | 1 301 | 2 512 (2) | – | 6 595 351 (816) |
| 8.000 | 1 301 | 2 512 (2) | – | 3 634 931 (768) |

Once per run: `xb_init` 493 547 instructions (20 016 float, 61 137 double),
`xb_audio_init` 72 581, one pass of the set-up 1 415 556 (161 383 double).
Textures: cube 64 2 545 872, normal map 4 277 271, one noise 6 488 982
(integer only, 65 538 multiplications), gradient 31 636, TM 6576; cube 128,
falloff and the radial texture are not counted, only timed (6.2). One block
of sound of 32 samples at t = 4.0: 376 137 instructions with a beat of the
sequencer, 375 550 without, 92 643 in double each and 3431 integer
divisions.

### 6.4 What follows

- **The picture.** `xb_frame` needs about 2200 instructions per draw call of
  the chamber, 260 of them arithmetic operations in `double` and 330
  conversions: the core computes W and the twelve constants anew per part
  and step, in the width of the x87, so that they are bit-exact. At 60 Hz
  that is, with the mesh of the blob, 229 million instructions per second as
  long as the chamber can be seen, and almost nothing after that (under 7000
  per frame). A port for which that is too much has two ways that the
  interface already offers: W once per part through `xb_chamber_world` and
  the constants itself from W, or the same computation in `float` if it does
  without the last place. Either is then an approximation of the port and
  stands in its row of `provenance.md`.
- **The mesh of the blob** costs a million instructions per frame,
  regardless of how the port draws.
- **The sound** is the most expensive part: 3.4 to 12.1 million
  instructions per frame of 16 or 17 ms, that is 210 to 710 million per
  second, almost all in `double` (up to 3.1 million arithmetic operations
  per frame). That is stage 3, the voices with the sinc of 16 taps; the GP
  stage alone computes in integers. For a PC that is nothing (5.7 % of a
  core). For a device without fast floating point in `double` it is to be
  expected that it will not suffice in real time; that is not measured here,
  the port does that. Such a port computes the sound in advance with the
  native host (`sound.wav`, 1.5 MB for 8 s) or builds the voices more
  cheaply; the interface for that is `xb_audio_block`. Its P0 decides.
- **The set-up** is one-off and small against a frame: six passes of 1.4
  million instructions, plus the textures, whose largest (cube 128) has four
  times as many texels as the counted one with 64.
- **The stack** stays under 9 KB; a port with 64-bit pointers calculates
  with these numbers, one with 32 bits with less.

### 6.5 In detail

Sound: 294 424 bytes of sound memory in both builds, provided by the
host only if it wants sound. The state itself grows by pointers and start
flags to 82 112 bytes natively and 82 056 in wasm32; `xboot.wasm` has
93 764 bytes. Per block of 32 samples: every ninth with a beat (at most one
command per track); per sounding voice two steps of the envelopes, up to
seven e^x, 16 `fsincos` for the taps and per sample two `fsincos` and 16
products, plus the low-pass; the GP stage with about 1700 products of
24 × 24 bits in 64 bits. Eight seconds of sound (12 000 blocks with all
fields of `xb_audio_block`) need 0.5 s natively on this machine. An
`xb_audio_out` has 3280 bytes. The voices compute in `double`; whether a
port without an FPU builds them in fixed point and with a cheaper resampler
its P0 decides.

State: 82 080 bytes native (64 bits), 82 036 bytes in wasm32 with orbit
values and effects included; orbit values and counter bring 24 bytes.
Before that, with the textures, it was 82 056 and 82 012 bytes; the
textures bring only the initial value of the noise (4 bytes, natively in an
alignment gap). Before that, with the logo, it was 82 056 and 82 008 bytes;
the logo brings 32 bytes. Before that, with the blob, it was 82 024 and
81 976 bytes; the blob brings 23 368 bytes, 20 808 of them for the unit
vectors of the large mesh (`blob+0x1C`). With camera and chamber alone it
was 58 656 and 58 608 bytes. Of that 271 × 156 bytes are placements,
about 14 KB quaternions, positions and index lists and about 1.9 KB camera
path. `xboot.wasm` has 70 696 bytes at this point.

The frame description lies in buffers of the host: 112 bytes per draw call
(`xb_draw`, the same in both builds), 4 bytes per value. The largest frame
of a run at 60 Hz (chamber, blob, logo, veil) has 1283 draw calls and
25 060 values, 243 936 bytes together; one pass of the set-up 433 draw
calls. From t ≈ 5.94 without the chamber there are at most nine draw calls.
`xb_frame` puts a few KB on the stack per call (the tracks of the chamber
2.3 KB, the values of the blob 1 KB).

The geometry lies in buffers of the host and is generated once. Detail level
0 has 5017 vertices (260 544 bytes of positions and attributes) and 9980
indices (19 960 bytes), detail level 1 has 2002 vertices (105 456 bytes) and
3858 indices (7716 bytes), 393 676 bytes together.

The host generates the sphere meshes once: 1734 vertices and 4054 indices
(28 916 bytes), 150 vertices and 346 indices (2492 bytes). The deformed
mesh has 27 744 bytes and comes anew every frame.

The host generates the seven meshes of the logo once: 986 vertices
(16 600 bytes for A to D, Q1 and Q2 with five float32, 1872 bytes for E) and
2148 indices (4296 bytes).

The host generates the eleven textures once, 1 382 736 bytes together: the
cubes 393 216 and 98 304, normal map 65 536, falloff with eight levels
349 520, the gradients 8192 each, radial 262 144, TM 1024, noise three times
65 536 (three times the same texels). The core needs no buffer for that
except that of the host, on the stack 2 KB per call for the tables of the
reordering.

Per frame: the camera once (one Hermite evaluation, one `fsincos`, two
roots); the tracks with 29 slerps (each up to three SSE sines and one arc
cosine) and 29 rotation matrices; per part up to two matrix products for W.
The time keeping computes at most twelve `fsincos`, mostly fewer, because
only pulses with |t − t0| ≤ w count. The blob: eight drops with one SSE sine
and one root each, 32 bulges with at most one sine or one new creation each;
plus the camera a second time for the switch `this+0xD`. The logo: per frame
from t ≈ 5.225 a blend of two points; on drawing two matrix products for D,
two for E and one `fsincos`. The deformation goes per cube face over 289
vertices and the bulges that reach the face, one approximate normalisation
per hit. The frame description: the camera and the tracks once more, per
part and step the world matrix (up to two matrix products), in D1 also the
inverse of L′, three matrix products, two point transforms and the weighted
centre over blob and eight drops; in U1 and U3 per part the world matrix and
one matrix product. A frame with all steps thus computes about 800 world
matrices. A host for which that is too much computes W once per part through
`xb_chamber_world` and the constants itself; that is a question for the
ports. The measured costs are in 6.2 and 6.3.
