"""K7 checks that need Python: constants, the models, xemu's DSP core, the recordings.

    python core/test/check_k7.py out/core/native/test_k7.exe

1. Constants of core/src/Audio.c: the five doubles of 0x80061AC1 against
   out/rom/kernel.img, the three immediates of the noise generator against
   the code bytes, the own constants (ln 2, ln 10, ln 0.99988799, pi)
   against their derivation; the parameters of the GP stage, read from the
   program's immediates as gp_x() reads them, against the X memory the
   program wrote on xemu's DSP core (out/dsp/x_after.bin); the taps and
   write buffers against its two DMA chains.
2. The eight tables (`test_k7 --tables`) value by value against the model
   tools/xemu/audio_model.py and against the guest's memory (c2final_A).
3. A run of 1340 ticks (`test_k7 --run`): every command and call against
   audio_model.Sequencer; after every tick the registers of all 16 voices
   against the voice layer of tools/xemu/vectors_c4.py, and whether each
   voice runs against the render tools/xemu/audio_render.py.
4. The same run, the voices: every channel in the bins 0, 1 and 11 against
   the render, sample by sample. The render computes in double and with
   numpy's functions, the core in double with its own exp and sin and hands
   out float32; the bound is measured and printed.
5. The GP stage (`test_k7 --gp`) against the program itself on xemu's DSP
   core (out/dsp/gp.exe, tools/dsp/build.py): noise in the bins 0, 1 and 11,
   loud enough to reach the limiter, every sample of both sides.
6. The output against the recordings with the measures of C4 (Audio.md 7):
   without DSP, compare_audio.py on the core's voices against c2audio; with
   DSP, compare_dsp.compare on the core's own output per channel (each
   channel alone through the GP stage) against c4_dsp_out. The numbers are
   set next to those of docs/vectors/c4_recording*.json.

Parts 5 and 6 and the guest's memory in part 2 need local files that are
not in the repo; what is missing is said and skipped.
"""

import json
import math
import os
import re
import struct
import subprocess
import sys

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'xemu'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'dsp'))
import audio_model  # noqa: E402
import audio_render  # noqa: E402
import compare_dsp  # noqa: E402
import vectors_c4 as V  # noqa: E402

KERNEL = os.path.join(ROOT, 'out', 'rom', 'kernel.img')
PACK = os.path.join(ROOT, 'out', 'pack', 'xboot.pak')
RUNS = os.path.join(ROOT, 'out', 'xemu', 'runs')
VEC = os.path.join(ROOT, 'docs', 'vectors')
TMP = os.path.join(ROOT, 'out', 'core', 'k7')
GP_EXE = os.path.join(ROOT, 'out', 'dsp', 'gp.exe')
GP_PROGRAM = os.path.join(ROOT, 'data', 'audio', 'gp_program.bin')
BASE = 0x80010000
TICKS, TICK_BLOCKS, BLOCK, NCH = 1340, 9, 32, 16
TABLES = [0x800A1B78, 0x80093B78, 0x80083B70, 0x80083328, 0x80078898, 0x800970F8, 0x800A5B80, 0x80093C78]

checks = failed = 0


def check(ok, what):
    global checks, failed
    checks += 1
    failed += not ok
    print(('ok    ' if ok else 'FAIL  ') + what)


def source():
    return open(os.path.join(ROOT, 'core', 'src', 'Audio.c'), encoding='utf-8').read()


# ---------------------------------------------------------------- 1. constants

def gp_x(prog, a):
    """X:a as gp_x() in Audio.c takes it from the program's immediates."""
    if 0x1DE <= a <= 0x1E3:
        a = a - 0x1DE + 0x67
    if 0x2E <= a <= 0x8B:
        return prog[0x050A + 3 * (a - 0x2E)]
    if 0x8C <= a <= 0x92:
        return prog[0x04AF + 3 * (a - 0x8C)]
    if 0x93 <= a <= 0x96:
        return prog[0x04C9 + 3 * (a - 0x93)]
    return None


