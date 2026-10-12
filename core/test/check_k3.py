"""K3 checks that need Python: constants, the guest's memory, the executable specification.

    python core/test/check_k3.py out/core/native/test_k3.exe

1. Every constant `#define K_* <value>f /* <address> */` in core/src/Blob.c
   is compared with the float32 at that address in out/rom/kernel.img
   (file offset = address - 0x80010000, Overview.md); every `B_*` with the
   four bytes of the immediate at its address; the reference points of
   chamber_prepass in core.c with 0x800669D0 and 0x800669CC.
2. Both meshes (`test_k3 --mesh`) against the model
   tools/xemu/blob_model.py and, where the recorded runs are present
   (out/xemu/runs/c6_*/probe_c6.json, local only), against the buffers
   blob_setup wrote in the guest.
3. The five recorded runs through `test_k3 --replay`: before every call
   of blob_update the whole object as the guest held it, byte for byte
   with the mask of tools/xemu/vectors_c6.py, and the generator where the
   probe read it; at every capture the deformed mesh, everything blob_draw
   handed the GPU (with the guest's camera, so also after the loop of
   c6_loop) and both outputs of every call of the weighted centre.
4. The core against the model on synthetic sequences of (t, dt) the runs
   do not cover: every millisecond from 0 to 8 s, 60 Hz, random frame
   lengths with restarts, and steps backwards (the branch t - dt > 0.6 of
   blob_update). The state at every call, the deformed mesh and both
   light views every 97 calls and at the last.
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
import blob_model as bm  # noqa: E402
import chamber_model as M  # noqa: E402

KERNEL = os.path.join(ROOT, 'out', 'rom', 'kernel.img')
RUNS = os.path.join(ROOT, 'out', 'xemu', 'runs')
PACK = os.path.join(ROOT, 'out', 'pack', 'xboot.pak')
BASE = 0x80010000
BLOB = 0x8006FBAC
TMP = os.path.join(ROOT, 'out', 'core', 'k3')

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


def floats(h):
    b = bytes.fromhex(h)
    return list(struct.unpack('<%df' % (len(b) // 4), b))


def words(h):
    b = bytes.fromhex(h)
    return list(struct.unpack('<%dI' % (len(b) // 4), b))


def f32(x):
    return float(np.float32(x))


# ---------------------------------------------------------------- 1. constants

def constants():
    img = open(KERNEL, 'rb').read()
    src = open(os.path.join(ROOT, 'core', 'src', 'Blob.c'), encoding='utf-8').read()
    found = re.findall(r'#define\s+(K_\w+)\s+(-?[0-9.]+(?:e[-+]?[0-9]+)?)f\s+/\*\s*(0x[0-9A-Fa-f]{8})\s*\*/', src)
    bad = []
    for name, value, addr in found:
        rom = struct.unpack_from('<f', img, int(addr, 16) - BASE)[0]
        if fb(rom) != fb(np.float32(float(value))):
            bad.append(f'{name} {value} at {addr}: kernel {rom!r}')
    for b in bad:
        print('      ' + b)
    check(len(found) == 39 and not bad, f'{len(found)} constants of Blob.c equal the kernel image, {len(bad)} differ')
    imm = re.findall(r'#define\s+(B_\w+)\s+(\S+?)[fu]?\s+/\*\s*(0x[0-9A-Fa-f]{8})', src)
    bad = []
    for name, value, addr in imm:
        want = int(value, 16) if value.startswith('0x') else fb(np.float32(float(value)))
        got = struct.unpack_from('<I', img, int(addr, 16) - BASE)[0]
        if got != want:
            bad.append(f'{name} at {addr}: {got:#010x}')
    for b in bad:
        print('      ' + b)
    check(len(imm) == 4 and not bad, f'{len(imm)} immediates of Blob.c equal the code bytes, {len(bad)} differ')
    ref = [struct.unpack_from('<f', img, a - BASE)[0] for a in (0x800669D0, 0x800669CC)]
    core = open(os.path.join(ROOT, 'core', 'src', 'core.c'), encoding='utf-8').read()
    check(ref == [-30.0, 40.0] and 'side ? 40.0f : -30.0f' in core,
          f'reference points of chamber_prepass in core.c equal 0x800669D0, 0x800669CC ({ref[0]}, {ref[1]})')


# ---------------------------------------------------------------- 2. meshes

def meshes(exe):
    os.makedirs(TMP, exist_ok=True)
    subprocess.run([exe, PACK, '--mesh', TMP], check=True)
    rd = lambda n: open(os.path.join(TMP, n), 'rb').read()  # noqa: E731
    model = {'big': bm.cube_sphere(32), 'small': bm.cube_sphere(8)}
    for k, (verts, idx) in model.items():
        u, i = rd(k + '.unit'), rd(k + '.idx')
        ok = (list(struct.unpack('<%dI' % (len(u) // 4), u)) == [fb(x) for v in verts for x in v] and
              list(struct.unpack('<%dH' % (len(i) // 2), i)) == idx)
        check(ok, f'{k} mesh: {len(u) // 12} vertices, {len(i) // 2} indices equal the model')
    p = os.path.join(RUNS, 'c6_main', 'probe_c6.json')
    if not os.path.exists(p):
        print('      recorded run c6_main not present, meshes not compared with the guest')
        return
    st = json.load(open(p))['static']
    ib = lambda h: bytes(struct.pack('<%dH' % (len(h) // 4), *struct.unpack('<%dH' % (len(h) // 4),  # noqa: E731
                                                                          bytes.fromhex(h))))
    ok = (rd('big.unit') == bytes.fromhex(st['vb_big']) == bytes.fromhex(st['unit']) and
          rd('small.unit') == bytes.fromhex(st['vb_small']) and
          rd('big.idx') == ib(st['ib_big']) and rd('small.idx') == ib(st['ib_small']))
    check(ok, 'both meshes, the copy at +0x1C and both index buffers equal the guest\'s memory (c6_main)')


# ---------------------------------------------------------------- replay helpers

def replay(exe, cmds):
    path = os.path.join(TMP, 'replay.txt')
    with open(path, 'w') as f:
        f.write('\n'.join(cmds) + '\n')
    out = subprocess.run([exe, PACK, '--replay', path], capture_output=True, text=True, check=True).stdout
    return [ln.split() for ln in out.split('\n') if ln]


def parse_state(w):
    """An "s" line: generator, steps, counts, centre, radius, colour, bulges, drops."""
    v = [int(x, 16) for x in w[1:]]
    s = {'rng': v[0], 'draws': v[1], 'nb': v[2], 'nd': v[3], 'center': v[4:7], 'radius': v[7],
         'colour': v[8:12]}
    s['bulges'] = [v[12 + 16 * i:28 + 16 * i] for i in range(32)]
    o = 12 + 16 * 32
    s['drops'] = [v[o + 14 * i:o + 14 * (i + 1)] for i in range(8)]
    return s


def guest_layout(s):
    """The object as the guest holds it, with the mask of vectors_c6.model_bytes."""
    out, mask = bytearray(0xA38), bytearray(0xA38)

    def put(off, data):
        out[off:off + len(data)] = data
        mask[off:off + len(data)] = b'\xff' * len(data)
    put(0x30, struct.pack('<I', 1734 // 6))
    put(0x44, struct.pack('<4I', *s['colour']))
    put(0x54, struct.pack('<3I', *s['center']))
    put(0x60, struct.pack('<3f', 1.0, 1.0, 1.0))
    put(0x6C, struct.pack('<I', s['radius']))
    for j, b in enumerate(s['bulges']):
        drop = 0 if b[14] == 0xFFFFFFFF else BLOB + 0x874 + 0x38 * b[14]
        put(0x70 + 0x40 * j, struct.pack('<14I', *b[:14]) + struct.pack('<IB', drop, b[15]))
    put(0x870, struct.pack('<I', s['nb']))
    for i, d in enumerate(s['drops']):
        o = 0x874 + 0x38 * i
        put(o, struct.pack('<4IB', *d[:4], d[4]))
        put(o + 0x14, struct.pack('<9I', *d[5:]))
    put(0xA34, struct.pack('<I', s['nd']))
    return bytes(out), bytes(mask)


def model_state(m):
    """The model's object in the words of an "s" line."""
    s = {'rng': m.rand.s, 'draws': m.rand.n, 'nb': m.n_bulges, 'nd': m.n_drops,
         'center': [fb(x) for x in m.center], 'radius': fb(m.radius), 'colour': [fb(x) for x in bm.COLOUR]}
    s['bulges'] = [[fb(x) for x in (b.size, b.size2, b.inv2, b.h, *b.c)] + [b.mask] +
                   [fb(x) for x in (*b.d, b.t0, b.freq, b.amp)] +
                   [0xFFFFFFFF if b.drop is None else b.drop, b.flag] for b in m.bulges]
    s['drops'] = [[fb(x) for x in (d.size, *d.pos)] + [d.neg] +
                  [fb(x) for x in (*d.d, d.t0, d.freq, d.amp, d.disp, d.scale, d.rate)] for d in m.drops]
    return s


