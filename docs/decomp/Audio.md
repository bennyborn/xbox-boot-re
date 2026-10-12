# Start-up sound

How the sound of the boot animation arises: tables, sequencer, voices,
registers of the sound hardware, the program in the GP DSP. Addresses in the
unpacked kernel (`Overview.md`), names from `README.md`. Evidence:

- static: the addresses named;
- measured: `vectors/c4_audio.json` (run `c4_seq`, made by
  `tools/xemu/vectors_c4.py`; with the list of the measured
  commands, the SHA-256 of every table in guest memory and what the guest
  sent to the hardware at every note and every release),
  `vectors/c4_gp.json` (the GP program in a harness, `tools/dsp/gp_ir.py`),
  `vectors/c4_recording.json` and `vectors/c4_recording_dsp.json`
  (recordings against the render, `tools/xemu/compare_audio.py`,
  `compare_dsp.py`);
- third-party source: the source of xemu (`provenance.md`) for the meaning
  of the methods and registers of the sound hardware.

The model in `tools/xemu/audio_model.py` is written after this text. The
data is in `data/audio/`, made by `tools/extract/audio.py`.

## 1. Sequence

- **Start.** The frame loop 0x800579B8 calls 0x8006166D before the first
  frame, in the long version only (`Timing.md` 3). 0x8006166D calls
  0x8006033C (sections 2 and 3), creates a timer (0x800194C0) and a deferred
  call with the callback 0x8006165E (0x8001A384) and sets the timer with due
  time 0 and period 5 (0x8001951C, arguments 0, 0, 5). 0x8001951C computes
  the period times −10000 (0xFFFFD8F0).
- **Beat.** Every callback 0x8006165E advances the 16-bit time 0x800A7958 by
  1 and calls the sequencer 0x80060252. Measured, the callback came after
  6 ms in 1307 of 1320 intervals (tick at 0x8003A89C, `Timing.md` 1), nine
  times after 7 ms, once each after 5, 11, 14 and 15 ms (`ticks` in
  `c4_audio.json`). The sequencer counts only callbacks, not milliseconds;
  one step is called a **beat** in what follows.
- **Why 6 and not 5 ms.** The kernel keeps its timers in 32 buckets.
  0x8001C4CF inserts a relative due time through 0x8001C3CF and 0x8001CCAC:
  absolute due time = counter at 0x8003A890 minus relative due time, bucket
  = (absolute due time / 10 000) & 0x1F (division as a multiplication by
  0x80010CC8 and a shift by 13, 0x8001CCCD–0x8001CD2E). In the guest the
  counter always stands at 10 000 · tick (memory dump `c2final_A`, frame 90:
  31 590 000 at tick 3159). The interrupt 0x80030E4C increases both and then
  checks only the bucket of the **old** tick (0x80030ED8–0x80030EFD), with
  the **new** counter. After a callback 0x8001C10D sets the timer anew
  (0x8001C17C–0x8001C19D): if tick has just reached T + 1, the due time is
  10 000 · (T + 6) and the bucket (T + 6) & 0x1F. At the interrupt with the
  old tick T + 5 it would be due but does not lie in the bucket checked; it
  is served at the old tick T + 6. The interval is therefore 6 ms. Longer
  intervals arise when the callback runs later.
- **End.** After the loop 0x800579B8 calls the tear-down 0x800616A7: it
  unlinks the timer (0x800194ED) and releases, with 0x80061D23, all 16
  voices (0x8004E150) and the sound device (0x8004E13A). In `c4_seq` that
  was after 1320 beats. The tracks are still running then; the sound breaks
  off.
- **Repetition.** If the animation begins again (`Timing.md` 3), 0x800571AD
  puts event 1 into the ring (0x800572A1). The tables are not made anew in
  the process.

## 2. Tables

0x80061AC1 (called from 0x8006033C) makes the sample tables. All values are
16 bits signed. Floating point is computed by the x87 FPU with the control
word from `Timing.md` 4.2; 0x80055F8C truncates, the lower 16 bits are
stored. The constants are doubles:

| Address | Value |
|---|---|
| 0x80012A08 | 32767.0 |
| 0x80012A00 | 0.04908734375 (≈ 2π/128, with π = 3.14159) |
| 0x800129F8 | 0.0981746875 (2 · the value above) |
| 0x800129E8 | 0.196349375 (4 · the value above) |
| 0x800129F0 | 2⁻¹⁴ |
| 0x800129E0 | 0.0078125 |
| 0x800129D8 | 65536.0 |

```
noise[i], i < 0x2000      at 0x800A1B78:   s = s * 0x343FD + 0x269EC3 (32 bits), value (s >> 16) & 0x7FFF
                                           initial value s = 0x3EB (0x80061AC7), step 0x80061A9E
sine[i],  i < 128         at 0x80093B78:   trunc(sin(i * K_A00) * 32767)
fm[i],    i < 0x8000      at 0x80083B70:   m = m + 1 for i < 0x4000, otherwise m - 1   (m begins at 0, before the computation)
                                           trunc(sin(i * K_9E8 + sin(K_9F8 * i) * m * 2^-14) * 32767)
saw[i],   i < 128         at 0x80083328:   trunc((i - 64) * 0.0078125 * 65536) = (i - 64) * 512
```

The order of the operations in `fm` stands like this in
0x80061B37–0x80061B59. The noise values are never negative.

Plus three 8-bit samples from the ROM, converted to 16 bits (value ≪ 8):

| Source | Samples | Kind | Destination |
|---|---|---|---|
| 0x80066AC0 | 0x5540 | signed | 0x80078898 |
| 0x8006C008 | 0xEB8 | unsigned, byte XOR 0x80 | 0x800A5B80 |
| 0x8006CEC8 | 0x1A3F | unsigned, byte XOR 0x80 | 0x80093C78 |

And a copy read backwards (0x80061BBB–0x80061BD4): 0x5540 words to
0x800970F8, read from 0x80083318 downwards. The first word therefore lies
**behind** the end of 0x80078898 (at 0x80083318, where 0 stands in the image
and in our runs); the first sample of the original is missing.

Evidence: all eight tables agree bit-exactly with the guest memory of
`c2final_A`, frame 90 (`tables` in `c4_audio.json`), `fm` with its 32768
nested sine values too.

