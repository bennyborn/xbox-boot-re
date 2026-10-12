"""Samples where the CPU is while the BIOS runs.

    python tools/xemu/sample_eip.py --name c3_eip --duration 20 --interval 0.004

Starts xemu like bench.py, with the GDB stub, and stops the guest every
INTERVAL seconds: EIP, ESP, EBP, the flip count from the trace at that
moment and the 64 dwords above ESP (candidate return addresses for the
call chain). The guest runs again right after. Output:

    out/xemu/runs/<name>/samples.json   one entry per stop
    out/xemu/runs/<name>/flips.json     as from bench.py

Every stop costs host time the vblank clock keeps counting (bench README,
"Limits"), so this run is for where the code is, not for timing.
"""

import argparse
import json
import os
import random
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402
from gdbstub import GDB  # noqa: E402

STACK_DWORDS = 64


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--name', default='eip')
    ap.add_argument('--duration', type=float, default=20)
    ap.add_argument('--interval', type=float, default=0.004)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--xemu', choices=bench.XEMU, default='release')
    a = ap.parse_args()

    ns = argparse.Namespace(name=a.name, frame=[], frames=None, brk='gdb',
                            video=False, audio=False, duration=a.duration,
                            xemu=a.xemu, timeout=a.duration + 60, keep_image=False)
    b = bench.Bench(ns)
    b.start()
    rnd = random.Random(a.seed)
    samples = []
    try:
        g = GDB(bench.GDB_PORT)
        g.cont()
        while time.monotonic() - b.t0 < a.duration:
            # Jitter, so the stops do not lock onto the vblank period.
            time.sleep(a.interval * (0.5 + rnd.random()))
            g.interrupt()
            r = g.regs()
            try:
                stack = struct.unpack('<%dI' % STACK_DWORDS, g.read(r['esp'], 4 * STACK_DWORDS))
            except RuntimeError:
                stack = ()
            b.tr.update()
            samples.append({'t_ms': round((time.monotonic() - b.t0) * 1000, 1),
                            'flips': b.tr.flips, 'vblanks': b.tr.vblanks,
                            'eip': r['eip'], 'esp': r['esp'], 'ebp': r['ebp'],
                            'stack': list(stack)})
            g.cont()
        g.close()
    finally:
        b.quit()
    with open(os.path.join(b.dir, 'samples.json'), 'w') as f:
        json.dump(samples, f)
    b.log['samples'] = len(samples)
    with open(os.path.join(b.dir, 'run.json'), 'w') as f:
        json.dump(b.log, f, indent=1)
    print(f'{len(samples)} samples, {b.tr.flips} flips -> {b.dir}')


if __name__ == '__main__':
    main()