# ---------------------------------------------------------------- 3. recorded runs

def cam_words(obj):
    o = lambda a, n: words(obj[(a - 0x8006F050) * 2:(a - 0x8006F050 + 4 * n) * 2])  # noqa: E731
    return o(0x8006F790, 3) + o(0x8006F7F4, 16) + o(0x8006F834, 16) + o(0x8006F874, 16)


def recorded(exe, name):
    p = os.path.join(RUNS, name, 'probe_c6.json')
    if not os.path.exists(p):
        print(f'      recorded run {name} not present')
        return
    d = json.load(open(p))
    caps = {c['frame']: c for c in d['captures']}
    lines = replay(exe, run_commands(d, caps))
    pos = 0
    state_bad, rand_bad, n_rand, first = 0, 0, 0, None
    cap_bad = {'mesh': 0, 'draw': 0, 'wc': 0}
    n_wc = 0
    for n, fr in enumerate(d['frames']):
        while lines[pos][0] != 's':
            pos += 1
        s = parse_state(lines[pos])
        pos += 1
        mine, mask = guest_layout(s)
        guest = bytes.fromhex(fr['blob'])
        bad = [o for o in range(0xA38) if mask[o] and mine[o] != guest[o]]
        state_bad += bool(bad)
        if bad and first is None:
            first = (n, bad[:4])
        if 'rand' in fr:
            n_rand += 1
            rand_bad += int(fr['rand'], 16) != s['rng']
        if n in caps:
            c = caps[n]
            m = [int(x, 16) for x in lines[pos][1:]]
            pos += 1
            cap_bad['mesh'] += m != words(c['deformed'])
            dr = [int(x, 16) for x in lines[pos][1:]]
            pos += 1
            cap_bad['draw'] += not draw_equal(dr, c)
            for w in c.get('wc', []):
                o = [int(x, 16) for x in lines[pos][1:]]
                pos += 1
                n_wc += 1
                cap_bad['wc'] += o != words(w['center']) + words(w['second'])
    if first:
        print(f'      first difference in frame {first[0]} at bytes {[hex(o) for o in first[1]]}')
    check(state_bad == 0 and rand_bad == 0,
          f'{name}: object before each of {len(d["frames"])} calls equals the guest\'s memory '
          f'({state_bad} differ); generator in {n_rand} frames ({rand_bad} differ)')
    check(not any(cap_bad.values()),
          f'{name}: at {len(caps)} captures deformed mesh ({cap_bad["mesh"]} differ), values handed to the '
          f'GPU with the guest\'s camera ({cap_bad["draw"]} differ), {n_wc} calls of the weighted centre, '
          f'both outputs ({cap_bad["wc"]} differ)')


