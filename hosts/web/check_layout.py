"""Holds hosts/web/layout.json against core/include/xboot.h.

    python hosts/web/check_layout.py

The web host reads the structs of xboot.h out of the wasm memory by offset.
The offsets stand in layout.json; this script turns each into a static
assertion and lets clang check them for wasm32, without building anything.
"""

import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
CLANG = os.path.join(ROOT, '.tools', 'llvm', 'bin', 'clang.exe')
OUT = os.path.join(ROOT, 'out', 'web')


def main():
    layout = json.load(open(os.path.join(ROOT, 'hosts', 'web', 'layout.json')))
    lines = ['#include <stddef.h>', '#include "xboot.h"']
    n = 0
    for name, s in layout.items():
        lines.append(f'_Static_assert(sizeof({name}) == {s["size"]}, "size of {name}");')
        n += 1
        for field, at in s['fields'].items():
            lines.append(f'_Static_assert(offsetof({name}, {field}) == {at}, "{name}.{field}");')
            n += 1
    os.makedirs(OUT, exist_ok=True)
    src = os.path.join(OUT, 'layout_check.c')
    with open(src, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    r = subprocess.run([CLANG, '--target=wasm32', '-std=c11', '-ffreestanding', '-fsyntax-only',
                        '-Icore/include', src], cwd=ROOT, capture_output=True, text=True)
    sys.stderr.write(r.stderr)
    if r.returncode:
        sys.exit('layout.json and xboot.h differ')
    print(f'layout.json: {n} sizes and offsets of {len(layout)} structs equal xboot.h for wasm32')


if __name__ == '__main__':
    main()
