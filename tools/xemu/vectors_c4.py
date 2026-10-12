"""Writes docs/vectors/c4_audio.json from the run c4_seq.

    python tools/xemu/probe.py --name c4_seq --at 0x80060305 --at 0x8006165E \
        --read 0x800A7900:0x70 --read 0x80078550:0x350 --read 0x8003A89C:4 \
        --trace mcpx_apu_method --max 8000 --timeout 1200 --idle 20
    python tools/xemu/vectors_c4.py

The probe stops before every command of the sequencer (0x80060305, the
call through the command table in 0x80060252) and at every tick
(0x8006165E), and traces every method the CPU sends to the sound
hardware (mcpx_apu_method). Checks, each against audio_model.py:

1. commands: tick, channel, address and number of every executed command
2. ticks: spacing of the sequencer ticks in ms (tick at 0x8003A89C)
3. registers: for every call into the voice layer the methods it caused,
   predicted from Audio.md section 5 and compared value by value
4. tables: the sample tables of audio_model.tables() against guest RAM
   from c2final_A, flip 90 (bench.py, C2), long after 0x80061AC1 ran

Since K7 the file also holds what the core is tested against: the measured
commands themselves (commands.list: tick, channel, address, number), the
SHA-256 of every table as it stands in guest RAM (16-bit little-endian), and
for every note and every release of the model whether the guest sent
VOICE_ON or VOICE_RELEASE in that call (voice_on, voice_release: tick,
channel, 0 or 1).

A breakpoint can stop twice at the same place when an interrupt falls on
the single step (Timing.md, vectors_c3.py); such direct repeats differ
only in EFLAGS.RF and the ms counter and are dropped.
"""

import collections
import hashlib
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audio_model  # noqa: E402

ROOT = audio_model.ROOT
RUN = os.path.join(ROOT, 'out', 'xemu', 'runs', 'c4_seq')
OUT = os.path.join(ROOT, 'docs', 'vectors', 'c4_audio.json')
G, POOL, MS = 0x800A7900, 0x80078550, '0x8003a89c'
DISPATCH, TICK = '0x80060305', '0x8006165e'
MRE = re.compile(rb'mcpx_apu_method 0x([0-9a-f]+) 0x([0-9a-f]+)')

# methods (xemu apu_regs.h, NV1BA0_PIO_*)
CUR, LOCK, FMT, ENV0, ENVA, ENV1, ENVF, MISC = 0x2F8, 0x2FC, 0x304, 0x308, 0x30C, 0x310, 0x314, 0x318
VBIN, VOLA, VOLB, VOLC, LFO, FCA, FCB, PITCH = 0x300, 0x360, 0x364, 0x368, 0x36C, 0x374, 0x378, 0x37C
ON, RELEASE, LBO, EBO = 0x124, 0x12C, 0x3A4, 0x3DC
ANTE, PAUSE, HRTF, BASE, SGE_I, SGE_O = 0x120, 0x140, 0x31C, 0x3A0, 0x804, 0x808
STRUCT = {CUR, LOCK}

# 0x80061AC1: mix levels per voice, passed to 0x8005190A with a bin mask
MIX_EVEN, MIX_ODD, MIX_35 = {0: 0, 1: -600}, {0: -600, 1: 0}, {0: 0, 1: -100, 2: 0}
LEVEL_SHIFT = 600       # [state+0x1C], set in 0x80051059 because the format flags are 0


def mix(ch):
    return MIX_35 if ch in (3, 5) else (MIX_EVEN if ch % 2 == 0 else MIX_ODD)


