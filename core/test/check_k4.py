"""K4 checks that need Python: constants, the guest's memory and method stream, the executable specification.

    python core/test/check_k4.py out/core/native/test_k4.exe

1. Every constant `#define K_* <value>f /* <address> */` in core/src/Logo.c
   is compared with the float32 at that address in out/rom/kernel.img
   (file offset = address - 0x80010000, Overview.md); every `B_*` with the
   four bytes of the immediate at its address; the words of key 1 in
   core/test/test_k4.c with 0x80073FC4.
2. The seven meshes (`test_k4 --mesh`) against the model
   tools/xemu/logo_model.py on data/logo/logo.json and, where the recorded
   runs are present (out/xemu/runs/c7_*/probe_c7.json, local only), against
   the buffers logo_setup wrote in the guest.
3. The two recorded runs through `test_k4 --replay` with the guest's t of
   every frame (logo_update where the guest's switch +0xE is set): after
   every frame the fields logo_update writes, against the logo object in
   the guest; the frames that draw into the texture. Then every draw of a
   logo buffer in trace.log, in order, against the draws the core makes in
   these frames: the mesh, D's vertex constants 0..8 with the guest's
   camera and with the core's, the factor colour before Q1, Q2 and E; the
   frame matrix against the guest's.
4. The core against the model on sequences of t the runs do not cover:
   every millisecond from 5.2 to 8.05, 60 Hz, random steps with jumps back
   across 6 and 6.25. State after every call; D's constants, the colours
   and the matrix of E on the screen and in every draw into the texture.
"""

import json
import os
import random
import re
import struct
import subprocess
import sys

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'xemu'))
import chamber_model as M  # noqa: E402
import logo_model as lm  # noqa: E402
from vectors_c7 import FLIP, LINE  # noqa: E402

KERNEL = os.path.join(ROOT, 'out', 'rom', 'kernel.img')
RUNS = os.path.join(ROOT, 'out', 'xemu', 'runs')
PACK = os.path.join(ROOT, 'out', 'pack', 'xboot.pak')
BASE = 0x80010000
TMP = os.path.join(ROOT, 'out', 'core', 'k4')
NAMES = ['A', 'B', 'C', 'D', 'Q1', 'Q2', 'E']

checks = failed = 0


def check(ok, what):
    global checks, failed
    checks += 1
    failed += not ok
    print(('ok    ' if ok else 'FAIL  ') + what)


def fb(x):
    return struct.unpack('<I', struct.pack('<f', float(x)))[0]


def bf(u):
    return struct.unpack('<f', struct.pack('<I', u))[0]


