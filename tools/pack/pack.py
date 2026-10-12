"""Builds the data pack that the core reads: data/ -> out/pack/xboot.pak.

    python tools/pack/pack.py            write the pack, print its SHA-256
    python tools/pack/pack.py --check    also compare with tools/pack/xboot.pak.sha256

The pack holds the ROM ranges from data/ unchanged, sorted by start
address. Format (docs/core.md, "Datenpaket"), all words 32-bit
little-endian:

    0   magic "XBPK"
    4   format version 1
    8   number of entries
    12  total size of the pack in bytes
    16  entries, 32 bytes each:
          +0  start address in the unpacked kernel
          +4  length in bytes
          +8  offset in the pack, a multiple of 16
          +12 0
          +16 file name without extension, zero-padded to 16 bytes
    ... data of each entry at its offset; gaps are zero

Every input is checked against data/*/SHA256SUMS and, where the decoded
.json names a range ("source"), against that range. Nothing depends on
time, locale or file system order, so the pack is the same byte for byte
on every run.
"""

import hashlib
import json
import os
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'out', 'pack', 'xboot.pak')
EXPECTED = os.path.join(ROOT, 'tools', 'pack', 'xboot.pak.sha256')

MAGIC = b'XBPK'
VERSION = 1
ALIGN = 16

# (file in data/, start address, length, extractor that documents the range).
# The same list is in core/src/pack.c.
RANGES = [
    ('audio/gp_program.bin', 0x80053568, 0x1894, 'tools/extract/audio.py'),
    ('overview/asin_factors.bin', 0x80066030, 0xC4, 'tools/extract/overview.py'),
    ('audio/pcm_80066ac0.raw', 0x80066AC0, 0x5540, 'tools/extract/audio.py'),
    ('audio/pcm_8006c008.raw', 0x8006C008, 0xEB8, 'tools/extract/audio.py'),
    ('audio/pcm_8006cec8.raw', 0x8006CEC8, 0x1A3F, 'tools/extract/audio.py'),
    ('audio/sequence.bin', 0x8006E908, 0x6F8, 'tools/extract/audio.py'),
    ('camera/campath.bin', 0x80070DB8, 0x24C, 'tools/extract/chamber.py'),
    ('logo/logo.bin', 0x80071008, 0x33C8, 'tools/extract/logo.py'),
    ('chamber/scene.bin', 0x800743D0, 0x4134, 'tools/extract/chamber.py'),
    ('audio/envblocks.bin', 0x800A7980, 0x230, 'tools/extract/audio.py'),
    ('audio/seq_pointers.bin', 0x800A7BB0, 0x64, 'tools/extract/audio.py'),
]


def sums(folder):
    out = {}
    with open(os.path.join(ROOT, 'data', folder, 'SHA256SUMS'), encoding='ascii') as f:
        for line in f:
            digest, name = line.split()
            out[name] = digest
    return out


def source_range(path):
    """Range named by the decoded .json next to a .bin, or None."""
    js = os.path.splitext(path)[0] + '.json'
    if not os.path.exists(js):
        return None
    with open(js, encoding='utf-8') as f:
        src = json.load(f).get('source', '')
    if ', ' not in src:
        return None
    lo, hi = src.split(', ')[1].split('-')
    return int(lo, 16), int(hi, 16)


def load(rel, va, size):
    path = os.path.join(ROOT, 'data', rel)
    with open(path, 'rb') as f:
        data = f.read()
    folder, name = rel.split('/')
    digest = hashlib.sha256(data).hexdigest()
    if sums(folder).get(name) != digest:
        sys.exit(f'{rel}: SHA-256 does not match data/{folder}/SHA256SUMS')
    if len(data) != size:
        sys.exit(f'{rel}: {len(data)} bytes, expected {size:#x}')
    rng = source_range(path)
    if rng is not None and rng != (va, va + size):
        sys.exit(f'{rel}: .json names {rng[0]:#x}-{rng[1]:#x}, expected {va:#x}-{va + size:#x}')
    return data


def build():
    ranges = sorted(RANGES, key=lambda r: r[1])
    for a, b in zip(ranges, ranges[1:]):
        assert a[1] + a[2] <= b[1], 'overlapping ranges'
    head = 16 + 32 * len(ranges)
    offset = -(-head // ALIGN) * ALIGN
    entries, blobs = [], []
    for rel, va, size, _ in ranges:
        data = load(rel, va, size)
        stem = os.path.splitext(os.path.basename(rel))[0].encode('ascii')
        assert len(stem) <= 16
        entries.append(struct.pack('<IIII16s', va, size, offset, 0, stem))
        blobs.append((offset, data))
        offset = -(-(offset + size) // ALIGN) * ALIGN
    pak = bytearray(offset)
    pak[0:16] = MAGIC + struct.pack('<III', VERSION, len(ranges), offset)
    pak[16:head] = b''.join(entries)
    for off, data in blobs:
        pak[off:off + len(data)] = data
    return bytes(pak)


def main():
    pak = build()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'wb') as f:
        f.write(pak)
    digest = hashlib.sha256(pak).hexdigest()
    print(f'{digest}  {os.path.relpath(OUT, ROOT)}  {len(pak)} bytes')
    if '--check' in sys.argv[1:]:
        with open(EXPECTED, encoding='ascii') as f:
            want = f.read().split()[0]
        if want != digest:
            sys.exit(f'differs from {os.path.relpath(EXPECTED, ROOT)}: {want}')
        print('equal to', os.path.relpath(EXPECTED, ROOT))


if __name__ == '__main__':
    main()
