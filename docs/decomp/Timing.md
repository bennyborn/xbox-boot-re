# Time keeping

How the animation measures its time and derives its time quantities from it.
Addresses in the unpacked kernel (`Overview.md`). The animation object lies
at 0x8006F050; `this+N` means 0x8006F050 + N in what follows. Evidence: the
vector file `vectors/c3_timing.json`, made by `tools/xemu/vectors_c3.py`
from the runs `c3_timing2` (long version) and `c3_short` (short version).
The model in `vectors_c3.py` is written after this text. It meets every step
bit-exactly in both runs (900 and 65 steps, 0 deviations).

## 1. Time source: a millisecond counter of the kernel

- The timer is programmed in 0x80063CB8: channel 0, mode 2 (`out 0x43` with
  0x34), divider 0x465 = 1125.
- The interrupt handler begins at 0x80030E4C. Ghidra has not assigned it to
  any function. At every interrupt it advances two 64-bit counters by
  0x2710 = 10 000 (at 0x80030E96 and 0x80030EBD) and the doubleword at
  **0x8003A89C** by 1 (0x80030EDC–0x80030EDD).
- Measured: a write watchpoint on 0x8003A89C stops only at 0x80030EE3, the
  instruction after the write, and the value rises by 1 there each time
  (`tools/xemu/watch.py 0x8003a89c --hits 6 --after-flips 30`, run
  `watch_8003a89c`).
- Clock: xemu models the timer with 1 125 001 Hz; the comment there names
  1 125 000 Hz and gives the reason for the deviation of 1 Hz
  (`include/hw/timer/i8254.h`, branch `XBOX`). 1125 cycles are thus 1 ms.
  The step of 10 000 in the 64-bit counter fits if its unit is 100 ns
  (*guessed*, concluded from the ratio only).

The value at 0x8003A89C is called **tick** here, unit 1 ms. In xemu it runs
with QEMU's virtual clock: that stands still while the guest stands at a
breakpoint. So the breakpoints of the measuring runs change the sequence of
frames, but not the computation.

## 2. The course of a frame

The loop 0x800579B8 calls 0x80057949 as long as t < 8.0 holds (comparison
of `this+0x10` with 0x80065E54 = 8.0). 0x80057949 computes:

```
d      = |tick - this.last_tick|              // this+0x28, tick at 0x8003A89C
dt_raw = f32(d * 0.001)                       // 0x800128A0 = 0.001
arg    = f32(dt_raw * factor[this+0x1818] + this.x14)   // factor = {1.0, 0.5, 0.0}
this.x14 = 0
update(arg, dt_raw)                           // 0x800571AD
draw()                                        // 0x80056D0B
this.last_tick = tick
```

The difference is 32 bits wide and signed: `last_tick − tick` (0x8005795C),
absolute value with `cdq`, `xor`, `sub` (0x8005795E–0x80057961), then `fild`
as int32 (0x8005796C). If the counter overflows, say from 0xFFFFFFFE to 3, d
is therefore 5. From a distance of 2³¹ ms on, d would be negative; that
happens in no run. `arg` comes from the unrounded product `d * 0.001` in the
register (`fld st(1)` at 0x80057996), `dt_raw` from the same product, stored
(0x80057992). With `factor` = 1.0 and `this.x14` = 0 both are equal.

`factor` is a local table on the stack (1.0, 0.5, 0.0 at 0x8005797E,
0x80057977, 0x80057983). In both runs `this+0x1818` and `this+0x14` are 0 in
every frame, so `arg = dt_raw`. The set-up sets `this+0x1818` to 0
(0x80056B55). We have found no other writer; the same holds for `this+0x14`.

At the start of 0x800571AD:

```
if arg > 1.0: arg = 0.001        // 0x800571BC, constant 0x8001288C = 1.0
t = t + arg                      // this+0x10
```

A jump of more than a second therefore counts as 1 ms only. That never
happened in the runs; the largest step was 36 ms.