## 3. Sequencer

### 3.1 Data

| Address | Content |
|---|---|
| 0x8006E908 | 16 bits: 30 track records; 16 bits: 16 channels |
| 0x8006E910–0x8006EB17 | 16 pointers per channel, in this order: filter (0x800619B1), set up the channel (0x800618D9), key on (0x800618DF), pitch (0x80061952), release (0x80061994), sample (0x80061A08), level (0x80061D48), byte +2 (0x80061A91); at 0x8006EB10 and 0x8006EB14 only one entry each (channel 0) |
| 0x8006EB18–0x8006EBF7 | 11 sample descriptions of 0x14 bytes (section 5.2) |
| 0x8006EBF8–0x8006EFDF | bytecode of the tracks |
| 0x8006EFE0 | event records of 8 bytes: handler (16 bits), channel mask (16 bits), track list (32 bits) |
| 0x800A7BB0 | 11 pointers to the sample descriptions, index = sample number |
| 0x800A7BDC, 0x800A7C0C | track lists of the events 1 and 2 |
| 0x800A7C10 | highest valid event number: 2 |
| 0x800669D8 | 38 pointers: the commands |
| 0x80066A70 | 13 pointers: the event handlers |

Events: 0 has handler 0 (0x80061762, stop everything), 1 has handler 1
(0x800617D0) with the mask 0x0FFF and the 12 tracks from 0x800A7BDC, 2 has
handler 1 with the mask 0x0001 and one track (0x8006EF80). The pointers at
0x800A7BB0 are not sorted by address; the assignment is in
`data/audio/sequence.json`. Raw, the three tables 0x800A7BB0–0x800A7C13
(pointers to the sample descriptions, track lists, highest event number) are
in `data/audio/seq_pointers.bin`.

### 3.2 State

- **Track record** (0x1C bytes, pool at 0x80078550): +0 next, +4 previous,
  +8 command pointer, +0xC waiting time (16 bits signed), +0xE last time,
  +0x10 next command, +0x12 layer, +0x13 channel.
- **List of the active tracks**: head at 0x800A792C, a track record of its
  own. 0x80062638 links a new track in at the **front**.
- **Channel record** (0x38 bytes from 0x80083428, index layer · 16 +
  channel): +0 sample, +2 byte 2, +3 volume byte, +6 pitch, +8 and +0x10
  loop counters and loop starts, +0x20 loop depth, +0x24 and +0x30 return
  addresses and their depth, +0x32 transposition, +0x34 filter cutoff, +0x36
  filter resonance. 0x800616C7 resets a channel record: everything 0, volume
  byte 0x7F.
- **Layers**: layer 1 of a channel locks its layer 0 out of the voice (a
  byte per channel from 0x80083318). Layer 1 is started only by handler 2
  (0x80061865), which no event uses; layer 0 holds in what follows.
- **Ring** for events at 0x800A7912, 16 bytes, read index 0x800A7910, write
  index 0x800A7911.

0x8006033C sets the ring and 0x800A7928 to 0, calls 0x80061AC1 (the tables;
at its end it puts event 1 into the ring) and 0x8006018A (chain the pool,
fetch the head), sets the time to 1.

The helpers for it:

| Address | Behaviour |
|---|---|
| 0x80060166 | takes the byte at the read index out of the ring, index + 1, from 16 back to 0; does not check whether the ring is empty (0x80060252 does) |
| 0x80060232 | (layer e, channel c): notes e · 16 + c (16 from 0x8006E90A) as a byte in 0x800A7942 and delivers 0x80083428 + 0x38 · (e · 16 + c) |
| 0x8006261D | takes the first track record of the free list (head 0x80078550), hangs the rest on the head; does not check whether the list is empty |
| 0x800625E6 | unlinks a track record from the active list and links it in at the front of the free list |
| 0x8006170B | (layer e): releases every active track with layer (+0x12) = e |
| 0x80061732 | (layer e, channel c): releases every active track with layer e and channel (+0x13) = c |
| 0x800616F1 | (channel c): for c < 16 the entry "set up the channel" of the channel (table 0x8006E950, section 3.1) |
| 0x800616B8 | returns at once; handlers that do nothing |
| 0x800616B9 | event handler: byte +1 of the current event record (pointer 0x800A7924) to 0x800A7943; no event of this data uses it |

Evidenced by the disassembly at the addresses named; the interplay is
checked bit-exactly with the model `tools/xemu/audio_model.py` in `c4_seq`
(all 900 commands, `vectors/c4_audio.json`).

### 3.3 One beat

```
seq_run():                                          // 0x80060252
    if ring not empty:
        e = take()
        if e > event_max: return                     // 0x80060270
        handler[event[e].handler](event[e])
    for track in active list, front to back:
        track.wait += track.last - now               // 16 bits
        track.last  = now
        if track.wait >= 0: continue
        if command[track.next](track) == 0:          // command 15 only
            return                                   // not the remaining tracks either
```

Per beat therefore at most one event and **exactly one command per track
that is due** (0x80060310 goes to the next track after every command).
Commands without a duration thus cost one beat of delay each; durations add
to the waiting time, which runs into the negative in the process, so that
the schedule as a whole does not slip. Event 1 starts the channels 0 to 11
in this order; because every new track is linked in at the front, they run
in the order 11, 10, … 0 per beat. A new track has waiting time 0 and the
current time as its last time; its first command therefore comes one beat
later.

### 3.4 Commands

Commands are 16-bit words, operands follow in 16-bit words. "Calls …" means:
through the channel table into the voice layer (section 5), only if the
layer is not locked.

