"""Checks chamber and camera of run c5_main against the model, writes docs/vectors/c5_chamber.json.

    python tools/xemu/probe_c5.py --name c5_main --t 0.75 --t 2.0 --t 3.0 --t 4.5 --t 5.6
    python tools/xemu/vectors_c5.py

Compared, value for value and bit for bit (float32), are

- once: the decoded quaternions and positions, the expanded track index
  lists, every placement record (matrices +8, +0x48, +0x88, scale +0xC8,
  +0xD8, form, tracks, side byte +0x118) of all twelve groups, and the
  generated geometry of all twelve groups (form table, vertex positions,
  vertex attributes, indices);
- at every captured time: the 29 track rotations and 32 track positions,
  the detail switch, the camera path keys, eye, target, view and
  projection matrix, the two views from the blob centre, and the world
  matrix W of every part that prim_group_draw hands on.

The model is tools/xemu/chamber_model.py. The vector file holds what the
guest had; every entry says whether the model gave the same bits.
"""

import hashlib
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chamber_model as M  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RUN = 'c5_main'
OBJ = 0x8006F050
CHAMBER, CAMERA, CAMPATH, BLOB = 0x968, 0x6E8, 0x1594, 0xB5C
KEYS = 0x800708C8 - OBJ


def fl(b, o, n):
    return list(struct.unpack_from('<%df' % n, b, o))


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def same(a, b):
    return all(x == y or (x != x and y != y) for x, y in zip(a, b)) and len(a) == len(b)


def sha(b):
    return hashlib.sha256(b).hexdigest()


class Check:
    def __init__(self):
        self.rows = []

    def __call__(self, name, model, guest):
        ok = same(model, guest)
        self.rows.append((name, ok, len(guest)))
        return ok


