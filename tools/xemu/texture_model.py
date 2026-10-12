"""Model of the textures the animation makes on the CPU, after
docs/decomp/Textures.md.

Every generator returns the texels in the order the GPU reads them, i.e.
after the swizzled copy of 0x800AAB46 (swizzle()). x87 arithmetic runs with
53-bit mantissa (Timing.md 4.2): Python floats are doubles, and f32() marks
every store to a float32.
"""

import math
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from chamber_model import c, f32, rom_f32, sqrt_sse, v_norm  # noqa: E402


def trunc_ss(x):
    """cvttss2si on a float32 value."""
    x = f32(x)
    if math.isnan(x) or abs(x) >= 2.0 ** 31:
        return -0x80000000
    return int(x)


def swizzle_masks(w, h):
    """Bit masks of x and y in the swizzled offset: from bit 0 upward, one
    bit of x, then one of y, while each dimension still has bits."""
    mx = my = 0
    bit = mbit = 1
    while True:
        done = True
        if bit < w:
            mx |= mbit
            mbit <<= 1
            done = False
        if bit < h:
            my |= mbit
            mbit <<= 1
            done = False
        bit <<= 1
        if done:
            return mx, my


def swizzle(lin, w, h, bpp):
    """0x800AAB46 on a w x h image: texel (x, y) goes to the offset whose
    bits under mask x are the bits of x, under mask y those of y. Images
    narrower than 3 or lower than 2 are copied unchanged."""
    lin = bytes(lin)
    if w < 3 or h < 2:
        return lin
    mx, my = swizzle_masks(w, h)
    out = bytearray(len(lin))
    oy = 0
    for y in range(h):
        ox = 0
        row = lin[y * w * bpp:(y + 1) * w * bpp]
        for x in range(w):
            o = (ox | oy) * bpp
            out[o:o + bpp] = row[x * bpp:(x + 1) * bpp]
            ox = (ox - mx) & mx
        oy = (oy - my) & my
    return bytes(out)


def u32s(vals):
    return struct.pack(f'<{len(vals)}I', *vals)


# ---------------------------------------------------------------- 0x8006036C

K_ARGB = rom_f32(0x800129C0)


def normal_to_argb(v):
    """0x8006036C: each component trunc(f32((v + 1) * K)), alpha 0xFF."""
    r, g, b = (trunc_ss((x + 1.0) * K_ARGB) for x in v)
    return 0xFF000000 | (r << 16) | (g << 8) | b


# ---------------------------------------------------------------- 0x800610DD

def normal_cube(n):
    """Six faces of n x n, face order 0..5; returns list of face bytes."""
    d = f32(float(n - 1))
    faces = []
    for face in range(6):
        px = []
        for v in range(n):
            fy = f32((v / d) * 2.0 - 1.0)
            for u in range(n):
                fx = f32((u / d) * 2.0 - 1.0)
                vec = {0: (1.0, -fy, -fx), 1: (-1.0, -fy, fx), 2: (fx, 1.0, fy),
                       3: (fx, -1.0, -fy), 4: (fx, -fy, 1.0), 5: (-fx, -fy, -1.0)}[face]
                px.append(normal_to_argb(v_norm([f32(x) for x in vec])))
        faces.append(swizzle(u32s(px), n, n, 4))
    return faces


# ---------------------------------------------------------------- 0x800607A1

K_BYTE = rom_f32(0x800129CC)
C255 = rom_f32(0x80012908)


def gradient(w, h, c0, c1):
    """Rows from colour c0 (top) to c1 (bottom), each row one colour."""
    a = [f32(((c0 >> s) & 0xFF) * K_BYTE) for s in (24, 16, 8, 0)]
    b = [f32(((c1 >> s) & 0xFF) * K_BYTE) for s in (24, 16, 8, 0)]
    inv = f32(1.0 / float(h - 1))
    px = []
    for y in range(h):
        s = y * inv
        r = 1.0 - s
        ch = [trunc_ss((r * a[k] + s * b[k]) * C255) for k in range(4)]
        col = (ch[0] << 24) | (ch[1] << 16) | (ch[2] << 8) | ch[3]
        px += [col & 0xFFFFFFFF] * w
    return swizzle(u32s(px), w, h, 4)


# ---------------------------------------------------------------- 0x800603D8

K_ANGLE = rom_f32(0x800129C8)
K_INDEX = rom_f32(0x800129C4)


