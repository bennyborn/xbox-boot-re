# Provenance

Every statement of this project has exactly one of these origins. The tables
grow with the milestones.

The basis is `bios_complex_4627_retail.bin`, the 1 MB "Complex 4627 Retail (1.0)"
image — a modified BIOS, not a stock dump — flashed on the author's own
(modded) console and dumped from it, decrypted with the MCPX key of the
same console. BIOS and key stay local.

The data in `data/` is, in content, Microsoft's geometry, samples and
tables. An extractor of our own takes it from the ROM. That it lies in the
repo is a deliberate decision; whoever publishes this repo publishes it
along with it. It stays Microsoft's property throughout and is not covered
by this repository's license (`LICENSE`), which applies only to the
project's own work.

## From the ROM, statically

| What | Address | Evidence |
|---|---|---|
| signature 0x7854794A and jump into the 2BL | MCPX 0xFFFFFF81–0xFFFFFF92 | `Rom.md` section 2 |
| preamble of the 2BL: self-copy to 0x400000, page directory, call of 0x40061D | 2BL 0x9024C–0x903B1 | `Rom.md` section 3 |
| sequence of the main part of the 2BL | 2BL 0x40061D | `Rom.md` section 4 |
| kernel key at 2BL 0x8C | 2BL 0x40061D (argument 0x40008C) | `Rom.md` section 4 |
| position, lengths and SHA-1 check of kernel, data block and ROM head | 2BL 0x4007D0, fields 0x5FDC–0x5FFF | `Rom.md` section 5; recomputed, `digest_ok: true` |
| RC4 in the 2BL | 2BL 0x40079F, 0x402B51, 0x402A04 | `Rom.md` section 5 |
| chunk framing, window 0x20000, chunk 0x8000 | 2BL 0x4006A5 | `Rom.md` section 6 |
| E8 reverse translation only below 0x8000 chunks | 2BL 0x4022C4 | `Rom.md` section 6 |
| destination 0x80010000; fix-up at 0x80010030 without effect | 2BL 0x4008F7, 0x400904 | `Rom.md` section 5 |
| image base, entry point, sections of the kernel | PE head at 0x80010000 | `Overview.md` |
| two LZX deviations from [MS-PATCH] (no Extra Length, aligned from 3 footer bits) | packed kernel, bank 0x57A2 | `Rom.md` section 6; evidenced by complete unpacking with the same hash |
| library marks `XGRAPHC`, `DSOUND` at the start of `INIT`; bounds of the animation range 0x80055E75–0x800626DF; entry points through pointers only | 0x8003CB20, pointer tables 0x800669D8, 0x80066A70, 0x8006E910 | `Overview.md`, "Map" |
| call graph of the top level, sub-objects of the animation object 0x8006F050 | 0x800579F4, 0x8005768C, 0x800571AD, 0x80056D0B | `Overview.md`, "Map" |
| timer divider 1125, tick at 0x8003A89C, formulas for dt, t, ramp, pulses, generator | 0x80063CB8, 0x80030E4C, 0x80057949, 0x800571AD, 0x80056EF6, 0x8005A645 | `Timing.md` |
| frame step: d = absolute value of the signed 32-bit difference, `fild`; `arg` from the unrounded product; sin and cos of the pulses stored as float32 | 0x8005795C–0x8005796C, 0x80057992–0x8005799E, 0x8005706F–0x80057079 | `Timing.md` 2, 4.2 |
| start mode from bit 4 at 0x8003B138 (`STICKY`) | 0x8001582F–0x8001583D, 0x80055FBA | `Timing.md` section 3 |
| name, subsystem and description of each of the 222 functions in the range | the addresses in `README.md` | `README.md`; names set and completeness checked with `tools/ghidra/apply_names.py` |
| structure of the range by subsystem; sound (sequencer) and texture generators separated | 0x80060252, 0x800669D8, 0x8006036C–0x800614E9 | `Overview.md`, "Structure of the range", "Sound and textures" |
| camera: view and projection matrix, parameters π/4, 0.75 or 0.5625, 0.4, 800 | 0x8005B2C0, 0x8005B318, 0x8005768C, 0x80056ACA | `Overview.md`, evidence for the sub-objects |
| camera path: four paths in the table, random choice, Hermite curve; first choice path 0 | 0x80070DB8, 0x8005AFB6, 0x8005A6CB, 0x8005AC32 | `Overview.md`; choice recomputed from the initial value 0x76543210 |
| after-image after the end of the animation | 0x800565F3, 0x80056657, 0x800561D1 | `Overview.md`, "After-image" |
| sound: start, beat with period 5, tear-down | 0x8006166D, 0x8006165E, 0x800616A7, 0x8001951C | `Audio.md` 1 |
| sound: beat of 6 instead of 5 ms from the buckets of the timers (bucket by due time, check by the old tick) | 0x8001C4CF, 0x8001C3CF, 0x8001CCAC, 0x8001C10D, 0x80030ED8–0x80030EFD | `Audio.md` 1; counter = 10 000 · tick in the memory dump `c2final_A` |
| sound: level deduction 600 from the flags of the format block | 0x80051059, 0x8005125A, 0x80061C3B | `Audio.md` 5.4 |
| sound: formulas of the tables (noise, sine, FM, sawtooth), 8-bit conversion, copy read backwards from 0x80083318 | 0x80061AC1, constants 0x800129D8–0x80012A08 | `Audio.md` 2 |
| sound: data and sequence of the sequencer, one command per due track and beat, abort at command 15, command formats | 0x80060252, 0x800669D8, 0x80066A70, 0x8006E908–0x8006EFFF, commands 0x80061D75–0x800625B7 | `Audio.md` 3; data in `data/audio/` (`tools/extract/audio.py`) |
| sound: wiring of the reverb from the frame routine and the DMA chains; tap delays from the offsets | GP P:0x0242–0x0395, P:0x03A2, X:0x5D–0x96, X:0x1DE–0x1E3, chains X:0xA3 and X:0x105 | `Audio.md` 6.2 |
| sound: GP program at 0x80053568 (0x625 words), loader, sequence of the frame routine, ring buffers from the DMA chains | 0x8004F8A1, 0x8004F852, GP P:0x0014, P:0x0242–0x0366, X:0xA3, X:0x105 | `Audio.md` 6; program in `data/audio/gp_program.bin` |
| sound: creating voices, mix shares, sample descriptions, envelope blocks, conversion of pitch, level and filter, start only for an idle voice | 0x80061AC1, 0x800618DF–0x80061D48, library 0x8004EB1D, 0x8004EE84, 0x8004EF2D, 0x8004EFC6, 0x80050819, 0x800508F9, 0x80051164, 0x80051F2E | `Audio.md` 5 |
| chamber: position of the data in the ROM, unpacking of quaternions, positions and index lists, six groups in two detail levels, generators of the six basic shapes, placement, tracks, world matrix, pre-pass with weighted centre (reference point on the stack), cube texture at time 5.35 | 0x8005FC65, 0x8005F7B8, 0x8005F3DB, 0x8005C9DC–0x8005E8B4, 0x8005F9F1, 0x8005EE08, 0x8005FDF9, 0x80058D1F, 0x800614E9; data 0x800743D0–0x80078503 | `Chamber.md`; data in `data/chamber/` (`tools/extract/chamber.py`) |
| camera: projection, view, path time s(t), path table, eight own keys, tangents, evaluation with blend, switches +0xD/+0xE, orbit values, free camera | 0x8005B18C, 0x8005B093, 0x800571AD, 0x8005AFB6, 0x8005A6CB, 0x8005AC32; table 0x80070DB8 | `Camera.md`; data in `data/camera/` |
| blob: object and fields, meshes of six cube faces, generator 0x800708BC, fade(t), creation and update of bulges and drops, deformation, glow surface, constants and colours on drawing, second output of the weighted centre and that no caller reads it, unreachability of the branches for t < −1 | 0x80059270, 0x800598E6, 0x80058DF2, 0x80059868, 0x80059320, 0x80059789, 0x800590A6, 0x800595B6, 0x80058FFC, 0x80058B93, 0x80058290, 0x8005820E, 0x800583BF, 0x80058D1F, 0x8005FE2F, 0x8005F15F–0x8005F16F; constants 0x80012880–0x80012938, 0x80066574–0x80066590, 0x800708C0, 0x800708C4 | `Blob.md` |
| order of summation of the matrix product, rounding places in 0x8005AC32 and 0x80058D1F | disassembly 0x800566CF, 0x8005AD72–0x8005AE1F, 0x80058D1F | `Chamber.md` 11, `MUL_ORDER` in `tools/xemu/chamber_model.py` |
| logo: position of the seven meshes, keys and texture at 0x80071008–0x800743CF; unpacking of vertices and indices; buffers with the replacement of v in mesh C; object and fields; update (position of the lettering, opacity); drawing in space, as a picture and with TM and lettering; constants 0–8 of mesh D; drawing into the texture at t ≥ 6 and t ≥ 6.25; world matrix = frame of the camera path | 0x8005B398–0x8005C4A5, 0x80056D0B (0x80056DAD–0x80056DDA), 0x800571AD (0x800575FB–0x80057617); constants 0x80012980–0x8001299C, 0x80066964–0x80066978 | `Logo.md`; data in `data/logo/` (`tools/extract/logo.py`) |
| graphics: device and ring of the push buffer (0x100000, pieces of 0x10000), head words, table of the states, the 76 library functions that the animation calls, seven vertex programs and seven combiner blocks, fixed function, finishing of the frame | 0x8003CB40–0x80044F40, 0x80057A5A, table 0x80065470, data 0x800656B0–0x80066868 | `GPU.md`; data in `data/gpu/` (`tools/extract/gpu.py`) |
| textures: resource and format word, layout of levels and faces, reordering on copying, the generators | 0x800406B0, 0x80043270, 0x800AAB46, 0x8006036C–0x800614E9 | `Textures.md` |
| veil and glow: object, target texture, veil, glow | 0x80059B17–0x8005A618 | `Effects.md` |
| order of the draw calls in set-up and frame | 0x8005768C, 0x80056D0B | `FrameOrder.md` |
| arithmetic helpers: conventions (row vector, store places in float32), computation and order of summation of every helper | 0x80055F8C, 0x8005667A–0x800582C0, 0x80059A2E, 0x8005A662, 0x8005C8BB–0x8005C940 | `Overview.md`, "Small helpers in the range" |
| after-image without data: 0x800561D1 returns at once; the animation reaches 29 library functions only through the after-image | 0x800561DF–0x800561E5; call graph (`tools/ghidra/export_graph.py`) | `Overview.md`, "After-image"; `GPU.md` 2.3 |
| order of summation of 0x8005667A, against the model | 0x80056682–0x800566C9 | `Overview.md`, "Small helpers"; `v_mul_m3` in `tools/xemu/chamber_model.py` |
| camera path: addresses of the constants of the evaluation and of the own keys, switches with `setg`/`setl`, unreachable branch for i = 0 | 0x8005A769–0x8005A77D, 0x8005AC38, 0x8005AC79–0x8005AC7B, 0x8005ACAD–0x8005ACE0, 0x8005ADB5, 0x8005ADF4, 0x8005AF20–0x8005AF44, 0x8005AF87–0x8005AFAD | `Camera.md` 5.3, 5.5 |
| factors of the series of the arc sine as a file; the 1.0 in the sine | 0x80066030–0x800660F3, 0x80065E80 | `Chamber.md` 11, `Overview.md`; data in `data/overview/` (`tools/extract/overview.py`) |
| blob: fade(t) as the reciprocal times (t − 0.6) with the 1 from 0x8001288C; addresses of the immediates (radius, blue, initial value, stretch 1.2) and of the constants in `spawn` and `step` | 0x80059150–0x8005917F, 0x80059393–0x800593C2, 0x800592A2, 0x800592F5, 0x8005931B, 0x800594FB, 0x8005935D–0x80059598, 0x800595E9–0x80059721 | `Blob.md` 2, 4, 5.2, 6.3; comment in `tools/xemu/blob_model.py` |
| camera path: row 3 of the frame F = m · −ρ through `vec3_scale`, that is d · ρ; bit-identical against `frame_matrix` from `c7_main` and `c7_b` | 0x8005A9CD–0x8005AA0F | `Camera.md` 5.3; `F3` in `tools/xemu/chamber_model.py` |
| logo: comparisons in `logo_update` and `logo_draw` with their behaviour for NaN, read places of the keys, condition of the drawing in space on t from 0x8006F060, addresses of the six immediates of d and e and of the colour of E, storing of sin and cos in `mat4_rot_x` | 0x8005C4C3–0x8005C623, 0x8005BC86–0x8005BCA6, 0x8005BFAE–0x8005C0A8, 0x8005C104–0x8005C134, 0x8005C2C4, 0x800570C1–0x80057102 | `Logo.md` 4, 5.1, 5.3; `clamp01`, `update`, `mesh_d_constants` in `tools/xemu/logo_model.py` |
| textures: origin of every argument of the CPU generators; initial value 0x3F00 of the noise, read as a byte only; second caller of `fx_noise_textures` on a restart; addresses of the ones, of the −1 in the cube, of the alpha 255, of the 256 of the radial table and of the immediates of falloff and subdivision; signed and unsigned comparisons in falloff, subdivision and noise | 0x800576CD–0x80057729, 0x8005A16C–0x8005A18B, 0x8005B771–0x8005B8CD, 0x80060E76–0x80060E80, 0x800572F4, 0x80060384, 0x80060463, 0x8006052E–0x80060536, 0x8006066A–0x80060737, 0x80060A82, 0x80060AC8–0x80060B33, 0x80060F48–0x80060F5A, 0x8006119E–0x800611E3 | `Textures.md` 1.1, 4.1–4.5, 4.7, 4.8; comparison in `falloff_mips` in `tools/xemu/texture_model.py` |
| sound: management of the track records, ring, channel records; thunks into the library; scratch memory of the GP | 0x80060166, 0x80060232, 0x800616B8–0x80061732, 0x800625E6, 0x8006261D, 0x80050FA4–0x80052995, 0x8004F763, 0x8004F7A5 | `Audio.md` 3.2, 5.6, 6.1 |
| orbit values of the camera: computation of + 0x690, + 0x698, + 0x6A0, + 0x6A8 with rounding places, branches of `angle_from_sin_cos`, adjustment by whole turns, initial values from the swap in the constructor | 0x80057435–0x80057587, 0x80056969–0x80056A5D, 0x8005691F, 0x80056B71–0x80056BDC | `Camera.md` 6 |
| projection: far / (far − near) stays unrounded in the register for P[14] | 0x8005B1CC–0x8005B220 | `Camera.md` 2; `perspective` in `tools/xemu/chamber_model.py` |
| track time of the chamber: k from the unrounded quotient, u only stored | 0x8005FA09–0x8005FA1E | `Chamber.md` 6, 11; `track_time` in `tools/xemu/chamber_model.py` |
| chamber on drawing: constants 4 to 11 and the three colours per part in both branches, reference point of the weighted centre, values of the light object from the constructor, order of summation of `vec3_transform` and `vec4_mul_mat4`, cube of directions and normal map per group | 0x8005EF41–0x8005EFCF, 0x8005F13A–0x8005F2FA, 0x80056BE5–0x80056D01, 0x8005C8BB, 0x80059A2E, 0x8005F7C1–0x8005F870 | `Chamber.md` 4.1, 7 |
| veil: the target texture created at 0x8005A445 lies in +0x18 at first, that at 0x8005A45B in +0x14; the swap happens only in `fx_draw` | 0x8005A42D–0x8005A45B, 0x80059BED–0x80059BF9 | `Effects.md` 1, 3 |
| sound: computation of the frame routine of the GP program in 24-bit fixed point, subroutine by subroutine; immediates from which the program writes its parameter block; the state machine P:0x013D does nothing; 31 instead of 30 read blocks | GP P:0x00F8–0x0124, P:0x0133, P:0x013D–0x0224, P:0x0235, P:0x0242–0x0395, P:0x03A2, P:0x03DF, P:0x04A9–0x0623 | `Audio.md` 6.2, 6.6 |
| sound: pointer tables of the sequencer (sample descriptions, track lists, highest event number) raw as a file | 0x800A7BB0–0x800A7C13 | `Audio.md` 3.1; `data/audio/seq_pointers.bin` (`tools/extract/audio.py`) |
| copy routine of the textures as a row of the main table: eight arguments, unchanged copy for w ≤ 2 or h ≤ 1, fast ways only without row length, cut-out and destination point and only from 8 × 8 (4 bytes) or 16 × 8 texels | 0x800AAB46, 0x800AAB57–0x800AAB94, 0x800AAB9A–0x800AAC0C, 0x800AAC11, 0x800AB270–0x800AB294 | `Textures.md` 3; `README.md`, name `copy_swizzled` set with `tools/ghidra/apply_names.py` (436 rows) |
| depth offset of the light views: switching on, value and factor from `chamber`+0x54 and +0x58, switching off; the constructor sets 10.0 and 4.0 | 0x8005FFDD, 0x8005FFF5, 0x8006000F, 0x800600D8; 0x8005FD94, 0x8005FD9B | `GPU.md` 6.2 |
| alpha test off in `chamber_draw`; conditions of `anim_draw` with their behaviour for NaN read again | 0x8005F8AB–0x8005F8B8, 0x80056D2F–0x80056DA8 | `FrameOrder.md` 3, 5; `frame_build` in `core/src/FrameOrder.c` |

