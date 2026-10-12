# Decomp index

The main table of this project. Every row assigns a name and a description
to a function or a data range in the unpacked kernel. Whoever loads the
same BIOS in Ghidra and applies `tools/ghidra/apply_names.py` can check
every row at its address.

## Columns

- **Address**: virtual address in the unpacked kernel. Image base
  (0x80010000) and sections are in `Overview.md`, the origin of the kernel
  from the ROM in `Rom.md`.
- **Ghidra name**: the label that `apply_names.py` sets in the Ghidra
  project.
- **Name in the rebuild**: stays empty until an implementation exists.
  "(reference only)" means: described but not rebuilt.
- **Subsystem**: reference to the narrative that sets out the behaviour.
- **Description**: one line, from the analysis of the binary only. Behind it
  in parentheses the origin of the name: `str@<address>` for a string in the
  binary, `obs.` for a descriptive name.

## Conventions

- Rows are sorted by ascending address.
- Code from libraries that does not belong to the animation, such as kernel
  routines, run-time library or graphics driver, stands here only if the
  animation calls it and its behaviour counts for the specification.
- If a label changes in Ghidra, the row here is changed with it.
- Every row under "Functions" is a function entry point. If Ghidra lacks a
  function there because the code is reached through pointers only,
  `apply_names.py` creates it. The table is thus also the list of these
  entry points.
- `this` means the object the function works on (register ecx or the
  sub-object, see `Overview.md`). "*guessed*" marks interpretations without
  evidence.
- The subsystems `Camera`, `Blob`, `Chamber`, `Logo`, `Effects`, `Audio`,
  `Textures` and `GPU` have their narrative in `<Subsystem>.md`; the order
  of the draw calls is in `FrameOrder.md`. `Kernel` stands for kernel
  routines that `Timing.md`, `Audio.md` or `Overview.md` describe.
- Every row is specified: its address or its name stands in the narrative of
  its subsystem or in a narrative that its description names. Or it carries
  the note "not needed" with the reason. `tools/spec/check_spec.py` checks
  that (0 reports).

## Functions

Complete for the range 0x80055E75–0x800626DF (222 functions), plus
two kernel routines that `Timing.md` needs, the functions of the
sound library and of the kernel that `Audio.md` needs, and every function of
the graphics library (0x8003CB40–0x80044F40) that
the animation calls directly (76, subsystem `GPU`; `GPU.md` 2).
33 rows carry the note "not needed": the 29 library functions that
the animation reaches only through the after-image, and four helpers of the
after-image, which draws nothing at start-up (`Overview.md`, "After-image";
`GPU.md` 2.3).
The copy routine 0x800AAB46 of the graphics library stands here
too, which lies outside these ranges and which every texture generator calls
(`Textures.md` 3).

