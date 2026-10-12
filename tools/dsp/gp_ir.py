"""Measures the GP program as a system: impulse responses of every mix bin.

    python tools/dsp/build.py
    python tools/dsp/gp_ir.py [--seconds 4]

Runs out/dsp/gp.exe on data/audio/gp_program.bin: a single sample of
0.5 in one mix bin, then silence, and records X:0x1400 (left) and X:0x1420
(right) as xemu plays them with audio.use_dsp on (gp_ep.c). Also checks
linearity (twice the input, twice the output) and superposition on noise.

Writes out/dsp/gp_ir.npz (the responses of bins 0, 1 and 11) and
docs/vectors/c4_gp.json (which bins reach the output, gains, delays, the
early peaks of bin 11, decay times, linearity, and the SHA-256 of the
responses as float32).
"""

import argparse
import hashlib
import json
import os
import subprocess

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
GP = os.path.join(ROOT, 'out', 'dsp', 'gp.exe')
PROG = os.path.join(ROOT, 'data', 'audio', 'gp_program.bin')
START = 100                      # frame of the impulse; the program sets itself up before


def run(x):
    frames = x.shape[0]
    fin = os.path.join(ROOT, 'out', 'dsp', 'ir_in.f32')
    fout = os.path.join(ROOT, 'out', 'dsp', 'ir_out.f32')
    x.astype(np.float32).tofile(fin)
    r = subprocess.run([GP, 'run', PROG, fin, fout, str(frames)], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stderr)
    y = np.fromfile(fout, np.float32).reshape(frames, 2, 32)
    return y[:, 0, :].ravel(), y[:, 1, :].ravel(), r.stderr.strip()


def decay(h):
    e = np.cumsum((h ** 2)[::-1])[::-1]
    db = 10 * np.log10(e / e[0] + 1e-30)

    def t(level):
        return float(np.argmax(db <= level) / 48000)
    return {'t_minus5_s': t(-5), 't_minus35_s': t(-35), 'rt60_from_5_35_s': (t(-35) - t(-5)) * 2}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--seconds', type=float, default=4)
    a = ap.parse_args()
    frames = START + int(a.seconds * 48000 / 32)
    t0 = START * 32
    out, irs = {'script': 'tools/dsp/build.py, tools/dsp/gp_ir.py',
                'program': 'data/audio/gp_program.bin', 'impulse': 0.5, 'frames': frames,
                'bins': {}}, {}
    for b in range(32):
        x = np.zeros((frames, 32, 32))
        x[START, b, 0] = 0.5
        L, R, log = run(x)
        L, R = L[t0:] / 0.5, R[t0:] / 0.5
        if (L ** 2).sum() + (R ** 2).sum() < 1e-12:
            continue
        info = {}
        for side, h in (('L', L), ('R', R)):
            nz = np.nonzero(np.abs(h) > 1e-6)[0]
            if not len(nz):
                info[side] = None
                continue
            info[side] = {'first_sample': int(nz[0]), 'last_s': float(nz[-1] / 48000),
                          'peak': float(np.abs(h).max()), 'sum_sq': float((h ** 2).sum())}
            if nz[-1] > nz[0]:
                info[side]['decay'] = decay(h)
                pk = [i for i in range(1, 4800) if abs(h[i]) > 0.01
                      and abs(h[i]) >= abs(h[i - 1]) and abs(h[i]) >= abs(h[i + 1])][:12]
                info[side]['early_peaks_ms'] = [[round(i / 48, 2), round(float(h[i]), 4)] for i in pk]
            irs['b%d_%s' % (b, side)] = h.astype(np.float32)
        out['bins'][str(b)] = info
    out['harness_log'] = log
    # linearity and superposition on noise in the three bins
    rng = np.random.default_rng(1)
    n = START + 3000
    x = np.zeros((n, 32, 32))
    for b in (0, 1, 11):
        x[START:START + 300, b, :] = rng.uniform(-0.1, 0.1, (300, 32))
    L1, R1, _ = run(x)
    L2, R2, _ = run(2 * x)
    parts = [run(np.where(np.arange(32)[None, :, None] == b, x, 0)) for b in (0, 1, 11)]
    out['linearity_max_err'] = float(max(np.abs(L2 - 2 * L1).max(), np.abs(R2 - 2 * R1).max()))
    out['superposition_max_err'] = float(max(np.abs(L1 - sum(p[0] for p in parts)).max(),
                                             np.abs(R1 - sum(p[1] for p in parts)).max()))
    np.savez_compressed(os.path.join(ROOT, 'out', 'dsp', 'gp_ir.npz'), **irs)
    out['ir_sha256'] = {k: hashlib.sha256(v.tobytes()).hexdigest() for k, v in sorted(irs.items())}
    with open(os.path.join(ROOT, 'docs', 'vectors', 'c4_gp.json'), 'w') as f:
        json.dump(out, f, indent=1)
    for b, v in out['bins'].items():
        print(b, {s: (None if i is None else {k: i[k] for k in ('first_sample', 'peak', 'sum_sq')})
                  for s, i in v.items()})
    print('linearity %.2e, superposition %.2e' % (out['linearity_max_err'], out['superposition_max_err']))


if __name__ == '__main__':
    main()
