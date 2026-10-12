"""Checks the timing model of docs/decomp/Timing.md against a probe run.

    python tools/xemu/probe.py --name c3_timing2 --at 0x80057949 --at 0x80057a48 \
        --at 0x800579aa --read 0x8006f050:0xc0 --read 0x8006f6dc:4 \
        --read 0x80070868:0x14 --read 0x80065e40:0x30 --max 3000 --stop-at 0x80057a48
    python tools/xemu/vectors_c3.py c3_timing2 [--out docs/vectors/c3_timing.json]

The model is written from Timing.md, not from the run: the pulse table from
the generator with seed 0x76543210, then per frame t, x18..x24 from the
tick values the guest stored in this+0x28. Every float the guest stores
is rounded to float32 here at the same place. Breakpoints:

    0x80057949  frame step entered (state before the step)
    0x800579aa  update 0x800571ad returned (state after the step)
    0x80057a48  frame loop left
"""

import argparse
import json
import math
import os
import struct

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OBJ = 0x8006F050
f32 = np.float32

# Constants with their addresses (Timing.md, section "Konstanten").
C_MS = f32(0.001)          # 0x800128A0
C_END = f32(8.0)           # 0x80065E54
C_RAMP0 = f32(0.6)         # 0x80065E60
C_2 = f32(2.0)             # 0x80065E58
C_08 = f32(0.8)            # 0x80065E5C
C_BASE = f32(0.0)          # 0x80065E64
C_PI = f32(math.pi)        # 0x80065E4C


def rng_step(s):
    """0x8005A645: s' = ror(s, 13) - (s - 11), 32 bit."""
    r = ((s >> 13) | (s << 19)) & 0xFFFFFFFF
    return (r - ((s - 11) & 0xFFFFFFFF)) & 0xFFFFFFFF


class Rng:
    def __init__(self, seed):
        self.s = seed

    def u01(self):          # 0x80056A60
        self.s = rng_step(self.s)
        return float(self.s & 0xFFFF) * 2.0 ** -16

    def pm1(self):          # 0x80056A82
        self.s = rng_step(self.s)
        return float(self.s & 0xFFFF) * 2.0 ** -15 - 1.0


def r32(x):
    """A store to a float32 in memory."""
    return float(f32(x))


# Float32 constants as the code loads them from memory.
K = {name: float(v) for name, v in (('c003', f32(0.03)), ('c113', f32(1 / 13)), ('c12', f32(1.2)),
                                    ('c005', f32(0.05)), ('c01', f32(0.1)), ('c02', f32(0.2)),
                                    ('c04', f32(0.4)), ('c06', C_RAMP0), ('c8', C_END),
                                    ('c2', C_2), ('c08', C_08), ('base', C_BASE),
                                    ('pi', C_PI), ('ms', C_MS))}


def pulse_table(seed=0x76543210):
    """0x80056EF6: twelve (t0, w, a), float32 as stored at this+0x30.

    The generator at this+0x68C is shared. Before the table the
    constructor draws once (0x8005AFB6 -> 0x8005A6CB(-1)); after it the
    set-up draws once more (0x8005A41D -> 0x8005A14F). Returns the table
    and the state after all 38 draws. Every x87 operation is one Python
    float operation: the FPU control word is 0x027F, 53-bit precision
    (Timing.md, "Rechengenauigkeit")."""
    g = Rng(rng_step(seed))
    out = []
    for i in range(1, 13):
        x = g.pm1() * K['c003'] + float(i) * K['c113']
        y = 1.0 - (x + 1.0) * x * 0.5
        y32 = r32(y)                                    # [esi]
        q = r32(K['c12'] - y)                           # [ebp-4]
        w = (g.u01() + 2.0) * q * q * K['c005']
        if K['c01'] > w:
            w = K['c01']
        w = r32(w)                                      # [esi+4]
        a = r32((g.u01() + 1.0) * (y32 + 0.5) * K['c02'])
        t0 = r32((K['c8'] - K['c2'] - K['c04'] - K['c06']) * y32 + K['c06'])
        lo = K['c06'] + w
        out.append([r32(t0 if lo < t0 else lo), w, a])
    out[11][0] = r32(K['c06'] + out[11][1])
    out[11][2] = r32(out[11][2] * 3.0)
    return [[f32(v) for v in row] for row in out], rng_step(g.s)


