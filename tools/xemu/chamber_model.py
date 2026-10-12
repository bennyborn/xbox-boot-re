"""Model of chamber and camera, written after docs/decomp/Chamber.md and Camera.md.

Imported by vectors_c5.py. Reads data/chamber/scene.json and
data/camera/campath.json (tools/extract/chamber.py) and the constants
named in the narratives.

Arithmetic follows the kernel: x87 operations run with 53-bit mantissa
(Timing.md 4.2), so they are done in Python floats and rounded to float32
where the kernel stores; SSE operations (sqrt_sse, sin_sse, asin_approx,
atan_sse) are done in numpy float32, op by op. rsqrtss and rcpss are
taken as exact 1/sqrt(x) and 1/x rounded to float32, which is what xemu
does (target/i386/ops_sse.h); real hardware returns a 12-bit
approximation there (see Chamber.md, "Rechengenauigkeit").
"""

import json
import math
import os
import struct

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
F = np.float32


def f32(x):
    return float(F(x))


def bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def from_bits(u):
    return struct.unpack('<f', struct.pack('<I', u & 0xFFFFFFFF))[0]


def c(u):
    """A float32 constant of the kernel, given by its bit pattern."""
    return from_bits(u)


# ---------------------------------------------------------------- SSE helpers

def rsqrt(x):
    with np.errstate(all='ignore'):
        r = F(1) / np.sqrt(F(x))
    return F(from_bits(0xFFC00000)) if np.isnan(r) else r


def rcp(x):
    return F(1) / F(x)


def sqrt_sse(x):
    """0x80057F42: x * r * (1.5 - 0.5 * x * r * r), r = rsqrtss(x), float32."""
    x = F(x)
    if bits(float(x)) == 0:
        return 0.0
    r = rsqrt(x)
    a = x * r * r * r * F(0.5)
    return float((r * F(1.5) - a) * x)


SIN_C = [c(0x3FC90FDB), c(0xBF255DE7), c(0x3DA335E3), c(0xBB996966)]   # 0x80065F50-5C


def sin_sse(x):
    """0x80057F9D: quarter-period reduction and an odd polynomial in float32."""
    with np.errstate(all='ignore'):
        xb = bits(f32(x))
        a = F(abs(f32(x))) * F(c(0x3F22F983))           # 2/pi, 0x80065EF0
        k = int(float(a)) if abs(float(a)) < 2.0 ** 31 else -2 ** 31   # cvttss2si
        sgn = ((k << 30) & 0x80000000) ^ (xb & 0x80000000)
        fr = a - F(k)
        if k & 1:
            fr = F(1) - fr                                # 0x80065E80
        f2 = fr * fr
        p = f2 * F(SIN_C[3]) + F(SIN_C[2])
        p = p * f2 + F(SIN_C[1])
        p = p * f2 + F(SIN_C[0])
        return float(p * F(from_bits(bits(float(fr)) | sgn)))


IMG = None


def image():
    global IMG
    if IMG is None:
        IMG = open(os.path.join(ROOT, 'out', 'rom', 'kernel.img'), 'rb').read()
    return IMG


def rom_f32(a):
    return struct.unpack_from('<f', image(), a - 0x80010000)[0]


def asin_approx(x):
    """0x80057E2D: series with the factors at 0x80066030 + 4*k, k = 0x30 down;
    from 0.7071 on (positive x only) through atan_sse."""
    x = f32(x)
    u = bits(x)
    if u & 0x80000000:
        if u & 0x7FFFFFFF == 0:
            return 0.0
        # the kernel compares the masked value with 0xBF800000/0xBF3504F3,
        # which never holds: negative x always take the series
    else:
        if u == 0:
            return 0.0
        if u >= 0x3F800000:
            return c(0x3FC90FDB)
        if u >= 0x3F3504F3:
            y = F(1) - F(x) * F(x)
            r = rsqrt(y)
            a = y * r * r * r * F(0.5)
            return atan_sse(float((r * F(1.5) - a) * F(x)))
    thr = F(c(0x358637BD))
    cf, s, p, x2 = F(1), F(1), F(1), F(x) * F(x)
    k = 0x30
    while True:
        cf = cf * F(rom_f32(0x80066030 + 4 * k))
        p = p * x2
        term = cf * p
        s = s + term
        k -= 1
        if not (term >= thr) or k == 0:
            break
    return float(s * F(x))


