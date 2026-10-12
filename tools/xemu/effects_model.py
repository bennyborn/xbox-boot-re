"""Model of the veil and glow object (anim_object + 0x17EC), after
docs/decomp/Effects.md.

Inputs per frame: t, x20 (anim_object + 0x20), D = anim_object + 0x6A8
(distance of the eye from the blob centre), the eye's angle about z
+ 0x698 and its elevation + 0x6A0 (written by anim_update at 0x80057558
and 0x80057461, the latter from asin_approx; Camera.md 6), the aspect factor
camera + 0x4C, and the product of view and
projection (camera_view_proj, 0x8005B06A). x87 with 53-bit mantissa;
f32() marks a store.
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from chamber_model import f32, rom_f32  # noqa: E402

T_END, T_LOGO, T_FADE = rom_f32(0x8006673C), rom_f32(0x80066740), rom_f32(0x80066744)  # 8, 2, 0.8
T_GLOW = rom_f32(0x80066748)          # 0.6
K_DIST = rom_f32(0x8006674C)          # 0.005
DEPTH_HALF = rom_f32(0x80066750)      # 40
HALF = rom_f32(0x80010A18)            # 0.5


def bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def t_flash():
    """((8 - 2) - 0.8) - 0.5 in x87, the start of the veil's colour ramp
    minus 0.3."""
    return ((T_END - T_LOGO) - T_FADE) - HALF


def clamp01(x):
    if not (x <= 1.0):          # 1 < x or NaN take the first branch
        return 1.0 if x > 1.0 else x
    return 0.0 if x < 0.0 else x


def project_origin(vp):
    """vec4_mul_mat4((0, 0, 0, 1), vp) and vec3_scale by f32(1 / w)."""
    w = vp[15]
    k = f32(1.0 / w)
    return f32(k * vp[12]), f32(k * vp[13])


# ---------------------------------------------------------------- 0x8005A194

def offscreen_constants(D, aspect, vp):
    """Constants 16..19 (c112..c115) for the quad, the matrix c96..c99, and
    constants 16, 17 for the depth pass."""
    kx = f32(D * K_DIST)
    ky = f32((aspect * D) * K_DIST)
    px, py = project_origin(vp)
    quad = [1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 1.0, kx, ky, 0.0, 1.0,
            f32(HALF - px * kx), f32(HALF - py * ky), HALF, 0.0]
    mat = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0]
    inv = f32(1.0 / f32(DEPTH_HALF + DEPTH_HALF))
    off = f32(DEPTH_HALF - D)
    depth = [inv, inv, inv, 1.0, off, off, off, 1.0]
    return quad, mat, depth


# ---------------------------------------------------------------- 0x80059BCD

def veil_colours(t, x20):
    """The two combiner constants of 0x800410B0: c0 green from x20, c1 the
    colour ramp from t = 5.0 to 5.25."""
    g = x20 * rom_f32(0x80012958) - rom_f32(0x80012888)        # 0.7, 0.1
    if not (0.0 < g):            # fcomp 0 with st: zero unless g > 0
        g = 0.0
    c0 = [f32(rom_f32(0x80012880) * g), f32(g), f32(rom_f32(0x80012880) * g), 1.0]
    f = (t - (t_flash() + rom_f32(0x800128FC))) * rom_f32(0x80012900)  # 0.3, 4
    f = clamp01(f) * rom_f32(0x80012898)                                # 0.75
    c1 = [f32(rom_f32(0x80012954) * f), f32(f), f32(f * rom_f32(0x800128AC)), 0.0]
    return c0, c1


def pack_colour(c):
    """0x800410B0: clamp each component to [0, 1], trunc(f32(x * 255 + 0.5)),
    A << 24 | R << 16 | G << 8 | B."""
    def byte(x):
        x = 1.0 if x > 1.0 else (0.0 if x < 0.0 else x)
        return int(f32(x * 255.0 + 0.5)) & 0xFF
    r, g, b, a = c
    return (byte(a) << 24) | (byte(r) << 16) | (byte(g) << 8) | byte(b)


def veil_texcoords(D, aspect, vp, az, el):
    """Constants c96..c101: for the noise layers i = 2, 1, 0 an offset
    (a_i, b_i, 0, 0) and a scale (-Y, -X, 1, 1)."""
    X = (D * K_DIST) * HALF
    Y = (aspect * D) * K_DIST
    px, py = project_origin(vp)
    c6c = f32(py * Y * rom_f32(0x80012950))       # 1.875
    c70 = f32(px * X * rom_f32(0x8001294C))       # 2.5
    mY, mX = f32(-Y), f32(-X)
    out = []
    for i in (2, 1, 0):
        q = (i * rom_f32(0x80012948) - rom_f32(0x800128B0)) * rom_f32(0x80012930)
        a = f32(c6c - el * q)
        b = f32(az * q - c70)
        out += [a, b, 0.0, 0.0, mY, mX, 1.0, 1.0]
    return out


def glow(t, D):
    """The quad over the veil: alpha byte, factor colour and the scale s of
    the world matrix diag(1/s, 1/s, 0) (row 3 = (0, 0, 1, 1))."""
    g = clamp01((t - t_flash()) * rom_f32(0x80012944))          # 3.3333
    if t < T_GLOW:
        h = T_GLOW * rom_f32(0x800128B0)                        # 0.6 * 0.2
        g = t / h if t < h else 1.0 - (t - h) / T_GLOW
        g = f32(g)
    else:
        g = f32(g)
    n = int(g * rom_f32(0x80012908))                           # 255, truncated
    n = 0xFF if n > 0xFF else (0 if n < 0 else n)
    n *= 2
    a = 0xC4 if n > 0xC4 else n
    if a == 0:
        return None
    if t < T_GLOW:
        s = (D * T_GLOW) / (t * rom_f32(0x8001293C))           # 8
    else:
        s = rom_f32(0x80012940) / g                             # 0.33
    s = f32(s)
    return {'alpha': a, 'factor': (a << 24) | 0xA0FF60, 's': s, 'inv_s': f32(1.0 / s)}
