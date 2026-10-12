"""Model of the logo after docs/decomp/Logo.md.

Builds what logo_setup (0x8005BBEE) leaves in the vertex and index buffers,
the state that logo_update (0x8005C4A5) writes for a time t, the vertex
constants that logo_draw (0x8005BC63) hands over with mesh D, the alpha of
the two quads and the matrices of mesh D and mesh E.

The meshes come from data/logo/logo.json (tools/extract/logo.py); constants
are read from out/rom/kernel.img. Arithmetic follows the x87 code with a
53-bit mantissa: Python floats for intermediate values, f32() wherever the
code stores to memory.
"""

import json
import math
import os
import struct

from chamber_model import c, f32, m_ident, m_mul, rom_f32

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

END = rom_f32(0x80066964)        # 8.0
TAIL = rom_f32(0x80066968)       # 2.0
FADE = rom_f32(0x8006696C)       # 0.8
SLIDE = rom_f32(0x80066970)      # 0.25
HALF = rom_f32(0x80010A18)       # 0.5
ONE = rom_f32(0x8001288C)        # 1.0
EPS = rom_f32(0x80012980)        # 0.01
MINUS_ONE = rom_f32(0x80012894)  # -1.0
ALPHA_MAX = rom_f32(0x80012908)  # 255.0
PI = rom_f32(0x8006695C)
KEYS = [[rom_f32(0x80073FB8 + 12 * k + 4 * j) for j in range(3)] for k in range(2)]
COL_A = rom_f32(0x8001299C)      # 0.81568
COL_B = rom_f32(0x80012998)      # 0.5921
COL_C = rom_f32(0x80012994)      # 0.5294
# immediates in logo_draw 0x8005C101-0x8005C131
COL_D = [c(0x3F4BC6A8), c(0x3F5FDF3B), c(0x3B7F9724)]
COL_E = [c(0x3E04816F), c(0x3ED566CF), c(0x3DB88659)]
E_COLOUR = 0xFF62CA13            # push at 0x8005C2C3

MESHES = ['A', 'B', 'C', 'D', 'Q1', 'Q2', 'E']


def load():
    d = json.load(open(os.path.join(ROOT, 'data', 'logo', 'logo.json')))
    return {m['name']: m for m in d['meshes']}


def vertex_buffer(m):
    """Bytes of the GPU buffer after logo_build_meshes / _mesh_e / _quads:
    a copy of the decoded vertices; for mesh C every v with
    |f32(v - 1)| <= 0.01 becomes -1 (0x8005B87F-0x8005B8BD)."""
    out = bytearray()
    for v in m['vertices']:
        v = list(v)
        if m['name'] == 'C':
            if abs(f32(v[4] - ONE)) <= EPS:
                v[4] = MINUS_ONE
        out += struct.pack('<%df' % len(v), *v)
    return bytes(out)


def index_buffer(m):
    return struct.pack('<%dH' % len(m['indices']), *m['indices'])


def clamp01(x):
    """0x8005C5E6-0x8005C623, 0x8005BFAE-0x8005BFDB: NaN passes both tests."""
    return 1.0 if x > 1.0 else (0.0 if x < 0.0 else x)


def update(t, prev=None):
    """logo_update for time t (float32). prev: the state before (for the
    fields that only logo_render_to_texture changes). Returns the fields
    +0x4C, +0x90..+0x98, +0xC4, +0xD4 and whether the texture is drawn."""
    # logo_build_mesh_e sets +0x60 to the identity, so the position starts at 0
    st = dict(prev or {'tm': 0, 'pos': [0.0, 0.0, 0.0], 'alpha': 0.0, 'phase': None})
    t0 = f32(END - TAIL)
    if t < t0:
        st['tm'], st['alpha'], st['rtt'] = 0, 0.0, False
        return st
    st['tm'] = 1
    inv = ONE / SLIDE
    p = (t - t0) * inv
    F = f32(p)
    k = int(F)                       # cvttss2si
    if F < 0.0 or F == 0.0:           # test ah, 0x41 / jp: NaN goes on to the next test
        st['pos'] = list(KEYS[0])
    elif not F < ONE:
        st['pos'] = list(KEYS[1])
    else:
        fr = F - k
        w0 = 1.0 - fr
        st['pos'] = [f32(w0 * KEYS[k][j] + fr * KEYS[k + 1][j]) for j in range(3)]
    a = clamp01(f32((t - t0) * f32(inv)))
    st['alpha'] = f32(a * ALPHA_MAX)
    late = not (f32(f32(END - TAIL) + SLIDE) > t)      # t >= 6.25
    st['rtt'] = st['phase'] is None or late != st['phase']
    if st['rtt']:
        st['phase'] = late
    return st


def mesh_d_constants(t):
    """Vertex constants 4..8 that logo_draw hands over with mesh D
    (0x8005BF6D-0x8005C16C)."""
    t6 = END - TAIL
    t0 = (t6 - FADE) - HALF                 # 4.7
    r = ONE / (t6 - t0)
    s = (t - t0) * r
    l14 = (s + s) - ONE
    l10 = f32(clamp01(-l14))
    lc = f32(clamp01((1.0 - l10) - clamp01(l14)))
    if f32(l14) < 0.0:                # test ah, 5 / jp at 0x8005C0A5: NaN takes the second branch
        rows = [[COL_A, 1.0, COL_B, 1.0], [COL_A, 1.0, COL_B, 1.0],
                [COL_A, 1.0, COL_C, 1.0], [COL_A, 1.0, COL_C, 1.0], [l10, l10, l10, 1.0]]
    else:
        rows = [[COL_A, 1.0, COL_C, 1.0], [COL_A, 1.0, COL_C, 1.0],
                COL_D + [1.0], COL_E + [1.0], [lc, lc, lc, 1.0]]
    return rows


def transpose(m):
    return [m[(i % 4) * 4 + i // 4] for i in range(16)]


def mesh_d_matrix(frame, view, proj):
    """Constants 0..3: transpose(frame * (view * proj)); camera_view_proj
    0x8005B06A forms view * proj, mat4_mul 0x800566CF."""
    return transpose(m_mul(frame, m_mul(view, proj)))


def rot_x(a):
    """mat4_rot_x 0x800570A3 with fsincos."""
    s, co = f32(math.sin(a)), f32(math.cos(a))
    m = m_ident()
    m[5], m[6], m[9], m[10] = co, s, f32(-s), co
    return m


def mesh_e_matrix(pos, frame):
    """rot_x(pi * 0.5) * M * frame, M = identity with row 3 = pos
    (logo+0x60, translation at +0x90; 0x8005C2DF-0x8005C317). Not yet compared
    with the guest: the driver turns it into the matrices in the method
    stream, which belongs to C8."""
    m = m_ident()
    m[12:15] = pos
    return m_mul(m_mul(rot_x(f32(PI * HALF)), m), frame)


def quad_alpha_word(alpha):
    """0x8005C1BB: cvttss2si(alpha) << 24."""
    return (int(alpha) << 24) & 0xFFFFFFFF