def atan_sse(x):
    """0x80057BA9: continued fraction with rcpss; |x| >= 1 through 1/x."""
    x = f32(x)
    u = bits(x)
    K = {o: F(c(v)) for o, v in {
        0x84: 0x40313B14, 0x88: 0x4011745D, 0x8C: 0x41C80000, 0x90: 0x41800000,
        0x94: 0x41300000, 0x98: 0x41100000, 0x9C: 0x40E00000, 0xA0: 0x40A00000,
        0xA4: 0x40800000, 0xA8: 0x40400000, 0xAC: 0x3F800000}.items()}

    def tail(x1, x2):
        x1 = x1 * x2 + K[0xAC]
        x2 = rcp(x1)
        x7 = x2 * x2 * x1
        return (x2 + x2) - x7

    with np.errstate(all='ignore'):
        if u >= 0x3F800000:
            x0 = rcp(x)
            x1 = x0 * x0
            x2 = rcp(x1 * K[0x84] + K[0x94])
            x2 = rcp(x2 * (x1 * K[0x8C]) + K[0x98])
            x2 = rcp(x2 * (x1 * K[0x90]) + K[0x9C])
            x2 = rcp(x2 * (x1 * K[0x98]) + K[0xA0])
            x2 = rcp(x2 * (x1 * K[0xA4]) + K[0xA8])
            r = x0 * tail(x1, x2)
            base = F(c(0xBFC90FDB)) if u >= 0xBF800000 else F(c(0x3FC90FDB))
            return float(base - r)
        x0 = F(x)
        x1 = x0 * x0
        x2 = x1 * K[0x88] + K[0x98]
        x6 = rcp(x2) * (x1 * K[0x90]) + K[0x9C]
        x2 = rcp(x6) * (x1 * K[0x98]) + K[0xA0]
        x6 = rcp(x2) * (x1 * K[0xA4]) + K[0xA8]
        x2 = rcp(x6)
        return float(x0 * tail(x1, x2))


def acos_approx(x):
    """0x80058039: pi/2 - asin_approx(x), with 0 -> pi/2 and x >= 1 -> 0."""
    x = f32(x)
    u = bits(x)
    if u & 0x7FFFFFFF == 0:
        return c(0x3FC90FDB)
    if not u & 0x80000000 and u >= 0x3F800000:
        return 0.0
    return c(0x3FC90FDB) - asin_approx(x)


# ---------------------------------------------------------------- vectors, matrices (x87)

def v_len(v):
    """0x8005690A: length squared in x87, stored as float32, then sqrt_sse."""
    return sqrt_sse(f32(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]))


def v_norm(v):
    """0x8005816D"""
    k = 1.0 / v_len(v)
    return [f32(k * v[0]), f32(k * v[1]), f32(k * v[2])]


def v_cross(a, b):
    """0x8005A662"""
    return [f32(b[2] * a[1] - a[2] * b[1]), f32(a[2] * b[0] - a[0] * b[2]),
            f32(a[0] * b[1] - b[0] * a[1])]


def v_sub(a, b):
    return [f32(a[i] - b[i]) for i in range(3)]


def v_add(a, b):
    return [f32(a[i] + b[i]) for i in range(3)]


def v_scale(a, s):
    return [f32(a[i] * s) for i in range(3)]


def v_madd(a, b, s):
    """0x800581E0: a + b * s"""
    return [f32(a[i] + b[i] * s) for i in range(3)]


def v_mul_m3(v, m):
    """0x8005667A: row vector times the 3x3 part; sums in the order of the
    code (0x80056682-0x800566C9, Overview.md, "Kleine Helfer")"""
    return [f32((m[8] * v[2] + m[4] * v[1]) + v[0] * m[0]),
            f32((m[1] * v[0] + m[9] * v[2]) + m[5] * v[1]),
            f32((m[2] * v[0] + m[10] * v[2]) + m[6] * v[1])]


def m_ident():
    return [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]


def m_diag3(x, y, z):
    m = m_ident()
    m[0], m[5], m[10] = x, y, z
    return m


# 0x800566CF adds the four products of each element in this order of k
# (read from the disassembly, one list per element 0..15)
MUL_ORDER = [[0, 2, 1, 3], [2, 0, 1, 3], [0, 2, 1, 3], [1, 3, 0, 2], [1, 2, 3, 0], [0, 1, 2, 3],
             [1, 2, 3, 0], [0, 3, 2, 1], [1, 2, 3, 0], [0, 1, 3, 2], [1, 3, 2, 0], [0, 3, 2, 1],
             [1, 2, 3, 0], [0, 1, 3, 2], [1, 3, 2, 0], [0, 3, 2, 1]]


def m_mul(a, b):
    """0x800566CF: a * b, row-major"""
    out = []
    for e in range(16):
        r, cc = divmod(e, 4)
        acc = None
        for k in MUL_ORDER[e]:
            p = a[r * 4 + k] * b[k * 4 + cc]
            acc = p if acc is None else acc + p
        out.append(f32(acc))
    return out


def m_inverse_rigid(m):
    """0x8005B22E"""
    o = [m[0], m[4], m[8], 0.0, m[1], m[5], m[9], 0.0, m[2], m[6], m[10], 0.0,
         0.0, 0.0, 0.0, 1.0]
    t = v_mul_m3([-m[12], -m[13], -m[14]], o)
    o[12:15] = t
    return o


def quat_to_m_t(q):
    """0x8005C790: rotation matrix of q = (x, y, z, w), the transposed form of 0x8005C665"""
    x, y, z, w = q
    m = [0.0] * 16
    m[0] = f32(((w * w + x * x) - y * y) - z * z)
    m[4] = f32((x * y + z * w) * 2.0)
    m[8] = f32(z * x * 2.0 - w * y * 2.0)
    m[1] = f32(x * y * 2.0 - z * w * 2.0)
    m[5] = f32((y * y + (w * w - x * x)) - z * z)
    m[9] = f32((z * y + x * w) * 2.0)
    m[2] = f32((w * y + z * x) * 2.0)
    m[6] = f32(z * y * 2.0 - x * w * 2.0)
    m[10] = f32(z * z + ((w * w - x * x) - y * y))
    m[15] = 1.0
    return m


