# The web port

Use, structure and measures of the host in `hosts/web/`, including Next Gen
(section 12). The host loads the core, draws with WebGPU, plays the
sound and delivers pictures on demand. It shows the animation the way the
BIOS shows it in xemu: light and shadow of the chamber, blob, logo, veil and
glow (section 4), measured against xemu in section 9. It is also a
workbench for looking inside (section 10): free camera, parts one by one,
wireframe, intermediate targets, values, events, and from every part the way
to its specification. Its surface (section 11) has tabs for
intro, models, blob and sound, the state in the URL, an outline around what
the pointer points at, and a playground. It draws in the size and
shape of the window, starts by itself and runs in a loop (11.7).

What the animation is stands in `docs/decomp/`; what the core offers in
`core.md`. This text describes only the host. Everything in it is an own
choice without a model in the binary (`provenance.md`).

## 1. Starting

```
python core/build.py --no-test      builds out/core/wasm/xboot.wasm and out/pack/xboot.pak
python hosts/web/check_layout.py    checks layout.json against xboot.h
node hosts/web/serve.mjs            http://127.0.0.1:8360/
```

Core and data pack are not in the repo. The server passes them through from
`out/`, everything else comes from `hosts/web/`. The page needs a browser
with WebGPU and runs only over `http://localhost` or `https`, not as a file.

## 2. Structure

| File | Content |
|---|---|
| `index.html` | the page: canvas, transport, values, the panels of the workbench |
| `src/core.js` | loads the wasm core, gives it memory, reads the structures from `xboot.h`; `Run` holds a run and its ticks |
| `src/gpu.js` | WebGPU: a picture off the canvas, 640 × 480 or in the size of the canvas (11.7), display, read-back; the test pattern (section 4) |
| `src/renderer.js` | draws the draw list of the core: targets, passes, states per draw call (section 4) |
| `src/shaders.js` | the vertex programs and combiners from `GPU.md` 3 to 5 in WGSL; for the workbench the output `fin`, the views of the targets and the path |
| `src/bench.js` | the workbench: which part a draw call belongs to, where its specification stands, the events of the time line, a reader for the narratives |
| `src/textures.js` | the eleven textures of the core, ordered back, onto the GPU |
| `compare_w1.py` | acceptance: geometry against xemu (section 8) |
| `compare_w2.py` | acceptance: colour against xemu (section 9) |
| `dump.mjs`, `lightspace.mjs` | tools in node: the draw list of a frame; where the parts lie in their light view |
| `src/sound.js`, `src/sound-worklet.js` | sound: `AudioContext` at 48 kHz and an AudioWorklet with a core of its own |
| `src/main.js` | loads core and data; pictures and sound on demand for scripts |
| `src/workbench.js` | the workbench for people: tabs, state in the URL, playback, selection, playground, export (section 11) |
| `src/models.js` | the viewer of the meshes in the tab Models (11.2) |
| `src/tracks.js` | the track view of the sound in the tab Sound (11.4) |
| `src/nextgen.js`, `src/nextgen-shaders.js` | the picture "Next Gen": geometry and BVH, the passes per frame, their shaders (section 12) |
| `layout.json`, `check_layout.py` | sizes and offsets of the structures read; clang checks them for wasm32 against `xboot.h` |
| `serve.mjs` | server on localhost |
| `shot.mjs` | pictures and sound on demand, in Chrome without a window |
| `test_w0.mjs` | acceptance (section 7) |
| `test_w3.mjs` | acceptance of the workbench (section 10) and of the path choice (11.6) |

In wasm the core has no import and creates no memory (`core.md` 2).
`core.js` therefore hands out state, pack and output buffers consecutively
from the wasm memory from `__heap_base`, aligned to 16 bytes, and frees
nothing.

## 3. Time and transport

The host gives the core ticks in milliseconds since the start (`xb_advance`,
`Timing.md` 2). A **run** is the sequence of ticks the core has received.
Blob and logo depend on this sequence, not only on t (`core.md` 2). That is
why there is no rewinding in the core: whoever wants to go back starts the
run anew and gives the same ticks once more. That is cheap, because only
`xb_advance` runs and the frame description is needed only at the
destination: 480 steps to t = 8 take 0.8 ms in node on this machine, one
`xb_frame` at t = 2 another 0.35 ms (measured with the wasm core, script in
the work folder `out/`, not in the repo).

| Control | What happens |
|---|---|
| Play | one step per browser frame onto the tick of the clock (`performance.now`), as the original runs by time and not by frames; at the end (t ≥ 8, `XB_DONE`) the run stops |
| Pause | no further step; the sound is silent |
| Frame forward | one step of the run at 60 Hz: 17, 17, 16 ms, counted by the frames so far (as `hosts/native/run.c`) |
| Frame back | the run anew with all ticks so far except the last |
| Time line | the run at 60 Hz from the start up to the first frame whose tick reaches the chosen millisecond |
| Start | the run anew, no step |

Below the picture stand t as a number and as a bit pattern, tick, frame
number, the quantities `x18` to `x24` (`Timing.md` 4) and the number of draw
calls and values of the frame (`xb_frame`).

## 4. The picture

`renderer.js` draws what `xb_frame` names for the frame (`core.md` 2,
`FrameOrder.md`), every draw call into the target the core names, with the
states the core passes along:

| Step | Target in WebGPU | What |
|---|---|---|
| U1 | two depth surfaces 512 × 512, 16 bits | the chamber from the blob downwards and upwards, depth only, with depth offset 10 and 4 (`GPU.md` 6.2) |
| U2, U3 | two surfaces 1024 × 512 alternating, one depth | disc and chamber for the veil (`Effects.md` 3) |
| U4 | surface 1024 × 1024 with depth, copied afterwards | the logo into its texture (`Logo.md` 6) |
| D1 to D4 | drawing surface 1280 × 480 with depth | chamber, blob, logo, veil and glow |
| D5 | picture 640 × 480 | per pixel the mean of two pixels of the drawing surface (`GPU.md` 7) |

Consecutive draw calls into the same target are one pass. A target is
cleared where the frame draws into it for the first time: the light views to
depth 1, the target of the veil to 0xFFFF0000, that of the logo and the
drawing surface to black; before the logo the depth of the drawing surface,
if the core reports it (`FrameOrder.md` 3).

| What | How |
|---|---|
| meshes | loaded once from the core: chamber in both detail levels with its axes per vertex, blob, drops, the seven of the logo, the two quads of the veil; the deformed mesh of the blob and its glow surface anew every frame |
| textures | the eleven from `xb_texture`, ordered back in `textures.js` (`Textures.md` 3); level 0 only, because no stage of the original reads another (`GPU.md` 6.1) |
| programs | the seven from `GPU.md` 3 in `shaders.js`, line by line after `data/gpu/programs.txt`; the fixed function as world · view · projection from the three slots, computed in `double` |
| combiners | the seven blocks from `GPU.md` 4.3 with the rules by which xemu reads them (4.1), and those from the stages (5) |
| colours | the colours for 0x800410B0 packed as there (`GPU.md` 4.2), the factor colour as four bytes |
| position | 0.53125 pixels to the right and down, then truncated to sixteenths of a pixel (`GPU.md` 3) |
| filter | every stage averaged between four texels; at the edge clamp or repeat as in `GPU.md` 6.1. Where a border colour holds (textures of the logo, light view), the shader reads the four texels itself, because WebGPU knows no border colour |
| shadow | depth of the light view filtered, then compared (`GPU.md` 4.4), with the case behind the plane of the light |
| states | depth test, comparison and write, blending, alpha test, back faces, colour mask per draw call from `xb_render_state`; front face clockwise (`GPU.md` 6.3) |

The texture of the logo arises in two frames of a run and is shown in all
later ones. The renderer therefore goes along with every step of a run, also
when going back and for a picture on demand, and draws only this one target
in doing so. All other targets arise in the frame that reads them.

**Not drawn** is the cube texture of the set-up (A2, `Textures.md` 5). No
draw call of the recorded frames binds it: among the textures in
`vectors/c8_gpu.json` (`kinds`, `state`) its format word 0x0881062E does not
occur. The picture therefore does not depend on it.

**What the host assumes** or does differently from the original:

- The reference is xemu, not the console. The rules of the combiners, the
  truncation to sixteenths of a pixel and the behaviour behind the light are
  those of xemu (`GPU.md` 3, 4.1, 4.4).
- Without the depth test a draw call writes no depth, even if "depth write"
  is set (TM and lettering). *Guessed*; in the picture it makes no
  difference, because nothing reads the depth afterwards.
