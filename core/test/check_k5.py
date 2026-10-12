"""K5 checks that need Python: constants, the guest's memory, the executable specification.

    python core/test/check_k5.py out/core/native/test_k5.exe

1. Every constant `#define K_* <value>f /* <address> */` in
   core/src/Textures.c is compared with the float32 at that address in
   out/rom/kernel.img (file offset = address - 0x80010000, Overview.md);
   every `B_*` with the immediate at its address, four bytes for imm32,
   one sign-extended byte for imm8.
2. The eleven textures (`test_k5 --dump`) byte for byte against the dumps
   probe_c8.py read from the guest's memory at the end of the set-up, in
   every recorded C8 run that has them (out/xemu/runs/c8_*/probe_c8.json,
   local only). A texture is found by the call site that created it, as in
   tools/xemu/vectors_c8.py.
3. The same textures against the model tools/xemu/texture_model.py.
"""

import json
import os
import re
import struct
import subprocess
import sys

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'xemu'))
import vectors_c8 as v8  # noqa: E402

KERNEL = os.path.join(ROOT, 'out', 'rom', 'kernel.img')
RUNS = os.path.join(ROOT, 'out', 'xemu', 'runs')
PACK = os.path.join(ROOT, 'out', 'pack', 'xboot.pak')
BASE = 0x80010000
TMP = os.path.join(ROOT, 'out', 'core', 'k5')
TEXTURES = 11

checks = failed = 0


def check(ok, what):
    global checks, failed
    checks += 1
    failed += not ok
    print(('ok    ' if ok else 'FAIL  ') + what)


def fb(x):
    return struct.unpack('<I', struct.pack('<f', float(x)))[0]


# ---------------------------------------------------------------- 1. constants

def constants():
    img = open(KERNEL, 'rb').read()
    src = open(os.path.join(ROOT, 'core', 'src', 'Textures.c'), encoding='utf-8').read()
    found = re.findall(r'#define\s+(K_\w+)\s+(-?[0-9.]+(?:e[-+]?[0-9]+)?)f\s+/\*\s*(0x[0-9A-Fa-f]{8})', src)
    bad = []
    for name, value, addr in found:
        rom = struct.unpack_from('<f', img, int(addr, 16) - BASE)[0]
        if fb(rom) != fb(np.float32(float(value))):
            bad.append(f'{name} {value} at {addr}: kernel {rom!r}')
    for b in bad:
        print('      ' + b)
    check(len(found) == 9 and not bad, f'{len(found)} constants of Textures.c equal the kernel image, {len(bad)} differ')
    imm = re.findall(r'#define\s+(B_\w+)\s+(0x[0-9A-Fa-f]+)u\s+/\*\s*(0x[0-9A-Fa-f]{8}), imm(8|32)', src)
    bad = []
    for name, value, addr, width in imm:
        off = int(addr, 16) - BASE
        if width == '32':
            got = struct.unpack_from('<I', img, off)[0]
        else:
            got = struct.unpack_from('<b', img, off)[0] & 0xFFFFFFFF
        if got != int(value, 16):
            bad.append(f'{name} {value} at {addr}: code {got:#x}')
    for b in bad:
        print('      ' + b)
    check(len(imm) == 38 and not bad, f'{len(imm)} immediates of Textures.c equal the code bytes, {len(bad)} differ')


# ---------------------------------------------------------------- 2. guest

def core_textures(exe):
    os.makedirs(TMP, exist_ok=True)
    subprocess.run([exe, PACK, '--dump', TMP], check=True)
    return [open(os.path.join(TMP, f'{i}.bin'), 'rb').read() for i in range(TEXTURES)]


def guest_textures(run):
    """The dumps of a run in the order of XB_TEX_*, found by the call site
    that created each resource (vectors_c8.check_textures)."""
    p = json.load(open(os.path.join(RUNS, run, 'probe_c8.json')))
    created = {}
    for c in p['calls']:
        if c.get('res'):
            created.setdefault(int(c['site'], 16), []).append(c['res'])
    data = {t['res']: t for t in p['textures']}
    out, used = [], {}
    for field, site, _, _ in v8.texture_cases(v8.fx_seed()):
        k = used.get(site, 0)
        used[site] = k + 1
        out.append((field, bytes.fromhex(data[created[site][k]]['data'])))
    return out


def against_guest(core):
    runs = sorted(r for r in os.listdir(RUNS) if r.startswith('c8_') and
                  os.path.exists(os.path.join(RUNS, r, 'probe_c8.json')))
    seen = 0
    for run in runs:
        try:
            guest = guest_textures(run)
        except (KeyError, IndexError):
            print(f'      {run}: not every texture was dumped, skipped')
            continue
        seen += 1
        bad = []
        for i, (field, g) in enumerate(guest):
            n = sum(1 for a, b in zip(core[i], g) if a != b) + abs(len(core[i]) - len(g))
            if n:
                bad.append(f'{field} {n} bytes')
        total = sum(len(g) for _, g in guest)
        check(not bad, f'{run}: eleven textures, {total} bytes, equal the guest\'s memory '
              f'({len(bad)} differ{": " + ", ".join(bad) if bad else ""})')
    if not seen:
        print('      no recorded C8 run present, textures not compared with the guest')


# ---------------------------------------------------------------- 3. model

def against_model(core):
    bad = []
    for i, (field, _, what, model) in enumerate(v8.texture_cases(v8.fx_seed())):
        if core[i] != model():
            bad.append(field)
    check(not bad, f'eleven textures equal the model texture_model.py{": " + str(bad) if bad else ""}')


def main():
    exe = os.path.abspath(sys.argv[1])
    constants()
    core = core_textures(exe)
    against_guest(core)
    against_model(core)
    print(f'{checks} checks, {failed} failed')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