def quat_slerp(a, b, u):
    """0x8005E0B1"""
    d = a[3] * b[3] + a[2] * b[2] + a[1] * b[1] + a[0] * b[0]
    if d < 0.0:
        b = [-x for x in b]
        d = -d
    d32 = f32(d)
    if abs(f32(d32 - 1.0)) <= c(0x3727C5AC):
        v = 1.0 - u
        return [f32(b[i] * u + v * a[i]) for i in range(4)]
    th = f32(acos_approx(d32))
    sa = sin_sse(f32((1.0 - u) * th))
    sb = sin_sse(f32(th * u))
    o = [f32(sb * b[i] + sa * a[i]) for i in range(4)]
    k = 1.0 / sin_sse(th)
    return [f32(k * x) for x in o]


# ---------------------------------------------------------------- chamber

POS_K = [c(0x3B875D57), c(0x3C073366), c(0x3B90DE09)]        # 0x80066984
POS_O = [c(0xC1DEC287), c(0xBE6A37EF), c(0x3EFE820E)]        # 0x80066990
TRK_K = [c(0x3B348D3B), c(0x3B348D3B), c(0x3B1FE868)]        # 0x8006699C
TRK_O = [c(0xBE22DCF4), c(0xBF3DDA38), c(0x4009F5BE)]        # 0x800669A8
QUAT_K = c(0x38001203)                                       # 0x800129A8


def load_scene():
    return json.load(open(os.path.join(ROOT, 'data', 'chamber', 'scene.json')))


class Chamber:
    def __init__(self, scene=None):
        s = scene or load_scene()
        self.s = s
        self.quats = []
        for (a, b, cc), pos in zip(s['quats']['i16'], s['quats']['w_positive']):
            # x and y are read back as stored; z stays in the x87 register
            x, y, zr = f32(a * QUAT_K), f32(b * QUAT_K), cc * QUAT_K
            z = f32(zr)
            w = sqrt_sse(f32(((1.0 - x * x) - y * y) - zr * zr))
            if not pos:
                w = f32(w * -1.0)
            self.quats.append([x, y, z, w])
        self.positions = [[f32(v[i] * TRK_K[i] + TRK_O[i]) for i in range(3)]
                          for v in s['positions']['i16']]
        self.rot_tracks = [t['index'] for t in s['rot_tracks']]
        self.pos_tracks = [t['index'] for t in s['pos_tracks']]
        self.groups = [[self.place(g, p) for p in g['places']] for g in s['groups']]

    def place(self, g, p):
        """0x8005D99D, 0x8005E6E6, 0x8005EA78, 0x8005EC3E, 0x8005E8B4"""
        typ = g['type']
        pos = [f32(p['pos_i16'][i] * POS_K[i] + POS_O[i]) for i in range(3)]
        if typ == 0:
            r = p['radius']
            S, sc = m_diag3(r, r, r), [r, r, r, 1.0]
            L = m_ident()
        else:
            if typ == 1:
                h2 = f32(p['half_height'] + p['half_height'])
                S, sc = m_diag3(p['radius'], p['radius'], h2), [p['radius'], p['radius'], h2, 1.0]
            elif typ == 2:
                S = m_diag3(p['size_x'], p['size_y'], p['size_z'])
                sc = [p['size_x'], p['size_y'], p['size_z'], 1.0]
            elif typ == 3:
                S, sc = m_diag3(p['scale'], p['scale'], p['scale']), [p['scale']] * 3 + [1.0]
            else:
                S, sc = m_ident(), [1.0] * 4
            L = quat_to_m_t(self.quats[p['quat']])
        L[12:15] = pos
        Linv = m_inverse_rigid(L) if typ else None
        if typ == 0:
            Linv = m_ident()
            Linv[12:15] = [-x for x in pos]
        SL = L[:] if typ in (4, 5) else m_mul(S, L)
        form = 0 if typ == 2 else p['form']
        rt, pt = p['rot_track'], p['pos_track']
        z = pos[2]
        if rt >= 0:
            z = v_mul_m3(pos, quat_to_m_t(self.quats[self.rot_tracks[rt][0]]))[2]
        if pt >= 0:
            z = f32(z + self.positions[self.pos_tracks[pt][0]][2])
        return {'form': form, 'pos_track': pt, 'rot_track': rt, 'L': L, 'Linv': Linv,
                'S': S, 'scale': sc, 'SL': SL, 'side': int(z > 0.0)}

    @staticmethod
    def track_time(t):
        """0x8005F9F1: (t - (0.6 + 0.25)) / 4.5, times 28. The quotient is
        stored as u (fst, 0x8005FA12) and stays in the register unrounded
        for the product (0x8005FA15)."""
        q = (t - (c(0x3F19999A) + c(0x3E800000))) / c(0x40900000)
        u = f32(q)
        k = f32(q * c(0x41E00000))
        return u, k

    def update(self, t):
        """0x8005F9F1: track rotations and positions at time t; LOD switch."""
        u, k = self.track_time(t)
        i = int(k)
        fr = f32(k - float(i))
        pos = []
        for tr in self.pos_tracks:
            if u > 0.0:
                if u >= 1.0:
                    pos.append(self.positions[tr[29]][:])
                    continue
                a, b = self.positions[tr[i]], self.positions[tr[i + 1]]
                v = 1.0 - fr
                pos.append([f32(fr * b[j] + v * a[j]) for j in range(3)])
            else:
                pos.append(self.positions[tr[0]][:])
        rot = []
        for tr in self.rot_tracks:
            if u > 0.0:
                if u >= 1.0:
                    q = self.quats[tr[29]]
                else:
                    q = quat_slerp(self.quats[tr[i]], self.quats[tr[i + 1]], fr)
            else:
                q = self.quats[tr[0]]
            rot.append(quat_to_m_t(q))
        lod = int(t >= (c(0x41000000) - c(0x40000000)) - c(0x3F4CCCCD))
        return {'u': u, 'k': k, 'rot': rot, 'pos': pos, 'lod': lod}

    def world(self, st):
        """0x8005EE08: world matrix of every part of the drawn group set."""
        out = []
        for gi, g in enumerate(self.groups):
            ws = []
            for p in g:
                if p['pos_track'] < 0 and p['rot_track'] < 0:
                    ws.append(p['SL'])
                    continue
                L = m_mul(p['L'], st['rot'][p['rot_track']]) if p['rot_track'] >= 0 else p['L'][:]
                if p['pos_track'] >= 0:
                    L[12:15] = v_add(L[12:15], st['pos'][p['pos_track']])
                ws.append(m_mul(p['S'], L))
            out.append(ws)
        return out