- Parts with NaN in their constants (`Chamber.md` 6, until t ≈ 1.17) are
  drawn nowhere, not into the light views and the target of the veil either.
  For the depth of the drawing surface that is measured (`FrameOrder.md` 4),
  for the two other targets *guessed*; the frames at t = 0.66 and 0.80 lie
  below the threshold with that (section 9).
- Copying into the texture `logo`+0xD0 (0x8003EBC0) is a copy texel for
  texel.
- The depth of the drawing surface and of the veil has 32-bit floating point
  instead of 24 bits, that of the light views 16 bits as in the original.
- The host computes the matrices of the fixed function in `double` instead
  of, like the library, in float32 with its own order of summation (`GPU.md`
  2.1).

### The test pattern

With `?pattern=1` the page draws, instead of the animation, three bars into
the picture of 640 × 480, on the ground (16, 16, 16), each as long as its
value times 640 pixels:

| Rows | Colour | Value |
|---|---|---|
| 40–79 | white | t / 8 |
| 120–159 | green | draw calls / 1320 (`XB_FRAME_DRAWS_MAX`) |
| 200–239 | blue | values / 40 960 (`XB_FRAME_PARAMS_MAX`) |

The pattern has one purpose only: from a picture read back one can read to
the pixel whether it belongs to the time ordered. `test_w0.mjs` still checks
with it.

## 5. Pictures on demand

```
node hosts/web/shot.mjs --t 0.5,2,6.1 [--hz 60] [--out out/web/shots]
node hosts/web/shot.mjs --ticks 17,34,50
```

The script starts the server and Chrome without a window and opens the page
with an order:

| Query | Picture |
|---|---|
| `?shot=<s>,<s>,…[&hz=N]` | per time in seconds the first frame of a run at N Hz (default 60) whose tick reaches the time |
| `?ticks=<ms>,<ms>,…` | the frame after exactly this sequence of ticks, such as that of an xemu run |
| `?audiotest=<n>[&from=<k>]` | n stereo frames of sound from frame k, computed without a sound device |
| `&full=1` | with every picture also the drawing surface 1280 × 480 and its depth (float32, 1 = nothing drawn) |
| `&pattern=1` | the test pattern instead of the animation |
| `&off=<a>,<b>,…` | the animation without what is named, for counter-checks: `shadow` (no shadow comparison), `veil`, `glow`, `cull` (no face left out), `flip` (the other winding as the front face), `snap` (position not truncated to sixteenths of a pixel), `cast2` … `cast5` (one group not into the light views), `prim7` (no fans into the light views) |
| `&cam=ex,ey,ez,tx,ty,tz` | the picture from another camera, up (0, 0, 1) (`xb_frame_from`, section 10); `&cam=path` that of the path through the same call |
| `&hide=<part>,…`, `&solo=<part>,…` | leave parts out or draw only them; the names of the parts are in `src/bench.js`, `PARTS` |
| `&wire=1`, `&wire=2` | wireframe over the picture or alone |
| `&path=1` | draw the path of the camera in |
| `&view=<target>` | instead of the picture `light0`, `light1`, `fx`, `logo` or `tex0` … `tex10` |
| `&pick=x,y` | which draw call can be seen at pixel (x, y) of the picture; stands in `shots.json` as `picked` |
| `&size=W,H` | the picture in W × H and this shape instead of 640 × 480 (11.7); for viewing, not for measurements. Width and height stand in `shots.json` as `width` and `height` |

For that `shot.mjs` knows `--full`, `--pattern`, `--off`, `--cam`, `--hide`,
`--solo`, `--wire`, `--path`, `--view` and `--pick`; `--ticks @file.json`
takes the list `ticks` of a recording from `tools/xemu/frame_at.py`.

**The picture comes from the program, not from the browser.** The page reads
its own texture back from the GPU and sends the bytes to the server, plus
tick, frame number, t as a bit pattern, `x18` to `x24` and the numbers from
`xb_frame`. `shot.mjs` writes one PNG file per picture and everything else
to `shots.json`. A screenshot of the browser occurs nowhere.