The initial value of `this.last_tick` and `this+0x2C` is set by the set-up
0x8005768C at its end to the current tick (0x80057932–0x8005793B).
`this+0x2C` does not change after that (checked in every step).

## 3. Start, end, short version

- **Initial value.** The constructor 0x80056ACA sets t = 0, or t = 7.5
  (0x40F00000) if the byte at 0x8003AEB8 is not 0 (0x80056B5B–0x80056B6E).
- **Where the byte comes from.** 0x80055FBA stores its argument there
  (0x80055FE0) and starts the thread of the animation (section 5). The
  kernel passes bit 4 of the doubleword at 0x8003B138 in the section
  `STICKY` (0x8001582F–0x8001583D). In our runs the doubleword is 0 (memory
  dump `c2final_A`, frame 90).
- **End.** The loop ends as soon as t ≥ 8.0. Measured: in `c3_timing2` it
  ends at t = 8.001027 after 8001 ticks, in `c3_timing` at 8.006001 after
  8006 ticks.
- **Short version**, measured in `c3_short`: the argument of 0x80055FBA is
  set to 1 at the breakpoint (`probe.py --poke`). The animation begins at
  t = 7.5 and ends after 65 frames and 501 ticks at t = 8.001. The loop
  0x800579B8 then does not call the set-up and the tear-down of the sound
  (0x8006166D, 0x800616A7) (0x800579B8–0x800579C4, 0x800579E2–0x800579EC).
- **Repetition.** If at t ≥ 8.0 the byte `this+0xA` is set, the animation
  begins again (0x80057295–0x80057301): t = 0 or 7.5, `x18` = 0, a new pulse
  table and a restart of the sub-objects. The constructor sets `this+0xA` to
  0 (0x80056B36). In our runs there was no repetition.

## 4. Derived time quantities

After t, 0x800571AD computes four quantities as long as the byte `this+0xC`
is 0 (constructor: 0, at 0x80056B52):

```
x1c = ramp(t_reg)                                   // this+0x1C
x20 = pulses(t) + x1c                                // this+0x20
x24 = (1 - arg/2) * x24 + x20 * arg/2               // this+0x24, smoothed
x18 = x18 + x20 * arg                               // this+0x18, integrated
```

**Ramp** (0x800571E5–0x80057245), with the constants at 0x80065E54 (8.0),
0x80065E58 (2.0), 0x80065E5C (0.8), 0x80065E60 (0.6), 0x80065E64 (0.0,
`base` here) and 0x80010A18 (0.5):

```
s = 0.6 + 0.5                                       // 1.1
ramp(t) = 0                                  for t < s
ramp(t) = base + (1 - base) * u * (u + 1) / 2 otherwise, u = (t - s) / (8.0 - 2.0 - 0.8 - s)
```

At t = 8.0, u ≈ 1.68 and the ramp ≈ 2.25.

**Pulses** (0x80057011, called with t as float32): a table of 12 entries
(t0, w, a) from `this+0x30`, 12 bytes each.

```
pulses(t) = Σ a_i * cos(π/2 * |t - t0_i| / w_i)   over all i with |t - t0_i| ≤ w_i
```

π is at 0x80065E4C. The absolute value is 0x80057F89 (clears the sign bit).

**The table** is built by 0x80056EF6 from a pseudo-random generator (section
4.1). For i = 1 … 12, with `pm1()` in [−1, 1) and `u01()` in [0, 1):

```
x  = pm1() * 0.03 + i / 13                         // 0x800128C0, 0x800128BC
y  = 1 - x * (x + 1) / 2
q  = 1.2 - y                                       // 0x800128B8
w  = max(0.05 * (u01() + 2) * q * q, 0.1)          // 0x800128B4, 0x80012888
a  = 0.2 * (u01() + 1) * (y + 0.5)                 // 0x800128B0
t0 = max((8.0 - 2.0 - 0.4 - 0.6) * y + 0.6, 0.6 + w)   // 0x800128AC = 0.4
```