| No. | Address | Operands | Effect |
|---|---|---|---|
| 0 | 0x80061E54 | duration | waiting time += duration; release the voice |
| 1 | 0x80061D75 | pitch (byte, then a filler byte), duration | waiting time += duration; pitch = pitch · 256 + transposition (16 bits); key the voice on |
| 2 | 0x80061EC1 | address (32 bits) | jump |
| 3 | 0x80061F02 | counter | begin a loop: store the counter and the address of the next command |
| 4 | 0x80061F5E | – | counter − 1; if it is 0, leave the loop, otherwise go back |
| 5 | 0x80061FB2 | sample | set the sample, volume byte and byte 2 to 0; voice: sample |
| 6 | 0x80062026 | byte | set byte 2; calls 0x80061A91 (only sets 0x80083B68 to 0) |
| 9 | 0x8006219E | value (signed byte) | volume byte += value (8 bits); voice: level |
| 10 | 0x8006224A | value | transposition += value |
| 11 | 0x80062284 | value | transposition = value |
| 12 | 0x8006238A | as 1 | as 1, but voice pitch only |
| 13 | 0x80062469 | duration | waiting time += duration |
| 15 | 0x800624ED | – | returns 0, the pointer stays where it is |
| 16 | 0x800622BE | value, resonance | filter cutoff += value, set the resonance; voice: filter (without the layer lock) |
| 17 | 0x80062324 | cutoff, resonance | set filter cutoff and resonance; voice: filter (without the layer lock) |

The tracks use only the commands 0, 1, 3, 4, 5, 9, 10, 12, 13, 16 and 17.
Not used: 2, 6, 11, 15 from the table above, also 7 (0x8006211B), 8
(0x80062164), 14 (0x800624BA), 20 (0x800624F0, puts an event into the ring),
24–26 (0x8006252A, 0x80062567, 0x800625B7), 28 (0x800620A2, increase byte
2), 37 (0x80062218) and the numbers 18, 19, 21–23, 27, 29–36, which point to
0x80061D71 (only returns 1, without moving the pointer). The commands 7, 8,
14, 20, 24–26 and 37 read byte-wise and do not fit the word format of the
data. If bit 7 of the pitch is set in command 1 or 12, 0x80061D9D reads the
duration as a byte at the place of the next command; that does not occur
either.

Layer 1 would get the value 0x800A794C (command 37) added to the pitch.

### 3.5 The tracks

The twelve tracks of event 1, decoded in `data/audio/sequence.json`. Track
12 (event 2) is never started: no command puts an event into the ring, and
apart from 0x80061AC1 and 0x800571AD (both event 1) nobody calls 0x8006013C.
Every track ends in a loop of 255 rests of 20000 or 2000 beats.

| Channel | Track | Sample | Content (beats) |
|---|---|---|---|
| 0 | 0x8006EBF8 | 1, sawtooth | from 147 ten notes on pitch 14, durations 130, 125, 70, 77, 97, 91, 47, 51, 57, 132, 2–3 beats of rest in between; filter fixed 29000 |
| 1 | 0x8006EC78 | 5, noise | from 6 one note on 24; filter from 1000 in ten steps of +3000 every 13 beats, then 60 steps of −1000 every 15 beats, getting quieter; release at 1040 |
| 2 | 0x8006ECC8 | 7, 8-bit 0x8006CEC8 (loop) | from 137 one note on 26 for 472; filter from 6000 in 20 steps of +1000; then 30 steps quieter |
| 3 | 0x8006ED08 | 9, 8-bit 0x80066AC0 | from 137 a note on 38 for 820, then once more for 200 louder; after that filter in 20 steps of −1000 |
| 4 | 0x8006ED50 | 5, noise | from 202 arpeggio 48, 53, 67, 53 each 15 on, 5 off, eight times, getting louder; then three times a semitone higher with 10 on, 5 off |
| 5 | 0x8006EDF0 | 6, 8-bit 0x8006C008 | from 1118: 34, 29, 22 of 20 each; then six times 34, 34, 34 each 18 on, 2 off, with the filter in steps of −2500 |
| 6 | 0x8006EE58 | – | rests only |
| 7 | 0x8006EE80 | 3, sawtooth | from 7 one note on 26 + 96/256, 19 filter steps of +1500 (resonance 2600), release for 800, key on again, 30 steps of −750, then 30 steps quieter |
| 8 | 0x8006EE64 | 6 | sets sample, level and filter but plays no note |
| 9 | 0x8006EEE0 | 9 | from 279 three times pitch 33: 280, 244, 500, louder each time |
| 10 | 0x8006EF18 | 9 | from 407 three times 33: 252, 194, 200 |
| 11 | 0x8006EF4C | 9 | from 479 three times 33: 274, 154, 200 |

The beats are those of the model; the first beat is 2. Measured: all 900
commands in `c4_seq` agree with the model in beat, channel, address and
number (`commands` in `c4_audio.json`).

## 4. Time

Beat n lies (n − 2) · 6 ms after the first callback (section 1). In the
recording `c2audio` the sound lies 2.61 s after the start of the recording
(`offset` in `c4_recording.json`); the deviation of the single channels from
that is named in section 7.

## 5. The voice layer

The sound functions of the range call a library in the kernel (mark `DSOUND`
at 0x8003CB30, `Overview.md`) whose functions are called through thunks
(this − 0x1C). It writes methods to the sound hardware (MMIO from
0xFE820000, waiting for 0xFE820010 ≥ 4). Meaning of the methods and fields
after xemu (`apu_regs.h`, `NV1BA0_PIO_*`, `NV_PAVS_VOICE_*`). Every
statement here is checked in `c4_seq`: for every call `vectors_c4.py`
predicts the methods, 1793 values together; all agree, no method remains
unexplained (`registers` in `c4_audio.json`).

### 5.1 Setting up

0x80061AC1 opens the sound device (0x80053302) and creates 16 voices
(0x8005339F, pointers from 0x80083B28, voice c for channel c). Format block:
0x18, 0, 0, pointer to (1, 1, 48000, 96000, 2, 16), that is one channel,
48 kHz, 16 bits. Then per voice 0x8005190A with a bit mask and mix shares in
1/100 dB:

| Channels | Mask | Shares |
|---|---|---|
| even (except 3, 5) | 3 | bin 0: 0, bin 1: −600 |
| odd (except 3, 5) | 3 | bin 0: −600, bin 1: 0 |
| 3, 5 | 0x803 | bin 0: 0, bin 1: −100, bin 11: 0 |

(0x80061C7B–0x80061CD5; 0x80083B34 and 0x80083B3C are the voices 3 and 5.)
To this comes the basic set-up of the device, measured twice before the
first command: headroom 1 for the mix bins 0–30 (methods 0x200–0x278), 0 for
bin 31, a test voice with 32 samples (handle 0x40) that is keyed on and
switched off again.

### 5.2 Setting the sample (0x80061A08)