Chrome is the copy in `.tools/chrome/` (`provenance.md`, "Third-party
tools"). It runs with `--headless=new` and a profile of its own in
`out/web/chrome-profile/`. WebGPU needs no further switch in doing so. The
switch `--disable-features=NetworkServiceSandbox` is needed, because
otherwise the first start with a new profile never loads the page.

## 6. Sound

In the core the sound has its own clock and does not depend on `xb_advance`
(`core.md` 2, "Sound"). The host uses that: in the AudioWorklet a second
core runs that only calls `xb_audio`, 128 stereo frames per call of the
browser, and divides the 16-bit values by 32 768. The `AudioContext` runs at
48 kHz, the rate of the core; there is no resampling in the host.

The host keeps picture and sound together through the millisecond, 48 frames
per tick, like the native host. When playing from a place, the second core
starts the sound from the beginning and computes silently up to there; there
is no other way to get to a place in the sound. While playing, picture and
sound run on two clocks (`performance.now` and that of the sound device) and
are not resynchronised. How far they drift apart in eight seconds and how
large the latency of the sound device is, W4 measures.

Browsers play sound only after a click. The `AudioContext` therefore arises
at the first "Play". The wasm core computes eight seconds of sound in node
on this machine in 0.6 s; seeking a place shortly before the end can take
that long.

## 7. The threshold: xemu against xemu

Every comparison of the web port with xemu has a limit: two runs of xemu do
not show the same picture themselves. This section measured how
far apart they lie.

**Runs.** `w0_A`, `w0_B`, `w0_C`, each with
`python tools/xemu/bench.py --frames A:520:10 --peek 0x8006F050:0x30`
(A = 10, 10, 15): one picture every ten flips, 48 per run over the whole
animation, plus from every snapshot the first 0x30 bytes of the animation
object and with them t (`this+0x10`) and the last tick (`this+0x28`,
`Timing.md` 2). Evaluated with
`python tools/xemu/frame_pairs.py w0_A w0_B w0_C`, result in
`vectors/w0_xemu_pairs.json`.

**Pairing.** No picture of one run has the t of a picture from another.
Every two runs are therefore paired so that to every picture of the one
belongs the picture of the other with the nearest t; the distance Δt is
recorded with it.

**Measures** between two pictures of 640 × 480 with 8 bits per channel:

| Measure | Meaning |
|---|---|
| `mad` | mean deviation per channel, 0 … 255 |
| `far` | share of the pixels whose largest channel deviation lies above 16 |
| `iou` | overlap of the masks "largest channel ≥ 24": intersection over union |

**Result**, 143 pairs from three runs:

| Δt | Pairs | `mad` median | `mad` at most | `far` at most | `iou` at least |
|---|---|---|---|---|---|
| under 2 ms | 7 | 0.52 | 2.89 | 0.028 | 0.728 |
| 2 to 10 ms | 19 | 1.06 | 30.11 | 0.525 | 0.636 |
| 10 to 30 ms | 36 | 1.68 | 5.96 | 0.212 | 0.527 |
| from 30 ms | 81 | 0.16 | 26.07 | 0.977 | 0.028 |

The seven pairs with Δt under 2 ms one by one:

| t | `mad` | `far` | `iou` |
|---|---|---|---|
| 2.917 | 0.03 | 0.0009 | 0.996 |
| 3.594 | 0.52 | 0.0118 | 0.990 |
| 4.266 | 0.70 | 0.0095 | 0.981 |
| 4.439 | 0.85 | 0.0097 | 0.968 |
| 4.615 | 0.43 | 0.0085 | 0.994 |
| 5.610 | 0.42 | 0.0121 | 1.000 |
| 6.258 | 2.89 | 0.0284 | 0.728 |

**What follows.**

1. **The threshold at equal t**, from six of the seven pairs: `mad` up to
   0.85, `far` up to 1.2 %, `iou` from 0.968. No comparison that only aligns
   t can fall below that. Measured at six pairs between t = 2.9 and 5.6; for
   the beginning and the end of the animation there is no pair this close.
2. **The seventh pair shows what it depends on.** At t = 6.258 in both runs
   the lettering stands at different places of its flight in (looked at:
   `w0_A` flip 371, `w0_C` flip 376). The t in the snapshot is that of the
   animation object at the moment of saving; the CPU may already have
   updated it for the next frame. Picture and t can therefore lie one frame
   apart. At 60 frames per second that is about 17 ms.
3. **Even a few milliseconds matter more in fast passages than any deviation
   of a renderer.** The pair with the largest `mad` (30.1) has Δt = 9 ms, at
   t = 5.77, where the logo fills the picture and the camera moves fast.
   Conversely, the final picture stands still: in all 32 pairs whose two t
   lie from 6.4 on, the pictures are equal, `mad` = 0, at up to 84 ms
   distance. 23 of them fall into the class from 30 ms.
4. **`iou` is no use for almost empty pictures.** In the first 0.55 s the
   picture is almost black, the masks have 48 to 520 pixels. There `iou`
   lies between 0.38 and 0.93 in the eight pairs at `mad` under 0.03. The
   vector file names the size of both masks per pair.

**For sections 8 and 9 that means:** a comparison by the nearest t is not enough.
The web port gets the ticks of the xemu run (`?ticks=`), so that the blob
also sees the same sequence of steps, and for every picture from xemu it has
to be fixed which tick it belongs to, not only which approximate t. The
measurement bench does not yet deliver either: it knows the tick of the
snapshot, but not the sequence before it, and not whether the picture
belongs to this tick or to the previous one. Section 8 solves that. For
comparisons without the ticks the threshold from point 1 holds with the
uncertainty from point 2.

In any case the comparison is against the rasteriser of xemu, not against
the console (`core.md` 4 names the same limit for the computation).

## 8. Geometry against xemu

**Recording.** `python tools/xemu/frame_at.py --name w1_t2000 --t 2.0` lets
the BIOS run in xemu up to the first frame with t ≥ 2. Through the GDB stub
it stops at every call of 0x80057949 and reads the animation object; the
last tick (`this+0x28`) at stop k is the tick with which frame k − 1 was
made. So at the end the whole sequence of the ticks is there. At the last
stop the script writes a jump to itself (EB FE) at the start of the function
and lets the guest run: it marks time, the GPU works off what it has sent,
and the snapshot comes from the running machine. Only then does xemu write
the drawn surfaces back into guest RAM; a snapshot with the guest stopped
contained neither picture nor depth. From the snapshot come the drawing
surface 1280 × 480 (0x3628000) and its depth surface (0x33D0000, depth in
bits 8 to 31).

Checked per run: the object in the snapshot equals that at the stop, and
xemu has executed exactly one flip more than the animation has made frames
(the finishing of the frame of the set-up). The snapshot therefore shows the
picture of the last tick, complete.

**The reference is the depth surface, not the picture.** Where the depth is
written, a body lies, independently of light and texture. That is the
silhouette this comparison needs; it cannot be read from the picture of xemu, because
large parts of the chamber lie in the dark. The depth surface holds what was
drawn last with depth: chamber, blob and drops (D1, D2), or, as soon as the
logo comes in and `anim_draw` clears the depth before it, the meshes A to D
of the logo. From t = 6 nothing writes depth any more; only the picture
remains there.

**Comparison.** `python hosts/web/compare_w1.py` gives the web port the same
ticks per recording, reads its drawing surface and depth back and measures:

| Measure | Meaning |
|---|---|
| t | t of the web port after the ticks as a bit pattern against that of the guest |
| `iou` | overlap of the two masks "depth written" |
| `differ` | pixels that lie in one mask only |
| `off_outline` | of those, the ones that lie further than one pixel from the outline of the other mask: a missing or surplus body, not an edge that fell differently |
| depth | on the pixels of both masks the distance in steps of the 24-bit surface; `deep` counts those above 256 |
| picture, from t = 6 | the same measures for the masks "green": green ≥ 8 and at least 8 above blue, in both drawing surfaces. In the web port only the meshes A, D and E of the logo have such a colour |

**Result**, 16 runs from t = 0.30 to 7.80, in `vectors/w1_frames.json` with
the steps of every run:

| t | Frame | Reference | Pixels xemu | Pixels web | `iou` | `differ` | `off_outline` | depth: median, 99 %, `deep` |
|---|---|---|---|---|---|---|---|---|
| 0.303 | 21 | – | 0 | 0 | – | 0 | 0 | |
| 0.802 | 54 | depth | 384 392 | 384 385 | 0.99962 | 147 | 0 | 7, 130, 918 |
| 1.309 | 90 | depth | 351 812 | 351 814 | 0.99951 | 174 | 0 | 6, 125, 1184 |
| 2.001 | 152 | depth | 292 094 | 292 093 | 0.99939 | 177 | 0 | 5, 84, 630 |
| 2.617 | 198 | depth | 245 650 | 245 653 | 0.99932 | 167 | 0 | 4, 47, 477 |
| 3.208 | 245 | depth | 201 841 | 201 847 | 0.99929 | 144 | 0 | 4, 49, 352 |
| 3.800 | 298 | depth | 199 031 | 199 038 | 0.99906 | 187 | 0 | 5, 54, 470 |
| 4.407 | 369 | depth | 228 106 | 228 100 | 0.99906 | 214 | 0 | 4, 57, 504 |
| 5.007 | 421 | depth | 329 556 | 329 556 | 0.99936 | 210 | 0 | 5, 80, 949 |
| 5.309 | 423 | depth (logo) | 3 862 | 3 858 | 0.99690 | 12 | 0 | 12, 34, 0 |
| 5.608 | 441 | depth (logo) | 368 065 | 368 077 | 0.99974 | 94 | 0 | 20, 58, 0 |
| 5.866 | 481 | depth (logo) | 614 399 | 614 399 | 1.00000 | 0 | 0 | 1, 18, 48 |
| 6.114 | 467 | picture | 76 258 | 76 328 | 0.98700 | 998 | 345 | |
| 6.309 | 490 | picture | 37 022 | 37 109 | 0.97957 | 765 | 382 | |
| 6.809 | 545 | picture | 37 022 | 37 109 | 0.97957 | 765 | 382 | |
| 7.800 | 620 | picture | 37 022 | 37 109 | 0.97957 | 765 | 382 | |

- **t** equals the guest bit for bit in all 16 runs, after 21 to 620 steps
  of 1 to 45 ms.
- **Twelve pictures against the depth:** `iou` from 0.9969 to 1, nowhere a
  pixel away from the outline. The chamber with its 271 parts, blob, drops
  and the logo in space therefore lie where xemu draws them; what deviates
  are single pixels at edges. The depth itself deviates in the median by 1
  to 20 steps of 16 777 215. The pixels with more than 256 steps (at most
  0.34 % of the common ones) lie at jumps of the depth, that is at inner
  edges: counted for t = 1.309 (1183 of 1184) and 5.007 (949 of 949).
- **Four pictures against the picture:** `iou` 0.987 and 0.980, above the
  threshold from section 7 (0.968). 345 to 382 pixels lie away from the
  outline. Looked at in the map of the deviations for t = 6.309
  (`out/web/w1/w1_t6300/sheet.png`): they lie at the two TM marks, which are
  green in xemu and white quads without texture in the web port, and at two
  small places of the X that are almost black in xemu. Both are appearance
  and belong to section 9; the pixels are not counted by place.
- **t = 0.303:** before t = 0.6 nothing writes depth, and the picture shows
  only the veil. There is nothing to compare here; both masks are
  empty.

**Counter-checks**, each reset afterwards:

| Change | Run | `iou` | `off_outline` | `deep` |
|---|---|---|---|---|
| none | t = 3.208 | 0.99929 | 0 | 352 |
| one tick fewer (the frame before) | t = 3.208 | 0.97053 | 1648 | 60 723 |
| one tick of 17 ms more (the frame after) | t = 3.208 | 0.96077 | 3126 | 83 866 |
| without the offset of 0.53125 pixels | t = 3.208 | 0.98839 | 0 | 16 350 |
| without the offset | t = 5.608 | 0.99586 (instead of 0.99974) | 2 | 186 780 (instead of 0) |
| without the offset | t = 6.309 | 0.90058 (instead of 0.97957) | 478 | |

The comparison therefore tells one frame from the next and half a pixel from
the right place. The uncertainty from section 7, point 2, is thereby
removed: the picture in the snapshot is that of the last tick.

**What the comparison does not say.**

- It holds for 16 times, not for every frame. Between them lie up to 1 s
  (6.809 to 7.800, in the still final picture) and otherwise at most 0.7 s.
- The chamber is checked from t = 0.8 to 5.0. From t = 5.225 to 5.94 it is
  still drawn, but the depth is cleared afterwards; only the logo is checked
  there.
- TM marks and lettering are checked only through the picture, the logo in
  the target 1024 × 1024 only through the picture that comes of it.
- This comparison does not draw the light views, the target texture of the veil and the
  cube texture; their geometry is the same chamber from another view and is
  checked in section 9.
- The reference is xemu at single resolution, not the console.

## 9. Colour against xemu

**Recording.** The 16 runs from section 8 and 18 more with the same command
(`python tools/xemu/frame_at.py --name w2_t<ms> --t <s>`), 34 pictures
together from t = 0.13 to 7.80: one picture each with all ticks before it
and the drawing surface 1280 × 480 from the snapshot.

**Comparison.** `python hosts/web/compare_w2.py` gives the web port the same
ticks per recording, reads its drawing surface back and measures it against
that of xemu with the measures from section 7: `mad`, `far` (share of the
pixels with more than 16 steps of distance in one channel), `iou` of the
masks "largest channel ≥ 24"; plus the 99th percentile and the largest
distance per pixel. The drawing surface is compared, not the picture
640 × 480: the finishing of the frame only averages (`GPU.md` 7) and would
halve deviations.

**Threshold.** That from section 7, point 1, measured between two runs of
xemu at equal t: `mad` up to 0.85, `far` up to 1.2 %, `iou` from 0.968. Here
both sides have the same ticks, the blob therefore the same sequence of
steps; the threshold is thus an upper bound, not a measure of how close a
renderer can come.

**Result**, in `vectors/w2_frames.json`:

| t | Run | `mad` | `far` | `iou` | 99 % | largest |
|---|---|---|---|---|---|---|
| 0.127 | `w2_t0100` | 0.000 | 0.0000 | 1.0000 | 0 | 1 |
| 0.303 | `w1_t0300` | 0.000 | 0.0000 | 0.9977 | 0 | 1 |
| 0.514 | `w2_t0500` | 0.000 | 0.0000 | 1.0000 | 0 | 1 |
| 0.663 | `w2_t0650` | 0.112 | 0.0020 | 0.9981 | 8 | 99 |
| 0.802 | `w1_t0800` | 0.085 | 0.0009 | 0.9964 | 6 | 69 |
| 1.001 | `w2_t1000` | 0.000 | 0.0000 | 1.0000 | 0 | 0 |
| 1.309 | `w1_t1300` | 0.001 | 0.0000 | 1.0000 | 0 | 1 |
| 1.713 | `w2_t1700` | 0.021 | 0.0000 | 0.9745 | 2 | 26 |
| 2.001 | `w1_t2000` | 0.015 | 0.0000 | 0.9595 | 1 | 25 |
| 2.305 | `w2_t2300` | 0.031 | 0.0002 | 0.9711 | 2 | 44 |
| 2.617 | `w1_t2600` | 0.091 | 0.0032 | 0.9980 | 6 | 108 |
| 2.902 | `w2_t2900` | 0.043 | 0.0009 | 0.9843 | 3 | 63 |
| 3.208 | `w1_t3200` | 0.048 | 0.0014 | 0.9944 | 3 | 79 |
| 3.502 | `w2_t3500` | 0.077 | 0.0025 | 0.9996 | 4 | 144 |
| 3.800 | `w1_t3800` | 0.058 | 0.0017 | 0.9983 | 3 | 95 |
| 4.107 | `w2_t4100` | 0.082 | 0.0024 | 0.9994 | 4 | 204 |
| 4.407 | `w1_t4400` | 0.079 | 0.0026 | 0.9993 | 4 | 147 |
| 4.719 | `w2_t4700` | 0.088 | 0.0024 | 0.9993 | 4 | 147 |
| 4.850 | `w2_t4850` | 0.101 | 0.0026 | 0.9997 | 5 | 143 |
| 5.007 | `w1_t5000` | 0.083 | 0.0018 | 1.0000 | 4 | 160 |
| 5.109 | `w2_t5100` | 0.057 | 0.0006 | 1.0000 | 3 | 86 |
| 5.208 | `w2_t5200` | 0.055 | 0.0008 | 1.0000 | 3 | 101 |
| 5.309 | `w1_t5300` | 0.037 | 0.0003 | 1.0000 | 2 | 76 |
| 5.456 | `w2_t5450` | 0.029 | 0.0003 | 1.0000 | 2 | 50 |
| 5.608 | `w1_t5600` | 0.005 | 0.0000 | 1.0000 | 0 | 26 |
| 5.866 | `w1_t5850` | 0.000 | 0.0000 | 1.0000 | 0 | 1 |
| 5.957 | `w2_t5950` | 0.000 | 0.0000 | 1.0000 | 0 | 1 |
| 6.041 | `w2_t6020` | 0.001 | 0.0000 | 1.0000 | 0 | 20 |
| 6.114 | `w1_t6100` | 0.001 | 0.0000 | 1.0000 | 0 | 1 |
| 6.210 | `w2_t6200` | 0.001 | 0.0000 | 0.9999 | 0 | 2 |
| 6.261 | `w2_t6260` | 0.001 | 0.0000 | 0.9999 | 0 | 1 |
| 6.309 | `w1_t6300` | 0.001 | 0.0000 | 0.9999 | 0 | 1 |
| 6.809 | `w1_t6800` | 0.001 | 0.0000 | 0.9999 | 0 | 1 |
| 7.800 | `w1_t7800` | 0.001 | 0.0000 | 0.9999 | 0 | 1 |

- **t** equals the guest bit for bit in all 34 runs.
- **`mad`** lies at most at 0.112 in every picture, **`far`** at most at
  0.32 %: all below the threshold, `mad` by at least a factor of seven.
- **Logo and final picture** (from t = 5.87): in eight of the nine pictures
  no pixel deviates by more than two steps; at t = 6.041 the largest
  distance is 20, the 99th percentile 0.
- **Chamber** (t = 0.66 to 5.46): the 99th percentile lies at at most 8
  steps. The pixels with more than 16 steps lie at edges: counted for
  t = 4.107 (1440 of 1461) and t = 2.617 (1847 of 1951) they lie where the
  picture of xemu itself jumps by more than 16 steps to the neighbouring
  pixel, that is at outlines and at edges of shadows. What shifts an edge by
  one pixel (the depth of the light view with 16 bits, the matrices of the
  fixed function in `double`) is not measured separately.
- **`iou` below the threshold** has one picture: t = 2.001 with 0.9595 at
  `mad` 0.015. The picture is dark (mean green 3.3 of 255); the mask has 9588
  pixels in xemu and 9664 in the web port, and the pixels that lie in one
  only (398) have the mean value 22.9 against 24.0. They therefore lie one
  step around the limit 24 of the mask. That is the weakness of `iou` from
  section 7, point 4, not a deviation of the picture.

**Counter-checks** with `--off`, four pictures each (t = 0.127, 3.208,
5.109, 5.608); "–" means unchanged:

| without | `mad` at 0.127 | 3.208 | 5.109 | 5.608 | What one sees |
|---|---|---|---|---|---|
| nothing | 0.000 | 0.048 | 0.057 | 0.005 | |
| shadow comparison (`shadow`) | – | 0.121 | 1.180 | 0.096 | `far` at 5.109 from 0.06 % to 4.5 % |
| truncation to sixteenths (`snap`) | – (`iou` 0.958) | 0.071 | 0.272 | 0.069 | column 0 and row 0 without the veil; edges |
| veil (`veil`) | – | 2.277 | 57.025 | 44.662 | |
| glow (`glow`) | 0.007 (`iou` 0) | – | 41.979 | 19.507 | at 0.127 the glow is the whole picture |
| other front face (`flip`) | 0.007 (`iou` 0) | 6.492 | 114.590 | 172.039 | the picture stays black |

The comparison therefore notices each of these parts. The shadow weighs
least in pictures with little chamber; in the bright picture at t = 5.109
its absence lifts `mad` above the threshold.

**Measured anew with this section's renderer: the geometry from section 8.**
`compare_w1.py` now gives `iou` 1 in all twelve pictures with depth, no
pixel in only one mask, and the depth deviates in the 99th percentile by at
most 3 of 16 777 215 steps, nowhere by more than 256. The four pictures
without depth have `iou` from 0.99995 (two or three pixels). The deviations
in section 8 therefore came from the missing truncation to sixteenths of a
pixel and, in the four pictures at the end, from the missing textures.
`vectors/w1_frames.json` is not updated; it still holds the values from section 8.

**Two findings on the way**, both now in `GPU.md`:

- In the first comparison the shell above the blob was dark in the web port
  and bright in xemu (t = 4.407: `mad` 0.527, `far` 1.85 %). No body cast
  the shadow: even without any group in the light views it stayed dark.
  `lightspace.mjs` shows that its parts lie behind the plane of the light
  view they are assigned to; there oT3.w = 0. xemu then reads the border
  colour and compares with the nearest depth (`GPU.md` 4.4).
- Without the truncation to sixteenths of a pixel the veil did not cover
  column 0 and row 0 of the drawing surface (t = 5.309: 861 pixels with 208
  in xemu against 13), because its edge at 0.53125 lies behind the pixel
  centre 0.5 (`GPU.md` 3).

**What the comparison does not say.**

- It holds for 34 times, not for every frame; between two lie at most
  0.41 s, in the still final picture from t = 6.3 up to 1 s.
- The reference is the rasteriser of xemu at single resolution. Where xemu
  deviates from the console, the web port deviates with it.
- The light views, the target of the veil and the texture of the logo are
  checked only through the picture that arises from them; they are not read
  from xemu.
- The picture 640 × 480 is not measured against the display buffer of xemu,
  only its derivation from the stream (`GPU.md` 7).
- Measured on one GPU (`shots.json`, field `gpu`); another driver can round
  differently at edges.

## 10. The workbench

The page without a query is the workbench. Everything in this section is
display and own choice of the host; nothing of it has a model in the
binary. As long as none of it is switched on, the renderer draws the same
picture as in section 9 (measured in 10.4).

### 10.1 Controls

| What | How | From where |
|---|---|---|
| time | transport and time line as in section 3 | |
| events | marks under the time line: yellow the times from the narratives, blue the twelve pulses of this run; a click jumps there and opens the place | `bench.js`, `events`; the pulses from `xb_get_timing`, logo and chamber on and off by the host querying `xb_camera_at` in steps of 1 ms |
| free camera | dragging turns around the target, Shift or the right button moves it, the wheel goes closer; unticking "free camera" leads back to the path. The camera begins where that of the path stands just now | `xb_frame_from` (10.2) |
| camera path | the eye from t = 0 to 8 in steps of 10 ms as a yellow line, crosses at eye and target of the time; useful with the free camera | `xb_camera_at` |
| parts | per part a tick box to hide it, "solo" shows only this one, § opens the specification | draw calls on the drawing surface (D1 to D4), assigned by step, kind and mesh (`bench.js`, `keyOf`) |
| wireframe | the edges of the triangles of every drawn part in its colour, over the picture or alone, hidden by the depth of the picture; not for veil, glow, glow surface of the blob and the logo as a picture, which are only a quad | the same draw calls with the same vertex programs |
| show | instead of the picture a light view (U1), the target of the veil (U2, U3), the texture of the logo (U4) or one of the textures of the core; cubes as six faces, the light views as distance from the light, the brighter, the nearer, up to 120 units | the targets and textures of the renderer |
| shadow | off: as `&off=shadow` | section 9 |
| click into the picture | which draw call can be seen at this pixel; the field below shows what it is (for the chamber the part with group, shape, tracks and side), its rows from `decomp/README.md` and the sections of the narrative, of the program and of the combiners | 10.3 |
| sound | the 16 channels one by one out of the mix | `xb_audio_mute` in the AudioWorklet |

The page reads the narratives itself: `serve.mjs` passes through the
Markdown files from `docs/` under `/docs/` and nothing else. `bench.js`
looks for the section in them by its number and for the row of
`decomp/README.md` by its address.

### 10.2 The free camera in the core

Another camera needs the core, not only the host: the constants of the
chamber, of the blob and of the veil contain eye and view, not only a
matrix that the host could swap (`Chamber.md` 7, `Blob.md` 7, `Effects.md`
3, 4). Therefore the core offers the call

```
xb_frame_from(state, eye, target, up, out)
```

It describes the same frame as `xb_frame`, only with a view from eye, target
and up after `Camera.md` 3. What does not depend on the camera stays: t, the
switches `this+0xD` and `this+0xE`, the light views, the blob, the state.
The veil gets the orbit values of this eye (`Camera.md` 6), the angle
adjusted to that of the animation. The renderer still draws the texture of
the logo with the camera of the path, because it belongs to the run
(`Logo.md` 6).

The free camera of the workbench is not the orbit around the origin that
`anim_update` takes if `this+0xB` is 0 (`Camera.md` 6). The core does not
rebuild that (`core.md` 2), and the workbench does not offer it.

### 10.3 One pixel, one draw call

For a click the renderer draws the frame a second time: every draw call on
the drawing surface writes its number instead of its colour, without
blending, with the same depths. After that it reads the two pixels of the
drawing surface from which the pixel of the picture is averaged (`GPU.md`
7), takes the first with a number and draws the picture again as before.
Veil and glow lie over everything; they count only where nothing else is
drawn, and for that the renderer draws a third time with them. Calls that
cover much area and give little light count only where they are visible:
the veil by brightness, glow and glow surface by alpha times brightness, TM
and the logo as a picture by alpha, each from about 8 of 255 steps
(`bench.js`, `SEEN`).

### 10.4 Measurement

`node hosts/web/test_w3.mjs`, 47 checks, 0 failures:

- **The core.** `xb_frame_from` with eye, target and up of the camera of the
  path equals `xb_frame` byte for byte, in every seventh frame of a run at
  60 Hz. With another camera at t = 3.2 the same draw calls come in the same
  order, the matrices of the light views stay the same, the constants
  change. The core rejects a camera that looks along up.
- **The page with `&cam=path`** draws the same picture at t = 3.2 as
  without, byte for byte.
- **Every part alone.** For each of the 18 parts from `bench.js`, `PARTS`:
  drawn alone the picture has light, at a time without the part none, and a
  click on its first bright pixel chooses exactly this part. Times: chamber
  and blob 3.2, meshes A to D 5.9, TM, lettering and logo as a picture 6.15,
  veil 5.1, glow 0.1. The meshes A to C lie outside the picture at t = 5.3
  and 5.6 and come in only towards 5.9; only mesh D fills the screen there.
  In the whole picture a click on the blob at t = 3.2 chooses its mesh and
  not the veil above it, at t = 0.3, where nothing else is drawn, the glow.
- **Wireframe, path, intermediate targets.** The wireframe alone has light
  at t = 3.2 and changes the picture over the picture; the path can be seen
  from a camera far outside; each of the four views of the targets and the
  nine textures shows more than eight colours.
- **Every way to the specification.** 27 entries (18 parts, four targets,
  five further subsystems) with 71 rows from `decomp/README.md`, the seven
  programs, the sections of the combiners and 27 events: every section and
  every row is there. The logo comes at t = 5.225, chamber and blob go at
  5.937 (`Camera.md` 5.5: ≈ 5.225, ≈ 5.936). The server gives out `docs/`
  and rejects what lies beside it or is not Markdown.
- **By hand**, through the debug port of Chrome: "solo" for the mesh of the
  blob opens `Blob.md` 7.2; a click into the middle opens `Blob.md` 7.2 and
  `GPU.md` 3.4; the mark of the flash leads to tick 5000 and `Effects.md` 4;
  each of the 27 buttons § opens its section; dragging switches the free
  camera on.

**The picture stays that of section 9.** With this section's renderer `compare_w2.py`
gives the same values for all 34 recordings as `vectors/w2_frames.json`,
`compare_w1.py` the same as in section 9, and `test_w0.mjs` passes.

### 10.5 What the workbench cannot do

- The camera is free, the projection is not: opening, near and far plane are
  those of the original (`Camera.md` 2). Whoever moves in close cuts off at
  the near plane at 0.4 units.
- Hiding acts only on the drawing surface. The light views and the target of
  the veil still draw everything; whoever wants that takes `&off=`
  (section 5).
- Lines are one pixel wide on the drawing surface 1280 × 480; in the picture
  640 × 480 vertical lines are averaged and darker.
- The wireframe shows the triangles the renderer draws, hence also the fans
  and strips of the original as triangles.
- The sound leaves channels out; it does not show them.

## 11. The surface

The workbench from section 10 with a new layout and more tools. The
functions of section 10 have stayed, except for "values in the picture" (11.7); some
stand in another place. Everything in this section is display and own
choice of the host (`provenance.md`), with one exception in the core: the
choice of the camera path (11.1).

**Layout.** On the left the stage with the canvas, at the top left a status
line (time, frame, calls drawn, triangles of all passes, phase, path, size
of the picture, free camera), at the bottom left the hints for the controls
of the tab; both only as long as the pointer is over the picture. Below the
picture the playback bar (11.5), in every tab at the same place. On the
right a panel with the tabs Intro, Models, Blob, Sound and Next Gen (12);
below them in every tab the selection (as soon as something is selected),
Playground (Intro and Blob only), the values of the core, Export and the
specification. Below 1000 pixels of width the panel slides under the stage.

**State in the URL.** What is set stands in the hash of the address
(`#tab=blob&t=4100&bg=grey…`), only what deviates from the default. A link
restores it. `pause=1` stands in it as long as the intro is paused; a link
with it opens at t and stays there instead of playing (checked:
`#t=5550&pause=1` opens on frame 333 and still stands there after 0.8 s).
The hash is separate from the queries for pictures on demand (section 5),
which still begin with `?`.

**The phase of the status line** (`bench.js`, `phase`): "glow" before
t = 0.6 (`Blob.md` 1), "flash" from 5.0 to 5.25 (`Effects.md` 4), otherwise
by the switches of the camera (`Camera.md` 5.5): chamber only "blob and
chamber", both "logo arrives, chamber still there", logo only "logo", and
from phase 1 of the logo (`Logo.md` 6) "final picture".

### 11.1 Intro

| What | How |
|---|---|
| camera path | the four paths of the table (`Camera.md` 5.1); the one that `xb_init` draws is called "default". The choice is made with `xb_select_path` (`core.md` 2, `Camera.md` 5.2). The events that the narratives name for path 0 only (`Logo.md` 7) drop out for other paths |
| original shaders | off: every part in its colour from `bench.js`, `PARTS`, bright by the inclination towards the eye (from the derivatives of the position in the picture); veil, glow and glow surface drop out, TM and the logo as a picture stay as they are |
| coarse detail level | the chamber in level 1 in every frame instead of only from t = 5.2 (`Chamber.md` 6). A draw call of a shape is put onto the same place of the shape in level 1: two fans of `fan` indices and the strip behind them, or the whole shape (that is how the calls lie in the draw lists at t = 5.3, looked up for cylinder and frustum) |
| halo | glow and glow surface of the blob on or off |
| free camera, show camera path | as in 10.1; the path now with the target as a blue line, the keys (white those of the table, orange the eight own ones), B, eye (magenta) and target now, from `xb_get_campath` and `xb_camera_at` |
| mesh over the picture, parts, targets and textures, shadow | as in 10.1 |

**One pixel, one part, now also on hovering.** That holds only if the
switch "pick parts" in the bar is on (key A, `sel` in the URL, off by
default); otherwise a click only turns the camera. If the pointer moves over
the picture, the workbench chooses, as on a click (10.3), the draw call at
this place, at most one at a time. It gets an outline in the accent colour
and a label with its name (for the chamber with the number of the part). A
click selects it: white outline, specification as before, plus the field
"Selection" with "solo", "hide", "in Models" (for the chamber the whole
scene with the part highlighted) and §. "The same" is, for the chamber, the
same part, for the drops the same drop, otherwise the same kind of draw
call. The outline arises from a mask: the selected calls are drawn a second
time, with the depth test against the drawing surface, into a surface of its
own as large as that; where the mask changes between a pixel and its eight
neighbours, the line lies, in pixels of the picture of whatever size. It is
laid over the picture after the finishing of the frame and is never part of
the picture of section 9; a screenshot leaves the outline under the pointer out.

### 11.2 Models

A viewer of its own (`src/models.js`) shows every mesh of the core alone:

| Group | Models |
|---|---|
| whole scene | the chamber at time t: each of the 271 parts in its shape, with W from `xb_chamber_world` (`Chamber.md` 6, 7), without the parts with NaN; the logo with all seven meshes |
| single parts | per group all shapes side by side, and every shape alone with the number of the parts that use it (`xb_chamber_part`) |
| logo parts | the meshes A, B, C, D, Q1, Q2, E from `xb_logo_mesh`; A, B, C with gradient A, radial B, gradient C, Q1 and Q2 with TM (`Logo.md` 2.4, 5) |
| spheres | the large and the small mesh from `xb_blob_mesh`; the deformed blob at time t as centre + (k + o.w) · p with the normal normalize(o.xyz) (`Blob.md` 6.4, 7) |

Shading "original" takes the normal n from the data of the chamber
(`Chamber.md` 4.2) and the textures of the logo, "flat" the normal of every
triangle. The triangles run clockwise around n, counted on all shapes of
level 0 (sphere 672 of 712, lathe 2563 of 2678, the other groups all); like
the renderer (`FRONT`) the viewer takes that as the front face. The logo has
no normals; there "original" is per triangle too. Light at the camera or at
the origin, brightness, highlighting one part (part of the scene, shape,
mesh); for checking coarse (level 1), edges of the triangles, leaving back
faces out, turning by itself, normals, the axes a and b of the chamber as
tangents, the axes of the world and the box around the mesh. Dragging turns,
the wheel zooms, Shift or the right button moves, a double click resets.
Numbers and a note on the model with the way to the specification stand
below. Light, camera and gloss are those of the viewer, not those of the
original.

During playback the scene and the deformed blob follow the clock in every
frame. `buildScene` writes the scene without a helper function per vertex
into an array that stays from frame to frame, into the same buffer of the
GPU. The values are bit-identical with the general way through `Builder`. In
Chrome on this machine a rebuild of the scene needs 11 ms like this instead
of 170 ms, in level 1 5 ms instead of 50 ms (measured at t = 0.5, 1.2, 3.2
and 5.5 with a script in the work folder, not in the repo).

### 11.3 Blob

The blob alone in front of black, grey, light grey or in the scene, with or
without the original shaders, edges, halo and drops. For diagnosis:

- **Markers at the surface**: every bulge as a cross at centre + (k + R · h)
  in its direction c, where its cap is highest (`Blob.md` 6.4), in the colour
  of its number; every drop as a white cross at its position with a line
  from the surface (2.2).
- **View through the surface**: the mesh of the blob half transparent,
  markers and trails over everything.
- **Trails of the drops**: the position of every drop over the last 400
  steps of the run; if a drop jumps by more than 3 units, a new trail
  begins.
- **Live** the values from `xb_get_blob` and k from `Blob.md` 7; in detail
  all 32 bulges and 8 drops.

### 11.4 Sound

At the top the picture, below it the track view of the twelve channels of
event 1 (`Audio.md` 3.5). `src/tracks.js` lets a core of its own compute the
sound from the start in blocks (`xb_audio_block`) and reads per block the
calls into the voice layer, the gain of every channel and the RMS value of
its voice. From that:

| What | From where |
|---|---|
| notes | call "note" or "pitch" up to the next, or until the gain becomes 0 after the release; height as semitone 60 + lib / 341 (`Audio.md` 5.3); a stroke at the front where the voice was keyed on (`effect`), pale where only the pitch changes |
| sample | the last call "sample" of the channel, named as in `Audio.md` 3.5 |
| volume | the largest gain per beat (9 blocks, 6 ms) |
| level bars | gain times RMS value of the voice, as the square root of the share of the largest value |
| events | sample, release, filter, level as marks at the bottom of the row |

A click or dragging jumps in time, hovering shows note, sample, times and
calls. M mutes a channel, S solos it; both go as a mask to `xb_audio_mute`.
Plus volume, a scope (waveform or spectrum, from an `AnalyserNode` behind
the output), the layers of the view one by one and a legend. The page
computes eight seconds of sound in about 0.6 s, when the tab is first
opened.

### 11.5 Playground, playback, export, keys

- **Playground** (Intro and Blob), reinterpreted and listed so in
  `provenance.md`: scene colour from a colour picker or a preset as a
  rotation of the finished picture about the grey axis by the distance of
  the hue from the green of the blob; white room (the scene drawn
  differently: white ground, the chamber white and without relief in the
  green light of the original, that is position, attenuation, Kd, Ks and
  shadow as in 3.1 of `GPU.md`, stronger, plus a white light from the eye;
  from 45 units from the eye it passes into white up to 140, so that its end
  cannot be seen; veil and glow tint the white instead of adding light; blob
  and drops glow in full colour; the logo, its meshes A to D with the dark
  ground they bring along, and its picture, with inverted brightness and the
  same colour on the white); the accent colour of the panel follows the scene
  colour. Reset restores the original.
- **Playback**, in the bar below the picture: time line with the events,
  time and frame, to the start, play and pause, one frame forward and back;
  speed from 0.1 to 2. The clock of the run then runs faster or slower by
  this factor, the sound like a tape: the AudioWorklet reads the output of
  the core with the factor as its step and averages between two frames,
  faster therefore sounds higher. Loop (on by default) begins again at the
  end; original resolution see 11.7. Sound on and off; a hint if tracks are
  muted or solo. Picture and sound run on two clocks as in section 6. In
  Intro and Blob the switch "pick parts" (11.1) stands there too.
- **Export**: the picture in its size (11.7) as PNG, as the page reads it
  back; a link with the current time; full screen with only the canvas, also
  when F11 lets the window fill the whole screen (recognised by
  `display-mode: fullscreen` or by the window being as large as the screen).
- **Keys**: space play and pause, ← and → one frame, with Shift one second,
  Home to the start, F full screen, A pick parts on and off (Intro and
  Blob).

### 11.6 Measurement

- **The picture stays that of section 9.** With this section's renderer `compare_w2.py`
  gives the same values for all 34 recordings as `vectors/w2_frames.json`
  (compared line by line, output in `out/web/w3b/`). The switches of this section take
  effect only if they are switched on.
- `node hosts/web/test_w3.mjs`: 50 checks, 0 failures; new are the three on
  the choice of path: path 0 equals the run without `xb_select_path` (68 of
  68 frames, NaN as NaN, `core.md` 2), path 2 keeps the pulses and changes
  the constants, path 4 is rejected. The test now switches "values in the
  picture" on itself, because it is no longer on by default.
- `node hosts/web/test_w0.mjs`: 19 of 19; the button is now called
  "▶ Play", the test looks for the word in it.
- `python core/build.py`: all tests green, the digest of the draw lists
  stays `F6EFF3BBA045238C`. `check_layout.py`: 174 values from 17 structures
  equal to `xboot.h`.
- By hand, with screenshots from Chrome without a window: every tab, outline
  and selection, flat shading with the coarse level, path 2 from outside,
  playground, speed 2 (1 s of play time gave 2.0 s), Shift+→, Home, the
  run-on to 10 s and the narrow window.

**What is not checked.** The sound at another speed is checked only in the
code, not listened to; full screen and F11 only as far as Chrome without a
window allows.

### 11.7 Picture size, wide screen, start

**The size of the picture.** The workbench draws the picture in the pixel
size the canvas has on the screen (CSS size times `devicePixelRatio`) and
adapts it as soon as window, tab or panel change. The drawing surface is
twice as wide as the picture and as high, so D5 stays the mean of two pixels
(`GPU.md` 7); at 640 × 480 it is the 1280 × 480 of the original. Light
views, target of the veil and texture of the logo keep their size (section
4). Position at 0.53125 pixels and truncation to sixteenths still hold per
pixel of the larger picture.

The drawing surface needs twice the width of the picture. Without a request
WebGPU gives at most 8192 pixels per side; `gpu.js` therefore requests the
limit of the adapter (16384 on the machine of these measurements). If that
is not enough either, the picture is reduced in its shape
(`Renderer.largest`) and scaled up by the browser. Before that the drawing
surface was then cut off, and at 8000 × 1200 the whole scene lay in the left
half of the picture.

**The shape of the picture.** The stage fills the window; the picture has
its shape. The projection of the core is built for 4:3. The host therefore
squeezes the position on the screen before the rasteriser: with
a = (W / H) / (4 / 3) in x by kx = min(1, 1 / a), in y by ky = min(1, a).
The 640 × 480 of the original thus lie entirely in the middle; a wider
window sees more at the sides, a higher one at the top and bottom. That
holds for the drawing surface and the target of the veil (U2, U3), not for
light views and logo (U1, U4).

Three quads already lie in picture coordinates (`Effects.md` 3 to 5):

| Quad | In wide screen |
|---|---|
| disc (U2, mesh 17) | stretched by 1/kx, 1/ky, fills the target; t0 computes from the stretched position, the disc stays round |
| veil (D4, mesh 16) | stretched by 1/kx, 1/ky, likewise its noise layer, which depends linearly on the position (p = 1.875 y, q = −2.5 x in the mesh); u, v read the target of the veil, which is wide itself |
| glow (D4, kind 15) | enlarged by 1/min(kx, ky) equally in x and y: it keeps its shape and covers the window as it covers the 4:3 picture. Before that it ended, in a window higher than 4:3, with a hard edge at the 4:3 strip (seen at 1000 × 1000, frame 333) |
| logo as a picture (D3, kind 12, also mesh 16) | not stretched; on mesh 16 only the veil (kind 14) is stretched |

Towards the end veil and glow compare the depth with "equal" (0x202 in the
draw lists at t = 5.3 to 5.9; at 4.6 to 5.0 still 0x207, "always"; the
change in between is not narrowed down). They then fall only where nothing
has been drawn since the clearing before the logo (depth 1). Before the
first such call `EDGE` (`shaders.js`) checks whether a pixel on the edge of
the 4:3 cut-out still has depth 1. If none has, veil and glow do not hit the
edge in 4:3, and `EXTEND` writes, beside the cut-out where the depth is 1, a
value just below it: they do not fall there either. Otherwise they fall
beside the cut-out as within it. The reason: from frame 346 `logo_b` covers
the 4:3 picture entirely; beside it, where `logo_b` ends, the flash K1 of the
veil lit the empty ground green (seen at 2560 × 800, t = 5.8, frame 348).
The decision holds for the whole picture; a rule per pixel (the depth of the
nearest edge pixel) left black wedges where an arm of the logo runs out of
the cut-out at a slant (seen at 1286 × 1171, frame 337).

That is an own choice without a model in the BIOS: the original shows only
4:3, what lies beside it is invented. *Guessed* is above all that nothing is
missing there that the original would have covered.

**Original resolution** (bar below the picture, `nat` in the URL): the
picture 640 × 480 in 4:3, scaled up by the browser, as before.

**Start.** The page plays the intro by itself on opening, except with
`pause=1` in the address; loop is on by default. Sound comes only after the
first click or key press, because browsers allow none before; if the intro
is running then, the sound starts at the place of the run.

**Taken out**, because they were only display and answer no question to the
BIOS: logo overlay at the end, 4:3 frame, values in the picture, in the
playground "cycle automatically" and the blob material. The coarse detail
level stays: level 1 is part of the BIOS (`Chamber.md` 6, chosen by the
original itself from t = 5.2).

**Measurement.**

- Pictures on demand stay 640 × 480 (with kx = ky = 1, hence unchanged);
  `test_w0.mjs` 19 of 19, `test_w3.mjs` 50 of 50. Both tests now pause
  first, `test_w0.mjs` also switches loop off; `test_w3.mjs` reads the free
  camera from the status line instead of from the values in the picture.
- By hand with `&size=2560,800`: t = 5.0 (chamber and veil over the whole
  width), t = 5.8 and 5.9 (logo on black, without a green edge).
- Glow and veil beside the cut-out, with `&size=`: at 1000 × 1000 frame 333
  without an edge, at 1286 × 1171 frame 337 without black wedges, frame 360
  at both sizes with an undistorted logo. At 2560 × 800 in the frames 346 to
  360 outside the 4:3 cut-out no pixel brighter than black (every pixel
  checked), in frame 345 green as above. `test_w0.mjs` 19 of 19,
  `test_w3.mjs` 50 of 50.
- At 8000 × 1200, run at 60 Hz: outside the 4:3 cut-out in the frames 347 to
  360 no pixel brighter than black (every second pixel checked). In frame
  345 there is green there: the logo itself, which comes through the picture
  still larger than the cut-out (`logo_b`, with and without the veil). The
  page itself with a canvas of 8000 × 1200: no error in
  `onuncapturederror`, `console.error` or `console.warn`.
- `compare_w2.py` over all 34 recordings: the output equals
  `vectors/w2_frames.json` as a whole (output outside the repo).

## 12. Next Gen

A tab of its own "Next Gen" (key N switches between it and Intro,
`tab=nextgen` in the URL; older links with `ng=1` open it) draws the intro
the way one would render it 25 years later. Everything in this section is
reinterpretation (`provenance.md`): scene, light and time come from the
core, the look is an own choice. It takes camera path, halo, hidden parts
and the free camera over from the tab Intro; it does not show the
playground, and its scene colour and white room do not act on its picture.
The other tabs draw the picture of section 9 as before.

### 12.1 What comes from the core

| What | From where |
|---|---|
| the 271 parts of the chamber in level 0, with the axes a, b, n and the position after W at time t | `xb_chamber_mesh`, `xb_chamber_world` (`Chamber.md` 4, 7); parts with NaN in W are missing as in the original |
| the light | at the centre of the blob, colour G = (0.2079, 1, 0.1), strength m = 2 · x20, attenuation 1 / (1 + a1 d + a2 d²) with a1 = a2 = 0.001 / m (`Chamber.md` 7, "The light object") |
| the blob | the deformed mesh as centre + (k + o.w) · p with the normal o.xyz, k and the colour c0 = colour · min(4t, 1) · br (`Blob.md` 6.4, 7, 7.2) |
| the drops | position, direction, σ and z from `Blob.md` 7.3 |
| the camera | view and projection of the frame, the free camera too (`xb_frame`, `xb_frame_from`) |
| what a frame shows | the parts that the frame draws on the screen (chamber, blob, drops; `renderer.js`, `seen`) |
| logo, veil, glow | drawn by `renderer.js` as in section 9, on a drawing surface up to twice as large in every direction (at most about 14 million pixels), without truncation to sixteenths of a pixel |

### 12.2 What is new

| Pass | What |
|---|---|
| shadow | a cube of six depth maps from the light (2048² at "high"), soft edge by the size of the blob: width about k / d, twelve samples on a disc rotated per pixel |
| main picture | HDR (16-bit floating point) with 4 × MSAA; metal after GGX with the light as a sphere, the relief along a, b with all mip levels and Toksvig, so that highlights do not flicker; brightness and roughness of its own per part |
| blob | the mesh as curved PN triangles, each divided into n² triangles in the vertex shader (n = 3, 4, 6 by quality), with a plasma from noise; the drops as finely divided spheres |
| ray tracing | reflections: per pixel at half size one ray, mirrored at the normal, through a BVH in two levels: per shape a BVH by the surface area heuristic, built once (7 906 triangles, 124 ms), above it per frame one over the 271 parts. Chamber, blob and drops are hit; shading there is with light and shadow. After that a blur that respects depth jumps |
| god rays | scattering in the fog along the view ray, 24 to 64 steps, the light at every step seen through the shadow map, forward scattering (Henyey-Greenstein, g = 0.72), a slowly drifting fog from noise; denser at the flash (t = 4.9 to 5.3) |
| particles | without state, each a function of t and its number, hence the same at every place of a run: sparks around the blob, dust in the light of the chamber, a burst at the flash. The shell of the burst grows ever faster with k + v · (1.2 a + 16 a³) (a the time since t = 5.0, v = 18 to 68) while the camera moves out of the chamber from t = 5.3 (distance to the blob 65 at 5.3, 177 at 5.6, 490 at 6.0; measured with `core.camera`); so it overtakes the camera, and from 5.82 it fades out until the chamber ends at 5.936 (`Camera.md` 5.5). Nearer than 0.5 in front of the eye a particle drops out, up to 4 it fades in, and its radius on the screen is limited to 3 % of the picture height; before that a particle at the eye could become infinitely large, and bloom made a white disc with a black core of it. Where the logo lies in front they are missing. They go into a target of their own after the depth of field: each grows by its own circle of confusion into a soft disc, its light spread over the larger area. Before that the blur gathered them with the depth of the wall behind and copied every bright point into the pattern of its spiral (small clusters in full screen) |
| composition | logo, veil and glow of the original: where the logo left depth, it lies in front (`anim_draw` clears the depth before the logo, `FrameOrder.md` 3); veil and glow only where the scene shows nothing, like their depth comparison "equal" in the original. Their picture is already tone-mapped and is computed back into light with the inverse of ACES |
| depth of field | sharp at the distance of the blob, the circle of confusion grows with the distance from it; 48 samples on a spiral, rotated per pixel, the background does not bleed over a sharp foreground |
| motion, TAA | a third target in the main picture with the velocity of every pixel: parts with their W from the frame before, blob with its control points before, drops with their position before. The projection jumps per frame by a fraction of a pixel (Halton 2, 3 over 16 frames); TAA meets the history where the point stood before, reads it with Catmull-Rom and clips it to the colours around the pixel (variance in YCoCg). Where logo, veil or glow of the original lie, the new picture weighs more, because they have no velocity. If the picture stands still, the workbench draws it anew up to 40 times and averages up to supersampling; pictures on demand gather likewise (`&ngacc=`, default 32) |
| indirect light | 1, 2 or 4 rays per pixel (fast, high, ultra) at half size, by the cosine around the normal, stratified in the first random number, through the same BVH; what they hit is shaded with light and shadow, the blob does not count (it is the light). Divided by the strength m of the light and averaged over the frames (`GI_ACC`): per frame 8 % new into the history, which is read where the point stood in the frame before; if it lies at another depth (more than 5 % + 0.3), only the new frame counts. After that blurred, in COMPOSE again times m and times the brightness of the wall. Before that only TAA averaged, which takes 12 % of every frame in motion and clips the history to the noisy surroundings: measured on two pictures of the same place with other random numbers (t = 4.5, without TAA, particles, bloom) they deviated by 1.66 steps on average, without indirect light by 0.67; the noise therefore came for the most part from it. The estimated brightening from `shade` steps back to 15 % for it |
| blob in ray tracing | the mesh of the blob is a cube of six faces of 17 × 17 vertices (`Blob.md` 2; measured: every vertex lies on the grid of its face). Per frame a table of normal and distance from the centre; the ray samples it in 40 steps through the enclosing sphere and then halves six times |
| limits | the targets are 16-bit floating point: above 65504 a value becomes infinite, and the mapping in TAA (c / (1 + max c)) turns that into NaN in the whole 3 × 3 neighbourhood that the history drags along; it could be seen as a white disc (bloom) with a black core and colour fringe (chromatic aberration). Therefore: the gloss after GGX before Fresnel at most 200, every target on writing to 0 to 3 · 10⁴ and NaN to 0 (`safe`), in COMPOSE no pixel brighter than 48 at the same hue, likewise the particles; the relief only where the axes a, b span a plane to the normal |
| finish | bloom over six halved levels, without the lettering: the original draws it in the one colour 0xFF62CA13 (`Logo.md` 5.3), on the screen and from t = 6.25 in the picture of the logo; COMPOSE recognises it by this hue (r/g and b/g to within 0.06) and does not pass it on to bloom, the X still glows. Then ACES, a slight tint, chromatic aberration at the edge, vignette, grain |

The tab has the quality (fast: 0.75 times the resolution; high: 1 times;
ultra: 1.5 times, that is supersampling) and switches for ray tracing, god
rays, particles, bloom, depth of field, indirect light and TAA (`ngq`,
`ngfx` in the URL). Halo off leaves the glow out. The status line names the
quality and the number of rays.

In the tab Next Gen the picture picks no parts (the outline belongs to the
picture of the original), and wireframe, views of the targets, flat shading
and white room do not act.

On demand: `?shot=…&ng=1[&ngq=ultra][&ngfx=rgpbd]`, with `shot.mjs` as
`--ng`, `--ngq`, `--ngfx`, plus `--size`. `&ngdbg=1` shows on the surfaces
how much light the shadow map lets through.

### 12.3 Measurement

- Pictures on demand at t = 0.3 to 7.5 in 960 × 540 and 1920 × 1080, every
  quality: without errors from WebGPU, looked at.
- In the workbench (Chrome without a window through the debug port, picture
  1400 × 1100, Turing GPU of this machine), frames per second during
  playback around t = 1.8: original 75, fast 82, high 83, ultra 66. Without
  a window the frame rate is not tied to the screen; the original itself
  does not lie at 60 there.
- `test_w3.mjs`: 50 of 50. `test_w0.mjs`: 18 of 19. The check "played for
  0.6 s" fails (2306 instead of above 2300): the run advances only a little
  over 300 ms in 0.6 s. It does that with Next Gen taken out too, on the
  state before this change; the cause has not been looked for.
- TAA in motion (twelve frames at 60 Hz up to t = 3.6, each drawn once)
  against a picture from 64 gathered at standstill, 1280 × 720: mean
  deviation 1.78 (without TAA 1.48), at edges 3.31 (3.56), pixels above 16
  steps 0.37 % (0.40 %). No ghosting measurable; somewhat softer in motion.
- With TAA, indirect light and the blob in ray tracing, frame rate as above:
  fast 66, high 70, ultra 36.

**What is not checked.** Other GPUs and browsers; the frame rate in a window
with a screen; the gathering at standstill in the workbench only in the
code, measured on demand. `test_w0.mjs` and `test_w3.mjs` have not run
again after this round; the picture of the original is not touched, only
the switches of the workbench have been added.