def radial_table(power, a, b):
    tab = []
    for i in range(256):
        ang = f32(i * K_ANGLE)
        cs = f32(math.cos(ang))
        for _ in range(power):
            cs = f32(cs * cs)
        tab.append(trunc_ss(((256 - i) * K_ANGLE * a + cs * b) * C255) & 0xFF)
    return tab


def radial(n, power, alpha, a, b, fill=0):
    """Brightness from a table over the distance to the centre. Row y gets
    only its first y texels (the inner loop counts with the row number,
    0x800604EB-0x80060591); the rest of the buffer keeps what was in the
    memory (fill)."""
    tab = radial_table(power, f32(a), f32(b))
    inv = f32(1.0 / float(n // 2))
    hc = int((n - 1) / 2)
    px = [fill] * (n * n)
    for y in range(n):
        dy = y - hc
        for k in range(y):
            dx = k - hc
            r = sqrt_sse(f32(float(dx * dx + dy * dy))) * inv
            if not (r < 1.0):
                px[y * n + k] = 0
                continue
            lum = tab[trunc_ss(r * K_INDEX)]
            al = lum if alpha else 0xFF
            px[y * n + k] = (al << 24) | (lum << 16) | (lum << 8) | lum
    return swizzle(u32s(px), n, n, 4)


# ---------------------------------------------------------------- 0x800605D2

def rcl32(v, n, cf):
    """rcl r32, n: rotate the 33 bits CF:v left by n; returns (v, CF)."""
    x = ((cf & 1) << 32) | (v & 0xFFFFFFFF)
    for _ in range(n):
        x = ((x << 1) | (x >> 32)) & 0x1FFFFFFFF
    return x & 0xFFFFFFFF, x >> 32


def falloff_mips(w, h, noise, seed):
    """Every mip level: v = max(0, 2^24 - d^2) with d the distance to the
    level centre scaled to 2^12 over the width; minus noise; byte
    b = bits 16-23 of v (0xFF in the centre); texel byte
    ((b^4 >> 16)^2 >> 24) in all four channels. Returns the levels."""
    def bits(x):
        n = 0
        while x:
            n += 1
            x >>= 1
        return n
    levels = bits(w >> 1) if bits(w >> 1) > bits(h >> 1) else bits(h >> 1)
    s = seed & 0xFFFFFFFF
    out = []
    for lv in range(levels):
        W, H = w >> lv, h >> lv
        q = 0x1000 // W
        sh = 1
        while q != 1:
            sh += 1
            q >>= 1
        cx, cy = int((W - 1) / 2), int((H - 1) / 2)
        px = []
        for y in range(H):
            for x in range(W):
                ax = ((x - cx) << sh) & 0xFFFFFFFF
                by = ((y - cy) << sh) & 0xFFFFFFFF
                d = (((ax * ax) & 0xFFFFFFFF) + ((by * by) & 0xFFFFFFFF)) & 0xFFFFFFFF
                v = 0x1000000 - d if d <= 0x1000000 else 0
                hi = (v * noise) >> 32
                old = s
                s, _ = rcl32(s, 13, 1 if hi else 0)
                s = (s - ((old - 11) & 0xFFFFFFFF)) & 0xFFFFFFFF
                n = (((hi * s) >> 32) << 15) & 0xFFFFFFFF
                # sub ebx, edx; jge (0x80060713): a signed comparison of v and n
                sv = v - (1 << 32) if v & 0x80000000 else v
                sn = n - (1 << 32) if n & 0x80000000 else n
                e = (v - n) & 0xFFFFFFFF if sv >= sn else 0
                e &= 0x1FF0000
                b = 0xFF if e & 0x1000000 else (e >> 16) & 0xFF
                a = ((b * b) * (b * b)) & 0xFFFFFFFF
                a = (((a >> 16) * (a >> 16)) & 0xFFFFFFFF) >> 16
                byte = (a >> 8) & 0xFF
                px.append(byte * 0x01010101)
        out.append(swizzle(u32s(px), W, H, 4))
    return out


# ---------------------------------------------------------------- 0x800609D1

class Rand:
    """Generator step 0x8005A645 / rand_below 0x8005A620 on a local state."""

    def __init__(self, s):
        self.s = s & 0xFFFFFFFF

    def step(self):
        s = self.s
        r = ((s >> 13) | (s << 19)) & 0xFFFFFFFF
        self.s = (r - ((s - 11) & 0xFFFFFFFF)) & 0xFFFFFFFF
        return self.s

    def below(self, n):
        return (self.step() * (n & 0xFFFFFFFF)) >> 32


def _adjust(rnd, v, amp, max_v, thr):
    """One random step of 0x800609D1: v + below(amp) up to max_v, or
    v - below(amp), 0 when it wraps."""
    if rnd.below(100) > thr:
        v = (v + rnd.below(amp)) & 0xFFFFFFFF
        if v > max_v:
            v = max_v
    else:
        v = (v - rnd.below(amp)) & 0xFFFFFFFF
        if v > 0xFF:
            v = 0
    return v


def fractal_walk(n):
    """The visiting order of 0x800609D1 and 0x80060DDC: yields (kind, x, y,
    half, level) with kind 0 for the centre of a square, 1 for an edge."""
    half, step, level = n >> 1, n, 0
    x = y = half
    centre, second = True, False
    while half > 0:
        yield (0 if centre else 1, x, y, half, level)
        x += step
        if centre:
            if x >= n:
                y += step
                x = half
                if y >= n:
                    y = 0
                    centre = False
        elif x >= n:
            y += step
            if y < n:
                x = 0 if second else half
            else:
                if second:
                    step = half
                    half >>= 1
                    level += 1
                    x = half
                    centre = True
                else:
                    x = 0
                y = half
                second = not second


def neighbours(kind, x, y, half, n):
    xp, yp, xm, ym = x + half, y + half, x - half, y - half
    if xm < 0:
        xm += n
    if xp >= n:
        xp -= n
    if ym < 0:
        ym += n
    if yp >= n:
        yp -= n
    if kind == 0:
        return (ym * n + xp, ym * n + xm, yp * n + xp, yp * n + xm)
    return (ym * n + x, yp * n + x, y * n + xp, y * n + xm)


K_TENTH = rom_f32(0x80012888)


def fractal(n, normal, scale, amp, seed, mask, start, use_start, max_v, thr, after=None):
    """0x800609D1. after: the n words that follow the buffer in memory; the
    last row of the normal map reads them as its lower neighbours."""
    from chamber_model import v_cross
    rnd = Rand(seed)
    P = [0] * (n * n)
    if not use_start:
        start = rnd.below(max_v)
    P[0] = (start * 0x010101) & 0xFFFFFFFF
    a = amp >> 1
    last = 0
    for kind, x, y, half, level in fractal_walk(n):
        while last < level:
            a >>= 1
            last += 1
        v = sum(P[i] & 0xFF for i in neighbours(kind, x, y, half, n)) >> 2
        v = _adjust(rnd, v, a, max_v, thr)
        P[y * n + x] = (v * 0x010101) & 0xFFFFFFFF
    P = [((d & mask) | (d << 24)) & 0xFFFFFFFF for d in P]
    if normal:
        after = after or [0] * n
        H = [(d >> 16) & 0xFF for d in P] + [(d >> 16) & 0xFF for d in after]
        S = f32(scale)
        out = list(P)
        for r in range(n):
            R = float(r)
            R1 = f32(R + K_TENTH)
            for x in range(n):
                i = r * n + x
                hc = H[i]
                hr = H[min(i + 1, n * n)]
                hb = H[min((r + 1) * n, n * n) + x]
                A = hc * S
                B = f32(hr * S)
                C = f32(hb * S)
                X = float(x)
                v1 = [f32((X + K_TENTH) - X), 0.0, f32(B - A)]
                v2 = [0.0, f32(R1 - R), f32(C - f32(A))]
                out[i] = normal_to_argb(v_norm(v_cross(v1, v2)))
        P = out
    return swizzle(u32s(P), n, n, 4)


# ---------------------------------------------------------------- 0x80060DDC

def fractal_multi(layers, n, amp, seed, start, max_v):
    """0x80060DDC: like 0x800609D1 for `layers` byte maps at once, each point
    of each layer with its own random number, layer after layer; the step
    is avg + below(2a) - a, clamped to [0, max_v]. Only layer 0 gets the
    start value. The final copy takes the source address of layer 0 for
    every texture (0x800610C0: push [ebp-0xC]), so all textures are
    layer 0. Returns (textures, layers)."""
    rnd = Rand(seed)
    L = [[0] * (n * n) for _ in range(layers)]
    L[0][0] = start & 0xFF
    a = amp >> 1
    last = 0
    for kind, x, y, half, level in fractal_walk(n):
        while last < level:
            a >>= 1
            last += 1
        idx = neighbours(kind, x, y, half, n)
        for k in range(layers):
            avg = sum(L[k][i] for i in idx) >> 2
            r = rnd.below(2 * a)
            v = avg + (r - a)
            v = max(0, min(max_v, v))
            L[k][y * n + x] = v
    tex = [swizzle(bytes(L[0]), n, n, 1) for _ in range(layers)]
    return tex, L
