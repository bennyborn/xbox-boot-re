"""Extracts the data of the chamber and the camera path from the unpacked kernel.

    python tools/extract/chamber.py

Reads out/rom/kernel.img (tools/rom/unpack.py) and writes

  data/chamber/scene.bin   0x800743D0-0x80078503 as it stands in the image:
                           the index lists of 29 rotation tracks and 32
                           position tracks, the packed positions and
                           quaternions with their sign bits, and the form and
                           placement records of the six groups
  data/chamber/scene.json  the same, decoded into fields (layout:
                           docs/decomp/Chamber.md); the index lists already
                           expanded by the rule of 0x8005DF27
  data/camera/campath.bin  0x80070DB8-0x80071003: 0x15 path keys of 0x1C bytes
  data/camera/campath.json the same, decoded (layout: docs/decomp/Camera.md)

Values that the kernel computes from these (floats from the packed
integers, matrices, vertices) are not data of the ROM; their formulas are
in Chamber.md and Camera.md, and tools/xemu/chamber_model.py builds them.
"""

import hashlib
import json
import os
import struct

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BASE = 0x80010000

SCENE = (0x800743D0, 0x80078504)
CAMPATH = (0x80070DB8, 0x80071004)

ROT_TRACKS = (0x800743D0, 29)      # 0x8005FC65: stride 0x5A, 30 entries each
POS_TRACKS = (0x80074E08, 32)
TRACK_STRIDE, TRACK_LEN = 0x5A, 30
POSITIONS = (0x80075948, 0x80075950)   # count, int16 x 3 each
QUATS = (0x800760D0, 0x800760D8, 0x80076A88)   # count, int16 x 3 each, sign bits

# chamber_build_groups 0x8005F7B8, in the order of the group pointers at
# chamber+0 .. chamber+0x14: type, forms, form count, placements, count,
# vertex format, the two texture words it hands over (chamber+0x14 group 5 ..)
GROUPS = [
    # slot, type, forms, n_forms, places, n_places, fmt
    (0, 0, 0x80076C80, 2, 0x80076AC0, 0x1C, 1),
    (1, 1, 0x80077034, 6, 0x80076C88, 0x2F, 0),
    (2, 2, None, 1, 0x80077040, 0x48, 0),
    (3, 3, 0x80077C40, 7, 0x80077700, 0x54, 0),
    (4, 4, 0x80077E10, 3, 0x80077C78, 0x22, 0),
    (5, 5, 0x80077E88, 5, 0x80077E40, 0x06, 1),
]
TYPE_NAMES = {0: 'sphere', 1: 'cylinder', 2: 'box', 3: 'torus', 4: 'cone', 5: 'lathe'}
FORM_SIZE = {0: 1, 1: 2, 2: 0, 3: 8, 4: 0x10, 5: 0x14C}
PLACE_SIZE = {0: 0x10, 1: 0x14, 2: 0x18, 3: 0x10, 4: 0xC, 5: 0xC}


class Image:
    def __init__(self, path):
        self.b = open(path, 'rb').read()

    def rd(self, a, n):
        return self.b[a - BASE:a - BASE + n]

    def u(self, fmt, a):
        return struct.unpack('<' + fmt, self.rd(a, struct.calcsize('<' + fmt)))


def expand_track(raw, n=TRACK_LEN):
    """0x8005DF27: signed byte differences; 0x7F introduces a big-endian
    16-bit difference. The first entry is the value itself."""
    out, i, prev = [], 0, 0
    for k in range(n):
        b = raw[i]
        if b == 0x7F:
            d = (raw[i + 1] << 8) | raw[i + 2]
            i += 3
        else:
            d = b - 256 if b >= 128 else b
            i += 1
        v = d if k == 0 else prev + d
        prev = ((v + 0x8000) & 0xFFFF) - 0x8000
        out.append(prev)
    return out, i


def form(img, typ, a):
    if typ == 0:
        return {'at': '%#010x' % a, 'segments': img.u('b', a)[0]}
    if typ == 1:
        rings, segs = img.u('bb', a)
        return {'at': '%#010x' % a, 'rings': rings, 'segments': segs}
    if typ == 3:
        r, = img.u('f', a)
        n1, n2 = img.u('bb', a + 4)
        return {'at': '%#010x' % a, 'tube': r, 'segments_axis': n1, 'segments_tube': n2,
                'pad': img.rd(a + 6, 2).hex()}
    if typ == 4:
        r0, r1, h = img.u('fff', a)
        rings, segs = img.u('bb', a + 0xC)
        return {'at': '%#010x' % a, 'r0': r0, 'r1': r1, 'height': h, 'rings': rings,
                'segments': segs, 'pad': img.rd(a + 0xE, 2).hex()}
    if typ == 5:
        n = img.u('b', a + 0x149)[0]
        pts = []
        for i in range(n):
            x, y, z = img.u('fff', a + i * 16)
            fl, = img.u('I', a + i * 16 + 12)
            pts.append({'p': [x, y, z], 'flags': fl})
        return {'at': '%#010x' % a, 'points': pts, 'axis': list(img.u('fff', a + 0x130)),
                'center': list(img.u('fff', a + 0x13C)), 'segments': img.u('b', a + 0x148)[0],
                'n_points': n, 'pad': img.rd(a + 0x14A, 2).hex()}
    raise ValueError(typ)


