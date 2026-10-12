# Measurement bench xemu

One command starts a run of the BIOS in xemu v0.8.136 and records what the
specification needs. Everything lands in `out/xemu/runs/<name>/`, none of it
goes into the repo.

```
python tools/xemu/bench.py --make-eeprom              # once: a fixed EEPROM
python tools/xemu/bench.py --frame 150 --video --audio
python tools/xemu/bench.py --frame 90 --frame 150 --frames 30:450:30
python tools/xemu/bench.py --break 0x9024C            # breakpoint, then end
python tools/xemu/compare.py RUN_A RUN_B              # compare two runs
python tools/xemu/bench.py --audio --trace mcpx_apu_method   # further trace events
python tools/xemu/bench.py --audio --dsp              # sound from the GP DSP instead of the monitor mix
python tools/xemu/bench.py --frames 10:520:10 --peek 0x8006F050:0x30   # per picture also bytes from guest RAM
python tools/xemu/frame_pairs.py w0_A w0_B w0_C       # pair the pictures of several runs by t and measure
python tools/xemu/frame_at.py --name w1_t2000 --t 2.0 # one picture with all ticks before it, drawing surface and depth
```

`frame_at.py` takes the snapshot from the running machine while the guest
stands at a jump to itself: a `savevm` with the guest stopped (as with
`--break`) contains the RAM but not the drawn surfaces; measured in W1
(`docs/web.md` 8).

| File | Content |
|---|---|
| `bench.py` | starts xemu, counts pictures in the trace, takes snapshots, takes them apart after the run |
| `compare.py` | compares two runs at every picture both have captured |
| `qcow2.py` | creates an empty qcow2 image and reads the VM state of a snapshot |
| `vmstate.py` | takes the VM state apart with `analyze-migration.py` from the xemu source |
| `qmp.py`, `gdbstub.py` | small clients for QMP and the GDB stub |
| `probe.py` | breakpoints with memory reads; `--trace EVENT` switches further trace events on and records the position in the trace per hit, `--idle S` ends the run after S seconds without a hit |
| `audio_model.py` | model of the start-up sound after `docs/decomp/Audio.md`: tables and sequencer |
| `vectors_c4.py` | sequencer and methods to the sound hardware against the run `c4_seq` → `vectors/c4_audio.json` |
| `audio_render.py`, `compare_audio.py` | render of the model after the VP of xemu and comparison with a sound recording without DSP → `vectors/c4_recording.json` |
| `compare_dsp.py` | comparison of a recording with the DSP output (`--dsp`) against the render plus the impulse responses of the GP program (`tools/dsp/gp_ir.py`) → `vectors/c4_recording_dsp.json` |
| `dsp_trace.py` | xemu's disassembly of the executed DSP instructions for a short window; from the start only with care (section "Limits") |
| `probe_c7.py` | logo in every frame, buffers of the logo, method trace from a chosen t (`--trace-from`) |
| `logo_model.py`, `vectors_c7.py` | model after `docs/decomp/Logo.md`; comparison of buffers, state and draw calls in the command stream → `vectors/c7_logo.json` |
| `nv2a.py` | helpers for the command stream of the NV2A: method names, taking the push buffer apart, reading the method trace, length of a texture |
| `probe_c8.py` | stops at every call site of the graphics library in the range of the animation, reads the write pointer of the push buffer and, at the end of the set-up, all textures created; in the set-up without the non-drawing calls of `prim_group_draw` |
| `texture_model.py`, `effects_model.py`, `vectors_c8.py` | models after `docs/decomp/Textures.md` and `Effects.md`; comparison of command stream, textures, GPU objects, draw calls (steps from `FrameOrder.md`) and effect constants, plus the time line of the run with the repeated stops and the orbit values per frame → `vectors/c8_gpu.json` |

## What a run delivers

