"""Compares the logo runs with the model and writes docs/vectors/c7_logo.json.

    python tools/xemu/vectors_c7.py [c7_main c7_b]

For each run of probe_c7.py:

1. Buffers: every vertex buffer, index list and count in the guest against
   tools/xemu/logo_model.py on data/logo/logo.json; the quad texture against
   the 16 x 16 texels at 0x80073FD0, stored in Z order.
2. State: at each stop in anim_draw, the fields of the logo object that
   logo_update writes (+0x4C, +0x90..+0x98, +0xC4, +0xD4, +0xD0 set) against
   the model run over the same sequence of t; the texture draws against the
   frames at which the model draws the texture.
3. Method stream: from the moment the trace was switched on, every
   BEGIN_END block whose vertex array 0 lies on a logo buffer. For each:
   primitive, array format, number of indices. xemu writes one line
   "NV097_ARRAY_ELEMENT16 * n" for a run of these methods, with n one less
   than the number of words (hw/xbox/nv2a/pgraph/pgraph.c,
   pgraph_method_log: count starts at 0 with the first word); each word
   holds two indices, each NV097_ARRAY_ELEMENT32 one. Mesh D's vertex
   constants 0..8 must equal the model for one recorded frame, in order;
   the factor colour before Q1, Q2 and E must equal the model's alpha word
   or E's colour.
"""

import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import logo_model as lm  # noqa: E402
from chamber_model import f32  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RUNS = os.path.join(ROOT, 'out', 'xemu', 'runs')
OUT = os.path.join(ROOT, 'docs', 'vectors', 'c7_logo.json')

LINE = re.compile(r'nv2a_pgraph_method(_abbrev)? \d+: 0x97 -> 0x[0-9a-f]+ (\S+?)(?:\[(\d+)\])? '
                  r'(?:\* (\d+)|(0x[0-9a-f]+))')
FLIP = 'nv2a_pgraph_flip_increment_write'
FMT = {'stride': lambda v: v >> 8, 'size': lambda v: (v >> 4) & 0xF, 'type': lambda v: v & 0xF}


def fl(b, o):
    return struct.unpack_from('<f', b, o)[0]


def bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def morton16(raw):
    out = bytearray(len(raw))
    for y in range(16):
        for x in range(16):
            z = 0
            for i in range(4):
                z |= ((x >> i) & 1) << (2 * i) | ((y >> i) & 1) << (2 * i + 1)
            out[4 * z:4 * z + 4] = raw[4 * (16 * y + x):4 * (16 * y + x) + 4]
    return bytes(out)


def check_static(st, meshes, fails):
    res = {}
    for name in lm.MESHES:
        g, m = st[name], meshes[name]
        ok = {'vertex_count': g['nv'] == m['vertex_count'],
              'index_count': g['ni'] == m['index_count'],
              'vertex_buffer': bytes.fromhex(g['vb']) == lm.vertex_buffer(m),
              'index_list': bytes.fromhex(g['ib']) == lm.index_buffer(m)}
        res[name] = {'phys': g['phys'], 'vertices': g['nv'], 'indices': g['ni'], **ok}
        fails += [f'{name} {k}' for k, v in ok.items() if not v]
    img = open(os.path.join(ROOT, 'out', 'rom', 'kernel.img'), 'rb').read()
    rom = img[0x80073FD0 - 0x80010000:][:0x400]
    tex = bytes.fromhex(st['quad_texture']['data'])
    res['quad_texture'] = {'phys': st['quad_texture']['phys'], 'z_order_equal': tex == morton16(rom),
                           'linear_equal': tex == rom}
    if tex != morton16(rom):
        fails.append('quad texture')
    return res


def check_state(d, fails):
    prev, rows, model_rtt = None, [], []
    for i, fr in enumerate(d['frames']):
        anim, logo = bytes.fromhex(fr['anim']), bytes.fromhex(fr['logo'])
        t = fl(anim, 0)
        prev = lm.update(t, prev)
        if prev.get('rtt'):
            model_rtt.append(i)
        want = {'tm': prev['tm'], 'pos': [bits(x) for x in prev['pos']],
                'alpha': bits(prev['alpha']),
                'phase': 0 if prev['phase'] is None else int(prev['phase']),
                'texture': prev['phase'] is not None}
        got = {'tm': logo[0x4C], 'pos': list(struct.unpack_from('<3I', logo, 0x90)),
               'alpha': struct.unpack_from('<I', logo, 0xC4)[0], 'phase': logo[0xD4],
               'texture': struct.unpack_from('<I', logo, 0xD0)[0] != 0}
        bad = [k for k in want if want[k] != got[k]]
        if bad:
            fails.append(f'state t={t:.4f}: {bad}')
        rows.append({'t': t, 'switch': fr['switch'] == '01', 'equal': not bad})
    # the texture is drawn in logo_update, before the stop in anim_draw of
    # the same frame
    guest_rtt = [r['frame'] + 1 for r in d['rtt']]
    if guest_rtt != model_rtt:
        fails.append(f'texture draws at frames {guest_rtt}, model {model_rtt}')
    return rows, guest_rtt, model_rtt


