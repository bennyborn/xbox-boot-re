"""Model of the start-up sound, written after docs/decomp/Audio.md.

Imported by vectors_c4.py; also runnable:

    python tools/xemu/audio_model.py [--ticks N]     # list the voice calls

Two parts:

- tables(): the sample tables the kernel computes in 0x80061AC1, built
  from the formulas in Audio.md section 2 with x87-like double precision.
- Sequencer: the interpreter of 0x80060252 and its commands, fed with
  data/audio/sequence.json (tools/extract/audio.py). It records every
  command it executes and every call it makes into the voice layer.
"""

import argparse
import json
import math
import os
import struct

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
DATA = os.path.join(ROOT, 'data', 'audio')

# constants of 0x80061AC1 (doubles at 0x800129D8-0x80012A08)
K_SCALE = struct.unpack('<d', bytes.fromhex('00000000c0ffdf40'))[0]   # 32767
K_SINE = struct.unpack('<d', bytes.fromhex('6e861bf0f921a93f'))[0]    # 0.04908734375
K_MOD = struct.unpack('<d', bytes.fromhex('6e861bf0f921b93f'))[0]     # 0.0981746875
K_CAR = struct.unpack('<d', bytes.fromhex('6e861bf0f921c93f'))[0]     # 0.196349375
K_IDX = 2.0 ** -14                                                    # 0x800129F0
K_SAW_A, K_SAW_B = 0.0078125, 65536.0                                 # 0x800129E0, 0x800129D8


def trunc16(x):
    """0x80055F8C truncates to a 64-bit integer; the caller stores the low 16 bits."""
    v = int(x)
    return ((v + 0x8000) & 0xFFFF) - 0x8000


def tables(junk=0):
    """Sample tables by kernel address, as lists of signed 16-bit values.
    junk: the 16-bit word at 0x80083318 when the reversed copy is made."""
    t = {}
    s = 0x3EB
    noise = []
    for _ in range(0x2000):
        s = (s * 0x343FD + 0x269EC3) & 0xFFFFFFFF
        noise.append((s >> 16) & 0x7FFF)
    t[0x800A1B78] = noise
    t[0x80093B78] = [trunc16(math.sin(i * K_SINE) * K_SCALE) for i in range(0x80)]
    fm, m = [], 0
    for i in range(0x8000):
        m += 1 if i < 0x4000 else -1
        fm.append(trunc16(math.sin(i * K_CAR + math.sin(K_MOD * i) * m * K_IDX) * K_SCALE))
    t[0x80083B70] = fm
    t[0x80083328] = [trunc16((i - 0x40) * K_SAW_A * K_SAW_B) for i in range(0x80)]
    raw = open(os.path.join(DATA, 'pcm_80066ac0.raw'), 'rb').read()
    a = [((b << 8) ^ 0x8000) - 0x8000 for b in raw]
    t[0x80078898] = a
    t[0x800970F8] = [junk] + a[:0:-1]
    for src, dst in ((0x8006C008, 0x800A5B80), (0x8006CEC8, 0x80093C78)):
        raw = open(os.path.join(DATA, 'pcm_%08x.raw' % src), 'rb').read()
        t[dst] = [(((b ^ 0x80) << 8) ^ 0x8000) - 0x8000 for b in raw]
    return t


def s16(x):
    return ((x + 0x8000) & 0xFFFF) - 0x8000


class Track:
    def __init__(self, ptr, layer, channel, now):
        self.ptr, self.layer, self.channel = ptr, layer, channel
        self.wait, self.last = 0, now


class Channel:
    def __init__(self):
        self.reset()

    def reset(self):
        """0x800616C7"""
        self.sample = 0          # +0
        self.b2 = 0              # +2
        self.vol = 0x7F          # +3
        self.pitch = 0           # +6 (left as it was by the kernel; set by every note)
        self.loops = []          # +8/+0x10, depth +0x20
        self.transpose = 0       # +0x32
        self.f34 = 0             # +0x34
        self.f36 = 0             # +0x36