# ---------------------------------------------------------------- camera

def load_campath():
    return json.load(open(os.path.join(ROOT, 'data', 'camera', 'campath.json')))


KEY_TIME = (c(0x41000000) - c(0x40000000)) - c(0x3F4CCCCD)   # 8 - 2 - 0.8, x87
OWN_X = [0.322, 1.821, 2.323, -11.926, -39.973, -60.774, -90.795]
OWN_Y = [30.548, -70.819, -150.298, -220.64, -243.021, -261.441, -287.773]


class CamPath:
    """0x8005AFB6, 0x8005A6CB, 0x8005AC32; keys as dicts with t, eye, eye_tan,
    target, target_tan, cut, split."""

    def __init__(self, path=None, data=None, rand_state=0x76543210):
        d = data or load_campath()
        self.table = []
        for k in d['keys']:
            self.table.append({
                't': f32(k['time_byte'] * KEY_TIME * c(0x3C23D70A)),
                'eye': [f32(x) for x in k['eye']], 'eye_tan': [0.0] * 3,
                'target': [f32(x) for x in k['target']], 'target_tan': [0.0] * 3,
                'cut': f32(k['cut_byte'] * c(0x3C23D70A)),
                'split': f32(k['split_byte'] * c(0x3C23D70A))})
        self.n_paths = sum(1 for k in d['keys'] if k['time_byte'] == 0)
        self.rand = rand_state
        self.select(path)

    def draw(self):
        """0x8005A645"""
        s = self.rand
        ror = ((s >> 13) | (s << 19)) & 0xFFFFFFFF
        self.rand = (ror - ((s - 11) & 0xFFFFFFFF)) & 0xFFFFFFFF
        return self.rand

    def select(self, path=None):
        if path is None:
            path = self.draw() & 0x7FFFFFFF
        if path >= self.n_paths:
            path %= self.n_paths
        self.path = path
        i, n = 0, len(self.table)
        while i < n:
            if self.table[i]['t'] == 0.0:
                if path == 0:
                    break
                path -= 1
            i += 1
        self.first = i
        j = i + 1
        while j < n and self.table[j]['t'] != 0.0:
            j += 1
        self.n_table = j - i
        c3 = c(0x3F4CCCCD)
        own = []
        for j in range(8):
            own.append({'t': f32(j * c3 * c(0x3E124925) + ((c(0x41000000) - c(0x40000000)) - c3)),
                        'eye': [0.0] * 3, 'eye_tan': [0.0] * 3, 'target': [0.0] * 3,
                        'target_tan': [0.0] * 3, 'cut': 0.0, 'split': 0.0})
        self.own = own
        keys = self.table[self.first:self.first + self.n_table]
        last = keys[-1]
        if len(keys) >= 2:
            v = v_sub(keys[-1]['eye'], keys[-2]['eye'])
            v = v_scale(v, f32(1.0 / (keys[-1]['t'] - keys[-2]['t'])))
        else:
            v = [0.0] * 3
        e = v_madd(last['eye'], v, f32((own[0]['t'] - last['t']) * c(0x3F333333)))
        ln = v_len(e)
        e = v_scale(e, f32(1.0 / ln))
        d = e[:]
        p1 = f32(ln * c(0x3F99999A) - c(0xC2BE0000))
        if p1 < c(0x43430000):
            p1 = c(0x43430000)
        own[0]['eye'] = v_scale(e, f32(p1 - c(0x42BE0000)))
        m = [f32(x * -1.0) for x in d]
        x = v_norm(v_cross(m, [0.0, 0.0, 1.0]))
        up = v_cross(x, m)
        F3 = m_ident()
        F3[0:3], F3[4:7], F3[8:11] = x, m, up
        # row 3 is m scaled by -rho (0x8005A9D9-0x8005AA0F), i.e. d * rho;
        # only logo_draw reads it (Logo.md 1)
        F3[12:15] = v_scale(m, -p1)
        self.frame = F3
        self.origin = v_scale(m, f32(c(0xC30423D7) - p1))
        for j in range(7):
            loc = [0.0, f32(f32(OWN_Y[j]) + c(0x430423D7)), f32(OWN_X[j])]
            own[j + 1]['eye'] = v_add(v_mul_m3(loc, F3), self.origin)
        self.look = v_add(v_mul_m3([0.0, f32(132.14), 25.0], F3), self.origin)
        self.fade_t0 = own[2]['t']
        self.fade_k = f32(1.0 / (own[5]['t'] - own[2]['t']))
        self.keys = keys + own
        n = len(self.keys)
        for k in self.keys:
            k['eye_tan'], k['target_tan'] = [0.0] * 3, [0.0] * 3
        for i, k in enumerate(self.keys):
            if i:
                p = self.keys[i - 1]
                w = f32((k['split'] + 1.0) * (1.0 - k['cut']) * 0.5)
                k['eye_tan'] = v_madd(k['eye_tan'], v_sub(k['eye'], p['eye']), w)
                k['target_tan'] = v_madd(k['target_tan'], v_sub(k['target'], p['target']), w)
            if i < n - 1:
                q = self.keys[i + 1]
                w = f32((1.0 - k['split']) * (1.0 - k['cut']) * 0.5)
                k['eye_tan'] = v_madd(k['eye_tan'], v_sub(q['eye'], k['eye']), w)
                k['target_tan'] = v_madd(k['target_tan'], v_sub(q['target'], k['target']), w)

    def eval(self, s):
        """0x8005AC32: eye and target at path time s."""
        keys, n = self.keys, len(self.keys)
        if s <= c(0x41000000) - c(0x40000000):
            i = 1
            while i < n and not s < keys[i]['t']:
                i += 1
            if i != n:
                k0, k1 = keys[i - 1], keys[i]
                dt = f32(k1['t'] - k0['t'])
                if dt < c(0x3A83126F):
                    dt = f32(0.001)
                prev = k0['t'] - keys[i - 2]['t'] if i > 1 else dt
                if prev < c(0x3A83126F):
                    prev = c(0x3A83126F)
                st = s - k0['t']
                u = min(max(st / dt, 0.0), 1.0)
                h = 3.0 * (u * u) - (u * u * u + u * u * u)
                uu = st / ((1.0 - h) * prev + h * dt)
                u2 = uu * uu
                u3 = u2 * uu
                U, U2, U3 = f32(uu), f32(u2), f32(u3)
                T2, T3 = f32(u3 + u3), f32(u2 * 3.0)
                h00 = (T2 - T3) + 1.0
                h10 = (U3 - (U2 + U2)) + U
                h11 = U3 - U2
                h01 = T3 - T2

                def herm(a0, m0, a1, m1):
                    return [f32(((h01 * a1[j] + h11 * m1[j]) + h10 * m0[j]) + h00 * a0[j])
                            for j in range(3)]
                eye = herm(k0['eye'], k0['eye_tan'], k1['eye'], k1['eye_tan'])
                tgt = herm(k0['target'], k0['target_tan'], k1['target'], k1['target_tan'])
                fz = (s - self.fade_t0) * self.fade_k
                fz = min(max(fz, 0.0), 1.0)
                w = f32((1.0 - f32(math.cos(f32(c(0x40490FDB) * fz)))) * 0.5)
                tgt = v_scale(tgt, f32(1.0 - w))
                tgt = v_madd(tgt, self.look, w)
                u = uu
                return eye, tgt, {'key': i, 'u': u, 'fade': w}
            eye = keys[n - 1]['eye'][:]
        else:
            eye = self.own[7]['eye'][:]
        return eye, self.look[:], {'key': None}


