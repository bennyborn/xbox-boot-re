"""Renders the start-up sound from the model, the way xemu's VP would play it.

    python tools/xemu/audio_render.py [--out out/audio/render.npz] [--wav out/audio/render.wav]

Chain: audio_model.Sequencer (Audio.md section 3) ticks every 6 ms (the
measured spacing, section 4); its calls go through the voice layer of
Audio.md section 5 (levels, filter, the two envelope generators, start only when the voice
is not running, release while it runs); the voices are then played after
xemu's hw/xbox/mcpx/apu/vp/vp.c (commit fc24584): frames of 32 samples
at 48 kHz, the envelope state machine of voice_step_envelope, the state
variable low-pass of svf.h, attenuate() and the submix headroom.

Not like xemu: resampling is a 16-tap windowed sinc (xemu uses
libsamplerate's sinc, so close but not bit-exact), and the library is
taken to notice the end of a voice at the next tick. What
comes after the mix bins (the GP DSP program) is not modelled; the output
has the three bins the voices use (0, 1, 11) and, per channel, xemu's VP
monitor mix (stems_mon): what xemu plays when audio.use_dsp is off, each
voice with the largest of its bin gains (vp.c, MCPX_APU_DEBUG_MON_VP).

The result is for comparing with a recording (compare_audio.py); it is
not part of the spec.
"""

import argparse
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audio_model  # noqa: E402
import vectors_c4 as V  # noqa: E402

RATE, FRAME, FRAMES_PER_TICK = 48000, 32, 9      # 9 * 32 samples = 6 ms
TAPS = 16                                        # windowed sinc for resampling
HEADROOM = 2.0                                   # 1 << submix headroom 1 (init methods 0x200..)


def attenuate(vol):
    vol &= 0xFFF
    return 0.0 if vol == 0xFFF else 10.0 ** (vol / (64.0 * -20.0))


