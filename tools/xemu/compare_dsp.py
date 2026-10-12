"""Compares a recording with audio.use_dsp on against render plus GP program.

    python tools/xemu/audio_render.py
    python tools/dsp/gp_ir.py
    python tools/xemu/compare_dsp.py [--run c4_dsp_out]

Builds the expected output per channel and side from the render
(audio_render.py, bins 0, 1, 11 per sequencer channel) and the measured
responses of the GP program (gp_ir.py): left = bin 0 + bin 11 * h11_L,
right = bin 1 + bin 11 * h11_R. Then, per side, the same comparison as
compare_audio.py: global offset and gain, and per channel the correlation
of the dominant time-frequency cells, the best shift and the level.
Also checks the stereo picture: the level difference left minus right
per channel, recording against expectation.

Output: docs/vectors/c4_recording_dsp.json
"""

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare_audio import HOP, RATE, ROOT, env, load_wav, stft_power, xcorr_offset  # noqa: E402


def conv(x, h):
    n = len(x) + len(h) - 1
    m = 1 << (n - 1).bit_length()
    return np.fft.irfft(np.fft.rfft(x, m) * np.fft.rfft(h, m), m)[:len(x)]


def expected():
    """Per channel and side what the output should be, from the render and
    the measured responses of the GP program."""
    z = np.load(os.path.join(ROOT, 'out', 'audio', 'render.npz'))
    ir = np.load(os.path.join(ROOT, 'out', 'dsp', 'gp_ir.npz'))
    s0, s1, s11 = z['stems0'], z['stems1'], z['stems11']
    nch = s0.shape[0]
    exp = {'L': np.zeros_like(s0), 'R': np.zeros_like(s0)}
    for c in range(nch):
        wet_l = conv(s11[c], ir['b11_L']) if s11[c].any() else 0
        wet_r = conv(s11[c], ir['b11_R']) if s11[c].any() else 0
        exp['L'][c] = s0[c] + wet_l
        exp['R'][c] = s1[c] + wet_r
    return exp


def compare(exp, run):
    """The measures of this script for an expectation exp['L'|'R'][channel]
    (core/test/check_k7.py calls it with the core's output)."""
    nch = exp['L'].shape[0]
    rec = load_wav(os.path.join(ROOT, 'out', 'xemu', 'runs', run, 'audio.wav'))
    R = {'L': stft_power(rec[0]), 'R': stft_power(rec[1])}
    S = {side: [stft_power(exp[side][c]) for c in range(nch)] for side in 'LR'}
    M = {side: sum(S[side]) for side in 'LR'}
    k0, c0 = xcorr_offset(env(R['L'] + R['R']), env(M['L'] + M['R']), 0,
                          R['L'].shape[1] - M['L'].shape[1])
    nf = M['L'].shape[1]
    out = {'run': run, 'offset': {'seconds': k0 * HOP / RATE, 'envelope_corr': round(float(c0), 4)},
           'sides': {}}
    level = {}
    for side in 'LR':
        RS = R[side][:, k0:k0 + nf]
        g = float((RS * M[side]).sum() / (M[side] ** 2).sum())
        floor = float(np.median(R[side][:, :max(10, k0 - 20)]))
        tot = M[side] + 1e-30
        lr = 10 * np.log10(R[side] + 1e-14)
        comps = {}
        for c, P in enumerate(S[side]):
            if P.sum() < 1e-9:
                continue
            m = (P >= 0.5 * tot) & (g * P >= 10 * floor)
            if m.sum() < 50:
                continue
            f_i, t_i = np.nonzero(m)
            lp = 10 * np.log10(g * P[f_i, t_i])
            score = {}
            for k in range(-30, 31):
                tt = k0 + t_i + k
                ok = (tt >= 0) & (tt < R[side].shape[1])
                score[k] = float(np.corrcoef(lr[f_i[ok], tt[ok]], lp[ok])[0, 1])
            kb = max(score, key=score.get)
            lev = 10 * np.log10(max((RS[f_i, t_i] - floor).sum(), 1e-30) / (g * P[f_i, t_i].sum()))
            comps[str(c)] = {'cells': int(m.sum()), 'corr_at_0': round(score[0], 4),
                             'best_shift_ms': kb * HOP * 1000 / RATE, 'corr_best': round(score[kb], 4),
                             'level_db': round(float(lev), 2)}
        out['sides'][side] = {'gain_db': 10 * np.log10(g), 'components': comps}
        level[side] = g
    # stereo picture: per channel, power left minus right, recording vs expected
    st = {}
    for c in range(nch):
        el, er = (exp['L'][c] ** 2).sum(), (exp['R'][c] ** 2).sum()
        if el + er < 1e-9:
            continue
        P = S['L'][c] + S['R'][c]
        m = P >= 0.5 * (M['L'] + M['R'] + 1e-30)
        if m.sum() < 50:
            continue
        rl = (R['L'][:, k0:k0 + nf][m]).sum() / level['L']
        rr = (R['R'][:, k0:k0 + nf][m]).sum() / level['R']
        pl, pr = S['L'][c][m].sum(), S['R'][c][m].sum()
        st[str(c)] = {'expected_db': round(float(10 * np.log10(pl / pr)), 2),
                      'recorded_db': round(float(10 * np.log10(rl / rr)), 2)}
    out['stereo_left_minus_right'] = st
    out['side_gain_difference_db'] = round(float(10 * np.log10(level['L'] / level['R'])), 2)
    return out, st


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--run', default='c4_dsp_out')
    ap.add_argument('--json', default=os.path.join(ROOT, 'docs', 'vectors', 'c4_recording_dsp.json'))
    a = ap.parse_args()
    out, st = compare(expected(), a.run)
    with open(a.json, 'w') as f:
        json.dump(out, f, indent=1)
    print(json.dumps(out['offset']), 'gain L-R %.2f dB' % out['side_gain_difference_db'])
    for side in 'LR':
        for c, v in out['sides'][side]['components'].items():
            print(side, c, v)
    for c, v in st.items():
        print('stereo', c, v)


if __name__ == '__main__':
    main()