def path_time(t):
    """0x800571AD: s = sin_sse(min(0.2833 t, pi/2)) * t * 0.2 + t^2 / 6 * 0.8"""
    l8 = f32(t * t * c(0x3E2AAAAB) * c(0x3F4CCCCD))
    a = f32(t * c(0x3E910CB3))
    if c(0x3FC90FDB) < a:
        a = c(0x3FC90FDB)
    return f32(sin_sse(a) * t * c(0x3E4CCCCD) + l8)


def view_toward(eye, tgt, up):
    """0x8005B093"""
    z = v_norm(v_sub(tgt, eye))
    x = v_norm(v_cross(z, up))
    y = v_cross(x, z)
    m = [x[0], y[0], z[0], 0.0, x[1], y[1], z[1], 0.0, x[2], y[2], z[2], 0.0,
         0.0, 0.0, 0.0, 1.0]
    m[12:15] = v_mul_m3([-eye[0], -eye[1], -eye[2]], m)
    return m


def perspective(fov, aspect, near, far):
    """0x8005B18C"""
    cs, sn = math.cos(fov * 0.5), math.sin(fov * 0.5)
    cot = f32(cs) / f32(sn)
    # far / (far - near) is stored as m[10] (fst, 0x8005B209) and stays in
    # the register unrounded for the product with near (0x8005B21B)
    q = far / (far - near)
    m = [0.0] * 16
    m[0] = f32(cot * aspect)
    m[5] = f32(cot)
    m[10] = f32(q)
    m[11] = 1.0
    m[14] = f32(-(q * near))
    return m