A sample description (0x14 bytes): data pointer, length in bytes, 16 bits
(equal to the length), 16 bits loop (1/0), two pointers to envelope blocks.

| No. | Data | Samples | Loop | Blocks |
|---|---|---|---|---|
| 0 | sine 0x80093B78 | 128 | yes | 0x800A7980, 0x800A79A8 |
| 1 | sawtooth 0x80083328 | 128 | yes | 0x800A7AC0, 0x800A7AE8 |
| 2 | sawtooth | 128 | yes | 0x800A7A20, 0x800A7A48 |
| 3 | sawtooth | 128 | yes | 0x800A7B10, 0x800A7B38 |
| 4 | sawtooth | 128 | yes | 0x800A7A20, 0x800A7A48 |
| 5 | noise 0x800A1B78 | 0x2000 | yes | 0x800A7B60, 0x800A7B88 |
| 6 | 0x800A5B80 (from 0x8006C008) | 0xEB8 | no | 0x800A7A70, 0x800A7A98 |
| 7 | 0x80093C78 (from 0x8006CEC8) | 0x1A3F | yes | 0x800A7A70, 0x800A7A98 |
| 8 | FM 0x80083B70 | 0x4000 | no | 0x800A7A70, 0x800A7A98 |
| 9 | 0x80078898 (from 0x80066AC0) | 0x5540 | no | 0x800A7A70, 0x800A7A98 |
| 10 | backwards 0x800970F8 | 0x5540 | no | 0x800A7A70, 0x800A7A98 |

Sample 8 uses only the first half of the FM table (0x8000 bytes).
0x80061A08 reads the length as a 16-bit word at +4 of the description
(0x80061A41, 0x80061A5B); all eleven lengths fit into it.

**Envelope block** (ten words): kind, initial state, delay, attack, hold,
decay, release, sustain level, w8, w9. Kind 1 sets the amplitude envelope
(0x8004EF2D), kind 0 the second envelope (0x8004EE84):

| Field | Kind 1 | Kind 0 |
|---|---|---|
| attack, delay | ENV0 bits 0–11, 12–23 | ENV1 bits 0–11, 12–23 |
| decay, hold, sustain level | ENVA bits 0–11, 12–23, 24–31 | ENVF bits 0–11, 12–23, 24–31 |
| release | LFO_ENV bits 0–11 | MISC bits 0–11 |
| initial state | bits 28–31 of VOICE_ON | bits 24–27 of VOICE_ON |
| w8 | – | ENV0 bits 24–31 (pitch scale) |
| w9 | – | ENV1 bits 24–31 (scale of the filter cutoff) |

Both functions replace only their own fields and leave the rest of the words
as they are (`xor`, `and`, `xor` per field): rates in the bits 0–11, times
in the bits 12–23, a byte on top. In the state of the voice MISC lies at
+0x88, ENV0, ENVA, ENV1, ENVF at +0x8C to +0x98, LFO_ENV at +0xA0, the
initial states at +0xB0 (0x8004EE8A–0x8004EF23, 0x8004EF33–0x8004EFBC).

The blocks are in `data/audio/envblocks.json`, raw (14 · 0x28 bytes from
0x800A7980 out of the unpacked kernel, `tools/extract/audio.py`) in
`data/audio/envblocks.bin`. All blocks used have the initial state 1 (after
xemu: delay). Measured were all fields except w8 ≠ 0, which occurs only in
unused blocks; there the assignment holds from 0x8004EE84 alone.

The methods on a change of sample: FMT 0x5000E0E6 (channels 3, 5:
0x5000E0C5; after xemu 16-bit samples, headroom 7, bins 6 and 7 or 5 and 6),
MISC = 0x800000 | filter mode ≪ 16 | release (kind 0), the envelopes,
FCA = FCB = stored filter (section 5.4), VBIN 0x14830820 (bins 0–5; channels
3, 5: 0x10622C20, bins 0, 1, 11, 2, 3, 4), the attenuations for the stored
level, pitch 0, TAR_HRTF 0xFFFF, then page table (0x804/0x808) and buffer
base (0x3A0); page and offset give the physical address of the data
(checked).

Only the first change of sample creates the hardware voice. Level and filter
commands before it (channels 0, 1, 4) produce no methods; their value stays
in the state of the library and goes out with the change of sample.

### 5.3 Pitch

0x800618DF and 0x80061952 compute the pitch P of the channel (16 bits,
semitone in the upper byte, fraction in the lower):

```
lib = (P & 0xFF) * 0x155 / 0xFF + ((P >> 8) - 60) * 0x155      // integer
```

0x155 = 341 per semitone, 4092 per octave. The library writes `lib` as a
16-bit value into the upper halfword of TAR_PITCH (0x8005112E, 0x800508F9).
After xemu the advance per output sample at 48 kHz is 2^(lib/4096). For the
tables with a period of 128 samples that means 375 Hz · 2^(lib/4096); pitch
14 on channel 0 (lib −15686) gives 26.4 Hz, pitch 26 + 96/256 on channel 7
(lib −11466) 53.9 Hz.

### 5.4 Level and filter

**Level** (0x80061D48): the argument is the volume byte v of the channel,
the library value 200 − 30 · v. 0x80051164 subtracts from it a value of the
state (+0x1C), 600 in all measurements. It is set when the voice is created
in 0x80051059 (0x80051085): 600 if in the word +4 of the format block
neither bit 4 nor one of the bits 0x82000 is set, otherwise 0. 0x8005125A
passes this word through, and 0x80061AC1 stores 0 there (0x80061C3B). The
initial level is thus −600 (0x800510B7–0x800510C2). 0x8004EB1D computes per
mix bin b:

```
att_b = min(0xFFF, max(0, -(level - 600 + share_b)) * 64 / 100)     // unused bins: share = -10000
```

and packs the eight values into VOLA, VOLB, VOLC (two 12-bit values each and
four bits each of the bins 6 and 7). After xemu the unit is 1/64 dB, 0xFFF
silent.