def constants():
    src = source()
    img = open(KERNEL, 'rb').read()
    found = re.findall(r'#define\s+(K_\w+_BITS)\s+0x([0-9A-F]{16})ull\s+/\*\s*(0x[0-9A-Fa-f]{8})', src)
    bad = [n for n, bits, addr in found
           if struct.unpack_from('<Q', img, int(addr, 16) - BASE)[0] != int(bits, 16)]
    check(len(found) == 5 and not bad, f'{len(found)} doubles of Audio.c equal the kernel image, {len(bad)} differ {bad}')
    imm = {n: int(v, 16) for n, v in re.findall(r'#define\s+(NOISE_\w+)\s+0x([0-9A-F]+)u', src)}
    code = img[0x80061A9E - BASE:0x80061A9E - BASE + 0x40]
    ok = (struct.pack('<I', imm['NOISE_MUL']) in code and struct.pack('<I', imm['NOISE_ADD']) in code and
          struct.pack('<I', imm['NOISE_SEED']) in img[0x80061AC1 - BASE:0x80061AC1 - BASE + 0x10])
    check(len(imm) == 3 and ok, '3 immediates of the noise generator are in the code at 0x80061A9E and 0x80061AC1')

    own = dict(re.findall(r'#define\s+(M_PI_D|LN2|LN2_HI|LN2_LO|LN10|K_DECAY_LN)\s+\(?(-?[0-9.]+(?:e[-+]?[0-9]+)?)\)?', src))
    want = {'M_PI_D': math.pi, 'LN2': math.log(2), 'LN10': math.log(10), 'K_DECAY_LN': math.log(0.99988799)}
    bad = [k for k, v in want.items() if float(own[k]) != v]
    hi, lo = float(own['LN2_HI']), float(own['LN2_LO'])
    # the upper part must leave room for the product with k (21 bits free), and both together give ln 2
    split = hi + lo == math.log(2) and struct.unpack('<Q', struct.pack('<d', hi))[0] & 0x1FFFFF == 0
    check(not bad and split, f'own constants of Audio.c equal their derivation (pi, ln 2 in two parts, ln 10, ln 0.99988799) {bad}')

    prog = [w & 0xFFFFFF for w in struct.unpack('<%dI' % 0x625, open(GP_PROGRAM, 'rb').read())]
    xpath = os.path.join(ROOT, 'out', 'dsp', 'x_after.bin')
    if not os.path.exists(xpath):
        print('      out/dsp/x_after.bin not present, GP parameters not compared with the program\'s X memory')
        return
    import reverb_model
    p = reverb_model.Params(xpath)
    states = {0x6E, 0x71, 0x74, 0x77, 0x7A, 0x7D, 0x80, 0x83, 0x86, 0x89}       # filter states, they run
    addrs = [a for a in list(range(0x2E, 0x97)) + list(range(0x1DE, 0x1E4)) if a not in states]
    bad = [hex(a) for a in addrs if gp_x(prog, a) != p.x[a]]
    half = (prog[0x024B] >> 8 & 0xFF) << 16
    check(not bad and half == 0x400000, f'{len(addrs)} parameters of the GP stage, read from the program\'s immediates, equal '
          f'X memory after its set-up on xemu\'s DSP core ({len(bad)} differ {bad[:6]}); the short immediate at P:0x024B is 0x40')

    # the delay lines: the core's tap table against the DMA chains
    taps = [(int(a), int(b)) for a, b in re.findall(r'\{ (\d+), (\d+) \}', src[src.index('gp_tap[29]'):src.index('gp_from[GP_LINES]')])]
    frm = [int(v) for v in re.search(r'gp_from\[GP_LINES\] = \{([^}]*)\}', src).group(1).split(',')]
    lines = [w['line'] for w in p.writes]                   # bases in the order the chain writes them
    order = sorted(set(lines), key=lines.index)
    core_lines = [order[i] for i in (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11)]
    woff = {w['line']: w['off'] for w in p.writes}
    size = {w['line']: w['n'] for w in p.writes}
    chain = {}
    for r in p.reads:
        chain[(r['buf'] - 0x204) // 0x20] = (r['line'], (woff[r['line']] - r['off']) % r['n'])
    bad = []
    for k, (line, delay) in enumerate(taps):
        if chain.get(k) != (core_lines[line], delay):
            bad.append((k, chain.get(k), delay))
    dead = [k for k in chain if k >= len(taps)]
    wbuf = [(w['buf'] - 0x204) // 0x20 for w in p.writes]
    lens = [size[b] for b in core_lines]
    ok_len = lens == [(gp_x(prog, 0x2F + 2 * i) + 1) // 4 for i in range(12)]
    check(len(taps) == 29 and not bad and wbuf[:12] == frm and ok_len,
          f'{len(taps)} taps of {len(core_lines)} delay lines and their write buffers equal the DMA chains X:0x105 and X:0xA3 '
          f'({len(p.reads)} reads, {len(p.writes)} writes; {len(bad)} differ {bad[:4]}); ring lengths {sum(lens)} samples')
    # the two lines the core leaves out: their taps and what feeds them
    feed = p.c(0x65)
    check(sorted(dead) == [29, 30] and wbuf[12:] == [9, 10] and feed == 0.0,
          f'left out: buffers {sorted(dead)} read the lines written from buffers {wbuf[12:]}, fed with X:0x65 = {feed}')


# ---------------------------------------------------------------- 2. tables

def tables(exe):
    os.makedirs(TMP, exist_ok=True)
    subprocess.run([exe, PACK, '--tables', TMP], check=True)
    core = [np.fromfile(os.path.join(TMP, f'table{i}.bin'), '<i2') for i in range(len(TABLES))]
    model = audio_model.tables(junk=0)
    n = sum(len(c) for c in core)
    bad = [hex(a) for a, c in zip(TABLES, core) if list(c) != list(model[a])]
    check(not bad, f'eight tables, {n} values, equal the model audio_model.py ({len(bad)} differ {bad})')
    ram_path = os.path.join(RUNS, 'c2final_A', 'frame_0090', 'ram.bin')
    if not os.path.exists(ram_path):
        print('      c2final_A not present, tables not compared with the guest\'s memory')
        return
    ram = open(ram_path, 'rb').read()
    bad = [hex(a) for a, c in zip(TABLES, core)
           if list(c) != list(struct.unpack_from('<%dh' % len(c), ram, a - 0x80000000))]
    check(not bad, f'eight tables, {n} values, equal the guest\'s memory in c2final_A, flip 90 ({len(bad)} differ {bad})')


# ---------------------------------------------------------------- 3. and 4. the run

def model_run():
    """audio_render.render() with the state after every tick kept: the
    registers the voice layer has sent (as vectors_c4.py predicts them) and
    the flags of the render after the first frame of the tick."""
    tabs = audio_model.tables()
    seq = audio_model.Sequencer()
    lib = audio_render.Library(seq, tabs)
    n = TICKS * TICK_BLOCKS * BLOCK
    stems = {b: np.zeros((NCH, n)) for b in (0, 1, 11)}
    reg = [dict(fmt=0, vbin=0, misc=0, pitch=0, fca=0, fmode=0, mrel=0, ebo=0, sample=0) for _ in range(NCH)]
    states, cmds, calls, done = [], [], [], 0
    for _ in range(TICKS):
        for v in lib.v:
            if v.lib_on and not v.active:
                v.lib_on = False
        k, e = len(seq.calls), len(seq.executed)
        seq.tick()
        cmds += [list(x) for x in seq.executed[e:]]
        for c in seq.calls[k:]:
            now, ch, fn = c[:3]
            r, v = reg[ch], lib.v[ch]
            effect = 0
            base = 0x5000E0C5 if ch in (3, 5) else 0x5000E0E6
            if fn == 'sample':
                d = seq.desc[c[3]]
                env = V.envelope(lib.blocks, d['block_a'], d['block_b'])
                r.update(fmt=base, vbin=0x10622C20 if ch in (3, 5) else 0x14830820, mrel=env['mrel'], pitch=0,
                         ebo=d['bytes'] // 2 - 1, sample=c[3])
                r['misc'] = 0x800000 | r['fmode'] << 16 | r['mrel']
            elif fn == 'filter':
                r['fca'], r['fmode'] = (c[3] & 0xFFFF) | c[4] << 16, 1
                if v.alloc:
                    r['misc'] = 0x800000 | 1 << 16 | r['mrel']
            elif fn in ('pitch_play', 'pitch') and v.alloc:
                r['pitch'] = (c[3] & 0xFFFF) << 16
                if fn == 'pitch_play':
                    r['fmt'] = base | int(c[4]) << 25
                    effect = 0 if v.lib_on else 1
            elif fn == 'stop':
                effect = 1 if v.lib_on else 0
            calls.append([now, ch, fn, c[3] if len(c) > 3 else 0, int(c[4]) if len(c) > 4 else 0, effect])
            lib.apply(c)
        for f in range(TICK_BLOCKS):
            s = slice(done, done + BLOCK)
            for v in lib.v:
                o = v.frame()
                if o:
                    for b in (0, 1, 11):
                        if b in o:
                            stems[b][v.ch, s] = o[b]
            done += BLOCK
            if f == 0:
                row = []
                for ch, v in enumerate(lib.v):
                    r, e = reg[ch], v.env
                    row.append([int(v.alloc), int(v.lib_on), int(v.active), r['sample'], v.level, r['fmt'], r['vbin'],
                                r['misc'], e.get(V.ENV0, 0), e.get(V.ENVA, 0), e.get(V.ENV1, 0), e.get(V.ENVF, 0),
                                e.get(V.LFO, 0), r['fca'], v.vol[V.VOLA], v.vol[V.VOLB], v.vol[V.VOLC], r['pitch'],
                                r['ebo']])
                states.append(row)
    return cmds, calls, states, stems


def core_run(exe):
    os.makedirs(TMP, exist_ok=True)
    blocks = TICKS * TICK_BLOCKS
    subprocess.run([exe, PACK, '--run', TMP, str(blocks)], check=True)
    cmds, calls, states = [], [], []
    for line in open(os.path.join(TMP, 'ticks.txt')):
        w = line.split()
        if w[0] == 'C':
            cmds.append([int(x) for x in w[1:]])
        elif w[0] == 'F':
            calls.append([int(w[1]), int(w[2]), w[3], int(w[4]), int(w[5]), int(w[6])])
        else:
            if int(w[2]) == 0:
                states.append([])
            states[-1].append([int(x) for x in w[3:]])
    voices = np.fromfile(os.path.join(TMP, 'voices.f32'), '<f4').reshape(blocks, NCH, BLOCK)
    gains = np.fromfile(os.path.join(TMP, 'gains.f32'), '<f4').reshape(blocks, NCH, 3)
    stems = {}
    for k, b in enumerate((0, 1, 11)):
        x = voices.astype(float) * gains[:, :, k:k + 1].astype(float)
        stems[b] = x.transpose(1, 0, 2).reshape(NCH, blocks * BLOCK)
    bins = np.fromfile(os.path.join(TMP, 'bins.i32'), '<i4').reshape(blocks, 3, BLOCK).transpose(1, 0, 2).reshape(3, -1)
    out = np.fromfile(os.path.join(TMP, 'out.i32'), '<i4').reshape(blocks, 2, BLOCK).transpose(1, 0, 2).reshape(2, -1)
    return cmds, calls, states, stems, bins, out


def against_model(exe):
    cmds, calls, states, stems, bins, out = core_run(exe)
    m_cmds, m_calls, m_states, m_stems = model_run()
    bad = sum(1 for a, b in zip(cmds, m_cmds) if a != b) + abs(len(cmds) - len(m_cmds))
    check(bad == 0, f'{TICKS} ticks: {len(cmds)} commands, {bad} differ from the model audio_model.py')
    # a call's arguments: the model keeps the unsigned filter word and the signed pitch as the core does
    bad = sum(1 for a, b in zip(calls, m_calls) if a != b) + abs(len(calls) - len(m_calls))
    notes = sum(1 for c in calls if c[2] == 'pitch_play')
    started = sum(c[5] for c in calls if c[2] == 'pitch_play')
    check(bad == 0, f'{TICKS} ticks: {len(calls)} calls with their arguments and whether a note starts or a release '
          f'releases the voice, {bad} differ from model and render; {started} of {notes} notes start the voice')
    flat = lambda rows, cols: [[r[c] for c in cols] for tick in rows for r in tick]  # noqa: E731
    regs, flags = list(range(3, 19)), [0, 1, 2]
    bad_r = sum(1 for a, b in zip(flat(states, regs), flat(m_states, regs)) if a != b)
    bad_f = sum(1 for a, b in zip(flat(states, flags), flat(m_states, flags)) if a != b)
    n = len(states) * NCH
    check(len(states) == TICKS and bad_r == 0, f'{TICKS} ticks: the registers of {NCH} voices after every tick '
          f'({n * len(regs)} values), {bad_r} voices differ from the voice layer of vectors_c4.py')
    check(bad_f == 0, f'{TICKS} ticks: allocated, running for the library, running in the hardware after the first '
          f'block of every tick ({n * 3} flags), {bad_f} voices differ from the render audio_render.py')

    worst, peak, where = 0.0, 0.0, None
    for b in (0, 1, 11):
        d = np.abs(stems[b] - m_stems[b])
        peak = max(peak, float(np.abs(m_stems[b]).max()))
        if d.max() > worst:
            worst = float(d.max())
            where = (b, int(np.unravel_index(d.argmax(), d.shape)[0]))
    sounding = sum(int(m_stems[b][c].any()) for b in (0, 1, 11) for c in range(NCH))
    check(worst < 1e-6, f'{TICKS * TICK_BLOCKS * BLOCK} samples of {sounding} sounding channel-bin pairs: largest difference '
          f'to the render {worst:.2e} (bin {where[0]}, channel {where[1]}), largest sample {peak:.3f}')
    # the bins the GP gets are the sum of the voices, as 24-bit values
    worst = 0.0
    for k, b in enumerate((0, 1, 11)):
        worst = max(worst, float(np.abs(bins[k] / 8388608.0 - m_stems[b].sum(axis=0)).max()))
    check(worst < 2e-6, f'the three mix bins as 24-bit values: largest difference to the render\'s sum {worst:.2e} '
          f'(half a step is {0.5 / 8388608:.1e})')
    return stems, bins, out, m_stems


# ---------------------------------------------------------------- 5. GP stage

def harness(x):
    """x: frames x 3 x 32 int32 (bins 0, 1, 11) -> frames x 2 x 32 int32 from
    the program on xemu's DSP core, 100 silent frames first (gp_ir.py)."""
    lead = 100
    frames = x.shape[0] + lead
    full = np.zeros((frames, 32, BLOCK), np.float32)
    for k, b in enumerate((0, 1, 11)):
        full[lead:, b, :] = x[:, k, :] / 8388608.0          # exact in float32
    fin, fout = os.path.join(TMP, 'gp_in.f32'), os.path.join(TMP, 'gp_ref.f32')
    full.tofile(fin)
    r = subprocess.run([GP_EXE, 'run', GP_PROGRAM, fin, fout, str(frames)], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stderr)
    y = np.fromfile(fout, np.float32).reshape(frames, 2, BLOCK)[lead:]
    return np.rint(y.astype(float) * 8388608.0).astype(np.int32)


def core_gp(exe, x):
    fin, fout = os.path.join(TMP, 'gp_in.i32'), os.path.join(TMP, 'gp_out.i32')
    x.astype('<i4').tofile(fin)
    subprocess.run([exe, PACK, '--gp', fin, fout, str(x.shape[0])], check=True)
    return np.fromfile(fout, '<i4').reshape(x.shape[0], 2, BLOCK)


def gp_stage(exe):
    if not os.path.exists(GP_EXE):
        print('      out/dsp/gp.exe not present (tools/dsp/build.py), GP stage not compared with the program')
        return
    rng = np.random.default_rng(7)
    frames = 3000
    x = np.zeros((frames, 3, BLOCK), np.int64)
    x[0:600] = rng.integers(-0x100000, 0x100000, (600, 3, BLOCK))            # an eighth of full scale
    x[600:1200] = rng.integers(-0x800000, 0x800000, (600, 3, BLOCK))         # full scale
    x[1200:1400, 2] = 0x7FFFFF                                               # bin 11 held at the top
    x[1400:1600, 2] = -0x800000
    x[1600:1700, :2] = rng.integers(-0x800000, 0x800000, (100, 2, BLOCK))    # then the tail runs out
    x = x.astype(np.int32)
    ref, got = harness(x), core_gp(exe, x)
    bad = int((ref != got).sum())
    limited = int(((np.abs(ref) >= 0x7FFFFF) | (ref == -0x800000)).sum())
    tail = int(np.nonzero(ref.reshape(frames, -1).any(axis=1))[0][-1])
    check(bad == 0, f'GP stage on noise: {ref.size} samples of {frames} frames equal the program on xemu\'s DSP core '
          f'({bad} differ); {limited} at the limit, the last frame that is not 0 is {tail}')


# ---------------------------------------------------------------- 6. recordings

def wav_present(run):
    return os.path.exists(os.path.join(RUNS, run, 'audio.wav'))


def recordings(exe, stems, bins, out):
    n = stems[0].shape[1]
    # what xemu's VP monitor plays: each voice with the largest of its bin gains (Audio.md 6.5).
    # That is bin 0 for even channels and for 3 and 5, bin 1 for the other odd ones.
    mon = np.array([stems[0][c] if c % 2 == 0 or c in (3, 5) else stems[1][c] for c in range(NCH)])
    if wav_present('c2audio'):
        npz = os.path.join(TMP, 'core_render.npz')
        np.savez_compressed(npz, bin0=bins[0] / 8388608.0, bin1=bins[1] / 8388608.0, bin11=bins[2] / 8388608.0,
                            stems=stems[0] + stems[1], stems11=stems[11], stems_mon=mon, stems0=stems[0],
                            stems1=stems[1], rate=48000, tick_samples=TICK_BLOCKS * BLOCK)
        js = os.path.join(TMP, 'cmp_mon.json')
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'xemu', 'compare_audio.py'), '--render', npz,
                        '--json', js], check=True, capture_output=True)
        got = json.load(open(js))['components']
        want = json.load(open(os.path.join(VEC, 'c4_recording.json')))['components']
        print('      without DSP, c2audio: channel  corr at 0 (C4)   best shift ms (C4)   level dB (C4)')
        dc = dl = 0.0
        for c in sorted(want, key=int):
            g, w = got.get(c, {}), want[c]
            if 'corr_at_0' not in g or 'corr_at_0' not in w:
                dc = 9.0
                continue
            print(f"        {int(c):2d}   {g['corr_at_0']:.4f} ({w['corr_at_0']:.4f})   {g['best_shift_ms']:+5.0f} "
                  f"({w['best_shift_ms']:+5.0f})   {g['level_db_vs_global']:+.2f} ({w['level_db_vs_global']:+.2f})")
            dc = max(dc, abs(g['corr_at_0'] - w['corr_at_0']))
            dl = max(dl, abs(g['level_db_vs_global'] - w['level_db_vs_global']))
        check(set(got) == set(want) and dc <= 0.001 and dl <= 0.01,
              f'without DSP against c2audio: {len(want)} channels, the measures of C4 met; largest difference to '
              f'c4_recording.json {dc:.4f} in the correlation, {dl:.2f} dB in the level')
    else:
        print('      recording c2audio not present, the comparison without DSP is skipped')

    if not wav_present('c4_dsp_out'):
        print('      recording c4_dsp_out not present, the comparison with DSP is skipped')
        return
    # the core's own output per channel: each channel alone through the GP stage
    frames = n // BLOCK
    exp = {'L': np.zeros((NCH, n)), 'R': np.zeros((NCH, n))}
    total = np.zeros((2, n))
    for c in range(NCH):
        if not (stems[0][c].any() or stems[1][c].any() or stems[11][c].any()):
            continue
        x = np.stack([np.rint(stems[b][c] * 8388608.0) for b in (0, 1, 11)]).astype(np.int32)
        y = core_gp(exe, x.reshape(3, frames, BLOCK).transpose(1, 0, 2))
        y = y.transpose(1, 0, 2).reshape(2, n)
        exp['L'][c], exp['R'][c] = y[0] / 8388608.0, y[1] / 8388608.0
        total += y
    lin = float(np.abs(total - out).max())
    check(lin <= 1024, f'with DSP: the channels alone through the GP stage add up to the output of the whole mix but for '
          f'{lin:.0f} steps of 2^-23, {lin / 8388608:.1e} (linear but for its rounding, Audio.md 6.3; the output is '
          f'shifted up by 7 bits, so one step inside is 128 at the output)')
    got, st = compare_dsp.compare(exp, 'c4_dsp_out')
    want = json.load(open(os.path.join(VEC, 'c4_recording_dsp.json')))
    dc = dl = ds = 0.0
    print('      with DSP, c4_dsp_out: side channel  corr at 0 (C4)   level dB (C4)')
    for side in 'LR':
        for c in sorted(want['sides'][side]['components'], key=int):
            g, w = got['sides'][side]['components'].get(c), want['sides'][side]['components'][c]
            if g is None:
                dc = 9.0
                continue
            print(f"        {side} {int(c):2d}   {g['corr_at_0']:.4f} ({w['corr_at_0']:.4f})   {g['level_db']:+.2f} ({w['level_db']:+.2f})")
            dc = max(dc, abs(g['corr_at_0'] - w['corr_at_0']))
            dl = max(dl, abs(g['level_db'] - w['level_db']))
    for c, w in want['stereo_left_minus_right'].items():
        ds = max(ds, abs(st.get(c, {'recorded_db': 99})['recorded_db'] - w['recorded_db']),
                 abs(st.get(c, {'expected_db': 99})['expected_db'] - w['expected_db']))
    same = sum(1 for c, v in st.items() if (v['expected_db'] > 0) == (v['recorded_db'] > 0))
    same_c4 = sum(1 for v in want['stereo_left_minus_right'].values() if (v['expected_db'] > 0) == (v['recorded_db'] > 0))
    check(dc <= 0.02 and dl <= 0.2 and ds <= 0.2 and abs(got['offset']['seconds'] - want['offset']['seconds']) < 0.011,
          f"with DSP against c4_dsp_out: offset {got['offset']['seconds']:.2f} s (C4 {want['offset']['seconds']:.2f}); "
          f'largest difference to c4_recording_dsp.json {dc:.4f} in the correlation, {dl:.2f} dB in the level, '
          f'{ds:.2f} dB in left minus right; the sign of the balance is right for {same} of {len(st)} channels (C4 {same_c4})')

    # the two channels recorded alone (Audio.md 7): balance of the sides
    for run, c in (('c4_dsp_solo1', 1), ('c4_dsp_solo9', 9)):
        if not wav_present(run):
            print(f'      recording {run} not present, skipped')
            continue
        rec = compare_dsp.load_wav(os.path.join(RUNS, run, 'audio.wav'))
        P = {s: compare_dsp.stft_power(exp[s][c]) for s in 'LR'}
        R = {'L': compare_dsp.stft_power(rec[0]), 'R': compare_dsp.stft_power(rec[1])}
        k0, _ = compare_dsp.xcorr_offset(compare_dsp.env(R['L'] + R['R']), compare_dsp.env(P['L'] + P['R']), 0,
                                         R['L'].shape[1] - P['L'].shape[1])
        nf = P['L'].shape[1]
        floor = {s: float(np.median(R[s][:, :max(10, k0 - 20)])) for s in 'LR'}
        m = (P['L'] + P['R']) > 1e-3 * (P['L'] + P['R']).max()
        e = {s: float((R[s][:, k0:k0 + nf][m] - floor[s]).sum()) for s in 'LR'}
        p = {s: float(P[s][m].sum()) for s in 'LR'}
        bal_rec, bal_core = 10 * math.log10(e['L'] / e['R']), 10 * math.log10(p['L'] / p['R'])
        check(abs(bal_rec - bal_core) <= 0.1, f'{run}: channel {c} alone, left minus right {bal_rec:+.2f} dB in the '
              f'recording, {bal_core:+.2f} dB in the core')


def main():
    exe = os.path.abspath(sys.argv[1])
    constants()
    tables(exe)
    stems, bins, out, _ = against_model(exe)
    gp_stage(exe)
    recordings(exe, stems, bins, out)
    print(f'{checks} checks, {failed} failed')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