def projection(wide=False):
    """0x8005768C: pi/4, 0.75 or 0.5625, 0.4, 800"""
    return perspective(f32(c(0x40490FDB) * c(0x3E800000)),
                       c(0x3F100000) if wide else c(0x3F400000), c(0x3ECCCCCD), c(0x44480000))


def weighted_center(ref, points, x20):
    """0x80058D1F on the blob (C6): points[0] is the blob centre (blob+0x54),
    the rest its drops (blob+0x878, stride 0x38); weights 1/|p - ref|^2."""
    def w(p):
        dx, dy, dz = ref[0] - p[0], ref[1] - p[1], ref[2] - p[2]
        return 1.0 / ((dz * dz + dy * dy) + dx * dx)
    acc = [0.0, 0.0, 0.0]
    w0 = w(points[0])
    tot = f32(w0)
    acc = v_madd(acc, points[0], f32(w0))
    for p in points[1:]:
        wi = f32(w(p))
        acc = v_madd(acc, p, wi)
        tot = f32(wi + tot)
    inv = 1.0 / tot                      # stays in the x87 register
    return [f32(acc[i] * inv) for i in range(3)]


def light_view(center, side):
    """0x8005FDF9: view from the blob centre straight down (side 0) or up
    (side 1), 120 degrees, square, 1 .. 500; times the texel matrix."""
    tgt = [center[0], center[1], f32(center[2] + (1.0 if side else -1.0))]
    view = view_toward(center, tgt, [0.0, -1.0 if side else 1.0, 0.0])
    proj = perspective(f32(c(0x40490FDB) * c(0x3F2AAAAB)), 1.0, 1.0, c(0x43FA0000))
    tex = m_ident()
    tex[0], tex[5], tex[10] = 256.0, -256.0, c(0x477FFF00)
    tex[12] = tex[13] = c(0x43804000)
    return view, proj, m_mul(m_mul(view, proj), tex)


def camera(cp, t):
    s = path_time(t)
    eye, tgt, info = cp.eval(s)
    view = view_toward(eye, tgt, [0.0, 0.0, 1.0])
    info['s'] = s
    return eye, tgt, view, info


# ---------------------------------------------------------------- geometry

TWO_PI = c(0x40490FDB) + c(0x40490FDB)      # 0x8006697C doubled, exact


def quat_to_m(q):
    """0x8005C665: the rotation matrix of q, not transposed"""
    t = quat_to_m_t(q)
    return [t[0], t[4], t[8], 0.0, t[1], t[5], t[9], 0.0, t[2], t[6], t[10], 0.0,
            0.0, 0.0, 0.0, 1.0]


def quat_axis_angle(a, ang):
    """0x8005E051"""
    h = ang * 0.5
    s = f32(math.sin(h))
    return [f32(s * a[0]), f32(s * a[1]), f32(s * a[2]), f32(math.cos(h))]


def strip_count(cols, rows, lead=0, trail=0):
    """0x8006127E"""
    if cols < 15:
        return (lead != 0) + ((cols + 2) * rows + cols) * 2 + (trail != 0)
    return strip_count(cols - 14, rows, 1, trail) + strip_count(14, rows, lead, 1)


def strip_build(cols, rows, lead, trail, start, row=0, step=0):
    """0x800612D3: strip indices over a grid of (cols + 1) x (rows + 1) vertices,
    split into blocks of 14 columns."""
    row = row or cols + 1
    step = step or 1
    if cols >= 15:
        a = strip_build(14, rows, lead, 1, start, row, step)
        return a + strip_build(cols - 14, rows, 1, trail, step * 14 + start, row, step)
    o = []
    if lead:
        o.append(start)
    o.append(start)
    for i in range(1, cols + 1):
        o += [step * i + start] * 2
    for r in range(rows):
        o.append(r * row + start)
        for i in range(cols + 1):
            o += [r * row + i * step + start, i * step + (r + 1) * row + start]
        if r < rows - 1:
            o.append(cols * step + start + (r + 1) * row)
    if trail:
        o.append(cols * step + start + rows * row)
    return [x & 0xFFFF for x in o]


class Mesh:
    def __init__(self, fmt):
        self.fmt = fmt
        self.pos, self.attr, self.idx = [], [], []

    def vert(self, p, basis, uv=None):
        self.pos.append([f32(x) for x in p])
        a = ([f32(x) for x in uv] if self.fmt == 1 else []) + [f32(x) for x in basis]
        self.attr.append(a)