After that the last entry is overwritten: t0 = 0.6 + w, a = 3 · a
(0x80056FF0–0x8005700D). The entries 1 to 11 therefore lie between about
1.6 and 5.4 s, descending; the last at 0.73 s with three times the height.
Values: `pulse_table_model` in the vector file.

### 4.1 The generator

State: a doubleword at `this+0x68C` (0x8006F6DC), set to 0x76543210 by the
constructor (0x80056B01). One step (0x8005A645):

```
s' = ror(s, 13) - (s - 11)        (32 bits)
u01() = (s' & 0xFFFF) * 2^-16     // 0x80056A60, factor at 0x80070874
pm1() = (s' & 0xFFFF) * 2^-15 - 1 // 0x80056A82, factor at 0x80070878
```

The generator is shared. Before the table the constructor draws once
(0x8005AFB6 → 0x8005A6CB with −1, draw at 0x8005A6E2). The table needs 36
draws. After that the set-up draws once more (0x8005A41D → 0x8005A14F, draw
at 0x8005A178). Measured: the state in the guest is 0xC12E9847, exactly 38
steps after 0x76543210, and the table in the guest equals the model
bit-exactly.

### 4.2 Precision of the arithmetic

The control word of the FPU is 0x027F (read through the GDB stub at
0x80056EF6 and 0x800571AD): precision control 2, that is a 53-bit mantissa,
and rounding to nearest. Every x87 operation thus rounds like an operation
in `double`. Rounding to float32 happens only on storing. Two places matter
here:

- `fst [this+0x10]` stores t as float32, but the unrounded value `t_reg`
  stays in the register, and the ramp computes with it. The pulses get t as
  a float32 argument.
- Likewise `x20` goes into `x24` and `x18` unrounded.

The cosine of the pulses comes from `fsincos` (0x8005706F). Both results are
stored as float32 (0x80057077, 0x80057079); only the cosine is used further.
xemu does not compute `fsincos` as an x87 instruction but calls `sin` and
`cos` of the host's C library in `double`
(`target/i386/tcg/fpu_helper.c`, `helper_fsincos`, commit `fc24584`). The
values in `vectors/c3_timing.json` therefore come from this path. How far it
differs, after storing as float32, from the instruction of a real CPU is
measured over all possible arguments (`core.md` 4).

Whoever computes with float32 per operation does not meet the table; whoever
does not carry the registers on unrounded misses `x1c` from about t = 1.1 on
by a few units in the last place. Both is carried out in `vectors_c3.py`.

## 5. The thread of the animation

- 0x80055FBA is called by the kernel from 0x8001576F (0x8001583D). It runs
  only if the doubleword at 0x8006F014 is not 0, and passes on to
  0x8002190E: stack size 0x4000 (*guessed*, from its position in the
  arguments), entry point 0x80055F7F, destination for the handle
  0x8003AEBC.
- 0x80055F7F calls 0x800579F4 and then 0x80021B78 with 0. Ghidra had
  recognised no function here.
- The kernel later waits in 0x80055EE4 for the handle at 0x8003AEBC
  (0x80020F5A) and releases it (0x80020696). 0x80055F0C waits for the thread
  or for the event 0x8006F03C, which the tear-down sets before it draws the
  after-image (`Overview.md`, section "After-image").

## 6. What is open

- Where the thread lies in the time course of the start: the distance from
  power-on to the first frame. In `c3_timing2` tick was 398 at the set-up.
- The frame rate in an undisturbed run. In `c2audio` (without breakpoints)
  there were about 8.4 s between the first and the last flip at 478 flips;
  the first interval was 344 ms long.
- The time quantities `x18` … `x24` go into the camera and the sub-objects.
  How is described by `Camera.md`, `Chamber.md`, `Blob.md`, `Logo.md` and
  `Effects.md`.