def pulses(table, t):
    """0x80057011: sum of a * cos(pi/2 * |t - t0| / w) over |t - t0| <= w."""
    s = 0.0
    for t0, w, a in table:
        d = abs(r32(float(t) - float(t0)))
        if not d > float(w):
            ph = r32(K['pi'] / float(w) * d * 0.5)
            s = r32(r32(math.cos(ph)) * float(a) + s)
    return s


def ramp(t):
    """this+0x1C before the pulses: 0 below 1.1, then a quadratic rise."""
    start = K['c06'] + 0.5
    t = float(t)
    if t < start:
        return f32(0.0)
    u = 1.0 / (K['c8'] - K['c2'] - K['c08'] - start) * (t - start)
    return f32(K['base'] + (1.0 - K['base']) * ((u + 1.0) * u * 0.5))


def step(state, table, d_ms):
    """One call of 0x80057949 -> 0x800571AD for the time fields.

    fst stores a float32 but leaves the unrounded value in st0; the code
    goes on with that value. So the ramp sees t before rounding, and x24
    and x18 see x20 before rounding."""
    dt = r32(float(d_ms) * K['ms'])
    if dt > 1.0:
        dt = K['ms']
    t_reg = dt + float(state['t'])          # st0 after fadd [esi+0x10]
    t = r32(t_reg)                          # fst [esi+0x10]
    x1c = ramp(t_reg)
    x20_reg = pulses(table, t) + float(x1c)  # argument of 0x80057011 is stored, so float32
    h = dt * 0.5
    x24 = r32((1.0 - h) * float(state['x24']) + x20_reg * h)
    x18 = r32(x20_reg * dt + float(state['x18']))
    return {'t': f32(t), 'x18': f32(x18), 'x1c': x1c, 'x20': f32(x20_reg), 'x24': f32(x24)}


def fields(h):
    b = bytes.fromhex(h['mem'][f'{OBJ:#010x}'])
    f = {k: f32(struct.unpack_from('<f', b, o)[0])
         for k, o in (('t', 0x10), ('x14', 0x14), ('x18', 0x18), ('x1c', 0x1C),
                      ('x20', 0x20), ('x24', 0x24))}
    f['last'] = struct.unpack_from('<I', b, 0x28)[0]
    f['first'] = struct.unpack_from('<I', b, 0x2C)[0]
    f['flags'] = b[8:0x0F].hex()
    f['table'] = [[f32(x) for x in struct.unpack_from('<3f', b, 0x30 + 12 * i)] for i in range(12)]
    return f


