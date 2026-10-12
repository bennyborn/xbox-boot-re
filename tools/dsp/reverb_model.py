"""Model of the GP program's frame routine, written after Audio.md 6.3.

    python tools/dsp/reverb_model.py      # compare with out/dsp/gp_ir.npz

Floating point, no 24-bit rounding or saturation. Delay lines are ring
buffers driven like the DSP DMA (dsp_dma.c): every tap is read as a block
of 32 samples at the start of a frame, the written blocks go out at the
end, and every ring offset advances by 32 samples per frame. A tap with
delay 0 therefore reads what was written one ring length earlier.

The parameters (ring lengths, tap delays, coefficients) are read from the
X memory that tools/dsp/gp_harness.c dumps after the program has set
itself up (out/dsp/x_after.bin, from `gp run ... 200 out/dsp/x_after.bin`
on silence); the addresses are those of the parameter block, Audio.md 6.3.
"""

import os
import struct

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
F = 32


def frac(v):
    v &= 0xFFFFFF
    return (v - (1 << 24) if v & 0x800000 else v) / float(1 << 23)


class Params:
    def __init__(self, path=os.path.join(ROOT, 'out', 'dsp', 'x_after.bin')):
        x = [v & 0xFFFFFF for v in struct.unpack('<6144I', open(path, 'rb').read())]
        self.x = x

        def chain(head):
            a, out = head, []
            while True:
                nb, ctl, cnt, dsp, off, base, size = [x[a + i] for i in range(7)]
                out.append({'buf': dsp, 'off': off // 4, 'line': base, 'n': (size + 1) // 4})
                if nb & 0x4000:
                    return out
                a = nb & 0x3FFF
        self.reads = chain(0x105)       # P:0x0396, at the start of a frame
        self.writes = chain(0xA3)       # P:0x039C, at the end
        self.c = lambda a: frac(x[a])


class Reverb:
    def __init__(self, p):
        self.p = p
        self.rings = {w['line']: np.zeros(w['n']) for w in p.writes}
        self.woff = {w['line']: w['off'] for w in p.writes}
        # read offset relative to the write offset is fixed
        self.rel = [(r['buf'], r['line'], (r['off'] - self.woff[r['line']]) % r['n']) for r in p.reads]
        self.wbuf = {w['line']: w['buf'] for w in p.writes}
        self.lp = {}                    # one-pole states, by coefficient block

    def onepole(self, blk, sig):
        """P:0x03A2: y = c[blk+1] * y_old + c[blk+2] * x, state at blk."""
        a, b = self.p.c(blk + 1), self.p.c(blk + 2)
        y = self.lp.get(blk, 0.0)
        out = np.empty(F)
        for i in range(F):
            y = a * y + b * sig[i]
            out[i] = y
        self.lp[blk] = y
        return out

    def frame(self, bin11):
        c = self.p.c
        B = {}
        for buf, line, rel in self.rel:                  # 1. read all taps
            n = len(self.rings[line])
            o = (self.woff[line] + rel) % n
            B[buf] = self.rings[line][(o + np.arange(F)) % n].copy()
        x = self.onepole(0x6E, bin11 * 0.5)              # 2. input
        for k in range(4):                               # 3. combs s1..s4
            B[0x224 + 32 * k] = B[0x224 + 32 * k] + B[0x204] + c(0x93 + k) * B[0x4A4 + 32 * k]
        for i in range(4):                               # taps into s5..s8
            for j in range(2):
                B[0x524 + 32 * i] = B[0x524 + 32 * i] + c(0x5D + 2 * i + j) * B[0x2A4 + 32 * (2 * i + j)]
        s324 = B[0x3C4] + B[0x3E4] + B[0x424] + B[0x484]  # A14 + B0 + C0 + D1
        s344 = B[0x3A4] + B[0x404] + B[0x444] + B[0x464]  # A13 + B1 + C1 + D0
        B[0x364] = (s324 + s344) * c(0x66)
        B[0x324] = s324 * c(0x65) + B[0x5A4]
        B[0x344] = s344 * c(0x65) + B[0x5C4]
        B[0x5A4], B[0x5C4] = B[0x324].copy(), B[0x344].copy()
        B[0x324] = self.onepole(0x86, B[0x324])
        B[0x344] = self.onepole(0x89, B[0x344])
        for k, blk in enumerate((0x71, 0x74, 0x77)):     # cascade A -> B -> C -> D
            B[0x3C4 + 64 * k] = self.onepole(blk, B[0x3C4 + 64 * k])
        s = [B[0x524 + 32 * j] for j in range(4)]        # 4. matrix over s5..s8
        for i in range(4):
            B[0x2A4 + 32 * i] = sum(c(0x8F - i + j) * s[j] for j in range(4))
        for i in range(4):
            B[0x2A4 + 32 * i] = self.onepole(0x7A + 3 * i, B[0x2A4 + 32 * i] + B[0x364])
        B[0x204] = x                                     # input into line A
        y0, y1 = c(0x1E2), c(0x1E3)                      # 5. outputs
        left = 128 * (c(0x1DF) * B[0x244] + c(0x1DE) * B[0x224]
                      + y0 * (B[0x544] + B[0x524]) + y1 * B[0x5A4])
        right = 128 * (c(0x1E1) * B[0x284] + c(0x1E0) * B[0x264]
                       + y0 * (B[0x584] + B[0x564]) + y1 * B[0x5C4])
        for line, buf in self.wbuf.items():              # 6. write
            n = len(self.rings[line])
            self.rings[line][(self.woff[line] + np.arange(F)) % n] = B[buf]
            self.woff[line] = (self.woff[line] + F) % n
        return left, right


def impulse_response(seconds=4.0):
    r = Reverb(Params())
    frames = int(seconds * 48000 / F)
    L, R = [], []
    for k in range(frames):
        x = np.zeros(F)
        if k == 0:
            x[0] = 0.5
        l, rr = r.frame(x)
        L.append(l)
        R.append(rr)
    return np.concatenate(L) / 0.5, np.concatenate(R) / 0.5


def main():
    L, R = impulse_response()
    ir = np.load(os.path.join(ROOT, 'out', 'dsp', 'gp_ir.npz'))
    for side, m in (('L', L), ('R', R)):
        h = ir['b11_' + side][:len(m)]
        m = m[:len(h)]
        err = h - m
        print('%s: max |h| %.4f, max |h - model| %.2e, '
              'error/signal %.1f dB' % (side, np.abs(h).max(), np.abs(err).max(),
                                         10 * np.log10((err ** 2).sum() / (h ** 2).sum())))


if __name__ == '__main__':
    main()