def gen_sphere(m, form, lod, base):
    """0x8005C9DC"""
    n = form['segments'] >> lod
    rings = int(n / 2)
    dlat = f32(c(0x40490FDB) / (rings - 1.0))
    for i in range(rings):
        lat = f32(i * dlat)
        c1, s1 = f32(math.cos(lat)), f32(math.sin(lat))
        for j in range(n):
            lon = f32(j * (TWO_PI / n))
            c2, s2 = f32(math.cos(lon)), f32(math.sin(lon))
            p = [c1, f32(s1 * c2), f32(s2 * s1)]
            t = [0.0, -s2, c2]
            m.vert(p, t + v_cross(t, p) + p, [lon / TWO_PI, f32(1.0 / c(0x40490FDB)) * lat])
    start = base
    for r in range(rings - 1):
        cur = start
        for j in range(n + 1):
            m.idx += [n + cur, cur]
            cur = cur + 1 if j < n - 1 else start
        start = start + n


def ring_table(n):
    out = []
    for j in range(n + 1):
        a = f32(j * (TWO_PI / n))
        out.append([f32(math.cos(a)), f32(math.sin(a)), f32((1.0 / TWO_PI) * a)])
    return out


IDENT3 = [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]
DOWN3 = [1.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, -1.0]


def caps_and_strip(m, n, rows, base):
    m.idx += list(range(base, base + 2 * n))
    m.idx += strip_build(n, rows, 0, 0, base + 2 * n)


def gen_cylinder(m, form, lod, base):
    """0x8005CBC8"""
    rows, n = form['rings'], form['segments'] >> lod
    tb = ring_table(n)
    for j in range(n - 1, -1, -1):
        m.vert([tb[j][0], tb[j][1], 1.0], IDENT3, [tb[j][0] + 0.5, tb[j][1] + 0.5])
    for j in range(n):
        m.vert([tb[j][0], tb[j][1], 0.0], DOWN3, [tb[j][0] + 0.5, tb[j][1] + 0.5])
    dz = f32(1.0 / rows)
    for r in range(rows + 1):
        z = f32(r * dz)
        for j in range(n + 1):
            x, y, w = tb[j]
            m.vert([x, y, z], [-y, x, 0.0, 0.0, 0.0, 1.0, x, y, 0.0], [w * 4.0, 32.0 * z])
    caps_and_strip(m, n, rows, base)


def gen_cone(m, form, lod, base):
    """0x8005CF51"""
    rows, n = form['rings'], form['segments'] >> lod
    tb = ring_table(n)
    A = [[f32(x * form['r0']), f32(y * form['r0']), w] for x, y, w in tb]
    B = [[f32(x * form['r1']), f32(y * form['r1']), w] for x, y, w in tb]
    h = form['height']
    for j in range(n - 1, -1, -1):
        m.vert([B[j][0], B[j][1], h], IDENT3, [B[j][0] + 0.5, B[j][1] + 0.5])
    for j in range(n):
        m.vert([A[j][0], A[j][1], 0.0], DOWN3, [A[j][0] + 0.5, A[j][1] + 0.5])
    df = f32(1.0 / rows)
    for r in range(rows + 1):
        fz = f32(r * df)
        for j in range(n + 1):
            a, b = A[j], B[j]
            p = [(b[0] - a[0]) * fz + a[0], (b[1] - a[1]) * fz + a[1], fz * h]
            t = v_norm([-b[1], b[0], 0.0])
            s = v_norm([f32(b[0] - a[0]), f32(b[1] - a[1]), h])
            m.vert(p, t + s + v_cross(t, s), [b[2], fz])
    caps_and_strip(m, n, rows, base)


# 0x8005D398: per side the four corners (signs of 0.5) and the three basis
# vectors as stored; read from the six cases of the code, checked against the guest
BOX = [
    ([(1, -1, 1), (1, 1, 1), (1, 1, -1), (1, -1, -1)], [0, 1, 0, 0, 0, 1, 1, 0, 0]),
    ([(1, 1, 1), (-1, 1, 1), (-1, 1, -1), (1, 1, -1)], [-1, 0, 0, 0, 0, 1, 0, 1, 0]),
    ([(-1, 1, 1), (-1, -1, 1), (-1, -1, -1), (-1, 1, -1)], [0, -1, 0, 0, 0, 1, 0, -1, 0]),
    ([(-1, -1, 1), (1, -1, 1), (1, -1, -1), (-1, -1, -1)], [1, 0, 0, 0, 0, 1, 0, -1, 0]),
    ([(-1, 1, 1), (1, 1, 1), (1, -1, 1), (-1, -1, 1)], [1, 0, 0, 0, 1, 0, 0, 0, 1]),
    ([(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1)], [1, 0, 0, 0, -1, 0, 0, 0, -1]),
]
BOX_UV = [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)]


def gen_box(m, form, lod, base):
    for k, (corners, basis) in enumerate(BOX):
        for i, cn in enumerate(corners):
            m.vert([0.5 * x for x in cn], basis, BOX_UV[i])
        b = base + 4 * k
        m.idx += [b, b + 1, b + 2, b, b + 2, b + 3]