| What | Where | From where |
|---|---|---|
| sequence of pictures | `video.mkv` (`--video`) | window recording with ffmpeg `gdigrab`, 60 pictures/s, lossless (FFV1) |
| exact single pictures | `sequence/NNNN.png` (`--frames`), `frame_NNNN/frame.png` | surface at the PCRTC start address from the guest RAM of the snapshot |
| sound | `audio.wav` (`--audio`) | recording of the host input "Stereo Mix", 48 kHz, 16 bits, stereo |
| guest memory | `frame_NNNN/ram.bin`, `pramin.bin` | RAM blocks `xbox.ram` (64 MiB) and `nv2a-ramin` (1 MiB) from the snapshot |
| registers | `frame_NNNN/cpu.json`, `state.json` | CPU and every device from the snapshot, among them all PGRAPH and PFIFO registers of the NV2A |
| GPU command stream | `frame_NNNN/methods.txt` | trace `nv2a_pgraph_method*` between flip N−1 and flip N |
| time axis | `flips.json` | every flip with `write3d`, number of vblanks and host time |

## What "picture N" means

Picture N is the state after the N-th `NV097_FLIP_INCREMENT_WRITE` that
PGRAPH executes. xemu reports each with the trace event
`nv2a_pgraph_flip_increment_write` (`hw/xbox/nv2a/pgraph/pgraph.c`,
`DEF_METHOD(NV097, FLIP_INCREMENT_WRITE)`). The bench reads the trace along
while xemu writes it. As soon as N flips are counted, it triggers `savevm`.

Which flip the snapshot really follows is fixed only afterwards. Between
reading the line and the stop the GPU can flip once more. Two independent
features decide:

1. **The block of the write-back.** On `savevm` xemu stops the FIFO and then
   writes all dirty surfaces back into guest RAM. That runs on the PFIFO
   thread, which also executes the methods
   (`hw/xbox/nv2a/pgraph/gl/renderer.c`, `pgraph_gl_process_pending`). The
   lines `nv2a_pgraph_surface_download` of this block therefore stand in the
   trace behind every flip before the stop and before every flip after the
   machine continues. The event is switched on only from two flips before the
   target to the end of the `savevm`.
2. **The parity.** Every flip advances the field `write3d` of
   `NV_PGRAPH_SURFACE` (bits 20–22) modulo `NV_PGRAPH_SURFACE_MODULO_3D`
   (bits 28–30, here 2) (`nv2a_regs.h`). The value in the saved PGRAPH state
   has to fit the one the trace line of the flip names.

`run.json` holds the candidates, the rule and the result (`frame`) for every
snapshot. With both features every one of the 80 snapshots in C2 had exactly
one candidate. The parity alone is not enough: now and then two flips follow
before the machine stands. Often it is N+1, which is why the folders are
named after the picture actually captured.

## Why `savevm` with the machine running

- xemu draws into GL textures. Rendered surfaces come back into guest RAM
  only on demand, completely only on entering the state `RUN_STATE_SAVE_VM`
  (`hw/xbox/nv2a/nv2a.c`, `nv2a_vm_state_change`). A `pmemsave` or
  `dump-guest-memory` without `savevm` therefore delivers stale pictures.
- With xemu 0.8.136 the process exits if `cont`, `loadvm` or a large
  `pmemsave` follows a `savevm` in the stopped state. With `pmemsave` that
  happens reproducibly when reading 0x02A06000. A `savevm` with the machine
  running stops it itself and lets it continue cleanly afterwards. xemu's own
  snapshot function does it the same way (`ui/xemu-snapshots.c`,
  `xemu_snapshots_save`).
- The snapshot goes into a qcow2 image of its own that is attached through
  `-blockdev`. The guest does not see it. An additional `-drive` makes xemu
  hang at start-up, also with `if=none` and also as a raw image.
- After the run `qcow2.py` reads the VM state from the image
  (`docs/interop/qcow2.rst`, snapshot table). `vmstate.py` removes the header
  that xemu writes in front of it and takes the migration stream apart with
  `scripts/analyze-migration.py`. In doing so `vmstate.py` reads integer
  arrays in one piece instead of element by element; the result is the same.
  A picture then needs about 4 s instead of 12 s, more under load.