def check_run(name, table, seed_after):
    rundir = os.path.join(ROOT, 'out', 'xemu', 'runs', name)
    hits = json.load(open(os.path.join(rundir, 'probe.json')))
    run = json.load(open(os.path.join(rundir, 'run.json')))
    # A single step over a breakpoint can enter an interrupt handler; the
    # breakpoint then hits a second time with nothing changed. Keep the first.
    hits = [h for i, h in enumerate(hits) if not (i and h['at'] == hits[i - 1]['at'])]
    before = [h for h in hits if h['at'] == '0x80057949']
    after = [h for h in hits if h['at'] == '0x800579aa']
    end = [h for h in hits if h['at'] == '0x80057a48']
    f0 = fields(before[0])
    report = {'run': name, 'command': run.get('command'), 'inputs': run.get('inputs'),
              'probe': run.get('probe'), 'frames': len(before), 'checks': {}}

    # 1. Pulse table from the generator equals the guest's table.
    tab_ok = all(np.array_equal(np.array(x, dtype=np.float32), np.array(y, dtype=np.float32))
                 for x, y in zip(table, f0['table']))
    bt = {'equal': tab_ok, 'generator_state_after': f'{seed_after:#010x}',
          'table_guest': [[float(v) for v in r] for r in f0['table']]}
    if '0x8006f6dc' in before[0]['mem']:
        gs = struct.unpack('<I', bytes.fromhex(before[0]['mem']['0x8006f6dc']))[0]
        bt['guest_state_this_0x68c'] = f'{gs:#010x}'
    report['checks']['pulse_table'] = bt

    # 2. Constants block and the start flag, read from the running guest.
    if '0x80065e40' in before[0]['mem']:
        cb = bytes.fromhex(before[0]['mem']['0x80065e40'])
        report['checks']['constants_0x80065e4c'] = [
            struct.unpack_from('<f', cb, o)[0] for o in range(0x0C, 0x28, 4)]
    if '0x8003aeb8' in before[0]['mem']:
        report['checks']['flag_0x8003aeb8'] = before[0]['mem']['0x8003aeb8'][:8]
    report['checks']['start'] = {'t': float(f0['t']), 'tick_this_0x2c': f0['first'],
                                 'pokes': [h['poked'] for h in hits if 'poked' in h]}

    # 3. Step by step: model from the stored ticks vs guest after the step.
    # this+0x28 is written after the update returns; the probe at
    # 0x800579aa still sees the old value, the next frame entry the new one.
    state = {'t': f0['t'], 'x18': f0['x18'], 'x24': f0['x24']}
    bad, rows, last = [], [], f0['last']
    for i, h in enumerate(after):
        g = fields(h)
        if g['first'] != f0['first']:
            bad.append((i, 'this+0x2c changed'))
        nxt = before[i + 1] if i + 1 < len(before) else (end[0] if end else None)
        if nxt is None:
            break
        tick = fields(nxt)['last']
        d = abs(tick - last)
        last = tick
        state = step(state, table, d)
        diff = [k for k in ('t', 'x18', 'x1c', 'x20', 'x24') if state[k] != g[k]]
        if diff or g['x14'] != 0:
            bad.append((i, diff, {k: [float(state[k]), float(g[k])] for k in diff}))
        rows.append([i, tick, d, float(g['t']), float(g['x18']), float(g['x1c']),
                     float(g['x20']), float(g['x24'])])
    report['checks']['steps'] = {'checked': len(rows), 'mismatches': len(bad),
                                 'first_mismatches': bad[:10]}
    report['checks']['end'] = {
        't_before_last_step': float(fields(before[-1])['t']),
        't_after_loop': float(fields(end[0])['t']) if end else None,
        'ticks_total': (fields(end[0])['last'] - f0['first']) if end else None}
    report['columns'] = ['step', 'tick', 'd_ms', 't', 'x18', 'x1c', 'x20', 'x24']
    report['steps'] = rows
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('runs', nargs='+')
    ap.add_argument('--out', default=os.path.join(ROOT, 'docs', 'vectors', 'c3_timing.json'))
    a = ap.parse_args()
    table, seed_after = pulse_table()
    out = {'model': 'tools/xemu/vectors_c3.py, from docs/decomp/Timing.md',
           'pulse_table_model': [[float(v) for v in r] for r in table], 'runs': []}
    for name in a.runs:
        r = check_run(name, table, seed_after)
        out['runs'].append(r)
        c = r['checks']
        print(name, '| frames', r['frames'], '| pulse table equal', c['pulse_table']['equal'],
              '| generator', c['pulse_table'].get('guest_state_this_0x68c'), 'model', c['pulse_table']['generator_state_after'],
              '| steps', c['steps']['checked'], 'mismatches', c['steps']['mismatches'],
              '| start', c['start']['t'], '| end', c['end'])
    with open(a.out, 'w') as f:
        json.dump(out, f, indent=None, separators=(',', ':'))
    print('->', a.out)


if __name__ == '__main__':
    main()