class Sequencer:
    def __init__(self, seq=None):
        self.seq = seq or json.load(open(os.path.join(DATA, 'sequence.json')))
        self.code = {}
        for start, cmds in self.seq['tracks'].items():
            for c in cmds:
                self.code[int(c['addr'], 16)] = c
        self.nch = self.seq['channels']
        self.desc = self.seq['descriptors']
        self.ch = [[Channel() for _ in range(self.nch)] for _ in range(2)]
        self.layer1 = [0] * self.nch        # 0x80083318
        self.tracks = []                    # active list, head first
        self.ring = [1]                     # 0x80061AC1 posts event 1
        self.now = 1                        # 0x800A7958 after 0x8006033C
        self.executed = []                  # (now, channel, addr, op)
        self.calls = []                     # (now, channel, function, args)
        self.call_op = []                   # index into executed for each call

    # -- events --------------------------------------------------------
    def event(self, e):
        ev = self.seq['events'][e]
        if ev['handler'] == 0:                      # 0x80061762
            self.tracks = []
            for c in range(self.nch):
                self.layer1[c] = 0
                self.ch[0][c].reset()
                self.ch[1][c].reset()
            return
        if ev['handler'] != 1:
            raise NotImplementedError('event handler %d' % ev['handler'])
        # 0x800617D0: start layer 0 tracks on the channels of the mask
        self.tracks = [t for t in self.tracks if t.layer != 0]
        ptrs = self.seq['event_lists'][ev['list']]
        mask, k = int(ev['mask'], 16), 0
        for c in range(self.nch):
            if mask >> c & 1:
                self.ch[0][c].reset()
                self.tracks.insert(0, Track(int(ptrs[k], 16), 0, c, self.now))
                k += 1

    # -- one tick of 0x8006165E -----------------------------------------
    def tick(self):
        self.now = (self.now + 1) & 0xFFFF
        if self.ring:
            e = self.ring.pop(0)
            if e > self.seq['event_max']:
                return
            self.event(e)
        for t in list(self.tracks):
            t.wait = s16(t.wait + s16(t.last - self.now))
            t.last = self.now
            if t.wait >= 0:
                continue
            if not self.step(t):
                break

    def call(self, t, fn, *args):
        if self.layer1[t.channel] <= t.layer:
            self.calls.append((self.now, t.channel, fn) + args)
            self.call_op.append(len(self.executed) - 1)

    def step(self, t):
        c = self.code[t.ptr]
        op, a = c['op'], c['args']
        ch = self.ch[t.layer][t.channel]
        self.executed.append((self.now, t.channel, t.ptr, op))
        nxt = t.ptr + {0: 4, 1: 6, 2: 6, 3: 4, 4: 2, 5: 4, 6: 4, 9: 4, 10: 4, 11: 4,
                       12: 6, 13: 4, 15: 0, 16: 6, 17: 6}[op]
        if op == 15:
            return False
        if op in (1, 12):
            t.wait = s16(t.wait + a[1])
            ch.pitch = ((a[0] << 8) + ch.transpose) & 0xFFFF
            p = ch.pitch
            lib = (p & 0xFF) * 0x155 // 0xFF + ((p >> 8) - 0x3C) * 0x155
            if op == 1:
                self.call(t, 'pitch_play', lib, self.desc[ch.sample]['loop'] != 0)
            else:
                self.call(t, 'pitch', lib)
        elif op in (0, 13):
            t.wait = s16(t.wait + a[0])
            if op == 0:
                self.call(t, 'stop')
        elif op == 2:
            nxt = int(a[0], 16)
        elif op == 3:
            ch.loops.append([a[0], nxt])
        elif op == 4:
            ch.loops[-1][0] = (ch.loops[-1][0] - 1) & 0xFFFF
            if ch.loops[-1][0] == 0:
                ch.loops.pop()
            else:
                nxt = ch.loops[-1][1]
        elif op == 5:
            ch.sample, ch.vol, ch.b2 = a[0], 0, 0
            self.call(t, 'sample', a[0])
        elif op == 6:
            ch.b2 = a[0] & 0xFF
            self.call(t, 'nop_b2')
        elif op == 9:
            ch.vol = (ch.vol + a[0]) & 0xFF
            self.call(t, 'level', 200 - 30 * ch.vol)
        elif op == 10:
            ch.transpose = s16(ch.transpose + a[0])
        elif op == 11:
            ch.transpose = s16(a[0])
        elif op in (16, 17):
            ch.f34 = (ch.f34 + a[0]) & 0xFFFF if op == 16 else a[0]
            ch.f36 = a[1]
            # 0x800619B1 is reached through 0x8006E910 without the layer gate
            self.calls.append((self.now, t.channel, 'filter', ch.f34 + 0x8000, ch.f36))
            self.call_op.append(len(self.executed) - 1)
        t.ptr = nxt
        return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--ticks', type=int, default=1700)
    a = ap.parse_args()
    s = Sequencer()
    for _ in range(a.ticks):
        s.tick()
    for c in s.calls:
        print(*c)
    print('%d commands, %d calls' % (len(s.executed), len(s.calls)))


if __name__ == '__main__':
    main()