class Env:
    """voice_step_envelope for one envelope (EA or EF)."""

    def __init__(self):
        self.cur, self.count, self.lvl = 0, 0, 0

    def start(self, state, delay, hold):
        self.cur = state
        if state == 1:
            self.count = delay * 16
        elif state == 2:
            self.count = 0
        elif state == 3:
            self.count = hold * 16

    def release(self, rr):
        self.count, self.cur = rr * 16, 6

    def step(self, attack, hold, decay, sustain, rr):
        c = self.cur
        if c == 0:
            return 0.0, False
        if c == 1:
            self.lvl = 0
            if self.count == 0:
                self.cur = 2
            else:
                self.count -= 1
            return 0.0, False
        if c == 2:
            if attack == 0:
                value = 255.0
            elif self.count <= attack * 16:
                value = float((self.count * 0xFF) // (attack * 16))
            else:
                value = 255.0
            self.lvl = int(value)
            if self.count == attack * 16:
                self.cur, self.count = 3, hold * 16
            else:
                self.count = (self.count + 1) & 0xFFFF
            return value / 255.0, False
        if c == 3:
            self.lvl = 0xFF
            if self.count == 0:
                self.cur, self.count = 4, decay * 16
            else:
                self.count -= 1
            return 1.0, False
        if c == 4:
            value = 0.0 if decay == 0 else 255.0 * 0.99988799 ** ((decay * 16 - self.count) * 4096 / decay)
            if value <= sustain + 0.2 or value > 255.0:
                self.cur = 5
            else:
                self.count = (self.count - 1) & 0xFFFF
                self.lvl = int(value)
            return value / 255.0, False
        if c == 5:
            self.count, self.lvl = 0, sustain
            return sustain / 255.0, False
        if c == 6:
            if rr == 0:
                self.count = 0
            if self.count == 0:
                self.cur = 7
                return 0.0, False
            pos = min(max(1 - self.count / (rr * 16.0), 0.0), 1.0)
            value = math.exp(-6.91 * pos) * self.lvl
            self.count -= 1
            return value / 255.0, False
        return 0.0, True                      # 7: force release, voice off


class Voice:
    def __init__(self, ch, tables):
        self.ch, self.tables = ch, tables
        self.active = False
        self.lib_on = False
        self.data, self.loop, self.ebo = None, False, 0
        self.pos = 0.0
        self.pitch = 0
        self.vol = {V.VOLA: 0xFFFFFFFF, V.VOLB: 0xFFFFFFFF, V.VOLC: 0xFFFFFFFF}
        self.env = {}
        self.ea, self.ef = Env(), Env()
        self.fmode, self.fc = 0, 0
        self.svf = [0.0, 0.0, 0.0]            # b, h, l
        self.level, self.alloc = 0, False
        self.bins = {0: 0, 1: 1, 2: 11} if ch in (3, 5) else {0: 0, 1: 1}

    def resample(self, step):
        """FRAME output samples from position pos, advancing by step, with a
        windowed sinc of TAPS taps and cutoff min(1, 1/step) (band-limited
        like xemu's libsamplerate, not bit-exact). The loop period is ebo + 1
        samples (xemu reads index ebo in most frames, vp.c voice_get_samples).
        Returns the samples and how many were produced before a one-shot ended."""
        d = self.arr
        length = self.ebo + 1
        p = self.pos + step * np.arange(FRAME)
        n = FRAME
        if not self.loop:
            ended = np.nonzero(p >= length)[0]
            if len(ended):
                n = int(ended[0])
        k = np.floor(p).astype(np.int64)
        frac = p - k
        j = np.arange(-TAPS // 2 + 1, TAPS // 2 + 1)
        idx = k[:, None] + j[None, :]
        x = frac[:, None] - j[None, :]
        fc = min(1.0, 1.0 / step)
        h = fc * np.sinc(fc * x) * (0.5 + 0.5 * np.cos(np.pi * np.clip(x / (TAPS / 2), -1, 1)))
        if self.loop:
            v = d[idx % length]
        else:
            v = np.where((idx >= 0) & (idx < length), d[np.clip(idx, 0, length - 1)], 0.0)
        out = (v * h).sum(axis=1) / 32768.0
        out[n:] = 0.0
        self.pos = float(p[-1] + step)
        if self.loop:
            self.pos %= length
        return out, n

    def vols(self):
        a, b, c = self.vol[V.VOLA], self.vol[V.VOLB], self.vol[V.VOLC]
        return [a >> 4 & 0xFFF, a >> 20, b >> 4 & 0xFFF, b >> 20, c >> 4 & 0xFFF, c >> 20]

    def frame(self):
        """32 output samples per bin, or None if the voice is silent."""
        if not self.active:
            return None
        e = self.env
        efv, _ = self.ef.step(e[V.ENV1] & 0xFFF, e[V.ENVF] >> 12 & 0xFFF, e[V.ENVF] & 0xFFF,
                              e[V.ENVF] >> 24, e['mrel'])
        ps = ((e[V.ENV0] >> 24) ^ 0x80) - 0x80
        step = 2.0 ** ((self.pitch + ps * 32 * efv) / 4096.0)
        eav, off = self.ea.step(e[V.ENV0] & 0xFFF, e[V.ENVA] >> 12 & 0xFFF, e[V.ENVA] & 0xFFF,
                                e[V.ENVA] >> 24, e[V.LFO])
        if off:
            self.active = False
            return None
        out, n = self.resample(step)
        if n < FRAME:
            self.active = False
        if self.fmode & 1:
            fc = (self.fc & 0xFFFF) - (0x10000 if self.fc & 0x8000 else 0)
            fcf = min(max(2.0 ** (fc / 4096.0), 0.003906), 1.0)
            qf = min(max((self.fc >> 16) / 32768.0, 0.079407), 1.0)
            qn = math.sqrt(qf / 2.0 + 0.01)
            b, h, l = self.svf
            for i in range(FRAME):
                x = qn * out[i]
                b = b - b * b * b * 0.001
                h = x - l - qf * b
                b = b + fcf * h
                l = l + fcf * b
                out[i] = min(max(l, -1.0), 1.0)
            self.svf = [b, h, l]
        vols = self.vols()
        o = {self.bins[k]: out * (eav * attenuate(vols[k]) / HEADROOM) for k in self.bins}
        # xemu's VP monitor (audio.use_dsp off): the largest bin gain of the voice
        o['mon'] = out * (eav * max(attenuate(vols[k]) for k in self.bins) / HEADROOM)
        return o


class Library:
    """The voice layer of Audio.md section 5 on top of the voices."""

    def __init__(self, seq, tables):
        import json
        self.seq = seq
        self.blocks = json.load(open(os.path.join(audio_model.DATA, 'envblocks.json')))['blocks']
        self.v = [Voice(c, tables) for c in range(seq.nch)]
        self.tables = tables

    def apply(self, call):
        now, ch, fn = call[:3]
        args = call[3:]
        v = self.v[ch]
        if fn == 'sample':
            d = self.seq.desc[args[0]]
            v.data = self.tables[int(d['data'], 16)]
            v.arr = np.asarray(v.data, dtype=float)
            v.loop = bool(d['loop'])
            v.ebo = d['bytes'] // 2 - 1
            v.env = V.envelope(self.blocks, d['block_a'], d['block_b'])
            v.vol = V.volumes(ch, v.level)
            v.alloc = True
        elif fn == 'level':
            v.level = args[0]
            if v.alloc:
                v.vol = V.volumes(ch, args[0])
        elif fn == 'filter':
            v.fc, v.fmode = (args[0] & 0xFFFF) | args[1] << 16, 1
        elif fn in ('pitch_play', 'pitch'):
            v.pitch = ((args[0] + 0x8000) & 0xFFFF) - 0x8000
            if fn == 'pitch_play' and not v.lib_on:
                e = v.env
                v.pos = 0.0
                v.ea.start(e['start'] >> 28, e[V.ENV0] >> 12 & 0xFFF, e[V.ENVA] >> 12 & 0xFFF)
                v.ef.start(e['start'] >> 24 & 0xF, e[V.ENV1] >> 12 & 0xFFF, e[V.ENVF] >> 12 & 0xFFF)
                v.svf = [0.0, 0.0, 0.0]
                v.active = v.lib_on = True
        elif fn == 'stop':
            if v.lib_on:
                v.ea.release(v.env[V.LFO])
                v.ef.release(v.env['mrel'])


def render(ticks=1340):
    tables = audio_model.tables()
    seq = audio_model.Sequencer()
    lib = Library(seq, tables)
    n = ticks * FRAMES_PER_TICK * FRAME
    bins = {b: np.zeros(n) for b in (0, 1, 11)}
    stems = np.zeros((seq.nch, n))              # bins 0 and 1
    stems11 = np.zeros((seq.nch, n))            # bin 11
    stems_mon = np.zeros((seq.nch, n))          # VP monitor mix
    stems0 = np.zeros((seq.nch, n))             # bin 0 alone
    stems1 = np.zeros((seq.nch, n))             # bin 1 alone
    done = 0
    for t in range(ticks):
        for v in lib.v:                          # the library notices ended voices
            if v.lib_on and not v.active:
                v.lib_on = False
        k = len(seq.calls)
        seq.tick()
        for c in seq.calls[k:]:
            lib.apply(c)
        for _ in range(FRAMES_PER_TICK):
            s = slice(done, done + FRAME)
            for v in lib.v:
                o = v.frame()
                if o:
                    stems_mon[v.ch, s] += o.pop('mon')
                    for b, x in o.items():
                        bins[b][s] += x
                    stems[v.ch, s] += o[0] + o[1]
                    stems0[v.ch, s] += o[0]
                    stems1[v.ch, s] += o[1]
                    if 11 in o:
                        stems11[v.ch, s] += o[11]
            done += FRAME
    return bins, stems, stems11, stems_mon, stems0, stems1, seq


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', default=os.path.join(V.ROOT, 'out', 'audio', 'render.npz'))
    ap.add_argument('--wav', default=os.path.join(V.ROOT, 'out', 'audio', 'render.wav'))
    a = ap.parse_args()
    bins, stems, stems11, stems_mon, stems0, stems1, seq = render()
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    np.savez_compressed(a.out, bin0=bins[0], bin1=bins[1], bin11=bins[11], stems=stems, stems11=stems11, stems_mon=stems_mon, stems0=stems0, stems1=stems1,
                        rate=RATE, tick_samples=FRAMES_PER_TICK * FRAME)
    import wave
    st = np.stack([bins[0], bins[1]], axis=1)
    st = np.clip(st / max(1e-9, np.abs(st).max()) * 30000, -32768, 32767).astype('<i2')
    with wave.open(a.wav, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(st.tobytes())
    print('rendered %.2f s -> %s' % (len(bins[0]) / RATE, os.path.relpath(a.out, V.ROOT)))


if __name__ == '__main__':
    main()