def parse_stream(path, start):
    """BEGIN_END blocks with the state they were drawn with."""
    with open(path, 'rb') as f:
        f.seek(start)
        text = f.read().decode('latin1')
    off, fmt, const, factor, mv = {}, {}, {}, None, [0] * 16
    load, k_const = 0, 0
    flips, cur, draws = 0, None, []
    for line in text.split('\n'):
        if line.startswith(FLIP):
            flips += 1
            continue
        m = LINE.match(line)
        if not m:
            continue
        abbrev, name, idx, n, val = m.groups()
        v = int(val, 16) if val else None
        if name == 'NV097_SET_VERTEX_DATA_ARRAY_OFFSET':
            off[int(idx) // 4] = v
        elif name == 'NV097_SET_VERTEX_DATA_ARRAY_FORMAT':
            fmt[int(idx) // 4] = v
        elif name == 'NV097_SET_TRANSFORM_CONSTANT_LOAD':
            load, k_const = v, 0
        elif name == 'NV097_SET_TRANSFORM_CONSTANT':
            const[(load + k_const // 4, k_const % 4)] = v
            k_const += 1
        elif name == 'NV097_SET_COMBINER_FACTOR0' and idx == '0':
            factor = v
        elif name == 'NV097_SET_MODEL_VIEW_MATRIX' and int(idx) < 64:
            mv[int(idx) // 4] = v
        elif name == 'NV097_SET_BEGIN_END':
            if v:
                cur = {'flip': flips, 'prim': v, 'off0': off.get(0), 'fmt0': fmt.get(0),
                       'indices': 0, 'arrays': 0, 'inline': 0,
                       'const': {i: [const.get((96 + i, j)) for j in range(4)] for i in range(9)},
                       'factor': factor, 'model_view': list(mv)}
            elif cur:
                draws.append(cur)
                cur = None
        elif cur is not None:
            if name == 'NV097_ARRAY_ELEMENT16':
                cur['indices'] += 2 * (int(n) + 1 if abbrev else 1)
            elif name == 'NV097_ARRAY_ELEMENT32':
                cur['indices'] += int(n) + 1 if abbrev else 1
            elif name == 'NV097_DRAW_ARRAYS':
                cur['arrays'] += (v >> 24) + 1
            elif name == 'NV097_INLINE_ARRAY':
                cur['inline'] += 1
    return draws, flips


def frame_matrices(fr):
    def m(h):
        return [fl(bytes.fromhex(h), 4 * i) for i in range(16)]
    return m(fr['frame_matrix']), m(fr['view']), m(fr['proj'])


def check_stream(run, d, meshes, fails):
    draws, flips = parse_stream(os.path.join(RUNS, run, 'trace.log'), d['trace_on_at'])
    phys = {int(d['static'][n]['phys'], 16): n for n in lm.MESHES}
    want_fmt = {n: (0xC, 3, 2) if n == 'E' else (0x14, 3, 2) for n in lm.MESHES}
    # candidate D constant sets: every stop at which logo_draw takes the 3D
    # branch (t < 6, switch on) and every texture draw
    frames = d['frames']
    t_of = [fl(bytes.fromhex(f['anim']), 0) for f in frames]
    cands = []
    for i, f in enumerate(frames):
        if f['switch'] == '01' and t_of[i] < f32(lm.END - lm.TAIL):
            cands.append(('frame', i, t_of[i], f['view'], f['proj']))
    for r in d['rtt']:
        cands.append(('texture', r['frame'] + 1, fl(bytes.fromhex(r['anim']), 0), r['view'], r['proj']))
    cands.sort(key=lambda c: (c[1], c[0] == 'frame'))
    fm = frame_matrices(frames[0])[0]
    states, prev = [], None
    for t in t_of:
        prev = lm.update(t, prev)
        states.append(dict(prev))
    out, counts, next_c, next_q = [], {}, 0, 0
    for dr in draws:
        name = phys.get(dr['off0'])
        if name is None or dr['inline']:
            continue        # inline vertices do not read array 0; its offset is stale
        f = dr['fmt0']
        row = {'flip': dr['flip'], 'mesh': name, 'primitive': dr['prim'],
               'stride': FMT['stride'](f), 'size': FMT['size'](f), 'type': FMT['type'](f),
               'indices': dr['indices']}
        ok = (dr['prim'] == 5 and (row['stride'], row['size'], row['type']) == want_fmt[name]
              and dr['indices'] == meshes[name]['index_count'] and not dr['arrays']
              and not dr['inline'])
        if name == 'D':
            got = [[x for x in dr['const'][i]] for i in range(9)]
            hit = None
            for j in range(next_c, len(cands)):
                kind, fi, t, vh, ph = cands[j]
                vm = [fl(bytes.fromhex(vh), 4 * i) for i in range(16)]
                pm = [fl(bytes.fromhex(ph), 4 * i) for i in range(16)]
                mat = lm.mesh_d_matrix(fm, vm, pm)
                want = [[bits(mat[4 * r + c]) for c in range(4)] for r in range(4)]
                want += [[bits(x) for x in r] for r in lm.mesh_d_constants(t)]
                if want == got:
                    hit, next_c = (kind, t), j + 1
                    break
            row['constants_0_8'] = [[f'{x:#010x}' for x in r] for r in got]
            row['matched'] = hit and {'by': hit[0], 't': hit[1]}
            ok = ok and hit is not None
        elif name in ('Q1', 'Q2', 'E'):
            row['factor'] = f'{dr["factor"]:#010x}'
            if name == 'E':
                good = dr['factor'] == lm.E_COLOUR
            else:
                alphas = [lm.quad_alpha_word(s['alpha']) for s in states[next_q:]]
                good = dr['factor'] in alphas
                if good:
                    next_q += alphas.index(dr['factor'])
            ok = ok and good
        row['equal'] = bool(ok)
        if not ok:
            fails.append(f'{run} draw {name} at flip {dr["flip"]}')
        counts[name] = counts.get(name, 0) + 1
        out.append(row)
    return out, counts, flips


def main():
    runs = sys.argv[1:] or ['c7_main', 'c7_b']
    meshes = lm.load()
    result = {'runs': {}, 'meshes': {n: {'vertices': meshes[n]['vertex_count'],
                                         'indices': meshes[n]['index_count'],
                                         'triangles': meshes[n]['triangles']}
                                     for n in lm.MESHES}}
    total_fail = []
    for run in runs:
        d = json.load(open(os.path.join(RUNS, run, 'probe_c7.json')))
        fails = []
        static = check_static(d['static'], meshes, fails)
        rows, grtt, mrtt = check_state(d, fails)
        draws, counts, flips = check_stream(run, d, meshes, fails)
        result['runs'][run] = {
            'command': f'python tools/xemu/probe_c7.py --name {run} --trace-from '
                       + str(json.load(open(os.path.join(RUNS, run, 'run.json')))
                             ['probe_c7']['trace_from']),
            'static': static,
            'state': {'frames': len(rows), 'equal': sum(r['equal'] for r in rows),
                      't_first': rows[0]['t'], 't_last': rows[-1]['t'],
                      'first_logo_frame': next(r['t'] for r in rows if r['switch'])},
            'texture_draws': {'guest_frames': grtt, 'model_frames': mrtt,
                              't': [fl(bytes.fromhex(r['anim']), 0) for r in d['rtt']]},
            'stream': {'flips': flips, 'draws_per_mesh': counts,
                       'equal': sum(r['equal'] for r in draws), 'draws': draws},
            'failures': fails}
        total_fail += fails
        print(f'{run}: state {sum(r["equal"] for r in rows)}/{len(rows)}, '
              f'texture draws {grtt} (model {mrtt}), '
              f'draws {sum(r["equal"] for r in draws)}/{len(draws)} {counts}, '
              f'{len(fails)} failures')
        for x in fails[:10]:
            print('  ', x)
    result['failures'] = len(total_fail)
    with open(OUT, 'w', newline='\n') as f:
        json.dump(result, f, indent=1)
        f.write('\n')
    print(OUT)


if __name__ == '__main__':
    main()