def words(h):
    b = bytes.fromhex(h)
    return list(struct.unpack('<%dI' % (len(b) // 4), b))


# ---------------------------------------------------------------- 1. constants

def constants():
    img = open(KERNEL, 'rb').read()
    src = open(os.path.join(ROOT, 'core', 'src', 'Logo.c'), encoding='utf-8').read()
    found = re.findall(r'#define\s+(K_\w+)\s+(-?[0-9.]+(?:e[-+]?[0-9]+)?)f\s+/\*\s*(0x[0-9A-Fa-f]{8})\s*\*/', src)
    bad = []
    for name, value, addr in found:
        rom = struct.unpack_from('<f', img, int(addr, 16) - BASE)[0]
        if fb(rom) != fb(np.float32(float(value))):
            bad.append(f'{name} {value} at {addr}: kernel {rom!r}')
    for b in bad:
        print('      ' + b)
    check(len(found) == 20 and not bad, f'{len(found)} constants of Logo.c equal the kernel image, {len(bad)} differ')
    imm = re.findall(r'#define\s+(B_\w+)\s+(0x[0-9A-Fa-f]+)u\s+/\*\s*(0x[0-9A-Fa-f]{8})', src)
    bad = [f'{n} at {a}' for n, v, a in imm if struct.unpack_from('<I', img, int(a, 16) - BASE)[0] != int(v, 16)]
    for b in bad:
        print('      ' + b)
    check(len(imm) == 7 and not bad, f'{len(imm)} immediates of Logo.c equal the code bytes, {len(bad)} differ')
    test = open(os.path.join(ROOT, 'core', 'test', 'test_k4.c'), encoding='utf-8').read()
    keys = [int(x, 16) for x in re.findall(r'keys\[\d\] = (0x[0-9A-F]{8})u', test)]
    rom = list(struct.unpack_from('<3I', img, 0x80073FC4 - BASE))
    check(keys == rom, f'key 1 in test_k4.c equals 0x80073FC4 ({len(keys)} words)')


# ---------------------------------------------------------------- 2. meshes

def meshes(exe):
    os.makedirs(TMP, exist_ok=True)
    subprocess.run([exe, PACK, '--mesh', TMP], check=True)
    rd = lambda n: open(os.path.join(TMP, n), 'rb').read()  # noqa: E731
    model = lm.load()
    bad = [n for n in NAMES if rd(n + '.vtx') != lm.vertex_buffer(model[n]) or
           rd(n + '.idx') != lm.index_buffer(model[n])]
    check(not bad, f'seven meshes equal the model\'s buffers (data/logo/logo.json){": " + str(bad) if bad else ""}')
    for run in ('c7_main', 'c7_b'):
        p = os.path.join(RUNS, run, 'probe_c7.json')
        if not os.path.exists(p):
            print(f'      recorded run {run} not present, meshes not compared with the guest')
            continue
        st = json.load(open(p))['static']
        bad = [n for n in NAMES if rd(n + '.vtx') != bytes.fromhex(st[n]['vb']) or
               rd(n + '.idx') != bytes.fromhex(st[n]['ib'])]
        check(not bad, f'{run}: vertex buffers and index lists of the seven meshes equal the guest\'s memory'
              f'{": " + str(bad) if bad else ""}')


# ---------------------------------------------------------------- replay helpers

def replay(exe, cmds):
    path = os.path.join(TMP, 'replay.txt')
    with open(path, 'w') as f:
        f.write('\n'.join(cmds) + '\n')
    out = subprocess.run([exe, PACK, '--replay', path], capture_output=True, text=True, check=True).stdout
    return [[int(x, 16) for x in ln.split()[1:]] for ln in out.split('\n') if ln]


def state(w):
    """An "s" line: tm, texture, phase, rtt, updates, rtt_count, pos, alpha."""
    return {'tm': w[0], 'texture': w[1], 'phase': w[2], 'rtt': w[3], 'updates': w[4], 'rtt_count': w[5],
            'pos': w[6:9], 'alpha': w[9]}


def params(w):
    """A "d" line, xb_logo_params."""
    return {'solid': w[0], 'text': w[1], 'world': w[2:18], 'd_vs': w[18:54], 'quad': w[54], 'text_colour': w[55],
            'text_world': w[56:72]}


def hx(words_):
    return ' '.join('%08X' % u for u in words_)


# ---------------------------------------------------------------- 3. recorded runs

def stream(run, start):
    """Every BEGIN_END block in trace.log with its array 0 offset, the
    constants 96..104 and the factor colour."""
    with open(os.path.join(RUNS, run, 'trace.log'), 'rb') as f:
        f.seek(start)
        data = f.read()
    off, const, factor = {}, {}, None
    load = k = 0
    cur, out, pos = None, [], start
    for raw in data.split(b'\n'):
        line = raw.decode('latin1')
        here, pos = pos, pos + len(raw) + 1
        if line.startswith(FLIP):
            continue
        m = LINE.match(line)
        if not m:
            continue
        abbrev, name, idx, n, val = m.groups()
        v = int(val, 16) if val else None
        if name == 'NV097_SET_VERTEX_DATA_ARRAY_OFFSET':
            off[int(idx) // 4] = v
        elif name == 'NV097_SET_TRANSFORM_CONSTANT_LOAD':
            load, k = v, 0
        elif name == 'NV097_SET_TRANSFORM_CONSTANT':
            const[load + k // 4, k % 4] = v
            k += 1
        elif name == 'NV097_SET_COMBINER_FACTOR0' and idx == '0':
            factor = v
        elif name == 'NV097_SET_BEGIN_END':
            if v:
                cur = {'prim': v, 'off0': off.get(0), 'inline': 0, 'factor': factor,
                       'const': [const.get((96 + i, j)) for i in range(9) for j in range(4)]}
            elif cur:
                out.append(cur)
                cur = None
        elif cur is not None and name == 'NV097_INLINE_ARRAY':
            cur['inline'] += 1
    return out


def recorded(exe, run):
    p = os.path.join(RUNS, run, 'probe_c7.json')
    if not os.path.exists(p):
        print(f'      recorded run {run} not present')
        return
    d = json.load(open(p))
    frames, rtt = d['frames'], {r['frame'] + 1: r for r in d['rtt']}
    ts = [words(f['anim'])[0] for f in frames]
    cmds = ['i']
    for n, fr in enumerate(frames):
        if fr['switch'] != '01':
            cmds.append('S')
            continue
        cmds.append('u %08X' % ts[n])
        if n in rtt:
            r = rtt[n]
            cmds += ['D %08X 1 %s %s' % (ts[n], hx(words(r['view'])), hx(words(r['proj']))), 'd %08X 1' % ts[n]]
        cmds += ['D %08X 0 %s %s' % (ts[n], hx(words(fr['view'])), hx(words(fr['proj']))), 'd %08X 0' % ts[n]]
    lines = iter(replay(exe, cmds))

    state_bad, first, core_rtt, got = 0, None, [], {}
    for n, fr in enumerate(frames):
        s = state(next(lines))
        logo = bytes.fromhex(fr['logo'])
        guest = {'tm': logo[0x4C], 'pos': list(struct.unpack_from('<3I', logo, 0x90)),
                 'alpha': struct.unpack_from('<I', logo, 0xC4)[0], 'phase': logo[0xD4],
                 'texture': int(struct.unpack_from('<I', logo, 0xD0)[0] != 0)}
        bad = [k for k in guest if guest[k] != s[k]]
        state_bad += bool(bad)
        if bad and first is None:
            first = (n, bad)
        if fr['switch'] != '01':
            continue
        if s['rtt']:
            core_rtt.append(n)
        if n in rtt:
            got[n, 'texture'] = (params(next(lines)), params(next(lines)))
        got[n, 'screen'] = (params(next(lines)), params(next(lines)))
    if first:
        print(f'      first difference in frame {first[0]}: {first[1]}')
    check(state_bad == 0 and core_rtt == sorted(rtt),
          f'{run}: logo state after each of {len(frames)} frames equals the guest\'s memory ({state_bad} differ); '
          f'into the texture in frames {core_rtt}, guest {sorted(rtt)}')

    # Every draw of a logo buffer in the stream, in order, against the
    # sequence the core gives: per frame the draw into the texture made by
    # logo_update, then the draw on the screen. The trace positions the
    # probe recorded lag behind the stops (xemu writes the trace buffered),
    # so the draws are matched by order, from the first frame with the trace on.
    phys = {int(d['static'][m]['phys'], 16): m for m in NAMES}
    first_on = next(n for n, f in enumerate(frames) if f['trace_pos'] >= d['trace_on_at'])
    want = []
    for n in range(first_on, len(frames)):
        for kind in ('texture', 'screen'):
            if (n, kind) not in got or (kind == 'texture' and n == first_on):
                continue
            g, own = got[n, kind]
            for m in (['A', 'B', 'C', 'D'] if g['solid'] else []) + (['Q1', 'Q2', 'E'] if g['text'] else []):
                want.append((n, kind, m, g, own))
    draws = [(phys[dr['off0']], dr) for dr in stream(run, d['trace_on_at'])
             if dr['off0'] in phys and not dr['inline']]     # inline vertices do not read array 0
    seq_bad = d_bad = d_own_bad = q_bad = e_bad = n_d = n_q = n_e = 0
    for (n, kind, m, g, own), (gm, dr) in zip(want, draws):
        if m != gm:
            seq_bad += 1
            continue
        if m == 'D':
            n_d += 1
            d_bad += dr['const'] != g['d_vs']
            d_own_bad += dr['const'] != own['d_vs']
        elif m in ('Q1', 'Q2'):
            n_q += 1
            q_bad += dr['factor'] != g['quad']
        elif m == 'E':
            n_e += 1
            e_bad += dr['factor'] != g['text_colour']
    check(seq_bad == 0 and len(want) == len(draws),
          f'{run}: {len(draws)} draws of logo buffers in the stream, the core makes {len(want)} in the same order '
          f'({seq_bad} differ in the mesh)')
    check(n_d and not d_bad and not d_own_bad and not q_bad and not e_bad,
          f'{run}: {n_d} draws of D, constants 0..8 equal with the guest camera ({d_bad} differ) and with the '
          f'core camera ({d_own_bad} differ); factor colour before {n_q} Q1, Q2 ({q_bad} differ) and {n_e} E '
          f'({e_bad} differ)')
    fm = words(frames[0]['frame_matrix'])
    check(all(g[0]['world'] == fm for g in got.values()) and all(f['frame_matrix'] == frames[0]['frame_matrix']
                                                                for f in frames),
          f'{run}: the frame W equals this+0x1774 in the guest (16 values, rows x, m, o and d * rho)')


# ---------------------------------------------------------------- 4. against the model

def sequences():
    rnd = random.Random(4)
    every_ms = [fb(np.float32(5.2 + 0.001 * i)) for i in range(2851)]
    hz60 = [fb(np.float32(i / 60.0)) for i in range(312, 484)]
    jumps, t = [], 5.3
    for _ in range(3000):
        t += rnd.uniform(0.0, 0.02)
        if rnd.random() < 0.01:
            t -= rnd.uniform(0.05, 0.6)    # back across 6.25 and 6
        if t > 8.05:
            t -= rnd.uniform(1.0, 2.8)
        jumps.append(fb(np.float32(t)))
    return {'every ms': every_ms, '60 Hz': hz60, 'random with jumps back': jumps}


def against_model(exe):
    cp = M.CamPath()
    total = 0
    for name, seq in sequences().items():
        cmds = ['i']
        for u in seq:
            cmds += ['u %08X' % u, 'c %08X' % u, 'd %08X 0' % u]
        lines = iter(replay(exe, cmds))
        prev, st_bad, dr_bad, n_rtt = None, 0, 0, 0
        extra = []
        for u in seq:
            t = bf(u)
            s = state(next(lines))
            cam = next(lines)
            g = params(next(lines))
            prev = lm.update(t, prev)
            want = {'tm': prev['tm'], 'pos': [fb(x) for x in prev['pos']], 'alpha': fb(prev['alpha']),
                    'phase': int(bool(prev['phase'])), 'texture': int(prev['phase'] is not None),
                    'rtt': int(bool(prev.get('rtt')))}
            st_bad += any(want[k] != s[k] for k in want)
            view, proj = [bf(x) for x in cam[1:17]], [bf(x) for x in cam[17:33]]
            solid = t < f32(lm.END - lm.TAIL)
            text = prev['tm'] == 1 and not prev['phase']
            dr_bad += not draw_matches(g, t, solid, text, prev, cp, view, proj)
            if s['rtt']:
                n_rtt += 1
                extra.append((u, view, proj, dict(prev)))
        # draws into the texture: phase as the draw sees it (Logo.md 6)
        cmds = ['i']
        for u, *_ in extra:
            cmds += ['u %08X' % u, 'd %08X 1' % u]
        lines = iter(replay(exe, cmds))
        for u, view, proj, st in extra:
            next(lines)
            g = params(next(lines))
            dr_bad += not draw_matches(g, bf(u), True, bool(st['phase']), st, cp, view, proj)
        total += len(seq)
        check(st_bad == 0 and dr_bad == 0,
              f'{name}: {len(seq)} calls, state after each ({st_bad} differ), drawn values on the screen and in '
              f'{n_rtt} draws into the texture ({dr_bad} differ) against the model')
    print(f'      {total} calls against the model')


def f32(x):
    return float(np.float32(x))


def draw_matches(g, t, solid, text, st, cp, view, proj):
    if g['solid'] != int(solid) or g['text'] != int(text) or g['world'] != [fb(x) for x in cp.frame]:
        return False
    if solid:
        mat = lm.mesh_d_matrix(cp.frame, view, proj)
        want = [fb(mat[4 * r + c]) for r in range(4) for c in range(4)]
        want += [fb(x) for row in lm.mesh_d_constants(t) for x in row]
        if g['d_vs'] != want:
            return False
    if text:
        if g['quad'] != lm.quad_alpha_word(st['alpha']) or g['text_colour'] != lm.E_COLOUR:
            return False
        if g['text_world'] != [fb(x) for x in lm.mesh_e_matrix(st['pos'], cp.frame)]:
            return False
    return True


def main():
    exe = sys.argv[1]
    os.makedirs(TMP, exist_ok=True)
    constants()
    meshes(exe)
    for run in ('c7_main', 'c7_b'):
        recorded(exe, run)
    against_model(exe)
    print(f'{checks} checks, {failed} failed')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
