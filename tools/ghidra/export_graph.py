"""Exports functions, calls and data references of the kernel project.

    python tools/ghidra/export_graph.py [--entry ADDR ...]

Runs ExportGraph.java on the existing project out/ghidra/kernel (made by
import_kernel.py) and writes out/ghidra/kernel_graph.json. --entry makes a
function at ADDR first, for code that Ghidra's analysis did not reach
(called only through pointers); apply_names.py does the same for every row
of the table and saves the names. This script opens the project read-only,
so it never changes it.
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import headless  # noqa: E402

ROOT = headless.ROOT


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--project', default=os.path.join(ROOT, 'out', 'ghidra'))
    ap.add_argument('--entry', action='append', default=[])
    ap.add_argument('--out', default=None)
    a = ap.parse_args()
    project = os.path.abspath(a.project)
    out = a.out or os.path.join(project, 'kernel_graph.json')
    if os.path.exists(out):
        os.remove(out)
    headless.run(project, 'kernel',
                 ['-process', 'kernel.img', '-noanalysis', '-readOnly'],
                 [('ExportGraph.java', [out] + a.entry)],
                 os.path.join(project, 'export_graph.log'))
    with open(out) as f:
        funcs = json.load(f)
    print(f'{len(funcs)} functions, '
          f'{sum(len(x["calls"]) for x in funcs)} call sites -> {out}')


if __name__ == '__main__':
    main()
