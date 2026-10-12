"""Model of the glowing blob, written from docs/decomp/Blob.md.

The blob is the sub-object at anim_object + 0xB5C = 0x8006FBAC. This module
rebuilds its meshes (cube_sphere_build), its bulges and drops from the second
generator (blob_restart, blob_update) and what blob_draw hands the GPU.
Arithmetic follows the x87 code with 53-bit precision: Python floats for
values that stay in a register, f32() where the code stores a float32.
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from chamber_model import (c, f32, sin_sse, sqrt_sse, strip_build, strip_count,  # noqa: E402
                           v_norm, m_ident, m_mul, rom_f32)

PI = c(0x40490FDB)          # 0x80066574
T_START = c(0x3F19999A)     # 0x80066588, 0.6
RADIUS = c(0x40133333)      # blob+0x6C, set by blob_construct, 2.3
COLOUR = [0.25, 1.0, c(0x3E19999A), 1.0]   # blob+0x44, blob_construct
SEED = 0x76543210

# fade(t) = max((t - 0.6) * k, 0), k = 1 / ((8 - 2 - 0.8) - (0.6 + 0.5)) with the 1 of
# 0x8001288C (fdivr, 0x80059170 and 0x800593B3); 0x8006657C ... 0x80066588
FADE_K = 1.0 / (((c(0x41000000) - c(0x40000000)) - c(0x3F4CCCCD)) - (T_START + 0.5))


def fade(t):
    f = (t - T_START) * FADE_K
    return f32(f if 0.0 <= f else 0.0)


# ---------------------------------------------------------------- generator

class Rand:
    """0x8005A645 on 0x800708BC; 0x800582E7 and 0x80058308 use the low 16 bits."""

    def __init__(self, s=SEED):
        self.s = s
        self.n = 0

    def step(self):
        s = self.s
        ror = ((s >> 13) | (s << 19)) & 0xFFFFFFFF
        self.s = (ror - ((s - 11) & 0xFFFFFFFF)) & 0xFFFFFFFF
        self.n += 1
        return self.s & 0xFFFF

    def unit(self):          # [0, 1), exact in the register
        return self.step() * c(0x37800000)

    def signed(self):        # [-1, 1)
        return self.step() * c(0x38000000) - 1.0


# ---------------------------------------------------------------- vector helpers

def len_sq(v):
    """0x800568E5"""
    return (v[0] * v[0] + v[1] * v[1]) + v[2] * v[2]


def dot(a, b):
    """0x8005814E"""
    return (a[2] * b[2] + a[1] * b[1]) + a[0] * b[0]


def len_approx(v):
    """0x8005820E: (L + 0.34375 M + 0.25 S) * 1.0433885 over the sorted |components|"""
    s, m, big = sorted(abs(x) for x in v)
    return ((s * 0.25 + m * rom_f32(0x800128F4)) + big) * rom_f32(0x800128F0)


def norm_approx(v):
    """0x80058290: unchanged if the approximate length is below 1e-6"""
    n = len_approx(v)
    if n < rom_f32(0x800128F8):
        return list(v)
    k = f32(1.0 / n)
    return [f32(x * k) for x in v]


def madd(a, b, s):
    """0x800581E0"""
    return [f32(a[i] + b[i] * s) for i in range(3)]


# ---------------------------------------------------------------- meshes

def cube_sphere(n):
    """0x80058DF2: unit vectors on six cube faces, (m+1)^2 each, m = n / 2,
    and strip indices for all six faces with joins."""
    m = max(n // 2, 1)
    step = f32(2.0 / m)
    verts = []
    for face in range(6):
        for j in range(m + 1):
            for i in range(m + 1):
                a = 1.0 if i == m else f32(i * step - 1.0)
                b = 1.0 if j == m else f32(j * step - 1.0)
                v = {0: (-1.0, -a, b), 1: (b, -1.0, -a), 2: (-a, b, -1.0),
                     3: (1.0, a, b), 4: (b, 1.0, a), 5: (a, b, 1.0)}[face]
                verts.append(v_norm([f32(x) for x in v]))
    idx = []
    per_face = (m + 1) * (m + 1)
    for face in range(6):
        idx += strip_build(m, m, int(face > 0), int(face < 5), face * per_face)
    assert len(idx) == strip_count(m, m) * 6 + 10
    return verts, idx


# ---------------------------------------------------------------- bulges and drops

class Bulge:
    """0x40 bytes at blob+0x70 + 0x40 i"""
    FIELDS = ['size', 'size2', 'inv2', 'h', 'cx', 'cy', 'cz', 'mask', 'dx', 'dy', 'dz',
              't0', 'freq', 'amp', 'drop', 'flag']

    def __init__(self):
        self.size = self.size2 = self.inv2 = self.h = 0.0
        self.c = [0.0, 0.0, 0.0]     # +0x10, the direction the mesh uses
        self.mask = 0                # +0x1C
        self.d = [0.0, 0.0, 0.0]     # +0x20
        self.t0 = self.freq = self.amp = 0.0
        self.drop = None             # +0x38, index of the drop or None
        self.flag = 0                # +0x3C


class Drop:
    """0x38 bytes at blob+0x874 + 0x38 i"""

    def __init__(self):
        self.size = 0.0              # +0x00
        self.pos = [0.0, 0.0, 0.0]   # +0x04
        self.neg = 0                 # +0x10
        self.d = [0.0, 0.0, 0.0]     # +0x14
        self.t0 = self.freq = self.amp = self.disp = 0.0   # +0x20 ... +0x2C
        self.scale = 1.0             # +0x30, blob_construct
        self.rate = 0.0              # +0x34


def face_mask(b):
    """0x80058FFC: bit f set if the bulge can reach cube face f"""
    lo, hi = c(0xBF13CD36), c(0x3F13CD36)
    m = 0
    for k in range(3):
        if b.d[k] - b.size < lo:
            m |= 1 << k
        if b.size + b.d[k] > hi:
            m |= 8 << k
    b.mask = m


class Blob:
    def __init__(self):
        self.rand = Rand()                       # blob_construct
        self.center = [0.0, 0.0, 0.0]            # +0x54
        self.radius = RADIUS
        self.bulges = [Bulge() for _ in range(32)]
        self.drops = [Drop() for _ in range(8)]
        self.n_bulges = self.n_drops = 0
        self.unit, self.idx = cube_sphere(32)    # +0x0C/+0x1C, +0x18
        self.small, self.small_idx = cube_sphere(8)
        self.per_face = len(self.unit) // 6      # +0x30
        self.mesh = None                         # last written vertex buffer
        self.reset_mesh()

    # -- 0x80058B2C
    def reset_mesh(self):
        self.mesh = [list(v) + [1.0] for v in self.unit]

    # -- 0x80059868
    def restart(self):
        self.n_drops = self.n_bulges = 0
        while self.n_bulges < 32:
            slot = self.n_drops if self.n_drops < 8 else None
            j = self.n_bulges
            self.n_bulges += 1
            if self.spawn(self.bulges[j], c(0xBE99999A), slot):
                self.n_drops += 1
        self.reset_mesh()

    # -- 0x80059320
    def spawn(self, b, t, slot):
        r = self.rand
        if t < 0.0:
            b.drop = None
        b.d = [f32(r.signed()), f32(r.signed()), f32(r.signed())]
        if len_sq(b.d) < c(0x3A83126F):
            b.d = [f32(r.signed()), f32(r.signed()), 1.0]
        b.c = [0.0, 0.0, 0.0]
        b.d = norm_approx(b.d)
        fd = fade(t)
        q = r.unit()
        s = (q + 1.0) * c(0x3ECCCCCD)
        b.size, b.size2, b.inv2, b.h = f32(s), f32(s * s), f32(1.0 / (s * s)), 0.0
        face_mask(b)
        b.t0 = f32(r.unit() * c(0x3ECCCCCD) + t)
        b.amp = f32((((1.0 - q) * 0.5 + c(0x3E4CCCCD)) * (fd + 1.0)) * 0.5)
        if b.drop is None:
            b.drop = slot
            if slot is None:
                x = (r.unit() + b.amp) * c(0x3E99999A)
                b.freq = f32((fd * c(0x3E4CCCCD) + c(0x3F4CCCCD)) / x * PI)
                if t < -1.0:
                    b.t0 = f32(-(r.unit() * (PI / b.freq)))
                return False
        dr = self.drops[b.drop]
        dr.size = f32((r.unit() + 1.0) * b.size * self.radius * 0.25)
        dr.d = list(b.d)
        q2 = r.signed()
        dr.amp = f32((q2 + q2 + 5.0) * self.radius)
        dr.amp = f32(dr.amp * c(0x3F19999A))
        dr.t0 = f32(r.unit() * c(0xBE99999A)) if t < -1.0 else t
        p = (r.unit() * c(0x3E99999A) + c(0x3F4CCCCD)) * c(0x3FD55555)
        dr.freq = f32(PI / p * 2.0)
        dr.scale, dr.rate = c(0x3F99999A), 0.0
        b.flag = int(p * c(0x3ECCCCCD) > t - b.t0)
        self.drop_update(dr, t, 0.0)
        self.step(b, t, 0.0, None)
        return True

    # -- 0x800590A6
    def drop_update(self, dr, t, dt):
        v = dt * dr.rate + dr.scale
        if v < 0.5:
            v = 0.5
        elif v > 2.0:
            v = 2.0
        dr.scale = f32(v)
        if dr.rate > 0.0:
            move = v < c(0x3F733333) or v > 1.0
        else:
            move = v < 1.0 or v > c(0x3F866666)
        if move:
            dr.rate = f32(dr.rate - (v - 1.0) * 1000.0 * dt)
        fd = fade(t)
        arg = f32((t - dr.t0) * dr.freq * (t * c(0x3DCCCCCD) + 1.0) * c(0x3FB33333))
        s = sin_sse(arg)
        u = f32(1.0 - abs(s))
        w = 1.0 - sqrt_sse(u) * u
        if not s > 0.0:
            w = -w
        e = w * dr.amp * fd
        dr.disp = f32(e)
        dr.neg = int(e < 0.0)
        dr.pos = madd(self.center, dr.d, dr.disp)
        return abs(dr.disp) + dr.size < self.radius * 0.5

    # -- 0x800595B6
    def step(self, b, t, dt, slot):
        if b.drop is not None:
            dr = self.drops[b.drop]
            q = (abs(dr.disp) + dr.size) / self.radius
            h = q - 1.0
            if h > 2.0:
                h = 2.0
            elif h < 0.0:
                h = 0.0
            b.h = f32(h)
            if b.flag:
                if not h > c(0x3F4CCCCD):
                    neg = int(dot(b.d, dr.d) < 0.0)
                    if neg == dr.neg:
                        return False
                    b.d = [f32(-x) for x in b.d]
                    b.c = list(b.d)
                    face_mask(b)
                    return False
                b.amp = f32(h)
                b.flag = 0
                p = c(0x3E99999A) * h
                b.freq = f32(PI / p * 2.0)
                b.t0 = f32(t - p * 0.25)
                h2 = h - 0.5
                if not c(0x3F4CCCCD) < h2 and h2 < c(0x3F19999A):
                    h2 = c(0x3F19999A)
                elif c(0x3F4CCCCD) < h2:
                    h2 = c(0x3F4CCCCD)
                dr.scale = f32(h2)
                dr.rate = 0.0
            if not b.flag and q < c(0x3F666666):
                b.flag = 1
        ph = (t - b.t0) * b.freq
        if ph > PI:
            if b.drop is None:
                return self.spawn(b, t, slot)
            b.h = 0.0
            return False
        if ph < 0.0:
            return False
        b.h = f32(sin_sse(f32(ph)) * b.amp)
        b.c = list(b.d)
        face_mask(b)
        return False

    # -- 0x80059789
    def update(self, t, dt):
        if not t < T_START:
            for i in range(self.n_drops):
                self.drop_update(self.drops[i], t, dt)
            for j in range(self.n_bulges):
                slot = self.n_drops if self.n_drops < 8 else None
                if self.step(self.bulges[j], t, dt, slot):
                    self.n_drops += 1
            self.deform()
        elif t - dt > T_START:
            self.reset_mesh()

    # -- 0x80058B93
    def deform(self):
        out = []
        for face in range(6):
            near = [b for b in self.bulges[:self.n_bulges] if b.mask & (1 << face)]
            for v in range(face * self.per_face, (face + 1) * self.per_face):
                p = self.unit[v]
                o = [p[0], p[1], p[2], 0.0]
                for b in reversed(near):
                    d = [f32(p[i] - b.c[i]) for i in range(3)]
                    dd = (d[2] * d[2] + d[1] * d[1]) + d[0] * d[0]
                    if not dd < b.size2:
                        continue
                    g = dd * b.inv2 - 1.0
                    w = f32(self.radius * b.h * g * g)
                    k = f32(b.h * b.inv2 * g * -4.0)
                    n = norm_approx(madd(p, d, k))
                    o = [f32(n[0] + o[0]), f32(n[1] + o[1]), f32(n[2] + o[2]), f32(w + o[3])]
                out.append(o)
        self.mesh = out

    # -- 0x80058D1F
    def weighted_center(self, ref, x20):
        """Centre and drops weighted with 1 / |ref - p|^2; second output
        x20 * (4 w0 + sum wi) / sum w, which no caller reads."""
        def w(p):
            dx, dy, dz = ref[0] - p[0], ref[1] - p[1], ref[2] - p[2]
            return 1.0 / ((dz * dz + dy * dy) + dx * dx)
        w0 = w(self.center)
        tot = f32(w0)
        second = f32(x20 * w0 * 4.0)
        acc = madd([0.0, 0.0, 0.0], self.center, f32(w0))
        for dr in self.drops[:self.n_drops]:
            wi = w(dr.pos)
            second = f32(x20 * wi + second)
            acc = madd(acc, dr.pos, f32(wi))
            tot = f32(f32(wi) + tot)
        inv = 1.0 / tot
        return [f32(x * inv) for x in acc], f32(inv * second)

    # ---------------------------------------------------------------- state as bytes

    def bulge_bytes(self, b, base):
        drop = 0 if b.drop is None else base + 0x874 + 0x38 * b.drop
        return struct.pack('<4f3fI3f3fIB3x', b.size, b.size2, b.inv2, b.h, *b.c, b.mask, *b.d,
                           b.t0, b.freq, b.amp, drop, b.flag)

    def drop_bytes(self, dr):
        return struct.pack('<f3fB3x3f6f', dr.size, *dr.pos, dr.neg, *dr.d, dr.t0, dr.freq,
                           dr.amp, dr.disp, dr.scale, dr.rate)


# ---------------------------------------------------------------- blob_draw

def draw(blob, t, x1c, x20, cam):
    """What 0x800583BF hands the GPU. cam: dict with 'eye' (camera+0x58),
    'inv' (camera+0xBC, 16 floats), 'view' (+0xFC), 'proj' (+0x13C)."""
    R = blob.radius
    k = f32((sqrt_sse(f32(x20 - x1c)) * c(0x3FA66666) + 1.0) * R)
    a = x20 if x20 < 1.0 else 1.0
    colour = (int(f32(a * c(0x437F0000))) << 24 | 0xA0FF40) & 0xFFFFFFFF
    kq = f32(k * c(0x40A66666))
    ax, ay = cam['inv'][0:3], cam['inv'][4:7]
    sub = lambda p, q: [f32(p[i] - q[i]) for i in range(3)]   # noqa: E731
    sc = lambda v: [f32(x * kq) for x in v]                    # noqa: E731
    quad = [sc(sub(ay, ax)) + [0.0, 1.0],
            sc([f32(ay[i] + ax[i]) for i in range(3)]) + [1.0, 1.0],
            sc(sub(ax, ay)) + [1.0, 0.0],
            sc([f32(-ax[i] - ay[i]) for i in range(3)]) + [0.0, 0.0]]
    vp = m_mul(cam['view'], cam['proj'])
    vp_t = [vp[(i % 4) * 4 + i // 4] for i in range(16)]
    ks = [k * s for s in (1.0, 1.0, 1.0)]                     # blob+0x60, all 1
    m = m_ident()
    m[0], m[5], m[10] = f32(ks[0]), f32(ks[1]), f32(ks[2])
    m[12:15] = blob.center
    m_t = [m[(i % 4) * 4 + i // 4] for i in range(16)]
    vs1 = {4: vp_t, 8: [0.0, 1.0, 2.0, 0.5] + list(cam['eye']) + [None]
           + [f32(x) for x in ks] + [1.0] + [f32(1.0 / x) for x in ks] + [None]
           + list(blob.center) + [0.0], 0: m_t}
    br = ((x20 - x1c) * c(0x3F4CCCCD) + x1c * c(0x3F99999A)) * 4.0 + c(0x3E99999A)
    ramp = t * 4.0
    if ramp > 1.0:
        ramp = 1.0
    f = f32(ramp * br)
    ps1 = [f32(COLOUR[i] * f) for i in range(3)] + [1.0, 0.0, 0.0, 0.0, 1.0]
    vs2 = {4: vp_t, 8: [0.0, 1.0, 2.0, 0.5] + list(cam['eye']) + [1.0]}
    g = f32(x20 * c(0x3E99999A))
    ps2 = ([f32(COLOUR[i] * g) for i in range(3)] + [1.0]
           + [f32(COLOUR[i] * c(0x3E4CCCCD)) for i in range(3)] + [1.0] + [2.0] * 4)
    drops = []
    for dr in blob.drops[:blob.n_drops]:
        sz = dr.size / sqrt_sse(dr.scale)
        z = f32(dr.scale * dr.size - sz)
        drops.append(list(dr.pos) + [0.0] + list(dr.d) + [1.0] + [f32(sz)] * 3 + [1.0]
                     + [f32(z * x) for x in dr.d] + [1.0])
    return {'k': k, 'colour': colour, 'quad': quad, 'vs1': vs1, 'ps1': ps1,
            'vs2': vs2, 'ps2': ps2, 'drops': drops}
