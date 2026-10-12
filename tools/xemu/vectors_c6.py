"""Compares the blob model with a probe_c6 run and writes vectors/c6_blob.json.

    python tools/xemu/vectors_c6.py [c6_main c6_tail c6_plain c6_loop c6_poke]

The model (blob_model.py) starts from blob_construct and blob_restart and is
fed only the arguments (t, dt) the guest passed to blob_update. Before every
update its whole state is compared with the guest's blob object, byte for
byte except padding and GPU handles. At each capture the deformed vertex
buffer and the values blob_draw handed the GPU are compared as well.
"""

import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blob_model as bm  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BASE = 0x8006FBAC
BRANCH_HITS = {'retry': 0}

_len_sq = bm.len_sq


def _count_len_sq(v):
    r = _len_sq(v)
    if r < bm.c(0x3A83126F):
        BRANCH_HITS['retry'] += 1
    return r


bm.len_sq = _count_len_sq   # counts the |dir|^2 < 0.001 branch of spawn


def floats(h):
    b = bytes.fromhex(h)
    return list(struct.unpack('<%df' % (len(b) // 4), b))


def bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def same(a, b):
    return all(x is None or bits(x) == bits(y) for x, y in zip(a, b)) and len(a) == len(b)


def model_bytes(m):
    """The blob object as the model sees it, with a mask of compared bytes."""
    out, mask = bytearray(0xA38), bytearray(0xA38)

    def put(off, data, compare=True):
        out[off:off + len(data)] = data
        if compare:
            mask[off:off + len(data)] = b'\xff' * len(data)

    put(0x30, struct.pack('<I', m.per_face))
    put(0x44, struct.pack('<4f', *bm.COLOUR))
    put(0x54, struct.pack('<3f', *m.center))
    put(0x60, struct.pack('<3f', 1.0, 1.0, 1.0))
    put(0x6C, struct.pack('<f', m.radius))
    for j, b in enumerate(m.bulges):
        o = 0x70 + 0x40 * j
        put(o, m.bulge_bytes(b, BASE)[:0x3D])
    put(0x870, struct.pack('<I', m.n_bulges))
    for i, d in enumerate(m.drops):
        o = 0x874 + 0x38 * i
        raw = m.drop_bytes(d)
        put(o, raw[:0x11])
        put(o + 0x14, raw[0x14:])
    put(0xA34, struct.pack('<I', m.n_drops))
    return bytes(out), bytes(mask)


def diff_state(m, guest):
    mine, mask = model_bytes(m)
    bad = [o for o in range(0xA38) if mask[o] and mine[o] != guest[o]]
    return bad


def where(o):
    if 0x70 <= o < 0x870:
        return f'bulge {(o - 0x70) // 0x40} +{(o - 0x70) % 0x40:#x}'
    if 0x874 <= o < 0xA34:
        return f'drop {(o - 0x874) // 0x38} +{(o - 0x874) % 0x38:#x}'
    return f'blob +{o:#x}'


def camera(obj):
    o = lambda a, n: floats(obj[(a - 0x8006F050) * 2:(a - 0x8006F050 + 4 * n) * 2])  # noqa: E731
    return {'eye': o(0x8006F790, 3), 'inv': o(0x8006F7F4, 16), 'view': o(0x8006F834, 16),
            'proj': o(0x8006F874, 16)}


def check_static(st, m, checks):
    n_big, n_small, i_big, i_small, per_face = st['counts']
    flat = lambda vs: [x for v in vs for x in v]  # noqa: E731
    ib = lambda h: list(struct.unpack('<%dH' % (len(h) // 4), bytes.fromhex(h)))  # noqa: E731
    checks.append(('counts', [n_big, n_small, i_big, i_small, per_face] ==
                   [len(m.unit), len(m.small), len(m.idx), len(m.small_idx), m.per_face], 5))
    checks.append(('vb_big', same(floats(st['vb_big']), flat(m.unit)), n_big * 3))
    checks.append(('unit_copy', same(floats(st['unit']), flat(m.unit)), n_big * 3))
    checks.append(('vb_small', same(floats(st['vb_small']), flat(m.small)), n_small * 3))
    checks.append(('ib_big', ib(st['ib_big']) == m.idx, i_big))
    checks.append(('ib_small', ib(st['ib_small']) == m.small_idx, i_small))


def check_capture(cap, m, frame, checks, vec):
    t = cap['t']
    anim = floats(frame['anim'])            # t, ?, ?, x1c, x20
    x1c, x20 = anim[3], anim[4]
    tag = f"t={t:.3f}"
    flat = [x for v in m.mesh for x in v]
    ok = same(floats(cap['deformed']), flat)
    checks.append((f'{tag} deformed mesh', ok, len(flat)))
    d = bm.draw(m, t, x1c, x20, camera(cap['anim_obj']))
    checks.append((f'{tag} glow colour', int(cap['colour'], 16) == d['colour'], 1))
    q = [x for v in d['quad'] for x in v]
    checks.append((f'{tag} glow quad', same(q, floats(cap['quad'])), 20))
    vs = cap['vs']
    exp_vs = [(4, d['vs1'][4]), (8, d['vs1'][8]), (0, d['vs1'][0]), (4, d['vs2'][4]),
              (8, d['vs2'][8])] + [(10, x) for x in d['drops']]
    ok = len(vs) == len(exp_vs) and all(
        c_['index'] == i and same(e, floats(c_['data'])) for c_, (i, e) in zip(vs, exp_vs))
    checks.append((f'{tag} vertex constants', ok, sum(len(e) for _, e in exp_vs)))
    ps = cap['ps']
    ok = (len(ps) == 2 and ps[0]['index'] == 0 and same(d['ps1'], floats(ps[0]['data']))
          and same(d['ps2'], floats(ps[1]['data'])))
    checks.append((f'{tag} colour constants', ok, len(d['ps1']) + len(d['ps2'])))
    wc_bad, wc_second = 0, []
    for w in cap.get('wc', []):
        ref = floats(w['ref'])
        centre, second = m.weighted_center(ref, x20)
        g_centre, g_second = floats(w['center']), floats(w['second'])[0]
        wc_bad += not (same(centre, g_centre) and same([second], [g_second]))
        wc_second.append(g_second)
    if cap.get('wc'):
        n = len(cap['wc'])
        checks.append((f'{tag} weighted centre, both outputs ({n} calls)', wc_bad == 0, n * 4))
    stale = [floats(vs[1]['data'])[7], floats(vs[1]['data'])[15]]
    vec.append({
        'target': cap['target'], 't': t, 'frame': cap['frame'], 'x1c': x1c, 'x20': x20,
        'bulges': [{'size': b.size, 'h': b.h, 'dir': b.d, 'mask': b.mask, 't0': b.t0,
                    'freq': b.freq, 'amp': b.amp, 'drop': b.drop}
                   for b in m.bulges[:m.n_bulges]],
        'drops': [{'size': x.size, 'pos': x.pos, 'dir': x.d, 'disp': x.disp,
                   'scale': x.scale, 'rate': x.rate, 't0': x.t0, 'freq': x.freq, 'amp': x.amp}
                  for x in m.drops[:m.n_drops]],
        'glow': {'k': d['k'], 'colour': f"{d['colour']:#010x}", 'quad': d['quad']},
        'constants': {'vs_c10': d['vs1'][8][8:12], 'ps1_c0': d['ps1'][:4],
                      'ps2_c0': d['ps2'][:4], 'drops_c10_c13': d['drops'],
                      'not_set_by_blob_draw': {'c9.w': stale[0], 'c11.w': stale[1]}},
        'mesh_extent': [min(v[3] for v in m.mesh), max(v[3] for v in m.mesh)],
        'weighted_centre_second': ({'calls': len(wc_second), 'min': min(wc_second),
                                    'max': max(wc_second)} if wc_second else None)})


def compare_run(name):
    run = os.path.join(ROOT, 'out', 'xemu', 'runs', name)
    d = json.load(open(os.path.join(run, 'probe_c6.json')))
    m = bm.Blob()
    m.restart()
    checks, vec, first_bad = [], [], None
    if d['static']:
        check_static(d['static'], m, checks)
    caps = {c_['frame']: c_ for c_ in d['captures']}
    n_ok, steps, rand_ok, prev_t, events = 0, [], 0, None, []
    for n, fr in enumerate(d['frames']):
        t, dt = struct.unpack('<2f', bytes.fromhex(fr['args']))
        if prev_t is not None and t < prev_t:
            m.restart()                     # anim_update at t >= 8 with this+0xA set
            events.append({'frame': n, 'event': 'blob_restart', 't': t,
                           'rand_after': f'{m.rand.s:#010x}'})
        prev_t = t
        if 'rand' in fr:
            rand_ok += int(fr['rand'], 16) == m.rand.s
        guest = bytes.fromhex(fr['blob'])
        bad = diff_state(m, guest)
        if bad and first_bad is None:
            mine, _ = model_bytes(m)
            first_bad = (n, [(where(o), mine[o], guest[o]) for o in bad])
        n_ok += not bad
        steps.append([t, dt])
        if 'rand_poked' in fr:
            m.rand.s = int(fr['rand_poked'], 16)
            hits = BRANCH_HITS['retry']
            events.append({'frame': n, 'event': 'generator set', 't': t,
                           'value': fr['rand_poked']})
        m.update(t, dt)
        if 'rand_poked' in fr:
            events[-1]['short_dir_retry_taken'] = BRANCH_HITS['retry'] > hits
        if n in caps:
            check_capture(caps[n], m, fr, checks, vec)
    checks.insert(0, ('state before each update', n_ok == len(d['frames']),
                      len(d['frames'])))
    if any('rand' in fr for fr in d['frames']):
        checks.insert(1, ('generator state before each update', rand_ok == len(d['frames']),
                          len(d['frames'])))
    print(f'== {name}')
    for name_, ok, n in checks:
        print(f"{'ok ' if ok else 'BAD'} {name_} ({n})")
    if first_bad:
        n, bad = first_bad
        print(f'first state difference in frame {n}: {len(bad)} bytes')
        for w, a, b in bad[:16]:
            print(f'  {w}: model {a:#04x} guest {b:#04x}')
    good = sum(ok for _, ok, _ in checks)
    print(f'{good} of {len(checks)} checks equal; {n_ok} of {len(d["frames"])} frame states')
    return {'run': name, 'frames': len(d['frames']), 'rand_steps': m.rand.n,
            'drops': m.n_drops, 'events': events, 'steps_t_dt': steps,
            'checks': [{'what': a, 'equal': b, 'values': c_} for a, b, c_ in checks],
            'captures': vec}


def main():
    names = sys.argv[1:] or ['c6_main', 'c6_tail', 'c6_plain', 'c6_loop', 'c6_poke']
    out = {'script': 'tools/xemu/probe_c6.py, tools/xemu/vectors_c6.py',
           'note': 'values are those of the model; each listed check found them '
                   'bit-equal to the guest', 'runs': [compare_run(n) for n in names]}
    with open(os.path.join(ROOT, 'docs', 'vectors', 'c6_blob.json'), 'w') as f:
        json.dump(out, f, indent=1)


if __name__ == '__main__':
    main()
