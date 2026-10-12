"""Applies the names of docs/decomp/README.md to the Ghidra project.

    python tools/ghidra/apply_names.py [--dry-run]

Reads the tables "Functions" and "Data" of docs/decomp/README.md and
runs ApplyNames.java on out/ghidra/kernel (made by import_kernel.py). A
function row makes a function at its address if there is none, because
some code in the animation is reached only through pointer tables; the
table is therefore also the list of these entries. A data row puts a label
on its address. Names land with source USER_DEFINED, so they are easy to
tell from Ghidra's defaults.

Checks afterwards: every function row has a function at exactly its
address, and every function in the animation's range (Overview.md,
"Der abgegrenzte Bereich") has a row. --dry-run opens the project
read-only, so nothing is saved.
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import headless  # noqa: E402

ROOT = headless.ROOT
TABLE = os.path.join(ROOT, 'docs', 'decomp', 'README.md')
RANGE = (0x80055E75, 0x800626E0)
NAME = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')


def read_rows(path):
    """(address, kind, name) for every row of the two tables; kind f or d."""
    rows, kind = [], None
    for line in open(path, encoding='utf-8'):
        if line.startswith('## '):
            kind = {'## Functions': 'f', '## Data': 'd'}.get(line.strip())
            continue
        m = re.match(r'\|\s*(0x[0-9A-Fa-f]{8})\s*\|\s*`([^`]+)`\s*\|', line)
        if kind and m:
            rows.append((int(m.group(1), 16), kind, m.group(2)))
    return rows


def check_rows(rows):
    problems = []
    addrs = [a for a, _, _ in rows]
    if len(set(addrs)) != len(addrs):
        problems.append('an address appears twice')
    names = [n for _, _, n in rows]
    if len(set(names)) != len(names):
        problems.append('a name appears twice')
    for a, _, n in rows:
        if not NAME.match(n):
            problems.append('0x%08X: name %r is not a plain identifier' % (a, n))
    for kind in 'fd':
        part = [a for a, k, _ in rows if k == kind]
        if part != sorted(part):
            problems.append('rows of kind %s are not sorted by address' % kind)
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--project', default=os.path.join(ROOT, 'out', 'ghidra'))
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    rows = read_rows(TABLE)
    problems = check_rows(rows)
    if problems:
        sys.exit('\n'.join(problems))
    project = os.path.abspath(a.project)
    rows_file = os.path.join(project, 'apply_names.rows.txt')
    report = os.path.join(project, 'apply_names.report.txt')
    with open(rows_file, 'w') as f:
        for addr, kind, name in rows:
            f.write('0x%08X %s %s\n' % (addr, kind, name))
    if os.path.exists(report):
        os.remove(report)
    mode = ['-readOnly'] if a.dry_run else []
    headless.run(project, 'kernel',
                 ['-process', 'kernel.img', '-noanalysis'] + mode,
                 [('ApplyNames.java', [rows_file, report, hex(RANGE[0]), hex(RANGE[1])])],
                 os.path.join(project, 'apply_names.log'))
    bad, unnamed, ok = [], [], 0
    for line in open(report):
        parts = line.split()
        if parts[0] == 'row':
            if parts[-1] == 'ok':
                ok += 1
            else:
                bad.append(line.strip())
        elif parts[0] == 'unnamed':
            unnamed.append(line.strip())
    inside = sum(1 for addr, k, _ in rows if k == 'f' and RANGE[0] <= addr < RANGE[1])
    print('%d rows applied, %d function rows in 0x%08X-0x%08X%s'
          % (ok, inside, RANGE[0], RANGE[1] - 1, ' (dry run, not saved)' if a.dry_run else ''))
    for line in bad + unnamed:
        print(line)
    if bad or unnamed:
        sys.exit(1)


if __name__ == '__main__':
    main()
