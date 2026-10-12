"""Extracts the meshes, keys and texture of the logo from the unpacked kernel.

    python tools/extract/logo.py

Reads out/rom/kernel.img (tools/rom/unpack.py) and writes

  data/logo/logo.bin    0x80071008-0x800743CF as it stands in the image: the
                        packed vertex and index lists of the seven meshes,
                        the two position keys of mesh E and the 16 x 16
                        texture of the two quads
  data/logo/logo.json   the same, decoded by the rules of logo_decode
                        (0x8005B4D1) and its three helpers: per mesh the
                        packed int16 values, the scale and offset with their
                        addresses, the float32 vertices and the expanded
                        index list (docs/decomp/Logo.md 2)
  data/logo/quad_texture.png  the 16 x 16 texture at 0x80073FD0, bytes taken
                        as B, G, R, A (Logo.md 2.4)

The vertex buffers on the GPU are copies of these vertices, with one change
to mesh C that logo_build_meshes makes after copying (Logo.md 3). That change
is not applied here; tools/xemu/logo_model.py applies it.
"""

import hashlib
import json
import os
import struct
import zlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BASE = 0x80010000

LOGO = (0x80071008, 0x800743D0)

# logo_decode 0x8005B4D1, in call order: name, object slot, vertex list,
# vertex count, index list, index count, kind. Kind 5 is decode_vertex5
# (0x8005B3D4: x, y, z with scale 0x80012984 / offset 0x80012988, u, v with
# scale 0x8001298C / offset 0x80012990); kind 3 is decode_i16_scaled
# (0x8005B398: n x 3 values with scale 0x80066974 / offset 0x80066978).
MESHES = [
    ('A', 0xD8, 0x80072AA0, 0xD2, 0x800732D8, 0x150, 5),
    ('B', 0xE0, 0x800735F0, 0x4D, 0x800738F8, 0xE1, 5),
    ('C', 0xE8, 0x80073428, 0x22, 0x80073580, 0x69, 5),
    ('D', 0xF0, 0x80071008, 0x1F5, 0x800723A0, 0x3F0, 5),
    ('Q1', 0x100, 0x800739DC, 4, 0x80073A04, 6, 5),
    ('Q2', 0xF8, 0x80073A0C, 4, 0x80073A34, 6, 5),
    ('E', 0x108, 0x80073A40, 0x9C, 0x80073DE8, 0x1CE, 3),
]
SCALE5 = (0x80012984, 0x80012988, 0x8001298C, 0x80012990)
SCALE3 = (0x80066974, 0x80066978)
KEYS = (0x80073FB8, 2)          # logo_update 0x8005C4A5: 2 x 3 float32
TEXTURE = (0x80073FD0, 16, 16)  # logo_build_quads 0x8005B9A8: 0x400 bytes


class Image:
    def __init__(self, path):
        self.b = open(path, 'rb').read()

    def rd(self, a, n):
        return self.b[a - BASE:a - BASE + n]

    def u(self, fmt, a):
        return struct.unpack('<' + fmt, self.rd(a, struct.calcsize('<' + fmt)))


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def scaled(i, scale, offset):
    """fild, fmul m32, fadd m32, fstp m32 with a 53-bit mantissa: the product
    of an int16 and a float32 is exact in a double; the sum is rounded once
    to double and once to float32."""
    return f32(float(i) * scale + offset)


def expand_indices(img, a, n):
    """decode_delta16_tilde 0x8005B473: the first byte, sign-extended, is the
    first index; each further byte is a signed difference, except 0x7E,
    which introduces a big-endian 16-bit difference in the next two bytes.
    Sums wrap at 16 bits. Returns the indices and the number of bytes read."""
    raw = img.rd(a, 3 * n)
    out = [struct.unpack('b', raw[:1])[0] & 0xFFFF]
    p = 1
    while len(out) < n:
        if raw[p] == 0x7E:
            d = (raw[p + 1] << 8) | raw[p + 2]
            p += 3
        else:
            d = struct.unpack('b', raw[p:p + 1])[0]
            p += 1
        out.append((out[-1] + d) & 0xFFFF)
    return out, p