def run_commands(d, caps):
    """Commands for a recorded run: restart where t goes back, the poke
    after the state line of its frame (the probe compared the state, then
    wrote the generator), captures after the update of their frame."""
    cmds, prev = [], None
    for n, fr in enumerate(d['frames']):
        t, dt = struct.unpack('<2I', bytes.fromhex(fr['args']))
        if prev is not None and bf(t) < bf(prev):
            cmds.append('r')
        prev = t
        if 'rand_poked' in fr:
            cmds += ['S', 'g %s' % fr['rand_poked'][2:], 'v %08X %08X' % (t, dt)]
        else:
            cmds.append('u %08X %08X' % (t, dt))
        if n in caps:
            c = caps[n]
            anim = floats(fr['anim'])
            cmds.append('m')
            cmds.append('D %08X %08X %08X ' % (fb(c['t']), fb(anim[3]), fb(anim[4])) +
                        ' '.join('%08X' % w for w in cam_words(c['anim_obj'])))
            for w in c.get('wc', []):
                cmds.append('w %08X ' % fb(anim[4]) + ' '.join('%08X' % u for u in words(w['ref'])))
    return cmds


def draw_equal(dr, c):
    """dr: the words of xb_blob_params; c: a capture of probe_c6.json."""
    if dr[1] != int(c['colour'], 16) or dr[2:22] != words(c['quad']):
        return False
    vs = c['vs']
    mesh_vs, mesh_ps = dr[22:74], dr[74:82]
    drop_vs, drop_ps, nd = dr[82:106], dr[106:118], dr[118]
    want = [(4, mesh_vs[16:32]), (8, mesh_vs[32:52]), (0, mesh_vs[0:16]), (4, drop_vs[0:16]),
            (8, drop_vs[16:24])] + [(10, dr[119 + 16 * i:135 + 16 * i]) for i in range(nd)]
    if len(vs) != len(want):
        return False
    for k, (v, (index, w)) in enumerate(zip(vs, want)):
        g = words(v['data'])
        if v['index'] != index or len(g) != len(w):
            return False
        skip = (7, 15) if k == 1 else ()      # c9.w and c11.w: not set by blob_draw
        if any(a != b for i, (a, b) in enumerate(zip(g, w)) if i not in skip):
            return False
    ps = c['ps']
    return len(ps) == 2 and words(ps[0]['data']) == mesh_ps and words(ps[1]['data']) == drop_ps