**Filter** (0x800619B1): six words 1, 0, F, Q, F, Q with F = (cutoff of the
channel, 16 bits) + 0x8000 and Q = resonance. 0x8004EFC6 sets MISC bits
16–17 to 1 (mode), FCA = FCB = (F & 0xFFFF) | Q ≪ 16. After xemu that is,
for a voice with one channel, a low-pass with cutoff 2^(FC0/4096) (FC0
signed, limited to 0.0039 … 1) and resonance Q/0x8000. The tracks use only
Q = 26000 and 2600. Because F has 16 bits, FC0 jumps from −1 (fully open) to
−32768 as soon as the cutoff of the channel reaches 0x8000, and the other
way round: channel 1 lowers the cutoff from 31000 in steps of 1000 below 0;
from there on the low-pass is fully open after xemu.

### 5.5 Key on and release

**Key on** (0x800618DF): pitch, then 0x80052DB0 with the loop from the
sample description. 0x80051F2E writes FMT with bit 25 for the loop,
EBO = number of samples − 1, LBO = 0. After that it starts the voice only
**if it is not running according to the state of the library**: then
ANTECEDENT 0x1FFFF, VOICE_ON 0x11000000 | handle, PAUSE handle. If it is
still running, also while decaying, a note changes only the pitch.

**Release** (0x80061994): 0x80052C04 with flag 1, that is VOICE_RELEASE, as
long as the voice is running, several times too.

**End of a voice.** The hardware reports the end (xemu: method 0x8000,
SE2FE_IDLE_VOICE, when the envelope has decayed or a sample without a loop
is at its end). The library answers later, on a way of its own, with CUR
handle and FMT (sometimes VOICE_OFF as well); only from then on does it
consider the voice ended. Whether a note keys on anew therefore depends on
the running time of the envelope in the hardware. In `c4_seq` channel 4, for
example, keyed on anew mostly only at every second note of its arpeggio.

How long the library needs for its answer is not evidenced. Render and
rebuild assume: until the next beat (*guessed*). `c4_seq` is not suited to
measuring it: the run stops at 2243 places and needs 30.7 s on the host for
7.95 s of guest time (`t_ms` of the stops in `probe.json`), while xemu goes
on computing the voices in real time. The envelopes therefore run through in
fewer beats there. Counted (`voice_on`, `voice_release` in `c4_audio.json`):
the guest started the voice at 56 of 81 notes, the model with nine frames
per beat at 34, equal in 59. If the model computes 35 frames per beat, 3.9
times as many, 79 of 81 are equal. What an
undisturbed run does is shown only by the recordings (section 7).

### 5.6 Thunks and ways into the library

The channel entries (section 3.1) reach the library through thunks that set
`this` back by 0x1C and jump on. Who calls what, after the calls in the call
graph (`tools/ghidra/export_graph.py`) and the disassembly:

| Channel entry | Thunk | Destination | Methods (section) |
|---|---|---|---|
| filter 0x800619B1 | 0x80050FA4 | 0x8004EFC6 | 0x318, 0x374, 0x378 (5.4) |
| level 0x80061D48 | 0x800518B6 | 0x80051164 → 0x80050819 | attenuations 0x360, 0x364, 0x368 (5.4) |
| key on 0x800618DF | 0x800518D2 | 0x8005112E → 0x800508F9 | pitch (5.3) |
| sample 0x80061A08 | 0x800518EE | 0x8005119D → 0x800509AA → 0x8004EE84 or 0x8004EF2D by kind | envelope, 0x318, 0x308–0x314, 0x36C (5.2) |
| sample 0x80061A08 | 0x80052975, 0x80052995 | loop range, play position | 0x3DC, 0x3A4 (5.2, 5.5) |
| set-up 0x80061AC1 | 0x8005190A | 0x800511CD → 0x80050819 | mix shares, attenuations (5.1) |

Plus 0x80052700 (buffer address and length in bytes of a voice, from
0x800531F7 on creation) and 0x80052307 (release; with the flag set
0x80051F2E before it). That each of these ways produces exactly the methods
named is checked by `vectors_c4.py` per call (section 5, `registers`).

## 6. Output: the program in the GP DSP

The voices mix into the bins 0 and 1, the channels 3 and 5 also into bin 11
(5.1). The bins are processed by a program in the GP, one of the two DSP
cores of the sound hardware (name after xemu,
`hw/xbox/mcpx/apu/dsp/gp_ep.c`).

### 6.1 The program in the ROM

0x625 words of 32 bits (24 used) at **0x80053568–0x80054DFB**, in
`data/audio/gp_program.bin`. 0x8004F8A1 passes the address and the length
0x1894 bytes (0x8004F8C4). Before that it creates, with 0x8004F763, the
management of the scratch memory (0x8004F8DE) and, with 0x8004F7A5, its page
table, which it writes into the registers 0xFE8020DC/0xFE802048 and
0xFE8020D4/0xFE802040 (0x8004F8F1); then 0x8004F852 copies the words to the
start of the scratch memory of the GP (0x8004F906). When the reset is
released the GP loads the first 0x800 words from there into its program
memory (xemu: `dsp.c`, `dsp_bootstrap`). Evidence: in the saved state of
`c2final_A`, frame 90, the first 0x625 words of the program memory equal
these words.

The disassembly was made with xemu's own disassembler (`dsp_dis.c.inc`),
compiled into a harness of our own (`tools/dsp/`, section 6.4). The
disassembly is local only (`out/dsp/gp_disasm.txt`).

### 6.2 Sequence

- **Set-up** (from P:0x0000): the program writes its parameter block from
  X:4 itself, from immediates in the program (such as 0xBB7F at 0x8005499C).
  The kernel writes nothing into it: after 200 frames in the harness 424 of
  the 469 occupied X words equal the state in xemu, the 45 others are write
  and read pointers of the delay lines, which advance with every frame.
- **Frame loop** (P:0x0014): report idle (bit 0 to X:0xFFFFC4), wait for the
  frame (P:0x00B7), process (P:0x0022 → 0x00C8 → 0x0242, parameter block
  r5 = 4), write the delay lines back (P:0x007D).