def place(img, typ, a):
    d = {'at': '%#010x' % a}
    if typ == 0:
        d['pos_i16'] = list(img.u('hhh', a))
        d['form'], d['pos_track'], d['rot_track'] = img.u('bbb', a + 6)
        d['pad'] = img.rd(a + 9, 3).hex()
        d['radius'], = img.u('f', a + 0xC)
        return d
    d['quat'], = img.u('b', a)
    d['pad0'] = img.rd(a + 1, 1).hex()
    d['pos_i16'] = list(img.u('hhh', a + 2))
    d['form'], d['pos_track'], d['rot_track'] = img.u('bbb', a + 8)
    if typ == 1:
        d['pad'] = img.rd(a + 0xB, 1).hex()
        d['radius'], d['half_height'] = img.u('ff', a + 0xC)
    elif typ == 2:
        d['pad'] = img.rd(a + 0xB, 1).hex()
        d['size_y'], d['size_z'], d['size_x'] = img.u('fff', a + 0xC)
    elif typ == 3:
        d['pad'] = img.rd(a + 0xB, 1).hex()
        d['scale'], = img.u('f', a + 0xC)
    else:
        d['pad'] = img.rd(a + 0xB, 1).hex()
    return d


def scene(img):
    s = {'source': 'out/rom/kernel.img, %#010x-%#010x' % SCENE}
    for key, (a, n) in (('rot_tracks', ROT_TRACKS), ('pos_tracks', POS_TRACKS)):
        tr = []
        for k in range(n):
            at = a + k * TRACK_STRIDE
            vals, used = expand_track(img.rd(at, TRACK_STRIDE))
            tr.append({'at': '%#010x' % at, 'bytes_used': used, 'index': vals})
        s[key] = tr
    n, = img.u('I', POSITIONS[0])
    s['positions'] = {'count_at': '%#010x' % POSITIONS[0], 'at': '%#010x' % POSITIONS[1],
                      'i16': [list(img.u('hhh', POSITIONS[1] + 6 * i)) for i in range(n)]}
    n, = img.u('I', QUATS[0])
    words = img.u('%dI' % ((n + 31) // 32), QUATS[2])
    s['quats'] = {'count_at': '%#010x' % QUATS[0], 'at': '%#010x' % QUATS[1],
                  'sign_bits_at': '%#010x' % QUATS[2],
                  'i16': [list(img.u('hhh', QUATS[1] + 6 * i)) for i in range(n)],
                  'w_positive': [(words[i >> 5] >> (i & 31)) & 1 for i in range(n)]}
    groups = []
    for slot, typ, fa, nf, pa, npl, fmt in GROUPS:
        g = {'slot': slot, 'type': typ, 'type_name': TYPE_NAMES[typ], 'vertex_format': fmt,
             'forms_at': '%#010x' % fa if fa else None, 'places_at': '%#010x' % pa,
             'forms': [form(img, typ, fa + i * FORM_SIZE[typ]) for i in range(nf)] if fa else [],
             'places': [place(img, typ, pa + i * PLACE_SIZE[typ]) for i in range(npl)]}
        groups.append(g)
    s['groups'] = groups
    return s


def campath(img):
    keys = []
    for i in range(0x15):
        a = CAMPATH[0] + i * 0x1C
        t, cut, split, pad = img.u('Bbbb', a)
        keys.append({'at': '%#010x' % a, 'time_byte': t, 'cut_byte': cut,
                     'split_byte': split, 'pad': pad, 'eye': list(img.u('fff', a + 4)),
                     'target': list(img.u('fff', a + 0x10))})
    return {'source': 'out/rom/kernel.img, %#010x-%#010x' % CAMPATH, 'keys': keys}


def write(out, files):
    os.makedirs(out, exist_ok=True)
    for name, data in files.items():
        with open(os.path.join(out, name), 'wb') as f:
            f.write(data)
    with open(os.path.join(out, 'SHA256SUMS'), 'w', newline='\n') as f:
        for name in sorted(files):
            f.write('%s  %s\n' % (hashlib.sha256(files[name]).hexdigest(), name))


def dump(obj):
    return (json.dumps(obj, indent=1) + '\n').encode()


def main():
    img = Image(os.path.join(ROOT, 'out', 'rom', 'kernel.img'))
    write(os.path.join(ROOT, 'data', 'chamber'), {
        'scene.bin': img.rd(SCENE[0], SCENE[1] - SCENE[0]),
        'scene.json': dump(scene(img))})
    write(os.path.join(ROOT, 'data', 'camera'), {
        'campath.bin': img.rd(CAMPATH[0], CAMPATH[1] - CAMPATH[0]),
        'campath.json': dump(campath(img))})
    print('data/chamber, data/camera written')


if __name__ == '__main__':
    main()
