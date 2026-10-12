"""Traces the DSP cores of the sound hardware for a short window.

    python tools/xemu/dsp_trace.py --name c4_dsp --after-flips 120 --window 0.2
    python tools/xemu/dsp_trace.py --name c4_dsp_boot --from-start --until-flips 15
    python tools/xemu/dsp_trace.py --name c4_dsp --listing   # only rebuild the listing

Starts xemu like bench.py, lets the guest run to the given flip (the
start-up sound plays from about flip 10 on), then switches on the trace
events dsp56k_execute_instruction (core, pc) and
dsp56k_execute_instruction_disasm (xemu's own disassembly of the
instruction, hw/xbox/mcpx/apu/dsp/dsp_dis.c.inc) for --window seconds
of host time, and ends the run. --from-start switches the disassembly
event on before the guest starts (the text carries the pc, p:XXXX) and
keeps it on up to --until-flips, so that code run only once is seen too.

Output in out/xemu/runs/<name>/: trace.log, and listing.txt with every
executed GP instruction once, sorted by pc, with its execution count.
For local reading; the DSP program itself is ROM data (Audio.md 6).
"""

import argparse
import collections
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402

EVENTS = ['dsp56k_execute_instruction', 'dsp56k_execute_instruction_disasm']
PC_RE = re.compile(r'^dsp56k_execute_instruction \[gp=(\d)\]: pc=0x([0-9a-f]+)')
TEXT_PC_RE = re.compile(r'p:([0-9a-f]{4})')
DIS_RE = re.compile(r'^dsp56k_execute_instruction_disasm (.*)$')


def listing(d):
    count = collections.Counter()
    text = {}
    core = pc = None
    with open(os.path.join(d, 'trace.log'), errors='replace') as f:
        for line in f:
            m = PC_RE.match(line)
            if m:
                core, pc = int(m.group(1)), int(m.group(2), 16)
                continue
            m = DIS_RE.match(line)
            if m and pc is None and core is None:      # --from-start: pc from the text
                t = TEXT_PC_RE.search(m.group(1))
                if t:
                    p = int(t.group(1), 16)
                    count[p] += 1
                    text.setdefault(p, m.group(1).rstrip())
                continue
            if m and core == 1 and pc is not None:
                count[pc] += 1
                text.setdefault(pc, m.group(1).rstrip())
                pc = None
    with open(os.path.join(d, 'listing.txt'), 'w') as f:
        for p in sorted(text):
            f.write('%04x %8d  %s\n' % (p, count[p], text[p]))
    return len(text), sum(count.values())


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--name', required=True)
    ap.add_argument('--after-flips', type=int, default=120)
    ap.add_argument('--window', type=float, default=0.2)
    ap.add_argument('--timeout', type=float, default=120)
    ap.add_argument('--listing', action='store_true')
    ap.add_argument('--from-start', action='store_true')
    ap.add_argument('--until-flips', type=int, default=15)
    a = ap.parse_args()
    d = os.path.join(bench.RUNS, a.name)
    if not a.listing:
        ns = argparse.Namespace(name=a.name, frame=[], frames=None, brk=None, video=False,
                                audio=False, duration=0, xemu='release', timeout=a.timeout,
                                keep_image=False, trace=[])
        b = bench.Bench(ns)
        b.start()
        try:
            if a.from_start:
                b.q.cmd('trace-event-set-state', name=EVENTS[1], enable=True)
                b.q.cmd('cont')
                while b.tr.flips < a.until_flips:
                    b.tr.update()
                    time.sleep(0.05)
                b.q.cmd('trace-event-set-state', name=EVENTS[1], enable=False)
                b.log['dsp_trace'] = {'from_start': True, 'until_flips': a.until_flips}
                raise StopIteration
            b.q.cmd('cont')
            while b.tr.flips < a.after_flips:
                b.tr.update()
                time.sleep(0.02)
            for e in EVENTS:
                b.q.cmd('trace-event-set-state', name=e, enable=True)
            time.sleep(a.window)
            for e in EVENTS:
                b.q.cmd('trace-event-set-state', name=e, enable=False)
            b.log['dsp_trace'] = {'after_flips': a.after_flips, 'window_s': a.window}
        except StopIteration:
            pass
        finally:
            b.quit()
        b.finish()
    n, total = listing(d)
    print('%d GP instructions seen, %d executions -> %s' % (n, total, d))


if __name__ == '__main__':
    main()
