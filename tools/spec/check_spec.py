"""Checks the specification for completeness, for milestone C9.

    python tools/spec/check_spec.py [--verbose]

Reads docs/decomp/README.md, the narratives in docs/decomp/, docs/vectors/,
data/ and docs/provenance.md, and reports:

  rows       a table row whose address or name neither its narrative nor a
             narrative its description cites mentions, unless the row
             says "not needed"
  names      a name in backticks in a narrative that looks like a Ghidra
             label (lower case with an underscore, with a prefix that the
             table uses, such as gpu_ or seq_) but has no row
  sections   a reference "`X.md` 4.3" (or "X.md 4.3") to a section that
             does not exist
  data       a file named in the table that is missing, a file in data/
             that no document names, a SHA256SUMS that does not match
  vectors    a vector file without the run or script that made it, or one
             that provenance.md does not list
  guessed    every place marked *guessed* in docs/decomp/, and whether
             provenance.md lists it under "Guessed"

Exit code 1 if anything but "guessed" is reported. --verbose prints, for
every row, where its narrative mentions it.
"""

import argparse
import glob
import hashlib
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOCS = os.path.join(ROOT, 'docs')
DECOMP = os.path.join(DOCS, 'decomp')
TABLE = os.path.join(DECOMP, 'README.md')
NOT_NEEDED = 'not needed'
# Narratives that specify a subsystem; "Kernel" rows are the timer code of
# Timing.md and Audio.md (section 1) and the bus access of Overview.md.
NARRATIVE = {'Kernel': ['Timing.md', 'Audio.md', 'Overview.md']}
CITED = re.compile(r'\b([A-Z][A-Za-z]+\.md)\b')
ROW = re.compile(r'^\|\s*(0x[0-9A-Fa-f]{8})\s*\|\s*`([^`]+)`\s*\|([^|]*)\|\s*([^|]*?)\s*\|\s*(.*?)\s*\|\s*$')
LABEL = re.compile(r'`([a-z][a-z0-9]*(?:_[a-z0-9]+)+)`')
SECREF = re.compile(r'`?([A-Z][A-Za-z]+)\.md`?\s+((?:\d+(?:\.\d+)?)(?:(?:,\s*|\s+and\s+|\s+to\s+)\d+(?:\.\d+)?)*)')
HEADING = re.compile(r'^#{2,4}\s+(\d+(?:\.\d+)?)\.?\s')


def read_table():
    rows, kind = [], None
    for n, line in enumerate(open(TABLE, encoding='utf-8'), 1):
        if line.startswith('## '):
            kind = {'## Functions': 'f', '## Data': 'd'}.get(line.strip())
            continue
        m = ROW.match(line)
        if kind and m:
            rows.append(dict(line=n, kind=kind, addr=int(m.group(1), 16), name=m.group(2),
                             col3=m.group(3).strip(), sub=m.group(4), desc=m.group(5)))
    return rows


def narratives():
    out = {}
    for p in sorted(glob.glob(os.path.join(DECOMP, '*.md'))):
        if os.path.basename(p) != 'README.md':
            out[os.path.basename(p)] = open(p, encoding='utf-8').read()
    return out


def mentions(text, row):
    """Lines of text that name the row by address or label."""
    pat = re.compile(r'0x0*%X\b|`%s`' % (row['addr'], re.escape(row['name'])), re.I)
    return [i for i, l in enumerate(text.splitlines(), 1) if pat.search(l)]


def check_rows(rows, texts, verbose):
    out = []
    for r in rows:
        files = NARRATIVE.get(r['sub'], [r['sub'] + '.md'])
        files = files + [f for f in CITED.findall(r['desc']) if f not in files]
        missing = [f for f in files if f not in texts]
        if missing and len(missing) == len(files):
            out.append('rows: README.md:%d %s: no narrative %s' % (r['line'], r['name'], files))
            continue
        hits = {f: mentions(texts[f], r) for f in files if f in texts}
        if any(hits.values()):
            if verbose:
                print('  %08X %-32s %s' % (r['addr'], r['name'],
                      ', '.join('%s:%s' % (f, ','.join(map(str, h[:4]))) for f, h in hits.items() if h)))
            continue
        if NOT_NEEDED in r['desc']:
            continue
        out.append('rows: README.md:%d 0x%08X %s (%s) not in %s'
                   % (r['line'], r['addr'], r['name'], r['sub'], '/'.join(files)))
    return out


def json_keys(d, keys):
    if isinstance(d, dict):
        keys.update(d)
        for v in d.values():
            json_keys(v, keys)
    elif isinstance(d, list):
        for v in d:
            json_keys(v, keys)
    return keys


