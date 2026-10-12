"""Maps EIP samples from sample_eip.py onto the functions of the Ghidra export.

    python tools/xemu/eipmap.py RUN [--from-flip A] [--to-flip B] [--top N]

Prints, per function, how many samples hit its body, and per function the
callers seen on the stack (return addresses right after a call site of the
export). Addresses outside every function are grouped by 0x100 page.
"""

import argparse
import bisect
import collections
import json
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))


def load_funcs(path):
    funcs = json.load(open(path))
    ranges = []
    ret_sites = {}      # return address -> (caller entry, callee)
    for f in funcs:
        e = int(f['entry'], 16)
        for lo, hi in f['body']:
            ranges.append((int(lo, 16), int(hi, 16), e))
    ranges.sort()
    return funcs, ranges, ret_sites


class Map:
    def __init__(self, graph):
        self.funcs, self.ranges, _ = load_funcs(graph)
        self.starts = [r[0] for r in self.ranges]
        self.byentry = {int(f['entry'], 16): f for f in self.funcs}

    def func(self, a):
        i = bisect.bisect_right(self.starts, a) - 1
        if i >= 0 and self.ranges[i][0] <= a <= self.ranges[i][1]:
            return self.ranges[i][2]
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('run')
    ap.add_argument('--graph', default=os.path.join(ROOT, 'out', 'ghidra', 'kernel_graph.json'))
    ap.add_argument('--from-flip', type=int, default=0)
    ap.add_argument('--to-flip', type=int, default=1 << 30)
    ap.add_argument('--top', type=int, default=60)
    a = ap.parse_args()
    m = Map(a.graph)
    samples = json.load(open(os.path.join(ROOT, 'out', 'xemu', 'runs', a.run, 'samples.json')))
    samples = [s for s in samples if a.from_flip <= s['flips'] <= a.to_flip]
    hits = collections.Counter()
    nofunc = collections.Counter()
    for s in samples:
        f = m.func(s['eip'])
        if f is None:
            nofunc[s['eip'] & ~0xFF] += 1
            hits['?'] += 1
        else:
            hits[f] += 1
    print(f'{len(samples)} samples')
    for f, n in hits.most_common(a.top):
        name = m.byentry[f]['name'] if f != '?' else '(no function)'
        print(f'{n:6d}  {f if f == "?" else hex(f)}  {name}')
    if nofunc:
        print('outside functions, by page:')
        for p, n in nofunc.most_common(20):
            print(f'{n:6d}  {p:#010x}')


if __name__ == '__main__':
    main()