## Measured in xemu

| What | Run | Vector file |
|---|---|---|
| at the entry point 0x9024C exactly the 0x6000 bytes of `2bl.bin` stand at 0x90000 | `bench.py --name c2_break2bl --break 0x9024C` | `vectors/c2_bench.json`, `break_2bl` |
| `.text` of the kernel lies in guest RAM at physical 0x10000 (address − 0x80000000) | `c2final_A`, frame 90 | `vectors/c2_bench.json`, `kernel_text_in_ram` |
| flips of the animation at the pace of vblank, single intervals of two vblanks at changing places | `c2final_A`, `c2final_B`, `flips.json` | `vectors/c2_bench.json`, `flip_timing` |
| two runs give different memory dumps at the same frame; assignment of the deviations | `c2final_A` against `c2final_B`, `compare.py` | `vectors/c2_bench.json`, `determinism` |
| where the CPU is during the animation (samples of EIP and stack) | `sample_eip.py --name c3_eip --duration 22`, `eipmap.py` | none; run in `out/`, result in `Overview.md` |
| the only writer of 0x8003A89C is 0x80030EDD, +1 per interrupt | `watch.py 0x8003a89c --hits 6 --after-flips 30` | none; result in `Timing.md` |
| FPU control word 0x027F in the animation | GDB packet `g` at 0x80056EF6 and 0x800571AD (run `c3_fpucw`) | none; result in `Timing.md` 4.2 |
| time keeping bit-exact: pulse table, generator state, t and x18..x24 in every frame, long and short version | `probe.py`, runs `c3_timing2`, `c3_short`; commands in `tools/xemu/vectors_c3.py` | `vectors/c3_timing.json` |
| which sub-object draws what | `bench.py --patch`, runs `c3_ko_*` | `vectors/c3_knockout.json` |
| sequencer bit-exact: beat, channel, address and number of all 900 commands; interval of the beats (6 ms); every method to the sound hardware predicted per call (1793 values) | `probe.py --trace mcpx_apu_method`, run `c4_seq`; command in `tools/xemu/vectors_c4.py` | `vectors/c4_audio.json` |
| tables of the sound bit-exact in guest memory | `c2final_A`, frame 90 | `vectors/c4_audio.json`, `tables` |
| every sounding channel at the right time and with the right level in the recording without DSP (xemu's monitor mix) | recording `c2audio`, render `tools/xemu/audio_render.py`, comparison `tools/xemu/compare_audio.py` | `vectors/c4_recording.json` |
| the same with the DSP output: stereo, reverb from bin 11; side balance played alone to 0.03 dB | `bench.py --audio --dsp`, runs `c4_dsp_out`, `c4_dsp_solo1`, `c4_dsp_solo9`; comparison `tools/xemu/compare_dsp.py` | `vectors/c4_recording_dsp.json` |
| channels 2 and 9 played alone follow the render at the same gain | `bench.py --audio --patch` on 0x800A7BDC, runs `c4_solo2`, `c4_solo9` | none; result in `Audio.md` 7 |
| the recording chain of the host removes a DC component (sent 5 : 1 to 1 kHz, recorded 0.015 : 1) | test signal through the WASAPI default device, recording through the stereo mix, without xemu | none; `Audio.md` 7 |
| played alone the channels 1, 2 and 9 have the same gain against the render (−10.5 … −10.9 dB) | runs `c4_dsp_solo1`, `c4_dsp_solo9`, `c4_solo2`, `c4_solo9` | none; `Audio.md` 7 |
| the GP DSP runs a program; its first 0x625 words equal 0x80053568–0x80054DFB | saved device state `mcpx-apu` of `c2final_A`, frame 90 | none; `Audio.md` 6.1 |
| GP program in a harness of our own: the same X memory as in xemu except for running pointers; impulse responses of all 32 bins; linearity | `tools/dsp/build.py`, `tools/dsp/gp_ir.py` | `vectors/c4_gp.json` |
| the wiring of the reverb (lines, taps, coefficients) reproduces the impulse responses to 43 dB | `tools/dsp/reverb_model.py` against `out/dsp/gp_ir.npz` | none; `Audio.md` 6.2 |
| which GP instructions run in a window | `tools/xemu/dsp_trace.py`, run `c4_dsp` | none; local |
| chamber and camera bit-exact: unpacked data, all placements and the geometry of both detail levels; at t = 0.784, 2.049, 3.024, 4.500, 5.642 track states, detail level, path keys, eye, target, view, projection, both light views and the world matrix of every part (107 checks, 172 014 values); quaternion 238 is NaN, 19 parts with a NaN matrix at t = 0.784 | `tools/xemu/probe_c5.py`, run `c5_main`; comparison `tools/xemu/vectors_c5.py` | `vectors/c5_chamber.json` |
| blob bit-exact: state before every update in 1283 frames (t = 0.029 to 5.926), meshes and indices from the set-up, at nine times the deformed mesh, glow surface, colour, 13 constant calls and two colour sets; end of `this+0xD` after t = 5.926; after every capture dt ≈ 0.18 (measurement artefact). Addendum: generator state in every frame, two restarts with `blob_restart`, branch `|dir|² < 0.001` after a set generator, both outputs of the weighted centre; 2623 frames together | `tools/xemu/probe_c6.py`, runs `c6_main`, `c6_tail`, `c6_plain`, `c6_loop` (`--loop`), `c6_poke` (`--rand-poke`); comparison `tools/xemu/vectors_c6.py` | `vectors/c6_blob.json` |
| logo bit-exact: all vertex buffers and index lists in the guest equal `data/logo/` (mesh C with the replacement of v), texture TM in Z order; state in 688 frames; drawing into the texture exactly at the first frames with t ≥ 6 and t ≥ 6.25; in the command stream 119 draw calls with primitive, format and index count of every mesh, constants 0–8 of mesh D, opacity of the TM marks and colour of the lettering | `tools/xemu/probe_c7.py`, runs `c7_main` (`--trace-from 5.0`), `c7_b` (`--trace-from 5.2`); comparison `tools/xemu/vectors_c7.py` | `vectors/c7_logo.json` |
| graphics: commands per call equal to the methods executed in set-up and 20 frames (1 140 108 methods); 93 program and block loads equal to the ROM; eleven CPU textures bit-identical (one of them was at first counted twice, as twelve), with the SHA-256 of every dump in the vector file; every draw call assigned to a step from `FrameOrder.md`; 65 values of the veil equal to the model | `tools/xemu/probe_c8.py`, runs `c8_main`, `c8_main2`; comparison `tools/xemu/vectors_c8.py` | `vectors/c8_gpu.json` |
| border colours of the stages 0 and 3 and viewport of the light view in the stream | trace of `c8_main2`, counted with `tools/xemu/nv2a.py` | none; `GPU.md` 2.2 |
| at start-up the kernel hands over no after-image: 0x800565F3 once at 63 ms with 0x8007087C = 0; 0x80056657 and 0x800561D1 in the tear-down with 0x8006F01C = 0 | `probe.py --name c9_post`, command in `Overview.md`, "After-image" | none; `Overview.md`, "After-image" |
| step of every stop at 0x80057949 in whole milliseconds with t after it (544 stops, four of them repeated without a frame: 3, 131, 162, 265); orbit values + 0x690, + 0x698, + 0x6A0, + 0x6A8 in 20 frames | run `c8_main2`, evaluated anew with `tools/xemu/vectors_c8.py` (`timeline`, field `track` in `check_effects`); no new run | `vectors/c8_gpu.json`, `timeline`, `effects` |
| tracks of the chamber at t = 0.933, 1.032, 1.130, 1.441, 1.758: 29 rotations and 32 positions equal to the model with k from the unrounded quotient; with rounded u up to 27 tracks different | `python tools/xemu/probe_c5.py --name k6_pose --t 0.9 --t 1.02 --t 1.1 --t 1.3 --t 1.7` | none; `Chamber.md` 6, locally `out/xemu/runs/k6_pose/` |
| constants, colours and matrices of every draw call in the stream: 447 530 values of vertex constants, 18 449 packed colours, 27 factor colours and 4442 model-view matrices equal to the core, bit for bit; the model-view of the library is the product in float32 with the order of summation 0, 1, 2, 3 | run `c8_main2`, words of the push buffer from `probe_c8.json`; `core/test/check_k6.py` | none; local |
| states per kind of drawing as they hold at the call: alpha test of the blob inherited, back faces 0x405 except in `logo_draw`, depth comparison set only by `fx_draw` | `vectors/c8_gpu.json`, `kinds`, counted over all 21 frames; no new run | `FrameOrder.md` 5; checked in `core/test/test_k6.c` |
| the frame routine of the GP program, recomputed in fixed point after `Audio.md` 6.6, equals the program on xemu's DSP core word for word: impulse responses of the bins 0, 1 and 11 over 4 s, and 192 000 samples of noise in all three bins, 4803 of them at the limit | `tools/dsp/gp_ir.py` (`vectors/c4_gp.json`) and `out/dsp/gp.exe` on noise; `core/test/test_k7.c`, `core/test/check_k7.py` | `vectors/c4_gp.json`, `ir_sha256`; the noise local |
| what the guest sent to the hardware at each of the 81 notes and 86 releases (voice started at 56 notes); the 900 measured commands as a list; SHA-256 of the eight tables in guest memory | runs `c4_seq` and `c2final_A`, evaluated anew with `tools/xemu/vectors_c4.py`; no new run | `vectors/c4_audio.json`, `voice_on`, `voice_release`, `commands.list`, `tables` |
| the run `c4_seq` needed 30.7 s on the host for 7.95 s of guest time; with 35 instead of 9 frames per beat the model meets 79 instead of 59 of the 81 voice starts | `t_ms` and tick of the stops in `out/xemu/runs/c4_seq/probe.json`; `test_k7` with a changed `XB_AUDIO_TICK_BLOCKS` | none; `Audio.md` 5.5 |
| the output of the core against the recordings: without DSP the same numbers as the render (ten channels), with DSP its own output per channel through the GP stage, channels 1 and 9 played alone with side balance −5.98 and −5.97 dB against −6.00 | recordings `c2audio`, `c4_dsp_out`, `c4_dsp_solo1`, `c4_dsp_solo9`; `core/test/check_k7.py` with `tools/xemu/compare_audio.py` and `compare_dsp.compare` | no new ones; comparison with `vectors/c4_recording.json` and `c4_recording_dsp.json` |
| how far apart the pictures of two runs lie at the nearest t: 143 pairs from three runs, in classes by Δt | `python tools/xemu/bench.py --name w0_A --frames 10:520:10 --peek 0x8006F050:0x30`, likewise `w0_B` and `w0_C` (from 15); `tools/xemu/frame_pairs.py` | `vectors/w0_xemu_pairs.json` |
| a picture with all ticks before it, drawing surface and depth surface, at 16 times from t = 0.30 to 7.80; when saving, the guest marks time at the start of 0x80057949 | `python tools/xemu/frame_at.py --name w1_t<ms> --t <s>`, 16 runs `w1_t0300` … `w1_t7800` | `vectors/w1_frames.json` (steps of every run in ms, t as a bit pattern, measures of the comparison) |
| a snapshot with the guest stopped contains neither picture nor depth of the drawing surface (both 0), one from the running machine both | `w1_t2000`, first attempt with the snapshot at the breakpoint, second with the jump to itself | none |
| `SET_VIEWPORT_SCALE`, `SET_VIEWPORT_OFFSET` and `SET_CLIP_MAX` of the drawing surface and of the fixed function; format 2 of the depth surface | `c2final_A`, frame 150: `methods.txt`, `state.json` | none; `GPU.md` 3 |
| parts of the chamber with NaN matrices write no depth in xemu | `w1_t0800`, `hosts/web/compare_w1.py` | `vectors/w1_frames.json`; `FrameOrder.md` 4 |
| a picture with all ticks before it and the drawing surface, at 18 further times from t = 0.13 to 6.26 | `python tools/xemu/frame_at.py --name w2_t<ms> --t <s>`, 18 runs `w2_t0100` … `w2_t6260` | `vectors/w2_frames.json` (together with the 16 runs above: t as a bit pattern and measures of the colour comparison per picture) |
| `SET_SHADER_STAGE_PROGRAM` on drawing per kind of drawing; `SET_TEXTURE_FILTER`, `SET_TEXTURE_ADDRESS`, `SET_TEXTURE_CONTROL0` and border colour per stage | `c8_main2`, `vectors/c8_gpu.json`, `kinds`, `state` | `GPU.md` 4, 6.1 |
| depth offset in the stream (0x41200000, 0x40800000, target with format 0x113); the program of the library for the finishing of the frame, its two constants and the three corners of the triangle | `c2final_A`, frame 150: `methods.txt`; the program disassembled with nv2a_vsh | `GPU.md` 6.2, 7 |
| the front face is the clockwise winding on the screen; surfaces behind the plane of the light with z < 0 are lit in xemu; the truncation of the position to sixteenths of a pixel acts in the picture | `hosts/web/compare_w2.py` with `--off flip`, `--off snap` and without; `hosts/web/compare_w1.py`; runs `w1_t*`, `w2_t*` | `vectors/w2_frames.json`, `vectors/w1_frames.json`; `GPU.md` 3, 4.4, 6.3; `web.md` 9 |

## From public third-party documentation

| What | Source | What for |
|---|---|---|
| Ghidra requires a 64-bit JDK 21 | [GettingStarted.md, tag `Ghidra_12.1.4_build`](https://github.com/NationalSecurityAgency/ghidra/blob/Ghidra_12.1.4_build/GhidraDocs/GettingStarted.md), section "Minimum Requirements" | choice of the JDK |
| layout of the ROM: banks, 2BL at 0x39E00 with 0x6000 bytes | [xboxdevwiki: BIOS](https://xboxdevwiki.net/BIOS) | `Rom.md` sections 1, 2 |
| RC4 key of the 2BL at MCPX offset 0x1A5, destination 0x90000; three 16-byte keys, the third for the kernel | [xboxdevwiki: Boot Process](https://xboxdevwiki.net/Boot_Process) | `Rom.md` sections 2, 4 |
| MCPX at 0xFFFFFE00–0xFFFFFFFF | [xboxdevwiki: MCPX ROM](https://xboxdevwiki.net/MCPX_ROM) | addresses in the MCPX dump |
| windows of the flash at 0xFFF00000 and 0xFFFC0000 | [xboxdevwiki: Flash ROM](https://xboxdevwiki.net/Flash_ROM) | ROM head from 0xFFF00000 |
| MD5 of the MCPX v1.0 | [README of XboxBiosTool](https://github.com/tommojphillips/XboxBiosTool/blob/master/README.md), "What MCPX ROM do i use?" | check of the MCPX dump |
| LZX bit stream, trees, position slots, E8 translation | [MS-PATCH] LZX DELTA Compression and Decompression, v14.1 of 2025-08-19, section 2 ([page](https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-patch/cc78752a-b4af-4eee-88cb-01f4d8a4c2bf), [PDF](https://officeprotocoldocs-f5hpbjgea6b8gneq.b02.azurefd.net/files/MS-PATCH/%5bMS-PATCH%5d.pdf)) | `tools/rom/lzx.py` |
| NV2A registers from 0xFD000000 | [xboxdevwiki: Memory](https://xboxdevwiki.net/Memory) | addresses of the MMIO registers in the measurement bench |
| clock of the timer in xemu 1 125 001 Hz, for the hardware 1 125 000 Hz | xemu, commit [`fc24584`](https://github.com/xemu-project/xemu/tree/fc24584ce88f0915ad7f04775bb7712c2e3f49ee): `include/hw/timer/i8254.h`, `hw/timer/i8254_common.c`, `hw/xbox/xbox.c` (locally in `ref/xemu/`) | `Timing.md` section 1 |
| register order of the GDB packet `g` up to `fctrl` | xemu, commit `fc24584`: `gdb-xml/i386-32bit.xml` | FPU control word, `Timing.md` 4.2 |
| behaviour of xemu: trace events and their formats, position of the register blocks and registers of the NV2A, flip and `write3d`, write-back of the surfaces on `savevm`, vblank from the host clock, sound output through SDL, layout of the snapshot header, randomness in the EEPROM, register order of the GDB stub | source of xemu, commit [`fc24584`](https://github.com/xemu-project/xemu/tree/fc24584ce88f0915ad7f04775bb7712c2e3f49ee) (the commit of v0.8.136 that `xemu.exe` reports): `hw/xbox/nv2a/{trace-events,nv2a.c,nv2a_regs.h,pcrtc.c,pfifo.c,pgraph/pgraph.c,pgraph/pgraph.h,pgraph/gl/renderer.c,pgraph/gl/display.c}`, `hw/xbox/mcpx/apu/{apu.c,monitor.c}`, `hw/xbox/eeprom_generation.c`, `ui/xemu.c`, `ui/xemu-snapshots.c`, `gdb-xml/i386-32bit.xml` | `tools/xemu/`, places of evidence in `tools/xemu/README.md` |
| layout of qcow2: head, snapshot table, L1/L2 | `docs/interop/qcow2.rst` in the same commit | `tools/xemu/qcow2.py` |
| DSP core of the sound hardware: bootstrap, frame sequence, DMA blocks, window X:0xC00 onto the mix buffer, short immediates, disassembler; setting `audio.use_dsp` (default off), monitor mix | xemu, commit `fc24584`: `hw/xbox/mcpx/apu/dsp/{dsp.c,dsp.h,dsp_cpu.c,dsp_cpu.h,dsp_cpu_regs.h,dsp_dis.c.inc,dsp_emu.c.inc,dsp_dma.c,dsp_dma.h,dsp_dma_regs.h,dsp_state.h,debug.c,debug.h}`, `config_spec.yml` (locally in `ref/xemu/`, fetched 2026-10-04) | `Audio.md` 6, `tools/dsp/` |
| sound hardware: methods `NV1BA0_PIO_*` and fields `NV_PAVS_VOICE_*`, interpretation of pitch, attenuation, envelopes, low-pass, headroom, mix bins, loop end; end report `SE2FE_IDLE_VOICE`; limit of the 3D voices; output through GP/EP | xemu, commit `fc24584`: `hw/xbox/mcpx/apu/{apu_regs.h,apu.h,apu.c,fpconv.h,debug.c,apu_debug.h}`, `hw/xbox/mcpx/apu/vp/{vp.c,vp.h,svf.h,adpcm.h}`, `hw/xbox/mcpx/apu/dsp/{gp_ep.c,gp_ep.h,trace-events}` (locally in `ref/xemu/`, fetched 2026-10-04) | `Audio.md` 5–7, `tools/xemu/vectors_c4.py`, `audio_render.py` |
| `fsincos` and `fcos` in xemu: `sin` and `cos` of the host's C library on the value in `double` | xemu, commit [`fc24584`](https://github.com/xemu-project/xemu/blob/fc24584ce88f0915ad7f04775bb7712c2e3f49ee/target/i386/tcg/fpu_helper.c): `target/i386/tcg/fpu_helper.c`, `helper_fsincos`, `helper_fcos` (locally in `ref/xemu/target_i386_tcg_fpu_helper.c`, fetched 2026-10-06, SHA-256 `563a83d1c60f292c4ae517061daf0b89b2facd63a6450540b627cd3af9684b2a`) | `Timing.md` 4.2, `core/src/x87.c`, comparison in `core/test/sweep_k1.c` |
| `rsqrtss` and `rcpss` exact in xemu (1/√x and 1/x in float32) | xemu, commit `fc24584`: `target/i386/ops_sse.h`, `helper_rsqrtss`, `helper_rcpss` (locally in `ref/xemu/`, fetched 2026-10-04) | `tools/xemu/chamber_model.py`, `Chamber.md` 11 |
| RCPSS and RSQRTSS: relative error ≤ 1.5 · 2⁻¹²; CVTTSS2SI delivers 0x80000000 for NaN | x86 instruction reference after the Intel SDM: [RCPSS](https://www.felixcloutier.com/x86/rcpss), [RSQRTSS](https://www.felixcloutier.com/x86/rsqrtss), [CVTTSS2SI](https://www.felixcloutier.com/x86/cvttss2si), fetched 2026-10-04 | `Chamber.md` 11 |
| command stream of the NV2A in xemu: `pgraph_method_log` writes a sequence of `NV097_ARRAY_ELEMENT16` as one line with words − 1; fields of `SET_VERTEX_DATA_ARRAY_FORMAT` (type bits 0–3, size 4–7, stride from 8, type 2 = float); `SET_BEGIN_END` 5 = triangles, 7 = fan; `DRAW_ARRAYS` count − 1 in bits 24–31 | xemu, commit `fc24584`: `hw/xbox/nv2a/pgraph/pgraph.c` (`pgraph_method_log`, `DEF_METHOD(NV097, DRAW_ARRAYS)`), `hw/xbox/nv2a/nv2a_regs.h` (locally in `ref/xemu/`) | `Logo.md` 8, `tools/xemu/vectors_c7.py` |
| graphics of the NV2A in xemu: names and numbers of the methods `NV097_*`, fields of `SET_TEXTURE_FORMAT`, `CLEAR_SURFACE` bits, `SET_CULL_FACE` values, `NV2A_VERTEX_ATTR_TEXTURE0` = 9, cube faces aligned to 128 bytes; viewport in c[58], c[59]; reading of the combiners (inputs, mappings such as "unsigned invert" = 1 − clamp(x, 0, 1), last stage, shadow comparison); length of a texture; reordering (swizzle) | xemu, commit `fc24584`: `hw/xbox/nv2a/nv2a_regs.h`, `hw/xbox/nv2a/pfifo.c`, `hw/xbox/nv2a/pgraph/{pgraph.c,pgraph.h,psh_regs.h,vsh_regs.h,texture.c,texture.h,swizzle.c}`, `hw/xbox/nv2a/pgraph/glsl/{psh.c,psh.h,vsh-prog.c}`, `hw/xbox/nv2a/pgraph/gl/{draw.c,texture.c}` (locally in `ref/xemu/`, fetched 2026-10-02 and 2026-10-05) | `GPU.md`, `Textures.md`, `Effects.md`, `tools/xemu/nv2a.py`, `tools/extract/gpu.py` |
| SHA-256 in the test code | FIPS 180-4, Secure Hash Standard ([NIST](https://csrc.nist.gov/pubs/fips/180-4/upd1/final)); not fetched; constants and sequence are general knowledge | `core/test/sha256.c`; checked by its meeting the 28 sums from `vectors/c5_chamber.json` (made with Python's `hashlib`), as does `node:crypto` in `wasm_k2.mjs` |
| FNV-1a with 64 bits (initial value 0xCBF29CE484222325, factor 0x100000001B3) as the digest of the runs | [draft-eastlake-fnv](https://datatracker.ietf.org/doc/html/draft-eastlake-fnv); not fetched; general knowledge | `core/test/test_k2.c`, `wasm_k2.mjs`; the sum only compares runs with each other, its constants need not equal any standard |
| `SET_TRANSFORM_CONSTANT_LOAD` sets the load pointer, `SET_TRANSFORM_CONSTANT` writes component (method − 0xB80) / 4 mod 4 of the constant at the pointer and advances it after the fourth | xemu, commit `fc24584`: `hw/xbox/nv2a/pgraph/pgraph.c`, `DEF_METHOD_INC(NV097, SET_TRANSFORM_CONSTANT)`, `DEF_METHOD(NV097, SET_TRANSFORM_CONSTANT_LOAD)` (locally in `ref/xemu/`) | `guest_draws` in `core/test/check_k6.py` |
| arithmetic of the DSP core: product as x · y · 2 in 48 bits, addition in 56 bits, rounding with 2²³ and halves to even without scaling, shifting in 56 bits, limiting on reading the accumulator; DMA blocks with an offset of their own that is written back; output as bits 8 to 23; floating point to 24 bits with rounding to even | xemu, commit `fc24584`: `hw/xbox/mcpx/apu/dsp/dsp_cpu.c` (`dsp_mul56`, `dsp_add56`, `dsp_rnd56`, `dsp_asl56`), `dsp_emu.c.inc` (`emu_pm_read_accu24`, `emu_macr_*`, `emu_asl_imm`), `dsp_dma.c`, `gp_ep.c` (`mcpx_apu_dsp_frame`), `hw/xbox/mcpx/apu/fpconv.h` (locally in `ref/xemu/`, not fetched anew). Licences after the heads of the files: `dsp_cpu.c` and `dsp_emu.c.inc` GPL 2 or later, the others LGPL 2 or later | `Audio.md` 6.5, 6.6; `gp_frame` and `to_24` in `core/src/Audio.c` |
| voices in xemu: states and counters of the envelopes, start and release, pitch as advance, low-pass with its limits, attenuation, headroom; the frame thread runs by the real-time clock of the host | xemu, commit `fc24584`: `hw/xbox/mcpx/apu/vp/vp.c` (`voice_step_envelope`, `voice_process`, `NV1BA0_PIO_VOICE_ON`, `NV1BA0_PIO_VOICE_RELEASE`, `attenuate`), `vp/svf.h` (`run_svf`), `hw/xbox/mcpx/apu/apu.c` (`throttle`, `mcpx_apu_frame_thread`) (locally in `ref/xemu/`, not fetched anew). Licences after the heads of the files: `vp.c` and `apu.c` LGPL 2 or later, `svf.h` GPL 2 | `Audio.md` 5.5, 7, 8; `env_step`, `voice_block`, `attenuate` in `core/src/Audio.c`, in the steps of `tools/xemu/audio_render.py` |
| rasteriser and stages in xemu: truncation of the position to sixteenths of a pixel; colours of the vertex stage limited to [0, 1], outputs begin at (0, 0, 0, 1); operations of the vertex programs (`RSQ` over the absolute value, `RCC`, `DST`); the fixed function passes texture coordinates through; per combiner stage limiting to [−1, 1], input mappings, last stage; shadow comparison with the third coordinate and border colour; alpha test through `round(a · 255)`; depth offset from value and largest slope; filter and address fields of the texture stages; texture modes | xemu, commit [`fc24584`](https://github.com/xemu-project/xemu/tree/fc24584ce88f0915ad7f04775bb7712c2e3f49ee): `hw/xbox/nv2a/pgraph/glsl/{vsh.c,vsh-ff.c,geom.c}` (fetched 2026-10-08 to `ref/xemu/`), `glsl/{vsh-prog.c,psh.c}`, `pgraph/psh_regs.h`, `pgraph/gl/{draw.c,texture.c}`, `nv2a_regs.h` (locally in `ref/xemu/`) | `hosts/web/src/shaders.js`, `renderer.js`; `GPU.md` 3, 4, 4.4, 6.1, 6.2 |

Local copies of these pages lie in `ref/web/` (not in the repo): the wiki
pages as raw text (`action=raw`, fetched 2026-10-02), the README of
XboxBiosTool and [MS-PATCH] as a PDF with a text extract. The files from the
source of xemu lie in `ref/xemu/`, fetched 2026-10-02 from
`raw.githubusercontent.com` with the commit in the path.

## Third-party tools

All lie locally in `.tools/`. Every archive was checked against the SHA-256
sum that its source publishes.

| Tool | Version | Source | SHA-256 of the archive | What for |
|---|---|---|---|---|
| Ghidra | 12.1.4 (`ghidra_12.1.4_PUBLIC_20260921.zip`) | [GitHub release `Ghidra_12.1.4_build`](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build) | `ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db` | static analysis of the kernel |
| Eclipse Temurin JDK | 21.0.12.1+1, Windows x64 (`OpenJDK21U-jdk_x64_windows_hotspot_21.0.12.1_1.zip`) | [Adoptium, release `jdk-21.0.12.1+1`](https://github.com/adoptium/temurin21-binaries/releases/tag/jdk-21.0.12.1%2B1) | `f9d6e191ab098c0d416e7d588a24420a8621cd2f4720dab2459b8b7b2d2d8b4e` | run time for Ghidra; `.tools/ghidra-headless.bat` sets `JAVA_HOME` only for the call |
| xemu | v0.8.136, Windows x86_64 (`xemu-0.8.136-windows-x86_64.zip`), in `.tools/xemu/` | [GitHub release `v0.8.136`](https://github.com/xemu-project/xemu/releases/tag/v0.8.136) | `b25a6c24a2c2c36a0843a153cd9ee59ca6833ef87bdcd855ba0824930e4ddd1d`; `xemu.exe`: `7da537938ea2ac09f894186ba793c9ae51dff37c95903b0002273b8d363818b7` | runs of the BIOS |
| xemu, debug build | v0.8.136, Windows x86_64 (`xemu-0.8.136-dbg-windows-x86_64.zip`), in `.tools/xemu-dbg/` | [GitHub release `v0.8.136`](https://github.com/xemu-project/xemu/releases/tag/v0.8.136) | `8210d2ca45063c8e219c485b32979178ca1e1a54ade78534eff2aaa160030bfb`; `xemu.exe`: `ef80e4a955d3a3fca15bea13c548f3d311e945bb5c05a223aec781dda20a9599` | runs with debug access |
| XboxBiosTool | v0.3.0.0 (`xbios_v0.3.0.0.zip`), in `.tools/xboxbiostool/` | [GitHub release `v0.3.0.0`](https://github.com/tommojphillips/XboxBiosTool/releases/tag/v0.3.0.0), digest of the release API | `875c90bf5a026f6495785390cbc8290954a0ec19dfef5d0990f193e8fb644dca`; `xbios.exe`: `e2ace55a6edc6f1ccd67ea35218df56e8608ba9d8856584ab816b911b3ecbeb0` | only as an independent comparison, not as a source for our own unpacker |
| nv2a_vsh | 0.1.12 (`nv2a_vsh-0.1.12-py3-none-any.whl`), unpacked in `.tools/pylib/` | [PyPI `nv2a-vsh` 0.1.12](https://pypi.org/project/nv2a-vsh/0.1.12/), source [abaire/nv2a_vsh_asm](https://github.com/abaire/nv2a_vsh_asm), digest of the PyPI API | `263e5a4d00ba603c542b1a0628b01e972225792086c852aee24ba817e957a22a` | disassembles the seven vertex programs (`tools/extract/gpu.py`, `data/gpu/programs.txt`) |
| antlr4-python3-runtime | 4.13.2 (`antlr4_python3_runtime-4.13.2-py3-none-any.whl`), unpacked in `.tools/pylib/` | [PyPI](https://pypi.org/project/antlr4-python3-runtime/4.13.2/), digest of the PyPI API | `fe3835eb8d33daece0e799090eda89719dbccee7aa39ef94eed3818cafa5a7e8` | dependency of nv2a_vsh |
| `analyze-migration.py` | from xemu, commit `fc24584`, unchanged, in `.tools/qemu-scripts/` | [`scripts/analyze-migration.py`](https://github.com/xemu-project/xemu/blob/fc24584ce88f0915ad7f04775bb7712c2e3f49ee/scripts/analyze-migration.py) | the file itself: `96c4b4048ba4ef25d80871c1374fed9be727e84ea2030538751c9dfab574ab77` | takes apart the VM state of a snapshot (`tools/xemu/vmstate.py`) |
| LLVM (clang, lld, wasm-ld) | 23.1.3, Windows x64 (`clang+llvm-23.1.3-x86_64-pc-windows-msvc.tar.xz`); unpacked to `.tools/llvm/` only `bin/clang.exe`, `lld.exe`, `ld.lld.exe`, `lld-link.exe`, `wasm-ld.exe`, `llvm-ar.exe`, `llvm-objdump.exe`, `lib/clang/*/include/` and `LICENSE.TXT` | [GitHub release `llvmorg-23.1.3`](https://github.com/llvm/llvm-project/releases/tag/llvmorg-23.1.3), digest of the release API | `9ee6a2192fbb30c776415af529927f76ca6b5d1d55699bb1315f9878e610b5b8` | builds the core natively and for wasm32 (`core/build.py`) |
| Chrome for Testing | 155.0.8059.39, Windows x64 (`chrome-win64.zip`), unpacked to `.tools/chrome/` | [Chrome for Testing, channel Stable](https://googlechromelabs.github.io/chrome-for-testing/), file from `storage.googleapis.com/chrome-for-testing-public/155.0.8059.39/win64/`, fetched 2026-10-08. The source publishes no SHA-256; compared is the MD5 from the server's head `x-goog-hash` (`qaxEJo9cgEVs6DspMjiWGA==`) | `59ab2a6e99bde9c0bc180414988394f9f5355a0d3764c704151ee8ecea235c7e` (computed ourselves) | browser with WebGPU without a window for pictures and sound on demand (`hosts/web/shot.mjs`) |

Installed on the host, not in `.tools/`: ffmpeg 8.1.1 (build by gyan.dev)
for the window recording with `--video`.

Also on the host, not in `.tools/`: gcc 6.3.0 from MinGW.org (32 bits),
compiles xemu's DSP core with a harness of our own in `tools/dsp/build.py`;
gdb 7.6.1 of the same installation once for debugging the harness.

Also on the host, not in `.tools/`, from the start: Visual Studio 2022 Community
with MSVC 14.39.33519 and Windows SDK 10.0.19041.0; clang takes header files
and run time for the native build from it (`core/build.py`). Node.js 22.18.0
runs the wasm test (`core/test/wasm_k0.mjs`). `core/test/cost_k8.c`
takes `windows.h` from the same SDK and `intrin.h` from clang (exception
handler, time stamp counter, flag register); `core/test/cost_k8.mjs`
measures with the clock of Node.js. Measured on an AMD Ryzen 7 3700X under
Windows 10.

Python packages from the local installation, not in `.tools/`: pefile
2022.5.30 (`tools/ghidra/import_kernel.py`, reading the PE head) and
capstone 5.0.9 (disassembly of the MCPX dump and of the 2BL preamble when
looking things up, also in `tools/rom/disasm.py` and
`tools/rom/factsheet.py`; for looking up the order of summation of
0x8005667A, the slerp and the memory operands in the functions of camera and
chamber, with helper scripts in the scratch folder of the session, not in
the repo; likewise for fade(t), the constants of `spawn` and `step`
and the immediates of the blob; likewise for row 3 of the frame in
`campath_select`, the comparisons and immediates in `logo_update` and
`logo_draw` and for `mat4_rot_x`; likewise for the generators of the
textures, the arguments of their calls and the restart in `anim_update`; also
through `tools/rom/disasm.py` for the copy routine 0x800AAB46). For the
measurement bench: numpy 1.24.1, Pillow 12.3.0 (pictures) and sounddevice
0.4.5 (sound).

## To the specification

Code of the core and what it takes over from `docs/decomp/`. Every row names
the place of the specification; new content from the binary goes there
first.

| What | Code | Specification | Check |
|---|---|---|---|
| initial value t = 0 or 7.5 depending on the start mode | `core/src/Timing.c` | `Timing.md` 3 (0x80056B5B–0x80056B6E) | `core/test/test_k0.c`, `core/test/wasm_k0.mjs` against `vectors/c3_timing.json`, `checks.start.t` |
| tick at the end of the set-up in `this+0x28` and `this+0x2C` | `core/src/Timing.c` | `Timing.md` 2 (0x80057932–0x8005793B) | ibid.; the tick is an input of the test |
| ranges of the unpacked kernel in the data pack, addresses and lengths | `core/src/pack.c`, `tools/pack/pack.py` | `tools/extract/audio.py`, `chamber.py`, `logo.py`; `Audio.md` 3, 5, 6, `Camera.md`, `Chamber.md`, `Logo.md` | `pack.py` against `data/*/SHA256SUMS` and the `source` ranges of the `.json` |
| output of the sound 48 kHz, 16 bits, stereo | `core/include/xboot.h` | `Audio.md` 5 (format block), 6.5 (output of the GP), 7 (recording stereo) | `test_k7.c` |
| intermediate values of the x87 in `double`, rounding to `float` on storing | `docs/core.md` 4, `r32` in `core/src/core_internal.h` | `Timing.md` 4.2, `Chamber.md` 11 | time keeping bit-exact |
| frame step: d as a signed 32-bit difference with absolute value, dt = f32(d · 0.001), a step above 1 s counts 1 ms, no step from t ≥ 8.0, last tick after the step | `core/src/Timing.c` (`timing_frame`, `update`) | `Timing.md` 2 (0x80057949, 0x8005795C–0x80057961, 0x800571BC, 0x800579B8), 3 | `core/test/test_k1.c`, `wasm_k1.mjs` against `vectors/c3_timing.json`, all steps of both runs; rules outside the runs in `test_k1.c` |
| ramp on the unrounded t, pulses on f32(t), `x20`, `x24`, `x18` from the unrounded `x20`; order of the operations as in the model | `core/src/Timing.c` (`ramp`, `pulses`, `update`) | `Timing.md` 4, 4.2; `tools/xemu/vectors_c3.py` | ibid., and `core/test/check_k1.py` against the model (10 254 steps) |
| pulse table from the generator, 38 draws to the end of the set-up | `core/src/Timing.c` (`pulse_table`, `timing_start`), `rng_step` in `core_internal.h` | `Timing.md` 4, 4.1 (0x80056EF6, 0x8005A645, 0x80056A60, 0x80056A82) | table against guest and model, generator against the guest (0xC12E9847) |
| 16 float32 constants with address | `core/src/Timing.c`, `K_*` | `Timing.md` 2, 4 | `check_k1.py` against `out/rom/kernel.img` |
| `fsincos` as sin and cos in `double`, stored as float32 | `core/src/x87.c` | `Timing.md` 4.2 (0x8005706F–0x80057079) | `core/test/sweep_k1.c`: 0 deviations over all float32 arguments up to 0x3FC90FE0 against the C library and against the instruction of this CPU (`docs/core.md` 4) |
| `fsincos` up to 2π for generators, blend and opening angle | `core/src/x87.c` | `Chamber.md` 4.3, `Camera.md` 2, 5.5 | `sweep_k1.exe 0x40C90FE0`: 0 deviations over 1 086 918 625 arguments |
| arithmetic helpers of the x87: vectors, matrix product, inverse, quaternion to matrix, slerp; order of summation of the disassembly | `core/src/Overview.c` | `Overview.md`, "Small helpers" (0x8005667A, 0x800566CF, 0x8005B22E, 0x8005C790, 0x8005E0B1 …), `Chamber.md` 6, 11 | `core/test/test_k2.c`, `wasm_k2.mjs` against `vectors/c5_chamber.json`; `core/test/check_k2.py` against the model |
| arithmetic helpers in the SSE manner: root, sine, arc sine with the table from the ROM, arc tangent, arc cosine; `rsqrtss` and `rcpss` exact as in xemu; `cvttss2si` with NaN → 0x80000000 | `core/src/Overview.c` (`sse_*`) | `Chamber.md` 11; table 0x80066030 | ibid.; every path time, every slerp and every length passes through them |
| projection, view and its inverse | `core/src/Camera.c` (`camera_perspective`, `camera_view_toward`), `m4_inverse_rigid` | `Camera.md` 2, 3 (0x8005B18C, 0x8005B093, 0x8005B22E) | view and projection at five times against the guest |
| path table, choice with the first draw of the generator, eight own keys, tangents | `core/src/Camera.c` (`camera_start`, `own_keys`) | `Camera.md` 5.1–5.4 (0x8005AFB6, 0x8005A6CB) | `check_k2.py`: keys, frame, U, B, blend against the model; generator after the set-up still 0xC12E9847 (`test_k1.c`) |
| path time s(t), Hermite evaluation, blend of the target, switches `this+0xD`/`+0xE` | `core/src/Camera.c` (`path_time`, `path_eval`) | `Camera.md` 4, 5.5 (0x800571AD, 0x8005AC32) | five times against the guest; `check_k2.py` every millisecond from 0 to 8 s against the model |
| unpacking of quaternions (z′ unrounded, quaternion 238 as NaN), positions, index lists | `core/src/Chamber.c` (`decode`, `expand`) | `Chamber.md` 3 (0x8005DDE2, 0x8005DEB1, 0x8005DF27) | world matrices against the guest; counter-check with rounded z′: 5 failures |
| placement of the 271 parts and their side | `core/src/Chamber.c` (`place`) | `Chamber.md` 5 | world matrices without a track (S · L) against the guest |
| tracks at time t, detail level, world matrix W | `core/src/Chamber.c` (`chamber_at`, `chamber_world`) | `Chamber.md` 6, 7 (0x8005F9F1, 0x8005EE08) | 271 parts at five times against the guest, 19 parts with NaN at t = 0.784; `check_k2.py` at 410 times against the model |
| generators of the six basic shapes, counting, strip over the grid, groups | `core/src/Chamber.c` (`gen_*`, `form_counts`, `strip`, `strip_count`, `chamber_mesh`, `groups`) | `Chamber.md` 4 (0x8005C9DC–0x8005E210, 0x8006127E, 0x800612D3, 0x8005F3DB, 0x8005F7B8) | SHA-256 from `vectors/c5_chamber.json` (indices of all twelve groups, positions and attributes of eight); `check_k2.py` value by value against model and guest memory |
| light view T from a point | `core/src/Chamber.c` (`chamber_light_view`) | `Chamber.md` 8 (0x8005FDF9) | both sides at five times from the points of the guest |
| 82 float32 constants with address | `core/src/Overview.c`, `Camera.c`, `Chamber.c`, `K_*` | `Camera.md`, `Chamber.md`, `Overview.md` | `check_k2.py` against `out/rom/kernel.img` |
| factors of the arc sine in the data pack | `core/src/pack.c`, `tools/pack/pack.py` | `tools/extract/overview.py`, `Chamber.md` 11 | `pack.py` against `data/overview/SHA256SUMS`; `check_k2.py` against the image |
| generator of the blob with initial value, draws in [0, 1) and [−1, 1), set-up with `blob_restart` (208 draws) | `core/src/Blob.c` (`draw16`, `unit`, `signed1`, `blob_start`, `blob_restart`) | `Blob.md` 4, 5.1 (0x8005A645, 0x800582E7, 0x80058308, 0x80059270, 0x800598E6, 0x80059868) | `test_k3.c`, `wasm_k3.mjs`: number of draws in all five runs of `vectors/c6_blob.json`, generator after every restart; `check_k3.py`: generator before every frame in `c6_loop` and `c6_poke` against the guest |
| meshes of six cube faces with strip indices | `core/src/Blob.c` (`sphere`, `blob_mesh`), `strip_indices` in `core/src/Chamber.c` | `Blob.md` 3 (0x80058DF2, 0x800612D3) | `check_k3.py`: both meshes, the copy +0x1C and both index buffers against model and guest memory |
| bulges and drops: creation, update, spring, fade(t), approximate normalisation, cube faces; computation in `double`, order as in the model | `core/src/Blob.c` (`spawn`, `step`, `drop_update`, `blob_update`, `fade`, `len_approx`, `norm_approx`, `face_mask`) | `Blob.md` 2, 4–6, 9 (0x80059320, 0x800595B6, 0x800590A6, 0x80059789, 0x8005820E, 0x80058290, 0x80058FFC); `tools/xemu/blob_model.py` | all bulges and drops at 16 captures from `vectors/c6_blob.json`, in both builds; `check_k3.py`: the whole object before each of the 2623 calls byte for byte against the guest and 10 256 calls on four synthetic sequences against the model |
| deformation, on request from the state of the bulges; bulges from the last to the first | `core/src/Blob.c` (`blob_deformed`) | `Blob.md` 6.4, 9 (0x80058B93, 0x80058B2C) | height range at all captures of the vector file; `check_k3.py` the whole mesh (6936 values) at 16 captures against the guest; counter-check with the bulges in ascending order: 2 failures in `test_k3`, 16 meshes different in `check_k3.py` |
| `blob_update` after the time step if the camera sets `this+0xD` at the new t, with the argument after the rule for steps above 1 s | `core/src/core.c` (`objects`), `arg` and `steps` in `timing_state` | `Blob.md` 1, `FrameOrder.md` 3, `Timing.md` 2 | `test_k3.c`, `wasm_k3.mjs`: four runs through `xb_advance_free` with the dt of the guest, t and the frames with `blob_update` equal to the guest, x1c and x20 at the captures equal |
| values on drawing: k, glow surface with colour, constants 0–12 of the mesh, 4–9 and 10–13 per drop, both colour sets | `core/src/Blob.c` (`blob_draw`) | `Blob.md` 7 (0x800583BF, 0x80040520, 0x800410B0) | vector file at 16 captures with the camera of the core (quad up to the first restart); `check_k3.py` all values at 16 captures with the camera of the guest against the guest |
| weighted centre with both outputs, light view from it with the reference points of the pre-pass | `core/src/Blob.c` (`blob_weighted_center`), `xb_blob_light_view` in `core/src/core.c` | `Blob.md` 8, `Chamber.md` 8 (0x80058D1F, 0x800600F5, 0x800669D0, 0x800669CC) | `check_k3.py`: 1098 calls from `c6_loop` and `c6_poke`, both outputs against the guest; light views on the synthetic sequences against the model |
| 39 float32 constants with address, four immediates | `core/src/Blob.c`, `K_*`, `B_*` | `Blob.md` 2, 4–7 | `check_k3.py` against `out/rom/kernel.img`, the immediates against the bytes of the code |
| frame of the camera path with row 3 = m · −ρ | `core/src/Camera.c` (`own_keys`) | `Camera.md` 5.3 (0x8005A9CD–0x8005AA0F) | `check_k4.py`: W equal to `frame_matrix` of the guest in both runs; `check_k2.py` against the model; counter-check with row 3 = 0: all 17 calls of D different in `test_k4`, plus W in `check_k4.py` |
| seven meshes of the logo: unpacking of the vertices (five or three int16, one rounding from `double`) and of the indices from differences with 0x7E; v near 1 in mesh C becomes −1 | `core/src/Logo.c` (`unpack`, `unpack_indices`, `logo_mesh`) | `Logo.md` 2.1–2.3, 3.2 (0x8005B398, 0x8005B3D4, 0x8005B473, 0x8005B4D1, 0x8005B87F–0x8005B8BD) | counts against `vectors/c7_logo.json` in both builds; `check_k4.py`: all buffers byte for byte against model and guest memory of both runs; counter-check without the replacement in C: mesh C different against model and guest |
| update: +0x4C, position of the lettering from two keys, opacity, drawing into the texture on a phase change; after the blob if the camera sets `this+0xE` | `core/src/Logo.c` (`logo_update`, `logo_start`), `objects` in `core/src/core.c` | `Logo.md` 3.1, 4, 6; `FrameOrder.md` 3 (0x8005C4A5, 0x8005C366, 0x800575FB–0x80057617) | `check_k4.py`: state after each of the 688 frames against the guest, frames with drawing into the texture equal; 6023 calls on three sequences against the model; counter-check with weights of the blend in float: 5 frames different against the guest |
| values on drawing: in space or as a picture, W, constants 0–8 of D, colour of Q1 and Q2 from the opacity, colour and world matrix of E; when drawing into the texture the phase as +0x4C and the old +0xD4 | `core/src/Logo.c` (`logo_draw`, `rot_x`), `xb_logo_draw` in `core/src/core.c` | `Logo.md` 5, 6 (0x8005BC63, 0x8005BF6D–0x8005C16C, 0x8005C181–0x8005C317, 0x800570A3) | `test_k4.c`, `wasm_k4.mjs`: constants 0–8 of all 17 calls of D from `vectors/c7_logo.json` with the camera of the core; `check_k4.py`: all 119 calls of the logo in the stream in order, mesh, constants and colours; world matrix of E only against the model; counter-check with the new +0xD4 when drawing into the texture: TM and lettering missing, 3 calls fewer per run |
| 20 float32 constants with address, seven immediates | `core/src/Logo.c`, `K_*`, `B_*` | `Logo.md` 2.2, 3.2, 4, 5 | `check_k4.py` against `out/rom/kernel.img`, the immediates against the bytes of the code |
| reordering of the texels on copying: bits of x and y alternately as long as the side has bits; unchanged below 3 × 2 | `core/src/Textures.c` (`swz_init`, `deposit`) | `Textures.md` 3 (0x800AAB46, 0x800AAB8C–0x800AABB9) | every texture against the guest, among them 16 × 128 (gradients), 16 × 16 (TM) and the levels 4 × 4 and 2 × 2 of the falloff |
| layout: levels one after the other, six cube faces aligned to 128 bytes; format word from colour format, levels, cube and size | `core/src/Textures.c` (`texture_info`), `xb_texture_info` in `core/src/core.c` | `Textures.md` 1, 2 (0x80043270) | format word and length of all eleven textures against the resources in `vectors/c8_gpu.json`, in both builds |
| direction as colour, cube of directions with fx in the register and fy stored, faces 0 to 5 | `core/src/Textures.c` (`normal_to_argb`, `normal_cube`) | `Textures.md` 4.1, 4.2 (0x8006036C, 0x800610DD, 0x8006118B–0x80061205) | SHA-256 of both cubes against the dump in both builds; `check_k5.py` byte for byte against `c8_main`, `c8_main2` and the model |
| colour gradient: bytes · 1/255, reciprocal of h − 1 rounded, row from r · a + s · b | `core/src/Textures.c` (`gradient`) | `Textures.md` 4.3 (0x800607A1, 0x800607DC–0x80060972) | likewise for both gradients; counter-check with an unrounded reciprocal: no value changes, the place follows the disassembly |
| radial brightness: table with `fsincos` and power, distance through `sse_sqrt`, only x < y per row, otherwise 0 | `core/src/Textures.c` (`radial`) | `Textures.md` 4.4 (0x800603D8, 0x80060468–0x80060591) | likewise; counter-check with whole rows: the radial texture deviates in `test_k5` |
| falloff with mip levels, integer, with the generator of the noise and `rcl`/`sbb` | `core/src/Textures.c` (`falloff_mips`, `falloff_levels`, `rcl32`) | `Textures.md` 4.5 (0x800605D2, 0x800605DE–0x80060749) | likewise for all eight levels; the noise itself is not evidenced at noise = 0 |
| subdivision: order of the points, mean of four values modulo n, step with 100 and thr, mask, normal map with 0 behind the buffer | `core/src/Textures.c` (`walk_*`, `fractal`, `rand_below`) | `Textures.md` 4.6, 4.7 (0x8005A620, 0x800609D1, 0x80060B01–0x80060B33, 0x80060C77–0x80060DB4); order of visits as `fractal_walk` in `tools/xemu/texture_model.py` | likewise; counter-check with row 0 instead of 0 behind the buffer: the normal map deviates; with v1.z from f32(A): no value changes |
| three noise layers: only layer 0 computed, layers 1 and 2 draw their numbers; initial value as a byte; all three textures equal | `core/src/Textures.c` (`fractal_multi`), `noise_seed` in `core/src/core.c` | `Textures.md` 1.1, 4.8 (0x80060DDC, 0x80060E76–0x80060E80, 0x80060F1E–0x80060F5E, 0x800610B3–0x800610CC, 0x8005A178) | likewise; initial value against `fx_seed`; counter-checks without the draws of the layers 1 and 2 and with initial value 1: all three noise textures deviate |
| TM: 16 × 16 texels from the ROM, reordered | `core/src/Textures.c` (`texture_make`) | `Textures.md` 4.9, `Logo.md` 2.4 (0x80073FD0) | likewise |
| nine float32 constants with address, 38 immediates | `core/src/Textures.c`, `K_*`, `B_*` | `Textures.md` 1.1, 4 | `check_k5.py` against `out/rom/kernel.img`, the immediates against the bytes of the code (imm8 sign extended) |
| orbit values after every evaluation of the path, before blob and logo; initial values 0 | `core/src/Camera.c` (`camera_track`, `angle_sin_cos`), `objects` and `xb_init` in `core/src/core.c` | `Camera.md` 6 (0x80057435–0x80057587, 0x80056969) | `test_k6.c`: all four values in 20 frames against the guest; counter-checks noted separately |
| projection with unrounded q for P[14]; track time with k from the unrounded quotient | `core/src/Camera.c` (`camera_perspective`), `core/src/Chamber.c` (`chamber_at`) | `Camera.md` 2, `Chamber.md` 6 (0x8005B1CC–0x8005B220, 0x8005FA09–0x8005FA1E) | `check_k6.py`: set-up and frame t = 1.019 against the stream; run `k6_pose` against the model, `check_k2.py` core against model |
| values of a part on drawing: W, constants 0 to 11, three colours, with and without the switch; camera of the light view; draw calls per part | `core/src/Chamber.c` (`chamber_part_draw`, `v3_transform`, `v4_mul_m4`, `chamber_light_camera`, `chamber_part_prims`, `group_cube128`) | `Chamber.md` 4.1, 7, 8 (0x8005EE08, 0x8005C8BB, 0x80059A2E, 0x8005FDF9, 0x8005F7B8) | `check_k6.py`: 443 392 constants and 18 186 colours of A2, U3 and D1 against the stream |
| veil and glow: constants of the quad and of the depth, two colours, coordinates of the noise layers, alpha, factor colour, s and matrix of the glow; the two quads; switch v | `core/src/Effects.c` (`fx_draw`, `fx_quads`, `fx_veil_on`) | `Effects.md` 1–5 (0x8005A194, 0x80059BCD, 0x8005A41D) | `test_k6.c`: colours and glow in 20 frames against the vector file; `check_k6.py`: 592 constants, 20 colours, six factor colours and six matrices against the stream |
| number of calls of `fx_draw` and from it the target of the veil | `core/src/core.c` (`objects`), `track_state` | `Effects.md` 3 | `test_k6.c`: target and texture of the veil in eleven frames; counter-check without the swap: six frames different |
| frame as a sequence of draw calls: steps U1 to U4 and D1 to D5 with their conditions, set-up A1 and A2; per call mesh, primitive, count, program, block, textures, targets, states, constants, colours, matrices | `core/src/FrameOrder.c` (`frame_build`, `frame_setup`, `chamber`, `group`, `logo`, `present`, `state`, `face_view`), `xb_frame`, `xb_frame_setup` | `FrameOrder.md` 1–3, 5, `GPU.md` 2.1, 3–6, `Chamber.md` 7–9, `Blob.md` 7, `Logo.md` 5, 6, `Effects.md`, `Textures.md` 5 | `test_k6.c`, `wasm_k6.mjs`: 15 396 draw calls in 21 frames against `vectors/c8_gpu.json`; `check_k6.py` against the stream |
| 48 float32 constants with address (39 in `Effects.c` and `FrameOrder.c`, nine new ones in `Camera.c` and `Chamber.c`), two immediates, 14 addresses of programs and blocks | `core/src/Effects.c`, `FrameOrder.c`, `Camera.c`, `Chamber.c`, `K_*`, `B_*`, `VSH_*`, `PSB_*` | `Effects.md`, `Camera.md` 6, `Chamber.md` 4.1, 7, `GPU.md` 3, 4 | `check_k6.py` and `check_k2.py` against `out/rom/kernel.img`, the addresses against `data/gpu/gpu.json` |
| tables of the sound: noise, sine, FM with m before the computation, sawtooth, three 8-bit samples, copy read backwards with 0 at the front; on request into buffers of the host | `core/src/Audio.c` (`table_value`, `noise_table`, `audio_table`) | `Audio.md` 2 (0x80061AC1, 0x80061A9E, 0x80061B37–0x80061B59, 0x80061BBB–0x80061BD4) | `test_k7.c`, `wasm_k7.mjs`: SHA-256 of all eight tables against guest memory (`vectors/c4_audio.json`); `check_k7.py` value by value against model and guest memory; counter-checks for the copy and for m |
| sequencer: ring, list of the tracks with linking in at the front, channel records of layer 0, event handlers 0 and 1, one command per due track and beat, commands 0 to 6, 9 to 13, 15 to 17, halt at unused commands | `core/src/Audio.c` (`seq_tick`, `seq_event`, `seq_step`, `channel_reset`, `track_release_all`, `track_release_channel`, `audio_start`) | `Audio.md` 3 (0x80060252, 0x8006165E, 0x8006033C, 0x80061762, 0x800617D0, 0x800669D8, 0x80061D71–0x80062469) | `test_k7.c`, `wasm_k7.mjs`: all 900 commands of `c4_seq` in beat, channel, address and number; `check_k7.py`: 918 commands over 1340 beats against `audio_model.py`; three counter-checks |
| voice layer: pitch with 0x155 per semitone, level 200 − 30 v with the deduction 600, eight attenuations packed, envelope blocks by kind, filter word, format and mix bins per channel, start only for an idle voice, release only for a running one | `core/src/Audio.c` (`voice_call`, `volumes`, `envelope_block`, `descriptor`; pitch in `seq_step`) | `Audio.md` 5 (0x800618DF–0x80061D48, 0x8004EB1D, 0x8004EE84, 0x8004EF2D, 0x8004EFC6, 0x80051F2E, 0x80051085) | `test_k7.c`, `wasm_k7.mjs`: 620 calls with arguments and 154 method values after the first sample call of eleven channels against the guest; `check_k7.py`: all registers of all 16 voices after each of 1340 beats against `vectors_c4.py`; two counter-checks |
| beat every nine blocks of 32 samples (6 ms); no sound in the short version | `core/src/Audio.c` (`audio_block`), `xb_audio_init` in `core/src/core.c` | `Audio.md` 1, 4 (0x8006166D, 0x800579B8; measured 6 ms) | `test_k7.c`: one beat per nine blocks; short version silent |
| frame routine of the GP program in 24-bit fixed point, parameters from the immediates of the program in the pack, twelve lines with 29 taps, without the dead lines L1 and L2 | `core/src/Audio.c` (`gp_frame`, `gp_x`, `gp_rnd`, `gp_store`, `blk_*`, `gp_tap`, `gp_start`) | `Audio.md` 6.2, 6.6 (P:0x0242–0x0395, P:0x00F8–0x0124, P:0x03A2, P:0x04A9–0x0623) | `test_k7.c`, `wasm_k7.mjs`: impulse responses against `vectors/c4_gp.json`; `check_k7.py`: 101 parameters and all taps against X memory and DMA chains of the program, 192 000 samples of noise against `out/dsp/gp.exe`; seven counter-checks |
| output as bits 8 to 23 of both sides; mix bins as rounded 24-bit values | `core/src/Audio.c` (`audio_block`, `to_24`, `asr8`) | `Audio.md` 6.5 | `test_k7.c`: `xb_audio` against the blocks |
| a whole run through the interface: set-up in seven passes, per frame time step, draw list and the deformed mesh where the blob is drawn; the sound on its own clock with 48 samples per millisecond; end with `XB_DONE` | `hosts/native/run.c` (`run_whole`), `hosts/native/xboot_run.c` | `FrameOrder.md` 1–3, `Timing.md` 2, 3, `Audio.md` 1; `docs/core.md` 2 | `core/test/test_k8.c`, `wasm_k8.mjs`: 480 frames, `XB_DONE` only in the last, 384 000 samples, digest `255E09F97DDD2114` in both builds and in the native host; draw lists with the digest of `test_k6` |
| picture and sound do not touch each other; queries do not change the state; the core stays in the memory it reports | `core/src/core.c` (all entry points) | `docs/core.md` 2; `Audio.md` 1 (own beat of the sound) | `test_k8.c`: draw lists with and without sound equal, samples equal to `xb_audio` alone, 84 queries at six frames without a changed byte, guard bytes behind every block, pack with the same SHA-256 |
| costs of the core: memory, time per call, instructions and arithmetic operations per call, stack depth | `core/test/cost_k8.c`, `core/test/cost_k8.mjs` | no statement about the original, only about this build | run `python core/build.py --cost`, output `out/core/k8/cost.json`; counter checked against a loop with a known number of instructions; numbers in `docs/core.md` 6 |
| five constants in `double` with address, three immediates of the noise generator | `core/src/Audio.c`, `K_*_BITS`, `NOISE_*` | `Audio.md` 2 | `check_k7.py` against `out/rom/kernel.img` |
| pointer tables of the sequencer in the data pack | `core/src/pack.c`, `tools/pack/pack.py` | `tools/extract/audio.py`, `Audio.md` 3.1 | `pack.py` against `data/audio/SHA256SUMS`; the sequencer runs only with them |

Ways of computing of our own in `core/src/x87.c`, without a model in the
binary and without a third-party template: reduction by multiples of π/2
with three partial constants, Taylor series up to x²¹, π from Machin's
formula (`core/test/gen_trig.py`). The original uses the instruction
`fsincos`; the core meets its result after storing but computes differently.

Own choices without a model in the binary, so that nobody takes them for a
finding: the format of the data pack with the identifier `XBPK`, the names
`xb_*` and `XB_*` of the interface, the codes `XB_STEP_*` for the steps from
`FrameOrder.md`, the alignment to 16 bytes.

For camera and chamber: camera and chamber as
queries at arbitrary t, the geometry in buffers of the host, 0 in the
vertices that the original does not write, the names of the functions in
`Overview.c`, `Camera.c`, `Chamber.c` (in `docs/decomp/README.md` under
"Name in the rebuild"), the digest FNV-1a in `test_k2.c` and `wasm_k2.mjs`.
The pattern 0xFFC00000 at quaternion 238 comes from the guest (`Chamber.md`
3); that the core sets it fixed is an own choice.

For the blob: the deformed mesh
on request instead of in two buffers that are swapped, with the flag `flat`
for the undeformed mesh; the drop of a bulge as an index with −1 instead of
as a pointer; 0 in the values 9.w and 11.w, which `blob_draw` does not set;
the interventions `xb_blob_update`, `xb_blob_restart` and `xb_blob_set_rng`
(after the model of the measurement, `Blob.md` 10); the counters `draws` and
`updates`; the names of the functions in `Blob.c`; the commands of
`test_k3 --replay`.

For the logo: the meshes of the logo on request instead of
in the state; the flags `rtt` and the remembered +0xD4 for the drawing into
the texture, so that the host can query its values after the update; the
counters `updates` and `rtt_count`; 0 in the fields of `xb_logo_params` for
parts that are not drawn; the intervention `xb_logo_update`; the names of
the functions in `Logo.c`; the commands of `test_k4 --replay`; the
assignment of the calls in the stream by their order in `check_k4.py`.

For the
textures: the textures on request into buffers of the host, every texel directly
at its reordered place instead of through an intermediate buffer; 0 in
texels that the original does not write, and behind the buffer of the
normal map; only layer 0 of the noise; the order and the names `XB_TEX_*`;
`xb_texture_desc` with the colour format as a number; the remembered initial
value `noise_seed`; the names of the functions in `Textures.c`; the digest
over all textures in `test_k5.c` and `wasm_k5.mjs`; the SHA-256 of the dumps
in `vectors/c8_gpu.json`.

For effects and the frame order: the frame description itself, that is
`xb_draw` with its fields, the numbers `XB_DRAW_*`, `XB_MESH_*` and
`XB_SURF_*`, the division of the constants into at most two runs per draw
call and the common field `params`; programs and blocks as their address in
the ROM, fixed function as its format value; the set-up as seven passes of
`xb_frame_setup`; three vertices for the triangle of the finishing of the
frame, whose values the library writes; `XB_ERR_SPACE` with the sizes
needed; the counters `fx_draws` and `fx_flip`; the names of the two target
textures of the veil after the order of their creation; the section
`timeline` and the field `track` in `vectors/c8_gpu.json`; NaN as one
pattern in the digest of `test_k6.c` and `wasm_k6.mjs`; the names of the
functions in `Effects.c` and `FrameOrder.c`.

For the sound: the memory of its own
for the sound and `xb_audio_init`; the clock of the sound in blocks,
separate from `xb_advance`; `xb_audio_block` with its fields, the numbers
`XB_CALL_*` and `XB_TABLE_*`, the field `effect`; `xb_voice` as an image of
the methods sent, without handle number and buffer address; the muting; the
tables on request and the samples read or computed in their place instead of
from tables in memory; the track records as an array with indices instead
of pointers; unused commands halt their track; the parameters of the GP
stage read from the immediates of the program instead of from an X memory;
the buffers of the frame routine as an array `B[k]`; the names of the
functions in `Audio.c`; the commands `--tables`, `--run`, `--gp`, `--wav` of
`test_k7`; the list of the commands, the SHA-256 of the tables and the
fields `voice_on` and `voice_release` in `vectors/c4_audio.json`; the
division of `compare_dsp.py` into `expected` and `compare`.

Ways of computing of our own in `core/src/Audio.c`, without a model in the
binary and without a third-party template: e^x through k ln 2 + r with the
series up to r¹³; sine and cosine of the taps through the addition
theorems; rounding to the even number through the addition of 1.5 · 2⁵²;
the remainder of a division through rounding down and correcting.

Own choices in the native host, without a model in the binary: its
files (`run.json`, `states.csv`, `frames.bin` with the identifiers `XBRN`,
`SETP`, `FRAM`, `sound.wav`, `assets.json`, `assets/`) and switches; the
steps 17, 17, 16 ms for 60 Hz and in general the rounded-up millisecond per
frame; 48 samples per millisecond as the coupling of picture and sound in
the host; the digest of the whole run and what it folds; the guard bytes;
the choice of the eleven frames whose instructions are counted, and the
division of the run into four sections for the time measurement (the limits
0.6, 5.2 and 5.94 come from `Blob.md` 6.1, `Chamber.md` 6 and `Camera.md`
5.5). The method of the cost measurement (single-stepping through the trap
bit with an exception handler of Windows, classifying the instructions by
their opcode bytes, time stamp counter) is written without a source looked
up; it is evidenced by the cross-check in the program, not by a document.
The instruction classes cover only the opcodes that `cost_k8.c` names in its
head.

Own choices in the web host, without a model in the binary: the whole host in
`hosts/web/` with its files, the page and the transport; the consecutive
memory allocation from the wasm memory; a run as the sequence of its ticks
and going back by starting anew; the queries `?shot=`, `?ticks=`,
`?audiotest=` and the paths `/shot`, `/audio`, `/done`, `/error`; the test
pattern of three bars with its rows and colours; a second core in the
AudioWorklet and the silent computing ahead up to a place; `layout.json` and
its check. The interfaces of the browser (WebGPU, WGSL, AudioWorklet,
WebAssembly) and the switches of Chrome are used without a source looked
up; their behaviour is evidenced by `hosts/web/test_w0.mjs`, not by a
document. Taken over from the core are the steps 17, 17, 16 ms and 48 frames
per millisecond. The measures `mad`, `far`, `iou` in
`tools/xemu/frame_pairs.py` with their thresholds 16 and 24 and the classes
by Δt are own choices; measured with them are runs of this BIOS in xemu
(`web.md` 7, `vectors/w0_xemu_pairs.json`). The address of the animation
object 0x8006F050 for `--peek` stands in `tools/xemu/probe_c8.py`.

Own choices in the renderer, without a model in the binary: `hosts/web/src/renderer.js`
with its passes, the one index list per frame of triangles, the colours per
body and the going along at every step for the texture of the logo; the
query `&full=1` with `/screen` and `/depth`; `frame_at.py` with the jump to
itself (EB FE) at the start of 0x80057949 and `capture.json`; the measures
`iou`, `differ`, `off_outline`, `deep` in `compare_w1.py` with their limits
(one pixel, 256 steps, green ≥ 8 and 8 above blue). Built to the
specification are the position of every vertex (`GPU.md` 2.1, 3.1 to 3.5,
3.7), the offset 0.53125 (`GPU.md` 3), the order and the clearing of the
depth (`FrameOrder.md` 3) and the states of the depth from
`xb_render_state` (`FrameOrder.md` 5). What the host assumes in doing so
without the specification saying it is in `web.md` 4 and is listed there as
an assumption.

Built to the specification for colour: the seven vertex programs and the seven
blocks in WGSL (`hosts/web/src/shaders.js` after `GPU.md` 3 and 4.3,
`data/gpu/programs.txt` and `data/gpu/gpu.json`), the combiners from the
stages (`GPU.md` 5), the shadow comparison (4.4), filter, addressing and
border colours (6.1), the depth offset (6.2), the front face (6.3), the
finishing of the frame as the mean of two pixels (7), the packing of the
colours (4.2), the targets and their clearing (`FrameOrder.md` 1,
`Effects.md` 3, `Logo.md` 6, `Chamber.md` 8) and the ordering back of the
textures (`Textures.md` 2, 3). Where the specification relies on xemu here
(limiting of the stages, truncation to sixteenths of a pixel, surfaces
behind the light), the reference is xemu and not the console; so it stands
at the places in `GPU.md`. Own choices for colour, without a model in the binary:
the structure of the renderer (one pass per sequence of draw calls into the
same target, one block of values per draw call, filtering by hand between
four texels where a border colour holds), the formats of the targets in
WebGPU, the switches `&off=`, `hosts/web/dump.mjs` and `lightspace.mjs`, and
`compare_w2.py`, which applies the measures from section 7 to the drawing surface.
Not drawn is the cube texture of the set-up (A2); `web.md` 4 names the
reason.

Own choices in the workbench, without a model in the binary: the call `xb_frame_from`
of the core, that is a camera the original does not have (the computation of
the view follows `Camera.md` 3, that of the orbit values `Camera.md` 6;
`core.md` 2); the whole workbench in `hosts/web/`: the assignment of the
draw calls to parts and their colours, the ways to sections and rows
(`src/bench.js`), the times of the events as far as they are copied from the
narratives, the free camera around a target, the wireframe, picking by
number instead of colour with the limits in `SEEN`, the views of the targets
and textures including the mapping of depth to brightness up to 120 units,
the path as a line, leaving channels of the sound out with `xb_audio_mute`,
the path `/docs/` of the server, the small reader for Markdown, the queries
`&cam=`, `&hide=`, `&solo=`, `&wire=`, `&path=`, `&view=`, `&pick=` and
`test_w3.mjs`. Nothing about the BIOS is measured with it; `test_w3.mjs`
checks the workbench against the core and the documents in `docs/`.

Own choices in the workbench's surface (`web.md` 11), without a model in the binary: the call
`xb_select_path` of the core (the branch of `campath_select` that it
rebuilds is evidenced, `Camera.md` 5.2; that the workbench takes it and
leaves the generator alone in doing so is an own choice); the surface with
tabs, status line, state in the hash of the URL, keys, export and full
screen; the phases of the status line (`bench.js`, `phase`, from the
switches `Camera.md` 5.5 and the times from `Blob.md` 1 and `Effects.md` 4);
the outline under the pointer and around the selection; the flat shading per
part with the inclination towards the eye; the coarse detail level for all
frames, with the position of the fans and strips in the shapes as the draw
lists show them; holding the final picture until 10 s; speed, loop and
playing the sound like a tape; the viewer of the meshes in `src/models.js`
with its own light, its own camera and its own check lines (the winding of
the triangles around n is measured on the meshes, `web.md` 11); the markers
and trails at the blob and the see-through view; the track view of the sound
in `src/tracks.js` (notes, samples and calls from `xb_audio_block`, level as
gain times RMS value of the voice per beat, the pitch computed back to a
semitone after `Audio.md` 5.3, the end of a released note where its gain
becomes 0); scope and volume. Nothing about the BIOS is measured with it.
With this renderer `compare_w2.py` gives the same values for all 34
recordings as `vectors/w2_frames.json`.

## Approximations

In the core: how a voice makes samples from its registers
(`voice_block`, `env_step`, `resample` in `core/src/Audio.c`). The BIOS does
not say it (`Audio.md` 8). The core follows xemu's interpretation of the
hardware in the steps of `tools/xemu/audio_render.py` and deviates from it
three times: it computes in `double` instead of `float`; it resamples with a
sinc of 16 taps of its own instead of with libsamplerate; and the library
learns of the end of a voice by the next beat. Measured against the render
(8.8 · 10⁻⁹) and against the recordings from xemu, not
against hardware.

On the licence, so that it is decided and not overlooked: this part of the
core and the frame routine of the GP are code of our own, but their rules of
computation (envelopes, low-pass, rounding and limiting of the DSP) are read
from xemu's source, which is under LGPL and GPL (rows above under
"third-party documentation"). The same holds for
`tools/xemu/audio_render.py`. Whether that means anything for the licence of
this repo is for the project owner to decide.

## Reinterpretation

The playground of the workbench (`web.md` 11.5) reinterprets the
picture and says so: another colour of the scene as a rotation about the
grey axis after the finishing of the frame, the white room with a white
chamber in the green light that passes into white in the distance, the
materials mercury, lava and water as a gradient over the brightness of the
blob, the accent colour of the panel. None of it comes from the BIOS.
Switched off, the renderer draws the unmodified picture.

The tab "Next Gen" (`web.md` 12) draws the same scene anew, the way one
would render it today. Only scene and time come from the core: the shapes of
the chamber with their axes, the world matrices W, the light at the centre
of the blob with colour G, strength m = 2 · x20 and attenuation (`Chamber.md`
7), mesh, radius k and colour c0 of the blob (`Blob.md` 6.4, 7.2), the drops
(`Blob.md` 7.3), the camera. Material, shadow, reflections, fog, particles,
bloom, depth of field, tone mapping and all numbers for them are own
choices, without a model in the BIOS and without a third-party source. The
methods (GGX, PN triangles, Toksvig, ACES after Narkowicz, BVH by the surface
area heuristic) are generally known graphics techniques and written here
from memory, not taken over from a source. Switched off, the picture stays
the unmodified one.

## Guessed

Not evidenced. Stands here so that nobody takes it for a finding.

| What | Where | Why guessed |
|---|---|---|
| unit 100 ns of the two 64-bit counters in the handler of the timer | 0x80030E96, 0x80030EBD | concluded only from the ratio 10 000 per 1 ms (`Timing.md` 1) |
| stack size 0x4000 of the animation thread | 0x80055FBA | only by its position in the argument of 0x8002190E (`Timing.md` 5) |
| the sound library learns of the end of a voice by the next beat of the sequencer | `Audio.md` 5.5; `audio_block` in `core/src/Audio.c`, `render` in `tools/xemu/audio_render.py` | the answer of the library to the end report comes "later, on a way of its own"; how much later is not measured, and the probe run `c4_seq` is not suited for it |
| the sign of a NaN from the wasm core changes with the tier in which the machine compiles it | `core.md` 2, "Another path" | only the change between two runs in the same process of node is observed; the cause has not been looked up |
| depth comparison 0x203 already in the set-up and in the steps U1 to U3 of the first frame | `FrameOrder.md` 5; `state` in `core/src/FrameOrder.c` | before the first `fx_draw` no call has sent it; assumed is the value that `fx_draw` leaves later |
| depth is written when drawing the cube texture in the set-up | `FrameOrder.md` 5; `state` in `core/src/FrameOrder.c` | in the set-up no call has sent the switch; assumed is the value of the later frames |

Since evidenced and taken off this list: +0x34 and +0x38 of the blob are
vertex programs with this assignment, and a drop is a stretched sphere
(`GPU.md` 3.4, 3.5).

Since evidenced and taken off this list: the remainder of 3 · 10⁻⁵ between
the reverb model in floating point and the impulse responses comes from the
fixed-point arithmetic. With rounding, truncation, limiting and `asl #7` as
in `Audio.md` 6.6 the computation is bit-identical with the program.