def mesh(img, name, slot, va, nv, ia, ni, kind):
    if kind == 5:
        s0, o0, s1, o1 = (img.u('f', a)[0] for a in SCALE5)
        raw = img.u('%dh' % (nv * 5), va)
        verts = []
        for k in range(nv):
            r = raw[5 * k:5 * k + 5]
            verts.append([scaled(r[0], s0, o0), scaled(r[1], s0, o0), scaled(r[2], s0, o0),
                          scaled(r[3], s1, o1), scaled(r[4], s1, o1)])
        scale = [{'at': '%#010x' % a, 'value': img.u('f', a)[0]} for a in SCALE5]
        layout = 'x y z u v; x y z = i16 * scale[0] + scale[1], u v = i16 * scale[2] + scale[3]'
        vbytes = nv * 10
    else:
        s0, o0 = (img.u('f', a)[0] for a in SCALE3)
        raw = img.u('%dh' % (nv * 3), va)
        verts = [[scaled(r, s0, o0) for r in raw[3 * k:3 * k + 3]] for k in range(nv)]
        scale = [{'at': '%#010x' % a, 'value': img.u('f', a)[0]} for a in SCALE3]
        layout = 'x y z = i16 * scale[0] + scale[1]'
        vbytes = nv * 6
    idx, used = expand_indices(img, ia, ni)
    return {'name': name, 'slot': '%#x' % slot,
            'vertices_at': '%#010x' % va, 'vertex_bytes': vbytes, 'vertex_count': nv,
            'indices_at': '%#010x' % ia, 'index_bytes': used, 'index_count': ni,
            'triangles': ni // 3, 'max_index': max(idx), 'layout': layout, 'scale': scale,
            'packed': list(raw), 'vertices': verts, 'indices': idx}


def keys(img):
    return [{'at': '%#010x' % (KEYS[0] + 12 * k), 'xyz': list(img.u('3f', KEYS[0] + 12 * k))}
            for k in range(KEYS[1])]


def png(w, h, rgba):
    def chunk(tag, data):
        c = struct.pack('>I', len(data)) + tag + data
        return c + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
    rows = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def texture(img):
    a, w, h = TEXTURE
    raw = img.rd(a, w * h * 4)
    rgba = b''.join(bytes((raw[i + 2], raw[i + 1], raw[i], raw[i + 3]))
                    for i in range(0, len(raw), 4))
    return raw, png(w, h, rgba)


def logo(img):
    meshes = [mesh(img, *m) for m in MESHES]
    # Coverage: every byte of the range is one of the lists, the keys or the
    # texture, up to padding between lists.
    spans = sorted([(int(m['vertices_at'], 16), m['vertex_bytes']) for m in meshes]
                   + [(int(m['indices_at'], 16), m['index_bytes']) for m in meshes]
                   + [(KEYS[0], 12 * KEYS[1]), (TEXTURE[0], 0x400)])
    gaps, pos = [], LOGO[0]
    for a, n in spans:
        if a < pos:
            raise SystemExit('overlap at %#x' % a)
        if a > pos:
            gaps.append({'at': '%#010x' % pos, 'bytes': img.rd(pos, a - pos).hex()})
        pos = a + n
    if pos != LOGO[1]:
        raise SystemExit('range ends at %#x, not %#x' % (pos, LOGO[1]))
    return {'source': 'out/rom/kernel.img, %#010x-%#010x' % LOGO,
            'meshes': meshes, 'keys_mesh_E': keys(img),
            'texture': {'at': '%#010x' % TEXTURE[0], 'width': TEXTURE[1],
                        'height': TEXTURE[2], 'bytes_per_texel': 4},
            'padding': gaps}


def write(out, files):
    os.makedirs(out, exist_ok=True)
    for name, data in files.items():
        with open(os.path.join(out, name), 'wb') as f:
            f.write(data)
    with open(os.path.join(out, 'SHA256SUMS'), 'w', newline='\n') as f:
        for name in sorted(files):
            f.write('%s  %s\n' % (hashlib.sha256(files[name]).hexdigest(), name))


def main():
    img = Image(os.path.join(ROOT, 'out', 'rom', 'kernel.img'))
    _, tex_png = texture(img)
    data = logo(img)
    write(os.path.join(ROOT, 'data', 'logo'), {
        'logo.bin': img.rd(LOGO[0], LOGO[1] - LOGO[0]),
        'logo.json': (json.dumps(data, indent=1) + '\n').encode(),
        'quad_texture.png': tex_png})
    for m in data['meshes']:
        print('%-2s %3d vertices %4d indices (%3d triangles), max index %3d, '
              'index bytes %d' % (m['name'], m['vertex_count'], m['index_count'],
                                  m['triangles'], m['max_index'], m['index_bytes']))
    print('padding:', data['padding'])
    print('data/logo written')


if __name__ == '__main__':
    main()