def static_part(st, ch, chk):
    out = {}
    q = bytes.fromhex(st['quats'])
    gq = fl(q, 0, len(q) // 4)
    out['quats'] = {'count': len(gq) // 4,
                    'equal': chk('quaternions', [x for v in ch.quats for x in v], gq),
                    'nan_w': [i for i in range(len(gq) // 4) if gq[4 * i + 3] != gq[4 * i + 3]]}
    p = bytes.fromhex(st['positions'])
    out['positions'] = {'count': len(p) // 12,
                        'equal': chk('positions', [x for v in ch.positions for x in v],
                                     fl(p, 0, len(p) // 4))}
    out['rot_tracks_equal'] = chk('rotation track lists', [x for t in ch.rot_tracks for x in t],
                                  [x for h in st['rot_tracks'] for x in struct.unpack('<30h', bytes.fromhex(h))])
    out['pos_tracks_equal'] = chk('position track lists', [x for t in ch.pos_tracks for x in t],
                                  [x for h in st['pos_tracks'] for x in struct.unpack('<30h', bytes.fromhex(h))])
    groups = []
    scene = ch.s
    for gi, G in enumerate(st['groups']):
        g, lod = scene['groups'][gi % 6], gi // 6
        pl = bytes.fromhex(G['places'])
        mod, gue, meta_ok = [], [], True
        for j, pp in enumerate(ch.groups[gi % 6]):
            o = j * 0x11C
            gue += (fl(pl, o + 8, 16) + fl(pl, o + 0x48, 16) + fl(pl, o + 0x88, 16)
                    + fl(pl, o + 0xC8, 4) + fl(pl, o + 0xD8, 16))
            mod += pp['L'] + pp['Linv'] + pp['S'] + pp['scale'] + pp['SL']
            meta_ok &= struct.unpack_from('<3h', pl, o) == (pp['form'], pp['pos_track'], pp['rot_track'])
            meta_ok &= pl[o + 0x118] == pp['side']
        m, info = M.build_group(g, lod)
        fm = bytes.fromhex(G['forms'])
        geo = G['geometry']
        P, A, I = (bytes.fromhex(geo[k]) for k in ('positions', 'attributes', 'indices'))
        stv = 11 if g['vertex_format'] == 1 else 9
        mp, gp, ma, ga, unwritten = [], [], [], [], 0
        for v, (pv, av) in enumerate(zip(m.pos, m.attr)):
            if pv is None:
                unwritten += 1
                continue
            mp += pv
            gp += fl(P, 12 * v, 3)
            ma += av
            ga += fl(A, 4 * stv * v, stv)
        gi_info = [list(struct.unpack_from('<6I', fm, k * 0x18)) for k in range(len(info))]
        mi_info = [[x['v0'], x['nv'], x['i0'], x['prims'], x['ni'], x['caps']] for x in info]
        name = f'group {gi} ({g["type_name"]}, detail {lod})'
        groups.append({
            'slot': gi, 'type': g['type_name'], 'detail': lod, 'group_ptr': G['ptr'],
            'placements': len(ch.groups[gi % 6]),
            'placements_equal': chk(name + ' placements', mod, gue) and meta_ok,
            'forms': gi_info, 'forms_equal': gi_info == mi_info,
            'vertices': len(P) // 12, 'vertices_not_written': unwritten, 'indices': len(I) // 2,
            'sha256': {'positions': sha(P), 'attributes': sha(A), 'indices': sha(I)},
            'positions_equal': chk(name + ' vertex positions', mp, gp),
            'attributes_equal': chk(name + ' vertex attributes', ma, ga),
            'indices_equal': chk(name + ' indices', m.idx, list(struct.unpack('<%dH' % (len(I) // 2), I)))})
    out['groups'] = groups
    return out


def capture_part(c, st, ch, cp, chk):
    ob = bytes.fromhex(c['obj'])
    t = fl(ob, 0x10, 1)[0]
    tag = f't={t:.4f}'
    s_ = ch.update(t)
    out = {'t': t, 'flips': c['flips'], 'frame_in_anim': c['frame_in_anim']}
    rm, tp = bytes.fromhex(c['rot_mats']), bytes.fromhex(c['track_pos'])
    out['tracks'] = {'u': s_['u'], 'k': s_['k'],
                     'rotations_equal': chk(tag + ' track rotations',
                                            [x for m in s_['rot'] for x in m], fl(rm, 0, 29 * 16)),
                     'positions_equal': chk(tag + ' track positions',
                                            [x for v in s_['pos'] for x in v], fl(tp, 0, 96))}
    out['detail'] = ob[CHAMBER + 0x5C]
    chk(tag + ' detail switch', [s_['lod']], [out['detail']])
    eye, tgt, view, info = M.camera(cp, t)
    cam = {'s': info['s'], 'key': info['key'],
           'eye': fl(ob, CAMERA + 0x58, 3), 'target': fl(ob, CAMERA + 0x64, 3),
           'up': fl(ob, CAMERA + 0x70, 3), 'view': fl(ob, CAMERA + 0xFC, 16),
           'projection': fl(ob, CAMERA + 0x13C, 16)}
    cam['equal'] = all([chk(tag + ' eye', eye, cam['eye']), chk(tag + ' target', tgt, cam['target']),
                        chk(tag + ' view', view, cam['view']),
                        chk(tag + ' projection', M.projection(ob[0x181E]), cam['projection'])])
    keys = []
    for k in cp.keys:
        keys += [k['t']] + k['eye'] + k['eye_tan'] + k['target'] + k['target_tan'] + [k['cut'], k['split']]
    gk = fl(ob, KEYS + cp.first * 0x3C, cp.n_table * 15) + fl(ob, CAMPATH, 8 * 15)
    cam['path_keys_equal'] = chk(tag + ' path keys', keys, gk)
    out['camera'] = cam
    n = struct.unpack_from('<i', ob, BLOB + 0xA34)[0]
    pts = [fl(ob, BLOB + 0x54, 3)] + [fl(ob, BLOB + 0x878 + k * 0x38, 3) for k in range(n)]
    lv = {}
    for side, off, name in ((0, 0x1A4, 'down'), (1, 0x164, 'up')):
        ctr = M.weighted_center([0.0, 0.0, 40.0 if side else -30.0], pts, fl(ob, 0x20, 1)[0])
        g = fl(ob, CHAMBER + off, 16)
        lv[name] = {'center': ctr, 'matrix': g, 'equal': chk(f'{tag} light view {name}',
                                                              M.light_view(ctr, side)[2], g)}
    out['light_views'] = lv
    W = ch.world(s_)
    gptr = [u32(ob, CHAMBER + 4 * i) for i in range(12)]
    plp = [u32(bytes.fromhex(st['groups'][i]['raw']), 0x14) for i in range(12)]
    parts, seen, repeats, mod, gue = [], set(), 0, [], []
    for p in c['parts']:
        gi = gptr.index(int(p['group'], 16))
        j = (int(p['place'], 16) - plp[gi]) // 0x11C
        if (gi, j) in seen:
            repeats += 1
            continue
        seen.add((gi, j))
        w = fl(bytes.fromhex(p['W']), 0, 16)
        mod += W[gi % 6][j]
        gue += w
        pp = ch.groups[gi % 6][j]
        parts.append({'group': gi, 'index': j, 'form': pp['form'], 'rot_track': pp['rot_track'],
                      'pos_track': pp['pos_track'], 'position': w[12:15],
                      'axes': w[0:3] + w[4:7] + w[8:11], 'nan': any(x != x for x in w),
                      'equal': same(W[gi % 6][j], w)})
    out['parts_equal'] = chk(tag + f' world matrices of {len(parts)} parts', mod, gue)
    out['parts_repeated_stops'] = repeats
    out['parts_drawn'] = len(parts)
    out['parts_nan'] = sum(1 for p in parts if p['nan'])
    out['parts'] = parts
    return out


def short(x):
    """float32 values in their shortest decimal form (reads back to the same
    bits); NaN as the string "NaN", which JSON has no number for."""
    if isinstance(x, float):
        return 'NaN' if x != x else float(str(M.F(x)))
    if isinstance(x, list):
        return [short(v) for v in x]
    if isinstance(x, dict):
        return {k: short(v) for k, v in x.items()}
    return x


def write(path, out):
    """indent 1, but each part on one line"""
    out = short(out)
    lines = {}
    for c in out['captures']:
        for p in c['parts']:
            key = '@@part%d@@' % len(lines)
            lines[key] = json.dumps(p, separators=(',', ':'))
        c['parts'] = ['@@part%d@@' % i for i in range(len(lines) - len(c['parts']), len(lines))]
    text = json.dumps(out, indent=1)
    for key, line in lines.items():
        text = text.replace('"%s"' % key, line, 1)
    with open(path, 'w', newline='\n') as f:
        f.write(text + '\n')
    json.load(open(path))


def main():
    d = json.load(open(os.path.join(ROOT, 'out', 'xemu', 'runs', RUN, 'probe_c5.json')))
    r = json.load(open(os.path.join(ROOT, 'out', 'xemu', 'runs', RUN, 'run.json')))
    chk = Check()
    ch, cp = M.Chamber(), M.CamPath()
    out = {'run': RUN, 'command': r['command'], 'inputs': r['inputs'],
           'probe': r.get('probe_c5'), 'model': 'tools/xemu/chamber_model.py',
           'units': {'position': 'row 3 of W', 'axes': 'rows 0-2 of W (scaled local axes)'},
           'static': static_part(d['static'], ch, chk),
           'captures': [capture_part(c, d['static'], ch, cp, chk) for c in d['captures']]}
    bad = [x for x in chk.rows if not x[1]]
    out['summary'] = {'checks': len(chk.rows), 'values': sum(x[2] for x in chk.rows),
                      'failed': [x[0] for x in bad]}
    path = os.path.join(ROOT, 'docs', 'vectors', 'c5_chamber.json')
    write(path, out)
    for name, ok, n in chk.rows:
        if not ok:
            print('MISMATCH', name)
    print(f'{len(chk.rows) - len(bad)}/{len(chk.rows)} checks equal, '
          f'{sum(x[2] for x in chk.rows)} values -> {path}')


if __name__ == '__main__':
    main()
