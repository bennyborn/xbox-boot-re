"""One line of facts per function in an address range, for writing the table.

    python tools/rom/factsheet.py 0x80055e75 0x800626e0 > out/ghidra/c3_facts.txt

Per function, from out/rom/kernel.img and out/ghidra/kernel_graph.json:
size, stack cleanup of its ret, whether it reads ecx before writing it
(thiscall), callers, callees inside and outside the range, absolute data
addresses it touches, float32 constants it loads from the image, and
notable instructions (SSE, fsin/fcos/fsincos/fsqrt, rep string ops,
in/out). Facts only, no names; for local reading.
"""

import argparse
import collections
import json
import os
import re
import struct

import capstone

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BASE = 0x80010000
NOTABLE = ('fsin', 'fcos', 'fsincos', 'fsqrt', 'rsqrtss', 'sqrtss', 'in', 'out',
           'rdtsc', 'cpuid', 'int3', 'hlt')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('lo', type=lambda x: int(x, 0))
    ap.add_argument('hi', type=lambda x: int(x, 0))
    a = ap.parse_args()
    img = open(os.path.join(ROOT, 'out', 'rom', 'kernel.img'), 'rb').read()
    funcs = json.load(open(os.path.join(ROOT, 'out', 'ghidra', 'kernel_graph.json')))
    callers = collections.defaultdict(set)
    for f in funcs:
        for _, t in f['calls']:
            if t != 'indirect':
                callers[int(t, 16)].add(int(f['entry'], 16))
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = False

    def inrange(x):
        return a.lo <= x < a.hi

    for f in sorted(funcs, key=lambda f: int(f['entry'], 16)):
        e = int(f['entry'], 16)
        if not inrange(e):
            continue
        size = 0
        rets, notes, consts, absdata = set(), collections.Counter(), [], set()
        sse = False
        ecx_this = None
        for lo, hi in f['body']:
            lo, hi = int(lo, 16), int(hi, 16)
            size += hi - lo + 1
            for ins in md.disasm(img[lo - BASE:hi + 1 - BASE], lo):
                m, op = ins.mnemonic, ins.op_str
                if ecx_this is None:
                    # the first instruction mentioning ecx decides
                    if re.search(r'\becx\b|\bcl\b|\bcx\b', op):
                        dst = op.split(',')[0]
                        if m in ('mov', 'lea', 'pop', 'movzx', 'movsx') and re.fullmatch(r'(ecx|cl|cx)', dst.strip()):
                            ecx_this = False
                        elif m == 'xor' and op.replace(' ', '') == 'ecx,ecx':
                            ecx_this = False
                        elif m != 'call':
                            ecx_this = True
                if m == 'ret':
                    rets.add(op or '0')
                if m in NOTABLE or m.startswith('rep'):
                    notes[m if not m.startswith('rep') else m + ' ' + op.split()[0]] += 1
                if 'xmm' in op:
                    sse = True
                for v in re.findall(r'0x([0-9a-f]{8})', op):
                    v = int(v, 16)
                    if BASE <= v < BASE + len(img) and m != 'call' and not m.startswith('j'):
                        if m.startswith('f') and ('dword ptr' in op) and '[0x' in op:
                            val = struct.unpack_from('<f', img, v - BASE)[0]
                            consts.append(f'{v:08x}={val:.6g}')
                        else:
                            absdata.add(f'{v:08x}')
        inner, outer = [], []
        for _, t in f['calls']:
            if t == 'indirect':
                outer.append('ind')
            elif inrange(int(t, 16)):
                inner.append(t)
            else:
                outer.append(t)
        cs = sorted(callers[e])
        print(f'{e:08x} sz={size} ret={"/".join(sorted(rets)) or "-"} this={"y" if ecx_this else "n"}'
              f'{" sse" if sse else ""}'
              f' callers={",".join("%08x" % c for c in cs[:8])}{"+%d" % (len(cs) - 8) if len(cs) > 8 else ""}'
              f' in={",".join(sorted(set(inner)))}'
              f' out={",".join(sorted(set(outer)))}'
              f' data={",".join(sorted(absdata))}'
              f' k={",".join(sorted(set(consts)))}'
              f' ops={",".join(f"{k}x{v}" for k, v in notes.items())}')


if __name__ == '__main__':
    main()