- **One frame** (P:0x0242–0x0366), 32 samples:
  1. Read the delay lines: DMA chain from X:0x105, 31 blocks of 32 words
     from ring buffers in the scratch memory.
  2. Bin 11 (X:0x1560) times 0.5 (P:0x024B; the short immediate 0x40 ends up
     shifted by 16 bits after xemu, that is 0x400000), then filtered with
     one pole (P:0x03A2: y = b · x + a · y_old, coefficients and state from
     X:0x6E).
  3. Early reflections and reverb tail, see below.
  4. Output: sums from X:0x4A4 and X:0x4C4 are added to X:0xC00 and X:0xC20
     (P:0x0358–0x0362). After xemu X:0xC00 is a second window onto the mix
     buffer (`dsp_cpu.c`, accesses to 0xC00 + mix buffer size), that is bin
     0 and bin 1. If the mode X:0x6D equals 4, further sums go into the bins
     4 and 5 (P:0x032A); here it is 2.
  5. Write the delay lines: DMA chain from X:0xA3, 14 blocks.

The ring buffers in the scratch memory and their taps, from the DMA chains
(4 bytes per sample). The delay of a tap is (write offset − read offset) mod
length; both offsets advance by 32 samples per frame. A tap with distance 0
reads before the write and gets the value from a whole ring length before.

| Line (base) | Length | Write buffer | Taps: buffer ← delay in samples |
|---|---|---|---|
| A (0xC000) | 12 000 | X:0x204 | 0x204 ← 336, 0x224 ← 392, 0x244 ← 372, 0x264 ← 428, 0x284 ← 460, 0x2A4 and 0x324 ← 524, 0x2C4 and 0x344 ← 640, 0x2E4 and 0x364 ← 792, 0x304 and 0x384 ← 988, 0x3A4 ← 1920, 0x3C4 ← 1016 |
| B (0x17B80) | 12 000 | X:0x3C4 | 0x3E4 ← 9044, 0x404 ← 12 000 |
| C (0x23700) | 12 000 | X:0x404 | 0x424 ← 10 960, 0x444 ← 11 908 |
| D (0x2F280) | 12 000 | X:0x444 | 0x464 ← 10 960, 0x484 ← 6512 |
| s1–s4 (0x3AE00, 0x3C340, 0x3D670, 0x3EA80) | 1360, 1228, 1284, 1440 | X:0x224, 0x244, 0x264, 0x284 | 0x4A4 ← 304, 0x4C4 ← 284, 0x4E4 ← 296, 0x504 ← 328 |
| s5–s8 (0x40100, 0x42C80, 0x46130, 0x4A110) | 2784, 3372, 4088, 4956 | X:0x2A4, 0x2C4, 0x2E4, 0x304 | 0x524, 0x544, 0x564, 0x584 ← whole length each |
| L1, L2 (0x4EE80, 0x6D740) | 31 280 each | X:0x324, 0x344 | 0x5A4, 0x5C4 ← whole length each |

One frame from step 3 on, as pseudocode of our own; `A[d]` is the block of
the tap with delay d, `lp(k, ·)` the one-pole low-pass of P:0x03A2 with
y = c[k+1] · y_old + c[k+2] · x. Coefficients c[a] = X:a as a fraction:

```
x      = lp(0x6E, 0.5 * bin11)                    // c 0.8465, 0.1535
write s1..s4: A[392|372|428|460] + A[336] + g_k * s_k read
                                                  // g = X:0x93..0x96 = -0.4, +0.4, -0.4, -0.4
e5 = s5 + A[524] - A[640]      e6 = s6 - A[792] + A[988]      // X:0x5D..0x64 = ±1
e7 = s7 + A[524] + A[640]      e8 = s8 - A[792] - A[988]
q  = 0.2 * (A[1016] + B[9044] + C[10960] + D[6512]
          + A[1920] + B[12000] + C[11908] + D[10960])   // X:0x66 = 0.2
write B: lp(0x71, A[1016]); C: lp(0x74, B[12000]); D: lp(0x77, C[11908])
                                                  // each c 0.7732, 0.0712
write s5..s8: lp(0x7A + 3i, sum_j M[i][j] * e(5+j) + q)
                                                  // M[i][j] = X:(0x8F - i + j): -0.5 everywhere, +0.5 on the diagonal
                                                  // lp: (0.4404, 0.4279), (0.4819, 0.3745), (0.5253, 0.3202), (0.5682, 0.2678)
write A: x
left   = 128 * (c[0x1DF] * s2_new + c[0x1DE] * s1_new + c[0x1E2] * (e5 + e6) + c[0x1E3] * L1)
right  = 128 * (c[0x1E1] * s4_new + c[0x1E0] * s3_new + c[0x1E2] * (e7 + e8) + c[0x1E3] * L2)
                                                  // c[0x1DE..0x1E1] ≈ 0.0009, c[0x1E2] = c[0x1E3] ≈ 0.0081
```

`s1_new` to `s4_new` are the values written into s1 to s4 in this frame. The
matrix M is 0.5 times a matrix with −1 everywhere and +1 on the diagonal.
The lines L1 and L2 are dead: their input is the sum of the taps times
X:0x65 = 0 plus their own output, and they begin at 0. B to D feed only q.

Evidence: `tools/dsp/reverb_model.py` recomputes this description in
floating point (parameters from the X memory of the harness) and meets the
impulse responses from 6.3 to 3 · 10⁻⁵ at peaks of 0.08; the error lies
43 dB below the signal. The rest comes from the fixed-point arithmetic:
rounding at `macr`, truncation on storing, amplified by `asl #7` at the
output. With the arithmetic from 6.6 instead of floating point it disappears
entirely. What counts remains the program itself in the harness.

### 6.3 Behaviour

Measured with the harness (`tools/dsp/gp_ir.py`, `vectors/c4_gp.json`): a
single impulse in one of the 32 bins each, output left = X:0x1400, right =
X:0x1420, as xemu plays them with `audio.use_dsp` (`gp_ep.c`).

- **Bin 0 → left, bin 1 → right**, unchanged: gain 1, no delay.
- **Bin 11 → reverb on both sides**, without a direct share: first signal
  after 336 samples (7.0 ms), clear early reflections at 10.9, 13.3, 16.5
  and 20.6 ms with different signs per side, a dense reverb tail from about
  70 ms, reverberation time (from −5 … −35 dB) 1.46 s left and 1.43 s right;
  sum of squares 0.171 left, 0.149 right; the decays left and right are
  uncorrelated (0.01).
- **All other bins** do not reach the output.
- **Linear**: double input gives double output except for 4.6 · 10⁻⁵
  (rounding in the 24-bit fixed point); superposition exact.