def volumes(ch, level):
    """0x8004EB1D: eight attenuations in 1/64 dB, then packed as VOLA..VOLC."""
    v = []
    for b in range(8):
        lv = level - LEVEL_SHIFT + mix(ch)[b] if b in mix(ch) else -10000
        v.append(min(0xFFF, max(0, -lv) * 64 // 100))
    a = (v[6] & 0xF) | v[0] << 4 | (v[7] & 0xF) << 16 | v[1] << 20
    b = (v[6] >> 4 & 0xF) | v[2] << 4 | (v[7] >> 4 & 0xF) << 16 | v[3] << 20
    c = (v[6] >> 8 & 0xF) | v[4] << 4 | (v[7] >> 8 & 0xF) << 16 | v[5] << 20
    return {VOLA: a, VOLB: b, VOLC: c}


def envelope(blocks, a_at, b_at):
    """0x8004EF2D (kind 1) and 0x8004EE84 (kind 0) on the two blocks of a descriptor."""
    w = {int(b['at'], 16): b['words'] for b in blocks}
    regs = {ENV0: 0, ENVA: 0, ENV1: 0, ENVF: 0, LFO: 0, 'mrel': 0, 'start': 0}
    for at in (a_at, b_at):
        k, start, delay, attack, hold, decay, release, sustain, w8, w9 = w[int(at, 16)]
        if k == 1:
            regs[ENV0] |= attack | delay << 12
            regs[ENVA] = decay | hold << 12 | (sustain & 0xFF) << 24
            regs[LFO] = release
            regs['start'] |= start << 28
        else:
            regs[ENV0] |= (w8 & 0xFF) << 24
            regs[ENV1] = attack | delay << 12 | (w9 & 0xFF) << 24
            regs[ENVF] = decay | hold << 12 | (sustain & 0xFF) << 24
            regs['mrel'] = release
            regs['start'] |= start << 24
    return regs


def load_hits():
    hits = json.load(open(os.path.join(RUN, 'probe.json')))

    def key(h):
        return ({a: v for a, v in h['mem'].items() if a != MS},
                {r: v for r, v in h['regs'].items() if r != 'eflags'})
    out, dropped = [], 0
    for i, h in enumerate(hits):
        if i and hits[i - 1]['at'] == h['at'] and key(hits[i - 1]) == key(h):
            dropped += 1
            continue
        out.append(h)
    return out, dropped


def main():
    hits, dropped = load_hits()
    trace = open(os.path.join(RUN, 'trace.log'), 'rb').read()
    run = json.load(open(os.path.join(RUN, 'run.json')))

    def gmem(h, a, fmt):
        b = bytes.fromhex(h['mem']['%#010x' % G])
        return struct.unpack_from('<' + fmt, b, a - G)[0]

    ops, ticks = [], []
    for k, h in enumerate(hits):
        if h['at'] == DISPATCH:
            rec = gmem(h, 0x800A7960, 'I')
            ptr = struct.unpack_from('<I', bytes.fromhex(h['mem']['%#010x' % POOL]), rec - POOL + 8)[0]
            ops.append({'tick': gmem(h, 0x800A7958, 'H'), 'channel': gmem(h, 0x800A7929, 'B'),
                        'addr': ptr, 'op': int(h['regs']['eax'], 16), 'hit': k})
        elif h['at'] == TICK:
            ticks.append((gmem(h, 0x800A7958, 'H'), struct.unpack('<I', bytes.fromhex(h['mem'][MS]))[0]))

    # 1. commands
    m = audio_model.Sequencer()
    while len(m.executed) < len(ops):
        m.tick()
    model = [list(x) for x in m.executed[:len(ops)]]
    meas = [[o['tick'], o['channel'], o['addr'], o['op']] for o in ops]
    cmd_bad = [i for i, (a, b) in enumerate(zip(model, meas)) if a != b]

    # 2. ticks
    spacing = collections.Counter(b[1] - a[1] for a, b in zip(ticks, ticks[1:]))

    # 3. registers
    pos = [h['trace_pos'] for h in hits] + [len(trace)]

    def methods(k):
        """Methods between hit k and the next one, without the library's
        answers to idle notices and without the notices themselves."""
        return [(int(mm.group(1), 16), int(mm.group(2), 16))
                for mm in MRE.finditer(trace, pos[k], pos[k + 1])
                if mm.start() not in answers and int(mm.group(1), 16) != 0x8000]

    # voice life from the trace: on at VOICE_ON; over when the library has
    # answered the idle notice 0x8000 with CUR h, FMT outside a lock (it does
    # so later, from its own path), see Audio.md section 5.4
    allm = [(mm.start(), int(mm.group(1), 16), int(mm.group(2), 16)) for mm in MRE.finditer(trace)]
    life, answers = [], set()
    for k, (q, a, v) in enumerate(allm):
        if a in (ON, RELEASE):
            life.append((q, a, v & 0xFFFF))
        elif (a == CUR and k + 2 < len(allm) and allm[k + 1][1] == FMT
              and allm[k + 2][1] not in (MISC, LOCK)):
            life.append((q, 0x8000, v))
            answers.add(allm[k + 1][0])
            answers.add(q)
            if allm[k + 2][1] == 0x128:
                answers.add(allm[k + 2][0])

    def state_at(h, p):
        on = rel = False
        for q, a, v in life:
            if q >= p:
                break
            if v != h:
                continue
            if a == ON:
                on, rel = True, False
            elif a == 0x8000:
                on = rel = False
            else:
                rel = True
        return on, rel

    seq = m.seq
    blocks = json.load(open(os.path.join(audio_model.DATA, 'envblocks.json')))['blocks']
    level = collections.defaultdict(int)        # library level per channel
    active = collections.defaultdict(bool)      # library has the voice running
    handle = {}
    mrel = collections.defaultdict(int)
    fmode = collections.defaultdict(int)        # 1 after the first filter call
    fc = collections.defaultdict(int)           # FCA/FCB kept by the library
    alloc = collections.defaultdict(bool)       # hardware voice exists (first sample call)
    ebo = {}
    leftover = collections.Counter()
    checked = collections.Counter()
    bad = []
    first = {}
    voice_on, voice_release = [], []    # what the guest did at every note and every release
    by_op = {j: c for j, c in zip(m.call_op, m.calls)}
    for j, o in enumerate(ops):
        if j not in by_op:
            continue
        now, ch, fn = by_op[j][:3]
        args = by_op[j][3:]
        got = methods(o['hit'])
        vals = collections.defaultdict(list)
        for a, v in got:
            vals[a].append(v)
        want = {}
        if fn == 'sample':
            d = seq['descriptors'][args[0]]
            e = envelope(blocks, d['block_a'], d['block_b'])
            mrel[ch] = e['mrel']
            handle[ch] = vals[CUR][0] if vals[CUR] else None
            alloc[ch] = True
            want = {FMT: 0x5000E0C5 if ch in (3, 5) else 0x5000E0E6,
                    MISC: 0x800000 | fmode[ch] << 16 | e['mrel'], FCA: fc[ch], FCB: fc[ch], ENV0: e[ENV0], ENVA: e[ENVA], ENV1: e[ENV1],
                    ENVF: e[ENVF], LFO: e[LFO], VBIN: 0x10622C20 if ch in (3, 5) else 0x14830820,
                    PITCH: 0}
            want.update(volumes(ch, level[ch]))
            want[HRTF] = 0xFFFF
            # the buffer: SGE entries (index, page) and a base offset into them
            sge = dict(zip(vals[SGE_I], vals[SGE_O]))
            base = vals[BASE][0] if vals[BASE] else None
            phys = int(d['data'], 16) & 0x7FFFFFFF
            ok = base is not None and sge.get(base >> 12, -1) + (base & 0xFFF) == phys
            npages = len(sge)
            checked['sample'] += 1
            if not ok:
                bad.append({'op': j, 'call': fn, 'channel': ch, 'buffer': 'base/SGE do not give %#x' % phys})
            want[BASE] = base
            ebo[ch] = d['bytes'] // 2 - 1
            if ch not in first:
                first[ch] = {'%x' % a: '%#x' % v for a, v in got if a not in (0x804, 0x808)}
            active[ch] = False
        elif fn in ('pitch_play', 'pitch'):
            want = {PITCH: (args[0] & 0xFFFF) << 16}
            if fn == 'pitch_play':
                loop = args[1]
                want[FMT] = (0x5000E0C5 if ch in (3, 5) else 0x5000E0E6) | (loop << 25)
                want[LBO] = 0
                want[EBO] = ebo[ch]
                if not state_at(handle[ch], pos[o['hit']])[0]:
                    want[ON] = 0x11000000 | handle[ch]
                    want[ANTE] = 0x1FFFF
                    want[PAUSE] = handle[ch]
        elif fn == 'level':
            level[ch] = args[0]
            if alloc[ch]:
                want = volumes(ch, args[0])
        elif fn == 'filter':
            fc[ch] = (args[0] & 0xFFFF) | args[1] << 16
            fmode[ch] = 1
            if alloc[ch]:
                want = {MISC: 0x800000 | 1 << 16 | mrel[ch], FCA: fc[ch], FCB: fc[ch]}
            elif got:
                bad.append({'op': j, 'tick': now, 'channel': ch, 'call': fn,
                            'unexpected': 'methods before the first sample call'})
        elif fn == 'stop':
            on, rel = state_at(handle[ch], pos[o['hit']]) if ch in handle else (False, False)
            if on:
                want = {RELEASE: handle[ch]}
        for a, _ in got:
            if a not in want and a not in STRUCT and not (fn == 'sample' and a in (SGE_I, SGE_O)):
                leftover['%s %x' % (fn, a)] += 1
        for a, v in want.items():
            checked[fn] += 1
            if v not in vals[a]:
                bad.append({'op': j, 'tick': now, 'channel': ch, 'call': fn, 'method': '%#x' % a,
                            'want': '%#x' % v, 'got': ['%#x' % x for x in vals[a]]})
        if fn == 'filter' and not alloc[ch] and got:
            print('filter before allocation', j, now, ch, ['%x=%x' % x for x in got])
        if fn == 'pitch_play':
            voice_on.append([now, ch, 1 if vals[ON] else 0])
        elif fn == 'stop':
            voice_release.append([now, ch, 1 if vals[RELEASE] else 0])
        if fn == 'pitch_play' and not want.get(ON) and vals[ON]:
            bad.append({'op': j, 'tick': now, 'channel': ch, 'call': fn, 'unexpected': 'voice on'})
        if fn == 'stop' and not want and vals[RELEASE]:
            bad.append({'op': j, 'tick': now, 'channel': ch, 'call': fn, 'unexpected': 'release'})

    # 4. tables
    ram_path = os.path.join(ROOT, 'out', 'xemu', 'runs', 'c2final_A', 'frame_0090', 'ram.bin')
    ram = open(ram_path, 'rb').read()
    tabs = {}
    for a, vals in audio_model.tables(junk=0).items():
        got = struct.unpack_from('<%dh' % len(vals), ram, a - 0x80000000)
        tabs['%#010x' % a] = {'values': len(vals),
                              'mismatches': sum(1 for x, y in zip(got, vals) if x != y),
                              'sha256': hashlib.sha256(struct.pack('<%dh' % len(got), *got)).hexdigest()}

    out = {
        'run': 'c4_seq',
        'tables': {'ram': 'c2final_A/frame_0090/ram.bin', 'junk_word_0x80083318': 0, 'by_address': tabs},
        'command': run.get('command'),
        'probe': run.get('probe'),
        'dropped_repeats': dropped,
        'commands': {'measured': len(meas), 'model': len(model), 'mismatches': len(cmd_bad),
                     'first_mismatch': cmd_bad[:1], 'list': meas},
        'ticks': {'count': len(ticks), 'first': ticks[0], 'last': ticks[-1],
                  'spacing_ms': {str(k): v for k, v in sorted(spacing.items())}},
        'registers': {'checked': dict(checked), 'mismatches': len(bad), 'list': bad[:50],
                      'unpredicted_methods': dict(leftover)},
        'voice_handles': {str(c): '%#x' % h for c, h in sorted(handle.items()) if h is not None},
        'first_sample_config': {str(c): v for c, v in sorted(first.items())},
        'voice_on': voice_on,
        'voice_release': voice_release,
        'calls_model': [[n, c, f] + list(a) for (n, c, f, *a) in m.calls[:len(m.calls)]
                        if n <= ticks[-1][0]],
    }
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'w') as f:
        json.dump(out, f, indent=1)
    print('commands: %d measured, %d mismatches' % (len(meas), len(cmd_bad)))
    print('ticks: %d, spacing %s' % (len(ticks), dict(sorted(spacing.items()))))
    print('registers: %s checked, %d mismatches' % (dict(checked), len(bad)))
    print('unpredicted methods:', dict(leftover))
    print('tables:', {k: v['mismatches'] for k, v in tabs.items()})
    for b in bad[:10]:
        print('  ', b)


if __name__ == '__main__':
    main()