- `--break ADDR` stops through the GDB stub with a hardware breakpoint and
  then makes a `savevm` in the stopped state. Because no `cont` goes after
  it, the run ends there.

## Limits

- **Not bit-identical between runs.** xemu generates the vblank from the
  real-time clock of the host, every 16.67 ms (`ui/xemu.c`,
  `vblank_timer_thread`). How many vblanks pass up to picture N therefore
  depends on the host.
- **Video without every picture.** gdigrab does not record every picture
  shown: in the run `c2final_A` 202 of 755 intervals were about 33 ms long,
  that is twice as long as a vblank. The time stamps in the video are real.
  For single pictures it is good, for a gapless sequence it is not; for that
  there is `--frames`.
- **Snapshots disturb the time axis.** Every `savevm` stops the machine for
  170–220 ms. The vblank clock keeps running, the guest sees only one vblank
  afterwards. For time measurements, take runs without `--frames`.
- **Gaps in the command stream.** For `NV097_ARRAY_ELEMENT16` xemu logs only
  the count, not the parameters (`pgraph.c`, `pgraph_method_log`). The number
  in the line `* n` is the number of words − 1: the counter stands at 0 at
  the first word of a sequence and counts every further one (checked on all
  meshes of the logo, `docs/decomp/Logo.md` 8). If END, BEGIN and
  `DRAW_ARRAYS` follow each other directly, xemu swallows END and BEGIN
  without a line (`pgraph.c`, "Squash repeated BEGIN,DRAW_ARRAYS,END").
- **Which sound.** Without `--dsp`, `audio.use_dsp` stands at xemu's default
  (off): xemu then mixes the voices itself, each with the largest of its bin
  levels, mono, without the program in the GP DSP
  (`hw/xbox/mcpx/apu/vp/vp.c`, `MCPX_APU_DEBUG_MON_VP`). Only `--dsp`
  records the output of the GP (`dsp/gp_ep.c`). All recordings up to C4,
  `c4_no35`, were made without `--dsp`.
- **DSP trace from the start.** The GP runs long before the animation; a
  trace of the DSP instructions from the start reached 6.4 GB in
  `c4_dsp_boot` before the first picture came, and was aborted. For the
  complete disassembly there is `tools/dsp/`.
- **DC component.** The recording chain (default device → Stereo Mix)
  removes a DC component almost entirely. `compare_audio.py` and
  `compare_dsp.py` therefore leave out the spectral lines below 35 Hz.
- **Sound not bit-exact.** xemu outputs the sound through SDL directly to the
  default device (`hw/xbox/mcpx/apu/monitor.c`, 48 kHz, S16LE, stereo). The
  disk driver of SDL is not contained in the build. The recording through
  Stereo Mix therefore contains volume, mixing and resampling of the host and
  every other sound that happens to run. For C4 that is enough to compare
  times and pitches, not for a comparison sample by sample.
- **Pixel format of the scan-out buffer.** Picture 640×480, pitch 2560 from
  `surface_create_color`. The format A8R8G8B8 (0x8) stands in the PGRAPH
  state only as long as this buffer is the current target. Otherwise it is
  assumed, and `run.json` says so (`fb_format`).

## Fixed inputs

Every run writes a `xemu.toml` of its own and passes it with `-config_path`.
Without this option xemu reads the user configuration in
`%APPDATA%\xemu\xemu\xemu.toml`, which points to files outside the repo. The
configuration switches off the NVIDIA driver profile
(`setup_nvidia_profile`) and the shader cache. The EEPROM is the same file
`out/xemu/eeprom.bin` for all runs, because xemu randomises serial number,
MAC and keys when creating one (`hw/xbox/eeprom_generation.c`). `run.json`
records the SHA-256 of xemu, MCPX, flash and EEPROM.