For the start-up sound therefore: left = bin 0 + reverb_L(bin 11), right =
bin 1 + reverb_R(bin 11). The impulse responses themselves are made by
`gp_ir.py` from the program; `c4_gp.json` holds their characteristics and
SHA-256.

### 6.4 The harness

`tools/dsp/build.py` compiles xemu's DSP core (`dsp.c`, `dsp_cpu.c`,
`dsp_dma.c`, commit `fc24584`) with gcc against substitute headers of our
own (`tools/dsp/stub/`) into `out/dsp/gp.exe` (harness
`tools/dsp/gp_harness.c`) and `out/dsp/gpdis.exe` (`tools/dsp/gp_disasm.c`).
One line in `dsp_dma.c` is adapted in the process: an `ssize_t` would be too
narrow under the 32-bit gcc, the buffer of the DMA would never be created.
Like the kernel, the harness puts the program at the start of the scratch
memory, writes the mix bins per frame into X:0x1400 + bin · 32 as xemu does
and lets the GP run until idle. Evidence for the fidelity to xemu: the
comparison of the X memory in 6.2.

### 6.5 What xemu outputs

Whether xemu plays the output of the GP depends on the setting
`audio.use_dsp`; without a value it is off (`config_spec.yml`). Then xemu
mixes the voices itself, each with the largest of its bin levels, onto both
sides equally (`vp.c`, `MCPX_APU_DEBUG_MON_VP`), and the GP runs unheard.
**All recordings up to `c4_no35` were made like this**, `c2audio` too. Hence
mono and no reverb there. With `bench.py --dsp` the measurement bench
records the output of the GP.

With `audio.use_dsp` xemu takes, per sample, the bits 8 to 23 of X:0x1400
and X:0x1420 as a 16-bit value (`gp_ep.c`, `mcpx_apu_dsp_frame`: `l >> 8`).
Before that it writes into the mix bins the floating-point value times 2²³,
rounded to the nearest integer, halves to even, limited to −2²³ … 2²³ − 1
(`fpconv.h`, the conversion from floating point to 24 bits).

### 6.6 The arithmetic in fixed point

What 6.2 names as formulas the program computes with words of 24 bits and an
accumulator A of 56 bits (a2:a1:a0 with 8, 24 and 24 bits). What the
instructions do stands here after xemu (`dsp_cpu.c`: `dsp_mul56`,
`dsp_add56`, `dsp_rnd56`, `dsp_asl56`; `dsp_emu.c.inc`:
`emu_pm_read_accu24`), without scaling (the bits S0 and S1 of the status
register are 0).

- **Word:** 24 bits signed, as a fraction w / 2²³.
- **Load** (`move x:…,a`): a1 = word, a0 = 0, a2 the sign.
- **Product** (`mpy`, `mac`, `macr`): x · y · 2 as a value of 48 bits in
  a1:a0, exact. `mac` and `macr` add it to A.
- **Round** (`macr`, after the addition): A + 2²³; if a0 is 0 after that,
  bit 0 of a1 is cleared (a half goes to the even number); then a0 = 0.
- **Store** (`move a,x:…`): a1. If A does not fit into 24 bits, it is
  limited to 0x7FFFFF or 0x800000. a0 is dropped, without rounding.
- **`asl #7`:** A left by 7 bits; what goes beyond 56 bits is dropped.

The subroutines, per sample of a block of 32:

| P | Computation |
|---|---|
| 0x00F8 | d = s |
| 0x00FD | d = store(d · c), truncated |
| 0x0103 | d = store(round(load(d) + s · c)) |
| 0x010A | d = store(load(d) + load(s)), that is a limited sum |
| 0x0111 | d = store(load(x) + load(y)) |
| 0x03A2 | z = store(c[k+1] · z + c[k+2] · x); x = z. No rounding; the state z is the stored value |
| 0x0367 | A = k₁ · s_b; A = round(A + k₀ · s_a); A = round(A + y₀ · e_b); A = round(A + y₀ · e_a); A = round(A + y₁ · L); `asl #7`; store |

The frame from 6.2 in this arithmetic, in the order of the program:

| P | Step |
|---|---|
| 0x024B–0x0257 | bin 11 with 0x00FD times 0x400000 (the word at P:0x024B carries the short immediate 0x40), that is halved with truncation; then 0x03A2 with X:0x6E |
| 0x025C–0x0271 | s1 to s4: 0x010A with A[336], then 0x0103 with the own tap and X:0x93 + k |
| 0x0272–0x0284 | e5 to e8: twice 0x0103 each on the tap of s5 to s8, with X:0x5D to X:0x64. The coefficients are 0x7FFFFF and 0x800001, not ±1 |
| 0x0285–0x02B0 | q: (A[1016] + B[9044]) + C[10960] + D[6512] and (A[1920] + B[12000]) + C[11908] + D[10960] with 0x0111 and 0x010A, their sum with 0x0111, then 0x00FD with X:0x66 |
| 0x02B1–0x02D6 | the two partial sums times X:0x65 = 0 plus the tap of L1 and L2; stays 0 |
| 0x02D7–0x02E4 | 0x03A2 on A[1016], B[12000], C[11908] with X:0x71, 0x74, 0x77 |
| 0x02E5–0x02FC | matrix: A = 0; for j = 0 … 3: A = round(A + X:(0x8F − i + j) · e(5+j)); store. Rounding happens after every product |
| 0x02FD–0x0311 | plus q with 0x010A, then 0x03A2 with X:0x7A + 3 i |
| 0x0312–0x0317 | the filtered input into the buffer that goes into line A |
| 0x0318–0x0357 | 0x0367 for left (X:0x1DE, s1, s2, e5, e6) and right (X:0x1E0, s3, s4, e7, e8) |
| 0x0358–0x0363 | left and right with 0x010A to bin 0 and bin 1 |

