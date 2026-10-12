"""Reads guest memory every time the CPU reaches a code address.

    python tools/xemu/probe.py --name c3_timing --at 0x80057949 --at 0x80057a48 \
        --read 0x8006f050:0x30 --read 0x80070868:8 --read 0x8003a89c:4 --max 2000

Starts xemu like bench.py with the GDB stub and sets a hardware breakpoint
on every --at address (at most four, x86 debug registers). On each hit it
records the address, the general registers, every --read range (virtual
address:length, as hex bytes), the flip and vblank counts from the trace
and the host time, then lets the guest run on. The run ends after --max
hits, after --stop-at was hit, after --idle seconds without a hit, or
after --timeout seconds (checked between hits).

While the guest is stopped QEMU's virtual clock stands still, so the
guest's timer interrupts see no time pass during a stop; the vblank
thread of xemu runs on host time and does (bench README, "Limits").

--trace enables further trace events (e.g. mcpx_apu_method). xemu writes
them from the CPU thread, so at a stop everything the guest caused before
the breakpoint is in trace.log; trace_pos in each hit is the offset read
so far, which splits the log between hits.

Output: out/xemu/runs/<name>/probe.json
"""

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402
from gdbstub import GDB  # noqa: E402


def rng(s):
    a, n = s.split(':')
    return int(a, 0), int(n, 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--name', required=True)
    ap.add_argument('--at', action='append', type=lambda x: int(x, 0), required=True)
    ap.add_argument('--read', action='append', type=rng, default=[])
    ap.add_argument('--max', type=int, default=1000)
    ap.add_argument('--stop-at', type=lambda x: int(x, 0))
    ap.add_argument('--timeout', type=float, default=180)
    ap.add_argument('--idle', type=float,
                    help='end the run when no breakpoint was hit for this many seconds')
    ap.add_argument('--poke', action='append', default=[],
                    help='AT:ADDR:HEX - when the breakpoint AT is hit, write the bytes HEX '
                         'to ADDR; ADDR may be esp+N. AT must also be given with --at')
    ap.add_argument('--trace', action='append', default=[],
                    help='enable this trace event too; each hit records the byte '
                         'offset reached in trace.log (repeatable)')
    a = ap.parse_args()
    if len(a.at) > 4:
        ap.error('at most four --at')
    ns = argparse.Namespace(name=a.name, frame=[], frames=None, brk='gdb', video=False,
                            audio=False, duration=0, xemu='release', timeout=a.timeout,
                            keep_image=False, trace=a.trace)
    b = bench.Bench(ns)
    b.start()
    hits = []
    try:
        g = GDB(bench.GDB_PORT)
        for x in a.at:
            g.hbreak(x)
        while len(hits) < a.max and time.monotonic() - b.t0 < a.timeout:
            g.cont()
            if g.wait_stop(a.idle) is None:
                b.log['notes'].append(f'no hit for {a.idle} s, run ended')
                break
            r = g.regs()
            b.tr.update()
            h = {'at': f"{r['eip']:#010x}", 't_ms': round((time.monotonic() - b.t0) * 1000, 1),
                 'flips': b.tr.flips, 'vblanks': b.tr.vblanks, 'trace_pos': b.tr.pos,
                 'regs': {k: f'{v:#010x}' for k, v in r.items()},
                 'mem': {f'{x:#010x}': g.read(x, n).hex() for x, n in a.read}}
            for p in a.poke:
                at, addr, data = p.split(':')
                if int(at, 0) == r['eip']:
                    x = (r['esp'] + int(addr[4:], 0)) if addr.startswith('esp+') else int(addr, 0)
                    g.write(x, bytes.fromhex(data))
                    h['poked'] = f'{x:#010x}:{data}'
            hits.append(h)
            if a.stop_at is not None and r['eip'] == a.stop_at:
                break
            # Continuing on a breakpoint address hits it again at once (QEMU
            # checks breakpoints before the instruction). Step over it first.
            if r['eip'] in a.at:
                g.hbreak(r['eip'], False)
                g.req('s')
                g.hbreak(r['eip'])
        for x in a.at:
            g.hbreak(x, False)
        g.close()
    finally:
        b.quit()
    b.log['probe'] = {'at': [f'{x:#010x}' for x in a.at], 'poke': a.poke,
                      'read': [f'{x:#010x}:{n:#x}' for x, n in a.read], 'hits': len(hits)}
    with open(os.path.join(b.dir, 'probe.json'), 'w') as f:
        json.dump(hits, f)
    with open(os.path.join(b.dir, 'run.json'), 'w') as f:
        json.dump(b.log, f, indent=1)
    print(f'{len(hits)} hits, {b.tr.flips} flips -> {b.dir}')


if __name__ == '__main__':
    main()
