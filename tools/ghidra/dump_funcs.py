"""Decompiles chosen functions of the kernel project, read-only.

    python tools/ghidra/dump_funcs.py OUT.c 0x80061ac1 0x80050fa4-0x80052000

Runs DumpFuncs.java on out/ghidra/kernel after apply_names.py, so the
output carries the names of docs/decomp/README.md. The output is for local
reading only and is not committed.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import headless  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('out')
    ap.add_argument('addr', nargs='+')
    a = ap.parse_args()
    project = os.path.join(headless.ROOT, 'out', 'ghidra')
    headless.run(project, 'kernel',
                 ['-process', 'kernel.img', '-noanalysis', '-readOnly'],
                 [('DumpFuncs.java', [os.path.abspath(a.out)] + a.addr)],
                 os.path.join(project, 'dump_funcs.log'))
    print(a.out)


if __name__ == '__main__':
    main()