def check_names(rows, texts):
    """Labels without a row; keys of the vector files and lines that cite
    xemu's source (names of xemu functions) are not labels."""
    known = {r['name'] for r in rows}
    for p in glob.glob(os.path.join(DOCS, 'vectors', '*.json')):
        json_keys(json.load(open(p, encoding='utf-8')), known)
    prefixes = {r['name'].split('_')[0] for r in rows}
    out = []
    for f, text in texts.items():
        for i, l in enumerate(text.splitlines(), 1):
            if 'xemu' in l:
                continue
            for m in LABEL.finditer(l):
                name = m.group(1)
                if name not in known and name.split('_')[0] in prefixes:
                    out.append('names: %s:%d `%s` has no row' % (f, i, m.group(1)))
    return out


def sections(text):
    return {m.group(1) for m in map(HEADING.match, text.splitlines()) if m}


def check_sections(texts):
    files = glob.glob(os.path.join(DECOMP, '*.md')) + [os.path.join(DOCS, 'provenance.md')]
    heads = {f: sections(t) for f, t in texts.items()}
    out = []
    for p in sorted(files):
        for i, l in enumerate(open(p, encoding='utf-8'), 1):
            for m in SECREF.finditer(l):
                target = m.group(1) + '.md'
                if target not in heads:
                    continue
                for num in re.findall(r'\d+(?:\.\d+)?', m.group(2)):
                    if num not in heads[target]:
                        out.append('sections: %s:%d %s %s does not exist'
                                   % (os.path.relpath(p, DOCS), i, target, num))
    return out


def check_data(rows):
    out = []
    named = set()
    for r in rows:
        if r['kind'] == 'd' and r['col3'] not in ('', '–', '-'):
            f = r['col3'].strip('`')
            named.add(f)
            if not os.path.exists(os.path.join(ROOT, 'data', f)):
                out.append('data: README.md:%d data/%s missing' % (r['line'], f))
    alldocs = ''
    for p in glob.glob(os.path.join(DOCS, '**', '*.md'), recursive=True):
        alldocs += open(p, encoding='utf-8').read()
    for p in sorted(glob.glob(os.path.join(ROOT, 'data', '*', '*'))):
        rel = os.path.relpath(p, os.path.join(ROOT, 'data')).replace(os.sep, '/')
        base = os.path.basename(p)
        if base == 'SHA256SUMS':
            for line in open(p, encoding='utf-8'):
                digest, fname = line.split(None, 1)
                fname = fname.strip().lstrip('*')
                q = os.path.join(os.path.dirname(p), fname)
                if not os.path.exists(q):
                    out.append('data: %s lists missing %s' % (rel, fname))
                elif hashlib.sha256(open(q, 'rb').read()).hexdigest() != digest:
                    out.append('data: %s does not match %s' % (fname, rel))
            continue
        if rel not in named and rel not in alldocs and base not in alldocs:
            out.append('data: data/%s is named in no document' % rel)
    return out


def vector_source(d):
    """True if a vector file names the run or script that made it."""
    text = json.dumps(d)[:200000]
    return bool(re.search(r'"(run|runs|script|source|command)"', text))


def check_vectors():
    prov = open(os.path.join(DOCS, 'provenance.md'), encoding='utf-8').read()
    out = []
    for p in sorted(glob.glob(os.path.join(DOCS, 'vectors', '*.json'))):
        name = os.path.basename(p)
        d = json.load(open(p, encoding='utf-8'))
        if not vector_source(d):
            out.append('vectors: %s names no run or script' % name)
        if 'vectors/' + name not in prov:
            out.append('vectors: %s not listed in provenance.md' % name)
    return out


def check_guessed(texts):
    prov = open(os.path.join(DOCS, 'provenance.md'), encoding='utf-8').read()
    guessed = prov.split('## Guessed', 1)[1] if '## Guessed' in prov else ''
    out = []
    for f, text in texts.items():
        for i, l in enumerate(text.splitlines(), 1):
            if '*guessed*' in l:
                listed = f in guessed
                out.append('guessed: %s:%d %s' % (f, i, 'listed' if listed else 'NOT LISTED in provenance.md'))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--verbose', action='store_true')
    a = ap.parse_args()
    rows = read_table()
    texts = narratives()
    problems = (check_rows(rows, texts, a.verbose) + check_names(rows, texts)
                + check_sections(texts) + check_data(rows) + check_vectors())
    guessed = check_guessed(texts)
    for line in problems + guessed:
        print(line)
    print('%d rows (%d functions, %d data), %d problems, %d places marked guessed'
          % (len(rows), sum(r['kind'] == 'f' for r in rows), sum(r['kind'] == 'd' for r in rows),
             len(problems), len(guessed)))
    sys.exit(1 if problems else 0)


if __name__ == '__main__':
    main()
