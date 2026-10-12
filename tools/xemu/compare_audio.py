"""Compares the rendered start-up sound with a recording from xemu.

    python tools/xemu/audio_render.py
    python tools/xemu/compare_audio.py [--run c2audio] [--json docs/vectors/c4_recording.json]

The recording (bench.py --audio) goes through the host mixer and has its
own level; the render (audio_render.py) stops at the mix bins. So the
comparison is on power spectrograms with fitted gains, not on samples:

1. offset: cross-correlation of the summed power per 10 ms
2. output mapping: recording L and R as non-negative sums of the bins
   0, 1 and 11 (least squares on power spectrograms)
3. per component (sequencer channel), in the time-frequency cells where
   the channel carries at least half of the rendered power and lies
   clearly above the recording's noise floor: the correlation of the
   cell powers (dB) at the global offset, the shift in -300..+300 ms
   that maximises it, and the level relative to the global gain
   (weighted by power)
"""

import argparse
import json
import os
import wave

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RATE, HOP, WIN = 48000, 480, 2048
LOW_BINS = 2


def load_wav(path):
    with wave.open(path) as w:
        x = np.frombuffer(w.readframes(w.getnframes()), '<i2').astype(float) / 32768.0
        return x.reshape(-1, w.getnchannels()).T


def stft_power(x):
    n = 1 + max(0, (len(x) - WIN) // HOP)
    win = np.hanning(WIN)
    idx = np.arange(WIN)[None, :] + HOP * np.arange(n)[:, None]
    f = np.fft.rfft(x[idx] * win, axis=1)
    p = (np.abs(f) ** 2).T                         # bins x frames
    # the host's recording chain removes DC (Audio.md 7); bins 0 and 1
    # (below 35 Hz) are left out of every comparison
    p[:LOW_BINS] = 0
    return p


def env(p):
    return np.log10(p.sum(axis=0) + 1e-12)


def xcorr_offset(a, b, lo, hi):
    """Frame offset k in [lo, hi) maximising corr(a[k:k+len(b)], b)."""
    best, bk = -2, lo
    for k in range(lo, hi):
        s = a[k:k + len(b)]
        if len(s) < len(b):
            break
        c = np.corrcoef(s, b)[0, 1]
        if c > best:
            best, bk = c, k
    return bk, best


def nnls_small(A, y):
    """Non-negative least squares for a few columns (active set, brute force)."""
    import itertools
    best, bx = None, None
    m = A.shape[1]
    for r in range(1, m + 1):
        for cols in itertools.combinations(range(m), r):
            x, *_ = np.linalg.lstsq(A[:, cols], y, rcond=None)
            if (x < 0).any():
                continue
            res = np.linalg.norm(A[:, cols] @ x - y)
            if best is None or res < best:
                full = np.zeros(m)
                full[list(cols)] = x
                best, bx = res, full
    return bx, best


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--run', default='c2audio')
    ap.add_argument('--render', default=os.path.join(ROOT, 'out', 'audio', 'render.npz'))
    ap.add_argument('--mix', choices=['monitor', 'bins', 'bins01'], default='monitor',
                    help="monitor: xemu's VP monitor (use_dsp off, the default of xemu); "
                         'bins: sum of bins 0, 1, 11; bins01: bins 0 and 1')
    ap.add_argument('--json', default=os.path.join(ROOT, 'docs', 'vectors', 'c4_recording.json'))
    a = ap.parse_args()
    rec = load_wav(os.path.join(ROOT, 'out', 'xemu', 'runs', a.run, 'audio.wav'))
    z = np.load(a.render)
    bins = {b: z['bin%d' % b] for b in (0, 1, 11)}
    if a.mix == 'monitor':
        stems = z['stems_mon']
        bins = {0: stems.sum(axis=0), 1: np.zeros(stems.shape[1]), 11: np.zeros(stems.shape[1])}
    else:
        stems = z['stems'] + (z['stems11'] if a.mix == 'bins' else 0)
    tick = int(z['tick_samples'])

    RL, RR = stft_power(rec[0]), stft_power(rec[1])
    RM = RL + RR
    B = {b: stft_power(x) for b, x in bins.items()}
    M = B[0] + B[1] + (B[11] if a.mix == 'bins' else 0)
    k0, c0 = xcorr_offset(env(RM), env(M), 0, RM.shape[1] - M.shape[1])
    nf = M.shape[1]
    sl = slice(k0, k0 + nf)
    out = {'run': a.run, 'render': os.path.relpath(a.render, ROOT).replace('\\', '/'),
           'hop_ms': HOP * 1000 / RATE, 'window': WIN,
           'offset': {'frames': int(k0), 'seconds': k0 * HOP / RATE, 'envelope_corr': round(float(c0), 4)}}

    # 2. output mapping
    mapping = {}
    for name, R in (('L', RL), ('R', RR)):
        A = np.stack([B[b].ravel() for b in (0, 1, 11)], axis=1)
        x, res = nnls_small(A, R[:, sl].ravel())
        mapping[name] = {'bin0': float(x[0]), 'bin1': float(x[1]), 'bin11': float(x[2]),
                         'residual_rel': float(res / np.linalg.norm(R[:, sl]))}
    out['mapping'] = mapping

    # global gain for the mono sum: recording power ~ g * render power
    g = float((RM[:, sl] * M).sum() / (M * M).sum())
    floor = float(np.median(RM[:, :max(1, k0 - 20)])) if k0 > 40 else 0.0

    # 3. per component: cells where the channel carries at least half of the
    # rendered power and is clearly above the recording's noise floor
    S = [stft_power(s) for s in stems]
    total = sum(S) + 1e-30
    lr = 10 * np.log10(RM + 1e-14)
    comps = {}
    for c, P in enumerate(S):
        if P.sum() < 1e-9:
            continue
        mask = (P >= 0.5 * total) & (g * P >= 10 * floor)
        if mask.sum() < 50:
            comps[str(c)] = {'mask_cells': int(mask.sum()), 'note': 'too few dominant cells'}
            continue
        f_i, t_i = np.nonzero(mask)
        lp = 10 * np.log10(g * P[f_i, t_i])
        score = {}
        for k in range(-30, 31):
            tt = k0 + t_i + k
            ok = (tt >= 0) & (tt < RM.shape[1])
            score[k] = float(np.corrcoef(lr[f_i[ok], tt[ok]], lp[ok])[0, 1])
        kb = max(score, key=score.get)
        # weighted by power: the median of cell ratios is pulled down by cells
        # that only the render's linear interpolation fills (images)
        rec_e = (RM[f_i, k0 + t_i] - floor).sum()
        ren_e = g * P[f_i, t_i].sum()
        comps[str(c)] = {'mask_cells': int(mask.sum()),
                         'frames': [int(t_i.min()), int(t_i.max())],
                         'corr_at_0': round(score[0], 4), 'best_shift_ms': kb * HOP * 1000 / RATE,
                         'corr_best': round(score[kb], 4),
                         'level_db_vs_global': round(float(10 * np.log10(max(rec_e, 1e-30) / ren_e)), 2)}
    out['components'] = comps
    out['global_gain'] = g
    out['noise_floor_power'] = floor
    os.makedirs(os.path.dirname(a.json), exist_ok=True)
    with open(a.json, 'w') as f:
        json.dump(out, f, indent=1)
    print(json.dumps({k: out[k] for k in ('offset', 'mapping')}, indent=1))
    for c, v in comps.items():
        print(c, v)


if __name__ == '__main__':
    main()
