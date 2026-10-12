"""Disassembles functions of the unpacked kernel, with the bounds from Ghidra.

    python tools/rom/disasm.py 0x80056d0b [0x8003e550 ...] [--raw N]

Uses out/rom/kernel.img and out/ghidra/kernel_graph.json (export_graph.py).
Call targets are annotated with the callee's label and, for the kernel's
export thunks, nothing else: names only come from this line's own table
(docs/decomp/README.md, column "Ghidra name"). --raw N disassembles N bytes
from the address without looking at function bounds. Output is for local
reading only and is not committed.
"""

import argparse
import json
import os
import re

import capstone

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BASE = 0x80010000


def table_names():
    names = {}
    path = os.path.join(ROOT, 'docs', 'decomp', 'README.md')
    for line in open(path, encoding='utf-8'):
        m = re.match(r'\|\s*`?(0x[0-9A-Fa-f]{8})`?\s*\|\s*`?([^|`]+?)`?\s*\|', line)
        if m:
            names[int(m.group(1), 16)] = m.group(2)
    return names


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('addr', nargs='+')
    ap.add_argument('--raw', type=lambda x: int(x, 0))
    a = ap.parse_args()
    img = open(os.path.join(ROOT, 'out', 'rom', 'kernel.img'), 'rb').read()
    funcs = json.load(open(os.path.join(ROOT, 'out', 'ghidra', 'kernel_graph.json')))
    byentry = {int(f['entry'], 16): f for f in funcs}
    names = table_names()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    def label(x):
        return names.get(x) or (byentry[x]['name'] if x in byentry else None)

    for s in a.addr:
        e = int(s, 0)
        if a.raw:
            ranges = [(e, e + a.raw - 1)]
        else:
            f = byentry[e]
            ranges = [(int(lo, 16), int(hi, 16)) for lo, hi in f['body']]
            print(f'== {e:#x} {label(e)}  callers:',
                  ' '.join(sorted({g['entry'] for g in funcs
                                   for _, t in g['calls'] if t == f['entry']})))
        for lo, hi in ranges:
            for ins in md.disasm(img[lo - BASE:hi + 1 - BASE], lo):
                note = ''
                for v in re.findall(r'0x[0-9a-f]{8}', ins.op_str):
                    v = int(v, 16)
                    if v in byentry or v in names:
                        note += f'  ; {label(v)}'
                print(f'{ins.address:08x}  {ins.mnemonic:6s} {ins.op_str}{note}')
            print('   ...')


if __name__ == '__main__':
    main()
