"""Finds the code that writes a guest address: a GDB write watchpoint.

    python tools/xemu/watch.py 0x8003a89c --hits 5 [--len 4] [--after-flips 30]

Starts xemu like bench.py with the GDB stub, waits until AFTER_FLIPS flips
have happened (0: from the start), sets a write watchpoint (Z2) on
ADDR..ADDR+LEN and records, for each hit, EIP (QEMU reports the stop after
the writing instruction), the value now at ADDR and the 16 dwords above
ESP. Output: out/xemu/runs/<name>/watch.json.
"""

import argparse
import json
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402
from gdbstub import GDB  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('addr', type=lambda x: int(x, 0))
    ap.add_argument('--len', type=int, default=4)
    ap.add_argument('--hits', type=int, default=5)
    ap.add_argument('--after-flips', type=int, default=0)
    ap.add_argument('--name', default=None)
    a = ap.parse_args()
    name = a.name or f'watch_{a.addr:08x}'
    ns = argparse.Namespace(name=name, frame=[], frames=None, brk='gdb', video=False,
                            audio=False, duration=0, xemu='release', timeout=120,
                            keep_image=False)
    b = bench.Bench(ns)
    b.start()
    hits = []
    try:
        g = GDB(bench.GDB_PORT)
        if a.after_flips:
            g.cont()
            b.wait_flips(a.after_flips)
            g.interrupt()
        r = g.req(f'Z2,{a.addr:x},{a.len:x}')
        if r != 'OK':
            raise RuntimeError(f'watchpoint: {r!r}')
        for _ in range(a.hits):
            g.cont()
            stop = g.wait_stop()
            regs = g.regs()
            val = struct.unpack('<I', g.read(a.addr & ~3, 4))[0]
            stack = struct.unpack('<16I', g.read(regs['esp'], 64))
            b.tr.update()
            hits.append({'stop': stop, 'flips': b.tr.flips,
                         'regs': {k: f'{v:#010x}' for k, v in regs.items()},
                         'value': f'{val:#010x}', 'stack': [f'{x:#010x}' for x in stack]})
        g.req(f'z2,{a.addr:x},{a.len:x}')
        g.close()
    finally:
        b.quit()
    with open(os.path.join(b.dir, 'watch.json'), 'w') as f:
        json.dump(hits, f, indent=1)
    for h in hits:
        print(h['flips'], h['regs']['eip'], h['value'], h['stop'][:40])


if __name__ == '__main__':
    main()