The program writes the parameters from immediates (`move #…,x0`, the word
after the instruction): X:0x2E + k from P:0x050A + 3 k for k = 0 … 93,
X:0x8C + k from P:0x04AF + 3 k for k = 0 … 6, X:0x93 + k from
P:0x04C9 + 3 k for k = 0 … 3. X:0x1DE to X:0x1E3 are copies of X:0x67 to
X:0x6C (P:0x0235). Some values: X:0x6F, 0x70 = 0x6C5A41, 0x13A5BF; X:0x72,
0x73 (likewise 0x75, 0x76 and 0x78, 0x79) = 0x62F77D, 0x091E2F;
X:0x66 = 0x199999; X:0x93 to 0x96 = 0xCCCCCD, 0x333333, 0xCCCCCD, 0xCCCCCD;
X:0x8C to 0x92 = 0xC00000 except X:0x8F = 0x400000; X:0x67 to 0x6C =
0x001E23, 0x001B7E, 0x001C8D, 0x001FB9, 0x0108FF, 0x0108FF. The length of a
line is (X:(0x2F + 2 n) + 1) / 4 samples for n = 0 … 13.

Between the input and s1 the frame calls P:0x013D. That is a state machine
that can cross-fade parameters. It does nothing here: P:0x0133 sets
X:(r5 + 496) = 0xFFFFFF and X:(r5 + 480) = 0 in the set-up, with which
P:0x01FE and P:0x0142 return at once.

Evidence: a version in C exactly after this section (the frame routine in
`core/src/Audio.c`) delivers the same words as the program in the harness:
the impulse responses of the bins 0, 1 and 11 with the same SHA-256 as in
`c4_gp.json`, and 192 000 samples of noise in all three bins, 4803 of them
at the limit, without one deviation (`core/test/check_k7.py`). If one of the
rules is left out (rounding, halves to the even number, limiting, truncation
in the low-pass, rounding per product in the matrix, truncation on halving),
the version deviates.

## 7. Checking against the recordings

`tools/xemu/audio_render.py` plays the model the way the VP in xemu would:
frames of 32 samples, envelopes, low-pass, attenuation, headroom 1,
resampling band-limited (windowed sinc with 16 taps, cutoff
min(1, 1/advance); xemu uses libsamplerate, hence similar, not bit-exact).
It delivers the bins 0, 1 and 11 per channel and the monitor mix of 6.5. The
comparison is made on power spectrograms (10 ms step) without the lines
below 35 Hz (see "Measuring chain"), per channel in the cells in which it
dominates and lies clearly above the noise floor: correlation of the cell
powers in dB at the common shift, best shift of its own within ±300 ms,
level against the overall gain.

**Without DSP** (`c2audio` against the monitor mix, `compare_audio.py`,
`vectors/c4_recording.json`):

| Channel | Correlation at 0 | best shift (correlation) | Level |
|---|---|---|---|
| 0 | 0.93 | +110 ms (0.95) | +2.1 dB |
| 1 | 0.85 | −10 ms (0.85) | +2.5 dB |
| 2 | 0.97 | 0 ms | +4.4 dB |
| 3 | 0.78 | +30 ms (0.79) | +3.0 dB |
| 4 | 0.51 | +20 ms (0.61) | +1.9 dB |
| 5 | 0.79 | +30 ms (0.95) | +2.2 dB |
| 7 | 0.96 | 0 ms | +2.8 dB |
| 9 | 0.79 | +20 ms (0.80) | +3.6 dB |
| 10 | 0.73 | +20 ms (0.78) | +4.2 dB |
| 11 | 0.80 | +10 ms (0.82) | +4.1 dB |

**With DSP** (`c4_dsp_out` against left = bin 0 + reverb_L(bin 11), right =
bin 1 + reverb_R(bin 11), `compare_dsp.py`,
`vectors/c4_recording_dsp.json`): the recording is stereo (correlation L/R
0.74). Per channel and side the levels lie between +0.4 and +4.3 dB, the
correlations at 0 between 0.43 (channel 4 right) and 0.95. The best shifts
are flat: at most 0.11 better than at 0. In the full mix the sign of the
side balance is right for all 10 channels (`stereo_left_minus_right` in the
vector file, corrected from an earlier "9 of 10"), but its amount is not for the
channels 9, 10 and 11 (−0.9, +2.3 and −1.0 dB instead of ∓6); played alone
(`c4_dsp_solo1`, `c4_dsp_solo9`) it is exact: left minus right −5.98 and
−5.97 dB, expected −6.00 dB.

**Played alone**, absolute against the render: channel 1 −10.87 dB, channel
9 −10.72 dB (both with DSP), channel 2 −10.49 dB, channel 9 −10.72 dB
(without DSP). That is the gain of the recording chain; equal to within
0.4 dB in all four runs. For channel 9 the chain is flat (−13 dB in all
bands from 35 Hz to 8 kHz, before subtracting the noise floor).

Every sounding channel is there at its time in all recordings, the levels in
the full mix lie within 2.5 dB (without DSP) and 3.9 dB (with DSP, per
side), alone within 0.4 dB.

**Measuring chain.** The recording path of the host (playback through the
default device, recording through the stereo mix) removes a DC component: a
test signal of 0.1 DC and a 1 kHz tone of amplitude 0.02 arrived with the
ratio 0.015 instead of 5. But the DC
component belongs to the sound: the noise table has the mean 0.50 (2.), and
channel 1 plays it with the low-pass. With the DC component and linear
interpolation channel 1 lay 9.4 dB too high in the render; without both it
is right.

## 8. Limit of the sources

Two questions cannot be decided with the sources of this project (BIOS,
xemu, public documentation). Only the hardware could answer them. The
specification therefore states what is certain and marks the
interpretation:

- **Envelopes.** Certain are the register fields of every voice, bit-exactly
  (5.2, 5.5; `vectors/c4_audio.json`). How the hardware turns delay, attack,
  hold, decay, sustain level and release into time stands here only
  after xemu (`vp.c`, `voice_step_envelope`: a step per frame of 32 samples,
  counters of 16 frames per unit), and xemu itself notes there several times
  that it is uncertain. On that also depends when a voice ends and which
  notes therefore key on anew (5.5). The render and all comparisons with
  recordings use xemu's interpretation.
- **Loop end.** Whether the hardware still plays the index EBO, that is
  whether the tables with 128 samples have the period 128. xemu mostly reads
  it too (`cbo <= ebo` in xemu's `voice_get_samples`, `vp.c`) and skips it
  only if a frame ends exactly before it. The render takes the period
  EBO + 1.