# ---------------------------------------------------------------- 4. synthetic sequences

class Model(bm.Blob):
    """blob_model.Blob that deforms only when asked; remembers what the last
    update did to the mesh."""

    def __init__(self):
        self.last = 'reset'
        super().__init__()

    def reset_mesh(self):
        self.last = 'reset'
        super().reset_mesh()

    def deform(self):
        self.last = 'deform'

    def mesh_now(self):
        if self.last == 'deform':
            bm.Blob.deform(self)
        return [fb(x) for v in self.mesh for x in v]


def sequences():
    out = {}
    t, s = 0.0, []
    while t < 8.0:
        dt = f32(0.001)
        t = f32(t + dt)
        s.append((t, dt))
    out['every ms'] = s
    t, s, n = 0.0, [], 0
    while t < 8.0:
        dt = f32((17, 16, 17)[n % 3] * f32(0.001))
        t = f32(t + dt)
        s.append((t, dt))
        n += 1
    out['60 Hz'] = s
    rnd = random.Random(3)
    t, s = 0.0, []
    for _ in range(3):
        while t < 8.0:
            dt = f32(rnd.randint(1, 40) * f32(0.001))
            t = f32(t + dt)
            s.append((t, dt))
        t = f32(rnd.randint(1, 40) * f32(0.001))
        s.append(('restart',))
        s.append((t, t))
    out['random with restarts'] = s
    t, s = 0.0, []
    for k in range(600):
        if k % 150 == 149:
            dt = f32(rnd.uniform(0.0, 0.5) - t)     # backwards below 0.6: t - dt > 0.6
            t = f32(t + dt)
        else:
            dt = f32(rnd.randint(5, 60) * f32(0.001))
            t = f32(t + dt)
        s.append((t, dt))
    out['steps backwards'] = s
    return out


def synthetic(exe):
    total = 0
    for name, seq in sequences().items():
        cmds, marks = [], []
        calls = [i for i, e in enumerate(seq) if e[0] != 'restart']
        check_at = set(calls[::97]) | {calls[-1]}
        for i, e in enumerate(seq):
            if e[0] == 'restart':
                cmds.append('r')
                continue
            cmds.append('u %08X %08X' % (fb(e[0]), fb(e[1])))
            if i in check_at:
                cmds += ['m', 'l']
        lines = replay(exe, cmds)
        m = Model()
        m.restart()
        pos = state_bad = mesh_bad = lv_bad = n_mesh = 0
        flat_seen = 0
        for i, e in enumerate(seq):
            if e[0] == 'restart':
                m.restart()
                continue
            t, dt = e
            s = parse_state(lines[pos])
            pos += 1
            state_bad += s != model_state(m)
            m.update(t, dt)
            if i in check_at:
                got = [int(x, 16) for x in lines[pos][1:]]
                pos += 1
                n_mesh += 1
                flat_seen += m.last == 'reset'
                mesh_bad += got != m.mesh_now()
                lv = [int(x, 16) for x in lines[pos][1:]]
                pos += 1
                want = []
                for side, z in ((0, -30.0), (1, 40.0)):
                    centre, _ = m.weighted_center([0.0, 0.0, z], 0.0)
                    want += [fb(x) for x in M.light_view(centre, side)[2]]
                lv_bad += lv != want
        total += len(calls)
        check(state_bad == 0 and mesh_bad == 0 and lv_bad == 0,
              f'{name}: {len(calls)} calls, state before each ({state_bad} differ), deformed mesh at {n_mesh} '
              f'calls ({mesh_bad} differ, {flat_seen} undeformed), light views ({lv_bad} differ) against the model')
    print(f'      {total} calls against the model')


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.splitlines()[2].strip())
    exe = os.path.abspath(sys.argv[1])
    os.makedirs(TMP, exist_ok=True)
    constants()
    meshes(exe)
    for name in ('c6_main', 'c6_tail', 'c6_plain', 'c6_loop', 'c6_poke'):
        recorded(exe, name)
    synthetic(exe)
    print(f'{checks} checks, {failed} failed')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