| Address | Ghidra name | Name in the rebuild | Subsystem | Description |
|---|---|---|---|---|
| 0x800194C0 | `timer_init` | | Kernel | creates a timer (bytes 0, 2, 3; list empty) (obs.) |
| 0x800194ED | `timer_cancel` | | Kernel | unlinks a timer from its list, returns whether it was linked in (obs.) |
| 0x8001951C | `timer_set` | | Kernel | sets due time and period of a timer; period times -10000 (0xFFFFD8F0) (obs.) |
| 0x8001A384 | `deferred_call_init` | | Kernel | creates a deferred call: kind 0x13, callback, context (obs.) |
| 0x8001C10D | `timer_expire` | | Kernel | serves the due timers of a bucket; inserts periodic ones anew with period times -10000 (obs.) |
| 0x8001C3CF | `timer_insert_bucket` | | Kernel | sorts a timer by due time into one of the 32 buckets from 0x8003A900 (obs.) |
| 0x8001C4CF | `timer_insert` | | Kernel | inserts a timer; relative due time against the counter 0x8003A890, absolute against 0x8003A880 (obs.) |
| 0x8001CCAC | `timer_bucket` | | Kernel | absolute due time = counter - relative; bucket = due time / 10000 & 0x1F (obs.) |
| 0x80030E4C | `timer_irq_handler` | | Kernel | interrupt handler of the timer; advances tick at 0x8003A89C by 1, two 64-bit counters by 10 000; Ghidra itself creates no function here (obs.) |
| 0x8003CB40 | `gpu_set_ring_sizes` | | GPU | notes the size of the push buffer ring and of the release pieces (0x8004DCC8, 0x8004DCC4); `device_setup` passes 0x100000 and 0x10000 (obs.) |
| 0x8003CB60 | `gpu_device_create` | | GPU | makes 0x8004AA80 the device (0x8004D610, 0x8004D614), for size 0 the defaults 0x80000 and 0x8000, sets up through 0x80041C20, on error 0x80042100 (obs.) |
| 0x8003CC00 | `gpu_set_present_flags` | | GPU | notes the switches of the finishing of the frame in 0x8004DC98 and sets the current target anew (0x8003E920); not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003CC30 | `gpu_set_word_420` | | GPU | notes a value in device+0x420 and 0x8004DC50, bit 0x400 in device+8; not needed: in the animation called by the after-image only (GPU.md 2.3), meaning open (obs.) |
| 0x8003CC60 | `gpu_put_method` | | GPU | writes a head word (ecx) and a value (edx) into the push buffer (obs.) |
| 0x8003CC90 | `gpu_defer_state` | | GPU | notes value edx for state ecx (0x52 to 0x73) in the copy from 0x8004DA80 and sets its bit from 0x800497D8 in 0x8004AA88; takes effect on drawing (obs.) |
| 0x8003CD10 | `gpu_set_stage_state` | | GPU | notes state edx of texture stage ecx; takes effect at the next drawing as texture and combiner state (obs.) |
| 0x8003CD40 | `gpu_set_smooth` | | GPU | `SET_LINE_SMOOTH_ENABLE` and `SET_POLY_SMOOTH_ENABLE` with the argument; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003CD70 | `gpu_set_shadow_func` | | GPU | `SET_SHADOW_DEPTH_FUNC` with the argument (obs.) |
| 0x8003CDA0 | `gpu_set_fog_color` | | GPU | `SET_FOG_COLOR` from 0xAARRGGBB with red and blue swapped; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003CDF0 | `gpu_set_cull` | | GPU | `SET_CULL_FACE_ENABLE` and, depending on argument and front face, `SET_CULL_FACE` (obs.) |
| 0x8003CE60 | `gpu_set_front_face` | | GPU | `SET_FRONT_FACE`, then 0x8003CDF0 with the noted value 0x8004DC7C; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003CEA0 | `gpu_set_normalize` | | GPU | `SET_NORMALIZATION_ENABLE`, bit 2 in device+8; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003CED0 | `gpu_set_factor_color` | | GPU | one colour into `SET_COMBINER_FACTOR0` and `FACTOR1` of all eight stages, 16 values (obs.) |
| 0x8003CF20 | `gpu_set_point_size` | | GPU | method 0x380 with trunc(x · device+0x508 · 8 + 0.5), at most 0x1FF; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003CF80 | `gpu_set_word_dca4` | | GPU | if the colour format of the target has the kind 0x20 after 0x80049850 and the value changes: `WAIT_FOR_IDLE`, method 0x1D8C with the value, `NO_OPERATION` 0x31C; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003D000 | `gpu_set_states_4d` | | GPU | states 0x4D to 0x51 through 0x8003CCB0 from n: −n · c(0x8001289C), −n, three times n ≠ 0; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003D080 | `gpu_set_logic_op` | | GPU | 0: `SET_LOGIC_OP_ENABLE` 0; otherwise `SET_LOGIC_OP_ENABLE` 1 and `SET_LOGIC_OP`; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003D0D0 | `gpu_set_fill_front` | | GPU | `SET_FRONT_POLYGON_MODE` and `SET_BACK_POLYGON_MODE`, back equal to front if 0x8004DC64 = 0; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003D110 | `gpu_set_fill_back` | | GPU | notes the fill mode of the back (0x8004DC60) and writes both fill modes anew; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003D160 | `gpu_set_fill_two_sided` | | GPU | notes the switch 0x8004DC64 for separate fill modes, writes both anew; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003D1C0 | `gpu_set_skin_mode` | | GPU | `SET_SKIN_MODE`, bit 2 in device+8; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003D200 | `gpu_set_texgen` | | GPU | `SET_TEXGEN_S/T/R` of the stage with a value from the upper halfword (0 off, 1 0x8511, 2 0x2400, 3 0x8512, 4 0x2401, otherwise 0x2402); not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003D2E0 | `gpu_set_stage_word` | | GPU | value k of a stage to method 0x1AD0 + 0x40 · s + 4k (s = stage, stage + 1 if device+0x414 = 0; nothing if s & 3 = 0), copy from 0x8004D880; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003D340 | `gpu_set_border_color` | | GPU | `SET_TEXTURE_BORDER_COLOR` of the stage, copy at 0x8004D8F4 + 0x80 · stage (obs.) |
| 0x8003D380 | `gpu_set_color_key` | | GPU | `SET_COLOR_KEY_COLOR` of the stage, copy at 0x8004D8F8 + 0x80 · stage; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003D910 | `gpu_set_depth_test` | | GPU | `SET_DEPTH_TEST_ENABLE`, 1 only with a depth surface; on a change from or to 2 states anew through 0x80043520, 0x80040340, 0x8003D690 (obs.) |
| 0x8003D980 | `gpu_set_stencil_test` | | GPU | method 0x1D84 from the switches 0x8004DCB0, 0x8004DCAC, then `SET_STENCIL_TEST_ENABLE`, 1 only with a depth surface; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003DA00 | `gpu_set_stencil_fail` | | GPU | method 0x1D84 as 0x8003D980, then `SET_STENCIL_OP_FAIL`; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003DA70 | `gpu_set_word_dca8` | | GPU | notes a value in 0x8004DCA8 and writes the group from 0x8003D690 anew; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003DAA0 | `gpu_set_word_dcac` | | GPU | notes a switch in 0x8004DCAC and writes method 0x1D84 anew; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003DB00 | `gpu_set_word_dcb0` | | GPU | notes a switch in 0x8004DCB0 and writes method 0x1D84 anew; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003DB60 | `gpu_set_word_dcb4` | | GPU | notes a switch in 0x8004DCB4, then 0x8003D840: two words of the device through `WAIT_FOR_IDLE`, 0x1D8C, `NO_OPERATION`; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003DB80 | `gpu_set_word_dcb8` | | GPU | as 0x8003DB60 with the switch 0x8004DCB8; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003DBA0 | `gpu_set_word_dcbc` | | GPU | as 0x8003DB60 with the switch 0x8004DCBC; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003DBC0 | `gpu_set_aa_low` | | GPU | notes 0x8004DC90, viewport anew (0x8003D4C0), `SET_ANTI_ALIASING_CONTROL` = 0x8004DC94 · 2¹⁶ + (0x8004DC90 & 0xF); not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003DC10 | `gpu_set_aa_high` | | GPU | as 0x8003DBC0, notes 0x8004DC94; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003E550 | `gpu_present` | | GPU | finishes a frame: drawing surface as a texture onto the display buffer, `FLIP_STALL`, `FLIP_INCREMENT_WRITE` (GPU.md 7) (obs.) |
| 0x8003E630 | `gpu_texture_create` | | GPU | creates a flat texture (width, height, levels, format), through 0x800406B0 (Textures.md 2) (obs.) |
| 0x8003E660 | `gpu_cube_texture_create` | | GPU | creates a cube texture (edge, levels, format), through 0x800406B0 (obs.) |
| 0x8003E690 | `gpu_surface_create` | | GPU | creates a surface to draw into (width, height, format, kind), through 0x800413F0 (obs.) |
| 0x8003E6B0 | `gpu_depth_surface_create` | | GPU | creates a depth surface, jump to 0x800413F0 (obs.) |
| 0x8003E6C0 | `gpu_set_matrix` | | GPU | notes a matrix in a slot; goes into the stream on drawing with fixed function (obs.) |
| 0x8003E7D0 | `gpu_device_release` | | GPU | counts device+0x408 down; the last time 0x80042100, zero the device, clear 0x8004D610 and 0x8004D614 (obs.) |
| 0x8003E810 | `gpu_device_init_wait` | | GPU | sets device+0x24F4 to 0 and calls 0x80019CDC for device+0x24F0 with (6, 1, 0, 0) (obs.) |
| 0x8003E920 | `gpu_set_target` | | GPU | sets colour and depth target: `SET_SURFACE_*`, viewport, depth and stencil test (obs.) |
| 0x8003EB80 | `gpu_get_display_surface` | | GPU | delivers a surface from device+0x2150 (stride 0x18; −1 → 1, 0 → 0, n → n + 1) with its counter increased; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003EBC0 | `gpu_copy_surface` | | GPU | copies a surface into another by drawing, with states of its own for alpha test, back faces, blending, depth (Logo.md 6) (obs.) |
| 0x8003EE20 | `gpu_get_color_target` | | GPU | delivers the colour target (device+0x40C) with its counter increased (obs.) |
| 0x8003EE40 | `gpu_get_depth_target` | | GPU | delivers the depth target (device+0x410) with its counter increased, without one 0x88760866 (obs.) |
| 0x8003EE70 | `gpu_set_viewport` | | GPU | viewport: `SET_WINDOW_CLIP_*`, `SET_VIEWPORT_OFFSET/SCALE`, `SET_CLIP_MIN/MAX` (obs.) |
| 0x8003EFD0 | `gpu_set_texture` | | GPU | texture of a stage: `SET_TEXTURE_OFFSET`, `SET_TEXTURE_FORMAT`, control word (obs.) |
| 0x8003F130 | `gpu_set_indices` | | GPU | notes index buffer (device+0x47C) and base (device+0x478), releases the old buffer (obs.) |
| 0x8003F410 | `gpu_hand_over_display` | | GPU | waits for the GPU, copies the display surface device+0x2168 with 0x8003EBC0 into a new surface (0x80041490) and calls 0x8003045C with it; not needed: in the animation called by the after-image only (GPU.md 2.3) (obs.) |
| 0x8003F530 | `gpu_buffer_create_cached` | | GPU | buffer as a resource 0x01010001 with the data directly behind it from 0x80056AC5 (obs.) |
| 0x8003F570 | `gpu_vertex_buffer_create` | | GPU | vertex buffer with memory from 0x8001DC6C (obs.) |
| 0x8003F5D0 | `gpu_buffer_lock` | | GPU | address of a buffer plus offset; without the switch 0x10 method 0x1710 first, without 0xA0 waiting through 0x80042920 (obs.) |
| 0x8003F620 | `gpu_draw_arrays` | | GPU | draws without indices: what was noted, `SET_BEGIN_END`, `DRAW_ARRAYS`, `SET_BEGIN_END` 0 (obs.) |
| 0x8003F6C0 | `gpu_draw_indexed` | | GPU | draws with indices: what was noted, `SET_BEGIN_END`, `ARRAY_ELEMENT16/32`, `SET_BEGIN_END` 0 (obs.) |
| 0x8003FF40 | `gpu_program_create` | | GPU | creates a program object from a vertex program and an attribute list (GPU.md 3) (obs.) |
| 0x80040080 | `gpu_set_stream` | | GPU | notes buffer and stride of a data stream until drawing (obs.) |
| 0x80040320 | `gpu_program_release` | | GPU | counts the program object (handle − 1) down and releases it at 0; tear-down only (obs.) |
| 0x80040440 | `gpu_set_program` | | GPU | loads and starts a vertex program or chooses fixed function (handle without bit 0) (obs.) |
| 0x80040520 | `gpu_set_constants` | | GPU | `SET_TRANSFORM_CONSTANT_LOAD` 0x60 + number, then four values per constant (obs.) |
| 0x800405C0 | `gpu_level_desc` | | GPU | jump to 0x80042EE0: description of a level (obs.) |
| 0x800405D0 | `gpu_texture_surface` | | GPU | surface of a level of a texture through 0x80042FA0 and 0x80041360 (obs.) |
| 0x80040620 | `gpu_texture_level_addr` | | GPU | address of a level of a texture through 0x800431D0 (obs.) |
| 0x80040650 | `gpu_cube_surface` | | GPU | surface of a face and level of a cube texture through 0x80042FA0 and 0x80041360 (obs.) |
| 0x80040770 | `gpu_available` | | GPU | delivers 1 (obs.) |
| 0x800409F0 | `gpu_resource_release` | | GPU | counts a resource down and releases it at 0, for kind 5 the connected one too (obs.) |
| 0x80040AB0 | `gpu_clear` | | GPU | clears target and depth by mask: `SET_COLOR_CLEAR_VALUE`, `SET_ZSTENCIL_CLEAR_VALUE`, `CLEAR_SURFACE` (obs.) |
| 0x80040E80 | `gpu_combiners_create` | | GPU | object of 0xFC bytes: counter 1, pointer, 0x3C words of combiner block (GPU.md 4) (obs.) |
| 0x80040ED0 | `gpu_combiners_release` | | GPU | counts a combiner object down and releases it at 0; tear-down only (obs.) |
| 0x80040EF0 | `gpu_set_combiners` | | GPU | sends a combiner block or switches to combiners from the stages (0) (GPU.md 4, 5) (obs.) |
| 0x800410B0 | `gpu_set_combiner_constants` | | GPU | colours packed into the factor registers after the assignment of the block, constant 0 also to `SET_EYE_VECTOR` (GPU.md 4.2) (obs.) |
| 0x80041340 | `gpu_cube_level_addr` | | GPU | address of a face and level of a cube texture through 0x800431D0 (obs.) |
| 0x8004E13A | `snd_device_release` | | Audio | releases the sound device, call through a jump table (this - 8) (obs.) |
| 0x8004E150 | `snd_voice_release` | | Audio | releases a voice, call through a jump table (this - 0x1C) (obs.) |
| 0x8004EB1D | `snd_volumes` | `volumes` | Audio | eight attenuations max(0, -(level + mix share)) * 64 / 100, at most 0xFFF, packed into three words (obs.) |
| 0x8004EE84 | `snd_env_filter` | `envelope_block` | Audio | envelope block kind 0: initial state, release, pitch scale, ENV1, filter scale, ENVF into the state (obs.) |
| 0x8004EF2D | `snd_env_amp` | `envelope_block` | Audio | envelope block kind 1: initial state, release, ENV0, ENVA into the state (obs.) |
| 0x8004EFC6 | `snd_filter_write` | in `voice_call` | Audio | filter mode and values into the state, then methods 0x318, 0x374, 0x378 (obs.) |
| 0x8004F763 | `gp_scratch_init` | (reference only) | Audio | creates the management of the scratch memory of the GP (device, kind) (obs.) |
| 0x8004F7A5 | `gp_scratch_pages` | (reference only) | Audio | creates the page table of the scratch memory, sets the registers 0xFE8020DC/0xFE802048 and 0xFE8020D4/0xFE802040 (obs.) |
| 0x8004F852 | `gp_scratch_copy` | (reference only) | Audio | copies data to an offset in the scratch memory of the GP (obs.) |
| 0x8004F8A1 | `gp_program_load` | (reference only) | Audio | GP program 0x80053568, length 0x1894, to offset 0 of the scratch memory (obs.) |
| 0x80050819 | `snd_volume_write` | in `voice_call` | Audio | per hardware voice: method 0x2F8 (voice), 0x2FC = 1, attenuations from 0x8004EB1D as 0x360, 0x364, 0x368, 0x2FC = 0 (obs.) |
| 0x800508F9 | `snd_pitch_write` | in `voice_call` | Audio | pitch as method 0x37C, value in the upper halfword (obs.) |
| 0x800509AA | `snd_env_write` | in `voice_call` | Audio | envelope block by kind to 0x8004EE84 or 0x8004EF2D, then methods 0x318, 0x308-0x314, 0x36C (obs.) |
| 0x80050FA4 | `snd_filter` | | Audio | thunk: this - 0x1C, on to 0x8004EFC6 (obs.) |
| 0x80051059 | `snd_voice_state_init` | `LEVEL_SHIFT` in `volumes` | Audio | creates the state of a voice: flags to +0xC; level deduction +0x1C = 600, except flags & 0x82010; initial level +0x18 = -deduction (obs.) |
| 0x8005112E | `snd_pitch_set` | in `voice_call` | Audio | pitch into the state +0x14, then 0x800508F9 (obs.) |
| 0x80051164 | `snd_level_set` | in `voice_call` | Audio | level minus state +0x1C into the state +0x18, then 0x80050819 (obs.) |
| 0x8005119D | `snd_env_set` | in `voice_call` | Audio | envelope block to 0x800509AA (obs.) |
| 0x800511CD | `snd_mix_set` | in `volumes` | Audio | mix shares by bit mask into the state from +0x24, then 0x80050819 (obs.) |
| 0x8005125A | `snd_voice_format_init` | | Audio | passes word +4 of the format block through to 0x80051059 as flags (obs.) |
| 0x800518B6 | `snd_level` | | Audio | thunk: this - 0x1C, on to 0x80051164 (obs.) |
| 0x800518D2 | `snd_pitch` | | Audio | thunk: this - 0x1C, on to 0x8005112E (obs.) |
| 0x800518EE | `snd_env` | | Audio | thunk: this - 0x1C, on to 0x8005119D (obs.) |
| 0x8005190A | `snd_mix` | | Audio | thunk: this - 0x1C, on to 0x800511CD (obs.) |
| 0x80051F2E | `snd_play_write` | in `voice_call` | Audio | methods 0x304, 0x3DC (last index), 0x3A4 (loop start); start through the jump table if the voice is not running (obs.) |
| 0x80052307 | `snd_release_impl` | in `voice_call` | Audio | releases a voice; with the flag 0x80051F2E before it (obs.) |
| 0x80052700 | `snd_buffer` | | Audio | thunk: this - 0x1C; buffer address and length in bytes (obs.) |
| 0x80052975 | `snd_loop_region` | | Audio | thunk: this - 0x1C; loop range start and length (obs.) |
| 0x80052995 | `snd_position` | | Audio | thunk: this - 0x1C; play position (obs.) |
| 0x80052C04 | `snd_stop` | | Audio | thunk: this - 0x1C; flags & 3 not 0 means release instead of stop (obs.) |
| 0x80052DB0 | `snd_play` | | Audio | thunk: this - 0x1C; bit 0 of the last argument is the loop (obs.) |
| 0x80053302 | `snd_device_open` | | Audio | opens the sound device or fetches the existing one; also called from 0x800579F4 (obs.) |
| 0x8005339F | `snd_voice_create` | | Audio | creates a voice with a format block, through 0x80053302 and 0x80052951 (obs.) |
| 0x80055E75 | `kernel_wait_anim_then_poll` | | Overview | waits through 0x80055F0C, then reads device 0x20 register 3 through 0x80014645 until (value & 0x70) is neither 0 nor 0x50; at 0x60 a different follow-up call (obs.) |
| 0x80055EE4 | `anim_thread_join` | | Timing | waits for the thread of the animation (handle 0x8003AEBC) and releases it (obs.) |
| 0x80055F0C | `anim_thread_wait_or_event` | | Overview | waits for the thread (handle 0x8003AEBC) or the event 0x8006F03C, whichever comes first (obs.) |
| 0x80055F5E | `mem_alloc_low` | | Overview | memory through 0x8001DC6C with the highest address 0x3FFB000, alignment as the argument, protection 0x404, Textures.md 4.4 (obs.) |
| 0x80055F7A | `buffer_free_jump` | | Overview | jump to 0x8001E331; releases the buffers from 0x80055F5E page by page, Textures.md 4.4 (obs.) |
| 0x80055F7F | `anim_thread_entry` | | Timing | entry point of the thread: calls 0x800579F4, then 0x80021B78(0) (obs.) |
| 0x80055F8C | `fpu_to_int64` | | Overview | floating point to 64-bit integer with truncation; also used by the kernel (obs.) |
| 0x80055FBA | `anim_thread_start` | | Timing | stores the start mode to 0x8003AEB8, starts the thread with the entry point 0x80055F7F (obs.) |
| 0x80055FF8 | `mem_alloc_zeroed` | | Overview | fetches memory through 0x80012E11 (tag 0x4B443344) and zeroes it (obs.) |
| 0x8005602B | `post_image_vb_create` | | Overview | after-image: vertex buffer of 0x90 bytes to 0x8006F024, filled with 0x24 doublewords from 0x80065618; not needed: the after-image draws nothing at start-up (Overview.md, After-image) (obs.) |
| 0x8005607F | `post_image_gray` | | Overview | after-image: value in st0 times 15 plus 0.5 as a byte v, result 0xFFvvvvvv; not needed: the after-image draws nothing at start-up (Overview.md, After-image) (obs.) |
| 0x800560C6 | `post_image_texture_decode` | | Overview | after-image: texture with 100 pixels per line to 0x8006F020, unpack run lengths from 0x8006F01C (length 0x8006F028); not needed: the after-image draws nothing at start-up (Overview.md, After-image) (obs.) |
| 0x800561A3 | `post_image_release` | | Overview | after-image: release the buffer 0x8006F01C, detach texture and vertex buffer; not needed: the after-image draws nothing at start-up (Overview.md, After-image) (obs.) |
| 0x800561D1 | `post_image_draw` | | Overview | after-image: build buffer and texture, set 14 states and 11 stage states, draw, release, 0x8003F410; without data (0x8006F01C = 0, as at start-up) back at once (obs.) |
| 0x800565F3 | `post_image_submit` | | Overview | called by the kernel: copies up to 0x1000 bytes of run-length data to 0x8006F01C, sets the event 0x8006F02C (obs.) |
| 0x80056657 | `post_image_show` | | Overview | from the tear-down: sets the event 0x8006F03C, waits for 0x8006F02C, draws the after-image (obs.) |
| 0x8005667A | `vec3_mul_mat3` | `v3_mul_m3` | Overview | vector times the 3x3 part of a 4x4 matrix, row vector (obs.) |
| 0x800566CF | `mat4_mul` | `m4_mul` | Overview | product of two 4x4 matrices (obs.) |
| 0x800568E5 | `vec3_length_sq` | in `v3_len`, `spawn` | Overview | square of the length of a vector (obs.) |
| 0x8005690A | `vec3_length` | `v3_len` | Overview | length of a vector through 0x800568E5 and 0x80057F42 (obs.) |
| 0x8005691F | `vec3_dist_sq` | in `camera_track` | Overview | square of the distance of two points (obs.) |
| 0x80056950 | `vec3_dist` | in `camera_track` | Overview | distance of two points (obs.) |
| 0x80056969 | `angle_from_sin_cos` | `angle_sin_cos` | Camera | angle from sine and cosine with the limit cases ±1, through 0x80057E2D and 0x80058039 (obs.) |
| 0x80056A60 | `anim_rand_unit` | `u01` | Timing | random number in [0, 1) from the generator at this+0x68C (obs.) |
| 0x80056A82 | `anim_rand_signed` | `pm1` | Timing | random number in [-1, 1) from the generator at this+0x68C (obs.) |
| 0x80056AAA | `bus_dev20_write` | | Overview | writes register and value to device 0x20 through 0x80014723; 0x800579F4 uses register 0xB (obs.) |
| 0x80056AC0 | `mem_free_jump` | | Overview | jump to 0x80012FC8, counterpart of 0x80012E11 (obs.) |
| 0x80056AC5 | `mem_alloc_zeroed_jump` | | Overview | jump to 0x80055FF8 (obs.) |
| 0x80056ACA | `anim_construct` | | Overview | constructor of the animation object; initial value of t, generator, sub-objects, bit 16 from 0x8002FFDE(6) to this+0x181E (obs.) |
| 0x80056D0B | `anim_draw` | `frame_build` | Overview | draws a frame: clear, sub-objects +0x968, +0xB5C, +0x57C, +0x17EC depending on the switches, then 0x8003E550 (obs.) |
| 0x80056E03 | `anim_destroy` | | Overview | tear-down: sub-objects, textures and GPU objects, after-image 0x80056657, device +0x6AC (obs.) |
| 0x80056EC1 | `gpu_create_with_decl` | | GPU | creates a GPU object from data and a list through 0x8003FF40, returns the handle (obs.) |
| 0x80056EDC | `gpu_create_from_data` | | GPU | creates a GPU object from data from argument+4 through 0x80040E80, returns the handle (obs.) |
| 0x80056EF6 | `pulse_table_build` | `pulse_table` | Timing | makes the pulse table this+0x30, 12 x (t0, w, a) (obs.) |
| 0x80057011 | `pulse_sum` | `pulses` | Timing | sum of the cosine pulses at time t (obs.) |
| 0x800570A3 | `mat4_rot_x` | `rot_x` (Logo.c) | Overview | rotation matrix about the x axis (obs.) |
| 0x80057127 | `mat4_rot_z` | | Overview | rotation matrix about the z axis (obs.) |
| 0x800571AD | `anim_update` | `update`, `path_time`, `objects`, `camera_track`, `frame_build` (steps U) | Timing | update: t, ramp, pulses, x18..x24, camera, sub-objects; repetition at t >= 8 (obs.) |
| 0x8005768C | `anim_setup` | `xb_init`, `texture_make` (CPU textures only) | Overview | set-up with 640, 480: textures, GPU objects, camera, sub-objects, initial value of tick (obs.) |
| 0x80057949 | `anim_frame` | `timing_frame` | Timing | one frame: dt from tick, update, draw (obs.) |
| 0x800579B8 | `anim_loop` | | Timing | frame loop until t >= 8.0, sound before and after it in the long version only (obs.) |
| 0x800579F4 | `anim_main` | | Overview | entry point of the animation on the object 0x8006F050 (obs.) |
| 0x80057A52 | `device_construct` | | GPU | sub-object +0x6AC: two doublewords to 0 (obs.) |
| 0x80057A5A | `device_setup` | | GPU | sub-object +0x6AC: set up the graphics with width and height, stage states, black picture, sets 0x8007087C (obs.) |
| 0x80057B96 | `device_teardown` | | GPU | sub-object +0x6AC: 0x8003E7D0, clears 0x8007087C (obs.) |
| 0x80057BA9 | `atan_sse` | `sse_atan` | Overview | arc tangent in SSE with a continued fraction through rcpss, for absolute values from 1 on through the reciprocal (obs.) |
| 0x80057E2D | `asin_approx` | `sse_asin` | Overview | arc sine: series with coefficients from 0x80066030, from 0.7071 on through 0x80057BA9 (obs.) |
| 0x80057F42 | `sqrt_sse` | `sse_sqrt` | Overview | square root through rsqrtss with a Newton step (obs.) |
| 0x80057F89 | `fabs32` | in `camera_track` | Overview | absolute value of a float32 (obs.) |
| 0x80057F9D | `sin_sse` | `sse_sin` | Overview | sine as a polynomial in SSE (obs.) |
| 0x80058039 | `acos_approx` | `sse_acos` | Overview | arc cosine as the value at 0x800128C8 minus 0x80057E2D (obs.) |
| 0x8005808F | `mat4_transpose` | in `logo_draw` | Overview | transposes a 4x4 matrix (obs.) |
| 0x800580F8 | `mat4_identity` | `m4_ident` | Overview | identity matrix (obs.) |
| 0x8005814E | `vec3_dot` | in `step` | Overview | dot product (obs.) |
| 0x8005816D | `vec3_normalize` | `v3_norm` | Overview | normalises a vector to length 1 (obs.) |
| 0x80058196 | `vec3_scale` | `v3_scale` | Overview | vector times number (obs.) |
| 0x800581B9 | `vec3_sub` | `v3_sub` | Overview | difference of two vectors (obs.) |
| 0x800581E0 | `vec3_madd` | `v3_madd` | Overview | a = a + b * s (obs.) |
| 0x8005820E | `vec3_length_approx` | `len_approx` | Overview | approximate length from the sorted absolute values of the components (obs.) |
| 0x80058290 | `vec3_normalize_approx` | `norm_approx` | Overview | normalises with 0x8005820E if the length reaches the value at 0x800128F8 (obs.) |
| 0x800582C0 | `vec3_add` | `v3_add` | Overview | sum of two vectors (obs.) |
| 0x800582E7 | `blob_rand_unit` | `unit` | Blob | random number k * 2^-16 in [0, 1), k = lower 16 bits of a step of the generator 0x800708BC (obs.) |
| 0x80058308 | `blob_rand_signed` | `signed1` | Blob | random number k * 2^-15 - 1 in [-1, 1) from the generator 0x800708BC (obs.) |
| 0x8005832F | `blob_destroy` | (reference only) | Blob | tear-down of sub-object +0xB5C: buffers +0x00 to +0x18, memory +0x1C, GPU objects +0x34 to +0x40 (obs.) |
| 0x800583BF | `blob_draw` | `blob_draw` | Blob | draws the glowing blob: glow surface (size and alpha from x20), deformed mesh, the small mesh per drop; constants and colours (obs., run c3_ko_b5c) |
| 0x80058B2C | `blob_mesh_reset` | `blob_deformed` (`flat`) | Blob | writes the unit vectors +0x1C with w = 1 into the buffer +0x14, six faces (obs.) |
| 0x80058B93 | `blob_mesh_deform` | `blob_deformed` | Blob | per cube face and vertex: height sum R*h*g^2 to w, directions p - gradient approximately normalised to xyz, bulges backwards (obs.) |
| 0x80058D1F | `blob_weighted_center` | `blob_weighted_center` | Blob | centre from blob centre and drops, weighted with 1/distance^2 to a point that the caller puts on the stack as three float32 (ebp+0x10); second output x20 * (4 w0 + sum wi) / sum w, read by no caller (obs.) |
| 0x80058DF2 | `cube_sphere_build` | `sphere` | Blob | sphere mesh of six cube faces with n/2 divisions, normalised; vertex and index buffer, optionally a copy of the vectors (obs.) |
| 0x80058FFC | `blob_bulge_face_mask` | `face_mask` | Blob | bit f of the mask +0x1C: cube face f lies within reach (dir -+ size against 0.57735) (obs.) |
| 0x800590A6 | `blob_drop_update` | `drop_update` | Blob | drop: stretch as a spring with a dead zone, displacement sharpened sine times width times fade(t), position = centre + direction times displacement (obs.) |
| 0x80059270 | `blob_construct` | `blob_start` | Blob | constructor of +0xB5C: 32 bulges without drops, 8 drops with stretch 1, generator 0x76543210, colour, centre, scale, radius 2.3 (obs.) |
| 0x80059320 | `blob_bulge_spawn` | `spawn` | Blob | bulge anew: direction, size, start, height; without a drop a frequency, otherwise create the drop anew (obs.) |
| 0x800595B6 | `blob_bulge_step` | `step` | Blob | updates a bulge: follow a drop, height as half a sine wave, then create anew; returns whether a drop was added (obs.) |
| 0x80059789 | `blob_update` | `blob_update` | Blob | update of +0xB5C from t = 0.6: drops, bulges, deformation (obs.) |
| 0x80059868 | `blob_restart` | `blob_restart` | Blob | creates all 32 bulges anew at t = -0.3, the first eight with drops, and fills both buffers undeformed (obs.) |
| 0x800598E6 | `blob_setup` | `blob_start` | Blob | set-up of +0xB5C: sphere meshes with n = 8 and 32, two buffers, blob_restart, four GPU objects (obs.) |
| 0x80059A2E | `vec4_mul_mat4` | `v4_mul_m4` | Overview | 4-vector times 4x4 matrix (obs.) |
| 0x80059ABB | `mat4_diag3` | in `fx_draw` | Overview | matrix with the three arguments on the diagonal 0, 5, 10, otherwise 0, element 15 = 1 (obs.) |
| 0x80059B17 | `fx_construct` | | Effects | constructor of +0x17EC: ten doublewords to 0 (obs.) |
| 0x80059B38 | `fx_destroy` | | Effects | tear-down of +0x17EC (obs.) |
| 0x80059BCD | `fx_draw` | `fx_draw`, `frame_build` | Effects | draws veil and glow, sub-object +0x17EC (obs., run c3_ko_17ec) |
| 0x8005A14F | `fx_noise_textures` | `noise_seed`, `texture_make` | Effects | makes three noise textures 256 x 256 through 0x80060DDC, initial value from the generator at 0x8006F6DC, Textures.md 4.8 (obs.) |
| 0x8005A194 | `fx_render_offscreen` | `fx_draw`, `frame_build` | Effects | draws the chamber into the target texture of +0x17EC and restores the target (obs.) |
| 0x8005A41D | `fx_setup` | `fx_quads` (the quads only) | Effects | set-up of +0x17EC: two textures and a depth surface 1024 x 512, two quads, GPU objects (obs.) |
| 0x8005A618 | `fx_update` | in `frame_build` | Effects | update of +0x17EC, calls 0x8005A194 (obs.) |
| 0x8005A620 | `rand_below` | `rand_below` | Overview | generator step, result (s' * n) >> 32 (obs.) |
| 0x8005A645 | `rand_step` | `rng_step` | Timing | step of the pseudo-random generator: s' = ror(s,13) - (s-11) (obs.) |
| 0x8005A662 | `vec3_cross` | `v3_cross` | Overview | cross product (obs.) |
| 0x8005A69F | `campath_key` | in `path_eval` | Camera | key i of the chosen camera path: from the table 0x800708C8 or one of the eight own keys (obs.) |
| 0x8005A6CB | `campath_select` | `camera_start`, `own_keys` | Camera | chooses a camera path (argument or at random), appends eight keys 5.2 to 6.0 outwards, tangents of all keys from cut and split (obs.) |
| 0x8005AC32 | `campath_eval` | `path_eval` | Camera | camera path at time s: Hermite curve with adjusted segment length for eye and target, blend of the target to the look-at point, switches this+0xD and +0xE (obs.) |
| 0x8005AFB6 | `campath_construct` | `camera_start` | Camera | constructor of +0x1594: 0x15 entries from 0x80070DB8 to 0x800708C8, then 0x8005A6CB (obs.) |
| 0x8005B061 | `camera_construct` | | Camera | constructor of +0x6E8: two switches to 0 (obs.) |
| 0x8005B06A | `camera_view_proj` | | Camera | product of view and projection matrix, cached at +0x7C (obs.) |
| 0x8005B093 | `mat4_view_toward` | `camera_view_toward` | Camera | view matrix: axis 2 = norm(arg. 2 - arg. 1), axis 0 from the cross product with arg. 3, translation from arg. 1 (obs.) |
| 0x8005B18C | `mat4_perspective` | `camera_perspective` | Camera | projection matrix from opening angle, aspect factor, near and far plane (obs.) |
| 0x8005B22E | `mat4_inverse_rigid` | `m4_inverse_rigid` | Overview | inverse of a matrix of rotation and translation (obs.) |
| 0x8005B2C0 | `camera_set_view` | `camera_at` | Camera | sets eye, target, up vector, view matrix +0xFC and its inverse +0xBC (obs.) |
| 0x8005B318 | `camera_set_projection` | `camera_start` | Camera | sets the projection matrix +0x13C and stores its parameters (obs.) |
| 0x8005B367 | `camera_set_view_matrix` | in `face_view` | Camera | sets the view matrix directly and its inverse, Chamber.md 9 (obs.) |
| 0x8005B398 | `decode_i16_scaled` | `logo_mesh` (`unpack`) | Logo | n x 3 int16 to float32: i * factor + offset, one pair for all axes; for mesh E (obs.) |
| 0x8005B3D4 | `decode_vertex5` | `logo_mesh` (`unpack`) | Logo | n vertices from 5 x int16: x, y, z with one, u, v with a second pair of factor and offset (obs.) |
| 0x8005B473 | `decode_delta16_tilde` | `unpack_indices` | Logo | index list from differences; byte 0x7E introduces a 2-byte difference (big-endian) (obs.) |
| 0x8005B4D1 | `logo_decode` | `logo_mesh` | Logo | unpacks the seven vertex and index lists at 0x80071008-0x80073FB7 to logo+0xD8..+0x10C (obs.) |
| 0x8005B712 | `logo_build_meshes` | `logo_mesh`, `texture_make` | Logo | vertex buffers of the meshes A to D, two gradient textures and a radial texture; v near 1 in mesh C becomes -1 (obs.) |
| 0x8005B934 | `logo_build_mesh_e` | `logo_mesh`, `logo_start` | Logo | vertex buffer of the lettering (mesh E, 0x9C vertices), matrix +0x60 = identity, +0x4C = 0 (obs.) |
| 0x8005B9A8 | `logo_build_quads` | `logo_mesh`, `logo_start`, `texture_make` (TM) | Logo | buffers of the two TM quads, texture 16 x 16 from 0x80073FD0, opacity +0xC4 = 0 (obs.) |
| 0x8005BACE | `logo_destroy` | | Logo | tear-down of +0x57C: twelve GPU resources, 14 memory blocks, two GPU objects (obs.) |
| 0x8005BBEE | `logo_setup` | `logo_start`, `logo_mesh` | Logo | set-up of +0x57C: unpack, buffers, GPU objects from 0x80066770 and 0x80066868 (obs.) |
| 0x8005BC63 | `logo_draw` | `logo_draw` (values only) | Logo | draws the X in space (meshes A-D) or as a texture, plus TM and lettering; sub-object +0x57C (obs., run c3_ko_57c) |
| 0x8005C366 | `logo_render_to_texture` | in `logo_update` (phase only) | Logo | draws the logo into a texture 1024 x 1024 when the phase t >= 6.25 changes, first at t >= 6 (obs.) |
| 0x8005C4A5 | `logo_update` | `logo_update` | Logo | update of +0x57C from t = 6: position of the lettering from the keys 0x80073FB8, opacity of the TM marks, texture (obs.) |
| 0x8005C665 | `quat_to_mat4` | `quat_to_m4_row` | Overview | rotation matrix from a quaternion (obs.) |
| 0x8005C790 | `quat_to_mat4_t` | `quat_to_m4` | Overview | rotation matrix from a quaternion, transposed (obs.) |
| 0x8005C8BB | `vec3_transform` | `v3_transform` | Overview | point times 4x4 matrix with translation (obs.) |
| 0x8005C919 | `vec4_dot` | in `quat_slerp` | Overview | dot product in four components (obs.) |
| 0x8005C940 | `float_near` | in `quat_slerp` | Overview | absolute value of the difference at most the value at 0x800129A0 (obs.) |
| 0x8005C968 | `prim_group_construct` | | Chamber | group of primitives: seven doublewords to 0 (obs.) |
| 0x8005C97F | `prim_group_release` | | Chamber | group of primitives: release buffers and memory (obs.) |
| 0x8005C9DC | `prim_gen_sphere` | `gen_sphere` | Chamber | vertices and strip indices of a sphere (obs.) |
| 0x8005CBC8 | `prim_gen_cylinder` | `gen_cylinder` | Chamber | vertices and indices of a cylinder with caps (obs.) |
| 0x8005CF51 | `prim_gen_cone` | `gen_cone` | Chamber | vertices and indices of a frustum with two radii (obs.) |
| 0x8005D398 | `prim_gen_box` | `gen_box` | Chamber | vertices and indices of a box, six faces of four corners; the face x = -0.5 carries (0,-1,0) as its third vector (obs.) |
| 0x8005D74F | `prim_gen_torus` | `gen_torus` | Chamber | vertices and strip indices of a ring (obs.) |
| 0x8005D99D | `prim_place_sphere` | `place` | Chamber | places a sphere: size, position from int16, references to motion tracks (obs.) |
| 0x8005DBB3 | `prim_count_sphere` | `form_counts` | Chamber | vertex and index count of a sphere (obs.) |
| 0x8005DC04 | `prim_count_cylinder` | `form_counts` | Chamber | vertex and index count of a cylinder (obs.) |
| 0x8005DC6C | `prim_count_cone` | `form_counts` | Chamber | vertex and index count of a frustum (obs.) |
| 0x8005DCD6 | `prim_count_box` | `form_counts` | Chamber | vertex and index count of a box: 24 and 36 (obs.) |
| 0x8005DD04 | `prim_count_torus` | `form_counts` | Chamber | vertex and index count of a ring (obs.) |
| 0x8005DD58 | `prim_count_lathe` | `form_counts` | Chamber | vertex and index count of a lathe from a profile (obs.) |
| 0x8005DDE2 | `decode_quats` | `decode` | Chamber | quaternions from 3 x int16, fourth component as a root (z unrounded), sign from a bit mask (obs.) |
| 0x8005DEB1 | `decode_positions` | `decode` | Chamber | points from 3 x int16 times factor plus offset per axis (obs.) |
| 0x8005DF27 | `decode_delta16_7f` | `expand` | Chamber | index list from differences; byte 0x7F introduces a 2-byte difference (obs.) |
| 0x8005DF9B | `chamber_destroy` | | Chamber | tear-down of +0x968 (obs.) |
| 0x8005E051 | `quat_from_axis_angle` | in `gen_lathe` | Overview | quaternion from axis and angle (obs.) |
| 0x8005E0B1 | `quat_slerp` | `quat_slerp` | Overview | spherical interpolation of two quaternions, linear if almost equal (obs.) |
| 0x8005E210 | `prim_gen_lathe` | `gen_lathe` | Chamber | vertices and strip indices of a lathe from a profile with crease points (obs.) |
| 0x8005E6E6 | `prim_place_cylinder` | `place` | Chamber | places a cylinder: radius, double height, rotation, position (obs.) |
| 0x8005E8B4 | `prim_place_unscaled` | `place` | Chamber | places a frustum or lathe without scale (obs.) |
| 0x8005EA78 | `prim_place_box` | `place` | Chamber | places a box with three edge lengths (obs.) |
| 0x8005EC3E | `prim_place_torus` | `place` | Chamber | places a ring with a uniform size (obs.) |
| 0x8005EE08 | `prim_group_draw` | `chamber_world`, `chamber_part_draw`, `chamber_part_prims`, `group` | Chamber | draws a group in one of four modes; world matrix per part S * (L * rotation track) with the position track in row 3 (obs.) |
| 0x8005F3DB | `prim_group_build` | `chamber_mesh` | Chamber | builds a group: count, create buffers, generate, place (obs.) |
| 0x8005F7B8 | `chamber_build_groups` | `groups` in `Chamber.c` | Chamber | builds the six groups of the chamber in one detail level (obs.) |
| 0x8005F897 | `chamber_draw` | `chamber` | Chamber | draws the chamber around the blob, sub-object +0x968 (obs., run c3_ko_968) |
| 0x8005F9F1 | `chamber_update` | `chamber_at` | Chamber | update of +0x968: 29 rotations (slerp) and 32 positions (linear) at u = (t - 0.85) / 4.5, 28 segments; detail level from t >= 5.2 (obs.) |
| 0x8005FC1C | `prim_group_draw_depth` | in `group` | Chamber | draws a group in mode 2 (depth only) (obs.) |
| 0x8005FC3C | `prim_group_draw_side` | in `group` | Chamber | draws a group in mode 3 for one side (obs.) |
| 0x8005FC65 | `chamber_setup` | `chamber_start` | Chamber | set-up of +0x968: unpack tracks, twelve groups, textures 512 x 512 (obs.) |
| 0x8005FDD6 | `chamber_draw_depth` | `chamber` | Chamber | draws all six groups into the depth only, Effects.md 3 (obs.) |
| 0x8005FDF9 | `chamber_light_view` | `chamber_light_view`, `chamber_light_camera` | Chamber | view from the weighted blob centre downwards (side 0) or upwards (side 1), 120 degrees, into a texture 512 x 512; matrix to +0x1A4 or +0x164 (obs.) |
| 0x800600F5 | `chamber_prepass` | `xb_blob_light_view`, `xb_blob_light_camera`, `frame_build` | Chamber | saves the camera, calls 0x8005FDF9 for both sides, restores it (obs.) |
| 0x8006013C | `seq_event_post` | in `audio_start` | Audio | puts a byte into the ring 0x800A7912 with 16 places (obs.) |
| 0x80060166 | `seq_event_take` | in `seq_tick` | Audio | takes a byte out of the ring 0x800A7912 (obs.) |
| 0x8006018A | `seq_tracks_init` | in `audio_start` | Audio | chains the free track records from 0x80078550, fetches the head of the active list (obs.) |
| 0x80060232 | `seq_channel_select` | in `seq_step` (layer 0) | Audio | channel record 0x80083428 + (layer * number of channels + channel) * 0x38 (obs.) |
| 0x80060252 | `seq_run` | `seq_tick` | Audio | sequence: events from the ring through the table 0x80066A70, due tracks through the command table 0x800669D8 (obs.) |
| 0x8006033C | `seq_init` | `audio_start` | Audio | resets ring and counter, tables 0x80061AC1, tracks 0x8006018A (obs.) |
| 0x8006036C | `normal_to_argb` | `normal_to_argb` | Textures | direction in [-1, 1] to colour 0xFFrrggbb (obs.) |
| 0x800603D8 | `texgen_radial` | `radial` | Textures | square texture with brightness by the distance from the centre through a table with 256 steps (obs.) |
| 0x800605D2 | `texgen_falloff_mips` | `falloff_mips` | Textures | texture with all mip levels, falloff 1 - r² with random noise (obs.) |
| 0x800607A1 | `texgen_gradient` | `gradient` | Textures | texture with a vertical colour gradient between two colours (obs.) |
| 0x800609D1 | `texgen_fractal` | `fractal`, `walk_*` | Textures | height field by continued subdivision with randomness, optionally as a normal texture (obs.) |
| 0x80060DDC | `texgen_fractal_multi` | `fractal_multi` (layer 0 only), `walk_*` | Textures | up to three 8-bit height fields of the same kind in one pass (obs.) |
| 0x800610DD | `texgen_normal_cube` | `normal_cube` | Textures | cube texture whose colour encodes the normalised direction (obs.) |
| 0x8006127E | `strip_index_count` | `strip_count` | Overview | number of strip indices for a grid, divided into blocks of 14 columns (obs.) |
| 0x800612D3 | `strip_index_build` | `strip` | Overview | strip indices for a grid, divided into blocks of 14 columns (obs.) |
| 0x80061418 | `cube_face_view` | `face_view` | Textures | view matrix for face 0 to 5 of a cube (obs.) |
| 0x800614E9 | `texgen_chamber_cube` | `frame_setup` | Textures | draws the chamber at a fixed time into the six faces of a cube texture (obs.) |
| 0x8006165E | `seq_tick` | `seq_tick` | Audio | counts the time 0x800A7958 up and calls 0x80060252; measured every 6 ms (obs.) |
| 0x8006166D | `audio_start` | `xb_audio_init` | Audio | starts the sound: 0x8006033C, timer with the callback 0x8006165E and period 5 (obs.) |
| 0x800616A7 | `audio_stop` | (reference only) | Audio | stops the timer, releases the voices (obs.) |
| 0x800616B8 | `seq_nop` | (reference only) | Audio | returns at once (obs.) |
| 0x800616B9 | `seq_ev_set_byte` | (reference only) | Audio | event: byte from the event record to 0x800A7943 (obs.) |
| 0x800616C7 | `seq_channel_reset` | `channel_reset` | Audio | channel record to its base values, byte +3 to 0x7F (obs.) |
| 0x800616F1 | `seq_channel_init` | (reference only) | Audio | calls the entry of the table 0x8006E950 for a channel (obs.) |
| 0x8006170B | `seq_stop_layer` | `track_release_all` | Audio | ends all tracks of a layer (obs.) |
| 0x80061732 | `seq_stop_channel` | `track_release_channel` | Audio | ends the tracks of a layer on a channel (obs.) |
| 0x80061762 | `seq_stop_all` | in `seq_event` | Audio | ends all tracks and resets all channels (obs.) |
| 0x800617D0 | `seq_ev_start_layer0` | `seq_event` | Audio | event: starts tracks of layer 0 by channel mask (obs.) |
| 0x80061865 | `seq_ev_start_layer1` | (reference only) | Audio | event: starts tracks of layer 1 by channel mask (obs.) |
| 0x800618D9 | `seq_return_1a` | (reference only) | Audio | returns 1 (obs.) |
| 0x800618DF | `voice_pitch_play` | in `voice_call` | Audio | voice of the channel: convert the pitch from 0x800A7954 (0x155 per semitone, 60 is 0), set it and play (obs.) |
| 0x80061952 | `voice_pitch` | in `voice_call` | Audio | voice of the channel: set the pitch only (obs.) |
| 0x80061994 | `voice_stop` | in `voice_call` | Audio | releases the voice of the channel (the envelope decays), only if it is running (obs.) |
| 0x800619B1 | `voice_filter` | in `voice_call` | Audio | voice of the channel: low-pass on, cutoff +0x34 plus 0x8000, resonance +0x36, through 0x80050FA4 (obs.) |
| 0x80061A08 | `voice_set_sample` | in `voice_call` | Audio | voice of the channel: sample description from 0x800A7BB0: two envelope blocks, buffer, loop, position 0 (obs.) |
| 0x80061A91 | `voice_clear_83b68` | (reference only) | Audio | sets 0x80083B68 to 0 (obs.) |
| 0x80061A9E | `noise_lcg` | `noise_table` | Audio | noise generator, linear congruential generator 0x343FD/0x269EC3 (obs.) |
| 0x80061AC1 | `audio_tables_voices` | `audio_table`, `audio_start` | Audio | fills the noise, sine, sawtooth and FM table, converts three 8-bit samples, opens the sound, creates 16 voices, event 1 into the ring (obs.) |
| 0x80061D23 | `audio_release` | (reference only) | Audio | releases voices and sound device (obs.) |
| 0x80061D48 | `voice_level` | in `voice_call` | Audio | voice of the channel: level 200 - 30 * argument in 1/100 dB through 0x800518B6 (obs.) |
| 0x80061D71 | `seq_return_1b` | in `seq_step` | Audio | returns 1 (obs.) |
| 0x80061D75 | `seq_op_note` | in `seq_step` | Audio | command 1: pitch and duration, key the voice on through 0x8006E990 (obs.) |
| 0x80061E54 | `seq_op_rest` | in `seq_step` | Audio | command 0: duration, release the voice through 0x8006EA10 (obs.) |
| 0x80061EC1 | `seq_op_jump` | in `seq_step` | Audio | command: jump to an address (obs.) |
| 0x80061F02 | `seq_op_loop_start` | in `seq_step` | Audio | command: loop start with a counter (obs.) |
| 0x80061F5E | `seq_op_loop_end` | in `seq_step` | Audio | command: loop end, back or on (obs.) |
| 0x80061FB2 | `seq_op_sample` | in `seq_step` | Audio | command 5: set the sample of the channel, volume byte to 0, calls 0x8006EA50 (obs.) |
| 0x80062026 | `seq_op_set_byte2` | in `seq_step` | Audio | command: set byte +2 of the channel, calls 0x8006EAD0 (obs.) |
| 0x800620A2 | `seq_op_add_byte2` | (reference only) | Audio | command: increase byte +2 of the channel, calls 0x8006EAD0 (obs.) |
| 0x8006211B | `seq_op_call` | (reference only) | Audio | command: call a subroutine (obs.) |
| 0x80062164 | `seq_op_return` | (reference only) | Audio | command: return from a subroutine (obs.) |
| 0x8006219E | `seq_op_vol_add` | in `seq_step` | Audio | command 9: increase volume byte +3 of the channel, calls 0x8006EA90 (obs.) |
| 0x80062218 | `seq_op_set_794c` | (reference only) | Audio | command: set the word 0x800A794C, added to the pitch on layer 1 (obs.) |
| 0x8006224A | `seq_op_transpose_add` | in `seq_step` | Audio | command 10: increase transposition +0x32 of the channel (obs.) |
| 0x80062284 | `seq_op_transpose_set` | in `seq_step` | Audio | command 11: set transposition +0x32 of the channel (obs.) |
| 0x800622BE | `seq_op_filter_add` | in `seq_step` | Audio | command 16: increase filter cutoff +0x34, set resonance +0x36, calls 0x8006E910 (obs.) |
| 0x80062324 | `seq_op_filter_set` | in `seq_step` | Audio | command 17: set filter cutoff +0x34 and resonance +0x36, calls 0x8006E910 (obs.) |
| 0x8006238A | `seq_op_glide` | in `seq_step` | Audio | command 12: as command 1, but sets only the pitch through 0x8006E9D0 (obs.) |
| 0x80062469 | `seq_op_wait` | in `seq_step` | Audio | command 13: duration only (obs.) |
| 0x800624BA | `seq_op_set_layer_byte` | (reference only) | Audio | command: set the byte 0x800A7930 + layer (obs.) |
| 0x800624ED | `seq_op_end` | in `seq_step` | Audio | command 15: returns 0; 0x80060252 then aborts the whole pass (obs.) |
| 0x800624F0 | `seq_op_post_event` | (reference only) | Audio | command: put an event into the ring (obs.) |
| 0x8006252A | `seq_op_channel_1` | (reference only) | Audio | command: one byte, calls the channel entry from 0x8006EB10 (obs.) |
| 0x80062567 | `seq_op_channel_2` | (reference only) | Audio | command: two bytes, calls the channel entry from 0x8006EB14 (obs.) |
| 0x800625B7 | `seq_op_skip_byte` | (reference only) | Audio | command: skip one byte (obs.) |
| 0x800625E6 | `seq_track_free` | | Audio | track record from the active list into the free list (obs.) |
| 0x8006261D | `seq_track_alloc` | | Audio | takes a track record from the free list (obs.) |
| 0x80062638 | `seq_track_start` | | Audio | starts a track: pointer, layer, channel, into the active list (obs.) |
| 0x80063CB8 | `timer_program_ch0` | | Kernel | programs timer channel 0, mode 2, divider 1125 (obs.) |
| 0x800AAB46 | `copy_swizzled` | `swz_init` | Textures | copies a row picture into a texture (source, row length, cut-out, destination, w, h, destination point, bytes per texel): unchanged for w ≤ 2 or h ≤ 1, otherwise with interleaved bits of x and y; without row length, cut-out and destination point from 8 × 8 (4 bytes) or 16 × 8 on through 0x800AA652, 0x800AA554, 0x800AA454 (obs.) |

## Data

| Address | Ghidra name | File in `data/` | Subsystem | Description |
|---|---|---|---|---|
| 0x8003A89C | `tick_ms` | – | Timing | tick in ms, counted up by the handler 0x80030E4C (obs.) |
| 0x8003AEB8 | `anim_start_mode` | – | Timing | start mode: not 0 means the short version from t = 7.5 (obs.) |
| 0x8003AEBC | `anim_thread_handle` | – | Timing | handle of the animation thread (obs.) |
| 0x80053568 | `gp_program` | `audio/gp_program.bin` | Audio | program of the GP DSP, 0x625 words of 32 bits, 24 used (obs.) |
| 0x80065470 | `gpu_state_methods` | `gpu/gpu.json` | GPU | head words of the states 0 to 0x51 for 0x8003CCB0 (GPU.md 2) (obs.) |
| 0x800655B8 | `prim_count_table` | – | Chamber | two doublewords a, b per primitive kind: index count = a * primitives + b (obs.) |
| 0x800656B0 | `chamber_comb_relief` | `gpu/gpu.json` | Chamber | combiner block "PSB0" of the chamber with relief (anim+0x94C), GPU.md 4.3 (obs.) |
| 0x800657A8 | `chamber_comb_smooth` | `gpu/gpu.json` | Chamber | combiner block "PSB0" of the smooth chamber (anim+0x948), GPU.md 4.3 (obs.) |
| 0x800658A0 | `fx_comb_depth` | `gpu/gpu.json` | Effects | combiner block "PSB0" for the target texture of the veil (anim+0x950), GPU.md 4.3 (obs.) |
| 0x80065998 | `chamber_vsh_relief` | `gpu/gpu.json` | Chamber | vertex program of the chamber with relief (anim+0x95C), 28 instructions, GPU.md 3.2 (obs.) |
| 0x80065B60 | `chamber_vsh_smooth` | `gpu/gpu.json` | Chamber | vertex program of the smooth chamber (anim+0x958), 28 instructions, GPU.md 3.1 (obs.) |
| 0x80065D28 | `fx_vsh_depth` | `gpu/gpu.json` | Effects | vertex program for the target texture of the veil (anim+0x960), 18 instructions, GPU.md 3.3 (obs.) |
| 0x80065E4C | `timing_constants` | – | Timing | constants pi, 0, 8.0, 2.0, 0.8, 0.6, 0.0 (obs.) |
| 0x80066030 | `asin_factors` | `overview/asin_factors.bin` | Overview | 0x31 float32, factors of the series of 0x80057E2D, read from entry 0x30 backwards to 1 (obs.) |
| 0x800660F8 | `blob_gpu_data_a` | `gpu/gpu.json` | Blob | combiner block "PSB0" of the blob, mesh (blob+0x3C): colour K0 by the angle between normal and view, plus K1 (GPU.md 4.3) (obs.) |
| 0x800661F0 | `blob_gpu_data_b` | `gpu/gpu.json` | Blob | combiner block "PSB0" of the drops (blob+0x40), as 0x800660F8 with alpha from K0 and K2 (GPU.md 4.3) (obs.) |
| 0x800662E8 | `blob_gpu_data_mesh` | `gpu/gpu.json` | Blob | vertex program of the mesh (blob+0x34), 24 instructions: position centre + (k + h) · p, normal, direction to the eye (GPU.md 3.4) (obs.) |
| 0x80066470 | `blob_gpu_data_drop` | `gpu/gpu.json` | Blob | vertex program of the drops (blob+0x38), 16 instructions: stretched small sphere (GPU.md 3.5) (obs.) |
| 0x80066598 | `fx_comb_veil` | `gpu/gpu.json` | Effects | combiner block "PSB0" of the veil (fx+0x20), GPU.md 4.3 (obs.) |
| 0x80066690 | `fx_vsh_veil` | `gpu/gpu.json` | Effects | vertex program of the veil (fx+0x24), 10 instructions, GPU.md 3.6 (obs.) |
| 0x80066770 | `logo_gpu_data_a` | `gpu/gpu.json` | Logo | combiner block "PSB0" for mesh D (logo+0xC8): colour 0 unchanged (GPU.md 4.3) (obs.) |
| 0x80066868 | `logo_gpu_data_b` | `gpu/gpu.json` | Logo | vertex program for mesh D (logo+0xCC), 15 instructions: colour from the constants 4–8 by v (GPU.md 3.7) (obs.) |
| 0x80066984 | `chamber_place_scale` | – | Chamber | factors and offset of the placement positions per axis, 6 float32 up to 0x80066998; behind them those of the track positions up to 0x800669B0 (obs.) |
| 0x800669D8 | `seq_op_table` | – | Audio | pointers to the commands of the tracks, up to 0x80066A6C (obs.) |
| 0x80066A70 | `seq_event_table` | – | Audio | pointers to the event handlers, up to 0x80066AA0 (obs.) |
| 0x80066AC0 | `pcm_a_8bit` | `audio/pcm_80066ac0.raw` | Audio | 0x5540 samples, 8 bits signed (obs.) |
| 0x8006C008 | `pcm_b_8bit` | `audio/pcm_8006c008.raw` | Audio | 0xEB8 samples, 8 bits unsigned, 0x80 is zero (obs.) |
| 0x8006CEC8 | `pcm_c_8bit` | `audio/pcm_8006cec8.raw` | Audio | 0x1A3F samples, 8 bits unsigned, 0x80 is zero (obs.) |
| 0x8006E908 | `seq_header` | `audio/sequence.bin` | Audio | number of track records (30) and of channels (16), 16 bits each (obs.) |
| 0x8006E910 | `seq_channel_tables` | – | Audio | pointer tables per channel, up to 0x8006EB14 (obs.) |
| 0x8006EB18 | `sample_descs` | `audio/sequence.json` | Audio | 11 sample descriptions of 0x14 bytes: data, length in bytes, loop, two envelope blocks (obs.) |
| 0x8006EBF8 | `seq_tracks` | `audio/sequence.json` | Audio | bytecode of the 13 tracks up to 0x8006EFDF (obs.) |
| 0x8006EFE0 | `seq_events` | `audio/sequence.json` | Audio | event records of 8 bytes: handler, channel mask, track list (obs.) |
| 0x8006F01C | `post_image_data` | – | Overview | pointer to the run-length data of the after-image; length at 0x8006F028 (obs.) |
| 0x8006F050 | `anim_object` | – | Overview | animation object; t at +0x10, tick at +0x28, pulses at +0x30 (obs.) |
| 0x8006F5CC | `logo_object` | – | Logo | logo, anim_object + 0x57C: seven meshes, matrix of the lettering +0x60, opacity +0xC4, texture +0xD0 (obs.) |
| 0x8006F738 | `camera_object` | – | Camera | camera, anim_object + 0x6E8: eye +0x58, target +0x64, view +0xFC, projection +0x13C (obs.) |
| 0x8006F8B4 | `light_params` | – | Chamber | light object, anim_object + 0x864: colours +0x04, +0x14, +0x24 for diffuse, specular and ambient share, coefficients of the attenuation +0x54 (Chamber.md 7) (obs.) |
| 0x8006F9B8 | `chamber_object` | – | Chamber | chamber, anim_object + 0x968: twelve groups, unpacked data, track results +0x1E4/+0x1EC (obs.) |
| 0x8006FBAC | `blob_object` | – | Blob | glowing blob, anim_object + 0xB5C: buffers, colour +0x44, centre +0x54, radius +0x6C, 32 bulges +0x70, 8 drops +0x874 (obs.) |
| 0x800705E4 | `campath_object` | – | Camera | camera path, anim_object + 0x1594: eight own keys, frame +0x1E0, look-at point +0x22C (obs.) |
| 0x8007083C | `fx_object` | – | Effects | veil, anim_object + 0x17EC: two quads, three noise textures, two target textures, depth surface, block, program (Effects.md 1) (obs.) |
| 0x8007087C | `device_ready` | – | GPU | not 0 as long as the device of +0x6AC is set up (obs.) |
| 0x800708B8 | `blob_ptr` | – | Blob | pointer to blob_object, set in blob_setup (obs.) |
| 0x800708BC | `blob_rand_state` | – | Blob | state of the second generator (obs.) |
| 0x800708C8 | `campath_keys` | – | Camera | 0x15 unpacked path keys of 0x3C bytes: time, eye, tangent, target, tangent, cut, split (obs.) |
| 0x80070DB8 | `campath_table` | `camera/campath.bin` | Camera | 0x15 keys of the camera paths of 0x1C bytes; byte 0 is the time, 0 begins a path (obs.) |
| 0x80071008 | `logo_body_verts` | `logo/logo.json` | Logo | mesh D, the extruded X: 0x1F5 vertices of 5 x int16 (obs.) |
| 0x800723A0 | `logo_body_indices` | `logo/logo.json` | Logo | mesh D: 0x3F0 indices as differences (obs.) |
| 0x80072AA0 | `logo_rim_verts` | `logo/logo.json` | Logo | mesh A, band on the outline of the X: 0xD2 vertices of 5 x int16 (obs.) |
| 0x800732D8 | `logo_rim_indices` | `logo/logo.json` | Logo | mesh A: 0x150 indices as differences (obs.) |
| 0x80073428 | `logo_wedge_verts` | `logo/logo.json` | Logo | mesh C, triangle above the middle of the X: 0x22 vertices of 5 x int16 (obs.) |
| 0x80073580 | `logo_wedge_indices` | `logo/logo.json` | Logo | mesh C: 0x69 indices as differences (obs.) |
| 0x800735F0 | `logo_plate_verts` | `logo/logo.json` | Logo | mesh B, plate with an X-shaped opening: 0x4D vertices of 5 x int16 (obs.) |
| 0x800738F8 | `logo_plate_indices` | `logo/logo.json` | Logo | mesh B: 0xE1 indices as differences (obs.) |
| 0x800739DC | `logo_tm_x_verts` | `logo/logo.json` | Logo | Q1, TM quad at the X: 4 vertices of 5 x int16, indices at 0x80073A04 (obs.) |
| 0x80073A0C | `logo_tm_text_verts` | `logo/logo.json` | Logo | Q2, TM quad at the lettering: 4 vertices of 5 x int16, indices at 0x80073A34 (obs.) |
| 0x80073A40 | `logo_text_verts` | `logo/logo.json` | Logo | mesh E, lettering: 0x9C vertices of 3 x int16 (obs.) |
| 0x80073DE8 | `logo_text_indices` | `logo/logo.json` | Logo | mesh E: 0x1CE indices as differences (obs.) |
| 0x80073FB8 | `logo_text_keys` | `logo/logo.json` | Logo | two positions of the lettering of 3 x float32, start and end of its motion (obs.) |
| 0x80073FD0 | `logo_tm_texture` | `logo/quad_texture.png` | Logo | texture TM, 16 x 16 texels of 4 bytes, up to 0x800743CF (obs.) |
| 0x800743D0 | `chamber_rot_tracks` | `chamber/scene.bin` | Chamber | 29 index lists of the rotation tracks of 0x5A bytes, differences with the 0x7F escape (obs.) |
| 0x80074E08 | `chamber_pos_tracks` | `chamber/scene.bin` | Chamber | 32 index lists of the position tracks of 0x5A bytes (obs.) |
| 0x80075948 | `chamber_positions` | `chamber/scene.bin` | Chamber | number 320, from 0x80075950 the positions of 3 x int16 (obs.) |
| 0x800760D0 | `chamber_quats` | `chamber/scene.bin` | Chamber | number 413, from 0x800760D8 the quaternions of 3 x int16 (obs.) |
| 0x80076A88 | `chamber_quat_signs` | `chamber/scene.bin` | Chamber | 413 bits: 1 means fourth component positive (obs.) |
| 0x80076AC0 | `chamber_sphere_places` | `chamber/scene.json` | Chamber | 28 placements of spheres of 0x10 bytes (obs.) |
| 0x80076C80 | `chamber_sphere_forms` | `chamber/scene.json` | Chamber | 2 sphere shapes: segments 16, 24 (obs.) |
| 0x80076C88 | `chamber_cylinder_places` | `chamber/scene.json` | Chamber | 47 placements of cylinders of 0x14 bytes (obs.) |
| 0x80077034 | `chamber_cylinder_forms` | `chamber/scene.json` | Chamber | 6 cylinder shapes of 2 bytes: rings, segments (obs.) |
| 0x80077040 | `chamber_box_places` | `chamber/scene.json` | Chamber | 72 placements of boxes of 0x18 bytes (obs.) |
| 0x80077700 | `chamber_torus_places` | `chamber/scene.json` | Chamber | 84 placements of rings of 0x10 bytes (obs.) |
| 0x80077C40 | `chamber_torus_forms` | `chamber/scene.json` | Chamber | 7 ring shapes of 8 bytes: tube radius, segments around axis and tube (obs.) |
| 0x80077C78 | `chamber_cone_places` | `chamber/scene.json` | Chamber | 34 placements of frustums of 0xC bytes (obs.) |
| 0x80077E10 | `chamber_cone_forms` | `chamber/scene.json` | Chamber | 3 shapes of frustums of 0x10 bytes: two radii, height, rings, segments (obs.) |
| 0x80077E40 | `chamber_lathe_places` | `chamber/scene.json` | Chamber | 6 placements of lathes of 0xC bytes (obs.) |
| 0x80077E88 | `chamber_lathe_forms` | `chamber/scene.json` | Chamber | 5 lathe shapes of 0x14C bytes: profile, axis, centre, segments, number of points (obs.) |
| 0x80078550 | `seq_track_pool` | – | Audio | head of the free list, then 30 track records of 0x1C bytes (obs.) |
| 0x80078898 | `pcm_a_16bit` | – | Audio | 0x5540 samples from 0x80066AC0 in 16 bits (obs.) |
| 0x80083328 | `saw_table` | – | Audio | 128 values sawtooth (i - 64) * 512 (obs.) |
| 0x80083428 | `seq_channel_sets` | – | Audio | channel records of 0x38 bytes, two layers of 16 channels (obs.) |
| 0x80083B28 | `voices` | – | Audio | 16 pointers to the voices, one per channel (obs.) |
| 0x80083B70 | `fm_table` | – | Audio | 0x8000 values FM sound (obs.) |
| 0x80093B78 | `sine_table` | – | Audio | 128 values sine (obs.) |
| 0x80093C78 | `pcm_c_16bit` | – | Audio | 0x1A3F samples from 0x8006CEC8 in 16 bits (obs.) |
| 0x800970F8 | `pcm_a_reversed` | – | Audio | 0x5540 samples of 0x80078898 backwards, beginning with the word at 0x80083318 (obs.) |
| 0x800A1B78 | `noise_table` | – | Audio | 0x2000 values noise from 0x80061A9E (obs.) |
| 0x800A5B80 | `pcm_b_16bit` | – | Audio | 0xEB8 samples from 0x8006C008 in 16 bits (obs.) |
| 0x800A7912 | `seq_event_ring` | – | Audio | ring with 16 event bytes; read index 0x800A7910, write index 0x800A7911 (obs.) |
| 0x800A7958 | `seq_time` | – | Audio | 16-bit time of the sequencer, initial value 1 (obs.) |
| 0x800A7980 | `env_blocks` | `audio/envblocks.json` | Audio | 14 envelope blocks of 0x28 bytes (obs.) |
| 0x800A7BB0 | `sample_desc_ptrs` | `audio/seq_pointers.bin` | Audio | 11 pointers to the sample descriptions, not sorted by address (obs.) |
| 0x800A7BDC | `seq_event_lists` | `audio/seq_pointers.bin` | Audio | track lists of the events 1 (12 pointers) and 2 (1 pointer) (obs.) |
| 0x800A7C10 | `seq_event_max` | `audio/seq_pointers.bin` | Audio | highest valid event number, 2 (obs.) |