def gen_torus(m, form, lod, base):
    """0x8005D74F"""
    n, k = form['segments_axis'] >> lod, form['segments_tube'] >> lod
    r = form['tube']
    for i in range(k):
        a = f32(i * (TWO_PI / k))
        c1, s1 = f32(math.cos(a)), f32(math.sin(a))
        rad = f32(c1 * r + 1.0)
        for j in range(n):
            b = f32(j * (TWO_PI / n))
            c2, s2 = f32(math.cos(b)), f32(math.sin(b))
            t = [-s2, c2, 0.0]
            s = [f32(-(c2 * s1)), f32(-(s2 * s1)), c1]
            m.vert([c2 * rad, s2 * rad, s1 * r], t + s + v_cross(t, s),
                   [(1.0 / TWO_PI) * b, (1.0 / TWO_PI) * a])
    cur, nxt = base, base + n
    for i in range(k):
        a, b = cur, nxt
        for j in range(n + 1):
            m.idx += [a, b]
            if j < n - 1:
                a, b = a + 1, b + 1
            else:
                a, b = cur, nxt
        cur = cur + n
        nxt = nxt + n if i + 1 < k - 1 else base


def gen_lathe(m, form, lod, base):
    """0x8005E210"""
    pts = form['points']
    A, C = form['axis'], form['center']
    n_p = len(pts)
    P, N, K = [], [], []
    for i, pt in enumerate(pts):
        p = pt['p']
        crease = not pt['flags'] & 1
        if crease:
            P.append(p[:])
            N.append([0.0, 0.0, 0.0])
            K.append(0)
        P.append(p[:])
        K.append(1 if crease else 2)
        q = pts[(i + 1) % n_p]['p']
        N.append(v_norm(v_cross(v_cross(A, v_sub(q, C)), v_sub(q, p))))
    nv = len(P)
    N2 = []
    for v in range(nv):
        prev = N[(v - 1) % nv]
        N2.append(prev[:] if K[v] == 0 else N[v][:] if K[v] == 1 else v_norm(v_add(prev, N[v])))
    segs = form['segments'] >> lod
    dang = f32(TWO_PI / segs)
    vstep = f32(1.0 / (n_p - 1.0))
    for r in range(segs + 1):
        ang = f32(r * dang)
        M = quat_to_m(quat_axis_angle(A, ang))
        u = f32(ang / TWO_PI)
        k = 0
        for v in range(nv):
            vv = f32(k * vstep)
            if K[v]:
                k += 1
            d = v_sub(P[v], C)
            p = v_add(v_mul_m3(d, M), C)
            t = v_norm(v_mul_m3(v_cross(A, d), M))
            nn = v_mul_m3(N2[v], M)
            m.vert(p, t + v_cross(nn, t) + nn, [u * c(0x41200000), vv * c(0x41200000)])
    for r in range(segs):
        cur, nxt = base + r * nv, base + (r + 1) * nv
        a, b = cur, nxt
        for j in range(nv + 1):
            m.idx += [b, a]
            if j < nv - 1:
                a, b = a + 1, b + 1
            else:
                a, b = cur, nxt


GEN = {0: gen_sphere, 1: gen_cylinder, 2: gen_box, 3: gen_torus, 4: gen_cone, 5: gen_lathe}


def form_counts(typ, form, lod):
    """0x8005DBB3-0x8005DD58: vertices, primitives, indices, cap fan length"""
    if typ == 0:
        n = form['segments'] >> lod
        p = ((n >> 1) - 1) * (n + 1) * 2 - 2
        return (n >> 1) * n, p, p + 2, 0
    if typ in (1, 4):
        rows, n = form['rings'], form['segments'] >> lod
        return (rows + 1) * (n + 1) + 2 + 2 * n, 0, strip_count(n, rows) + 2 * n, n
    if typ == 2:
        return 24, 12, 36, 0
    if typ == 3:
        n, k = form['segments_axis'] >> lod, form['segments_tube'] >> lod
        p = (n + 1) * k * 2 - 2
        return n * k, p, p + 2, 0
    nc = sum(1 for q in form['points'] if not q['flags'] & 1)
    segs = form['segments'] >> lod
    p = (len(form['points']) + 1 + nc) * segs * 2 - 2
    return (len(form['points']) + nc) * (segs + 1), p, p + 2, 0


def build_group(g, lod):
    """0x8005F3DB: the forms one after another in one vertex and one index buffer"""
    m = Mesh(g['vertex_format'])
    info, vb, ib = [], 0, 0
    for f_ in (g['forms'] or [{}]):
        nv, npr, ni, caps = form_counts(g['type'], f_, lod)
        info.append({'v0': vb, 'nv': nv, 'i0': ib, 'prims': npr, 'ni': ni, 'caps': caps})
        n0 = len(m.pos)
        GEN[g['type']](m, f_, lod, vb)
        while len(m.pos) < n0 + nv:          # counted but not written (cylinder, cone)
            m.pos.append(None)
            m.attr.append(None)
        vb += nv
        ib += ni
    return m, info
