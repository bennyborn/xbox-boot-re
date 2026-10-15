"""Opens the BIOS: decrypts the 2BL, checks it, decrypts and unpacks the kernel.

    python tools/rom/unpack.py [--rom rom/bios_complex_4627_retail.bin]
                               [--mcpx rom/mcpx_1.0.bin] [--out out/rom]

Writes to the output folder:
    2bl.bin          decrypted 2BL, 0x6000 bytes, runs at 0x90000
    kernel.lzx       decrypted, still packed kernel chunks
    kernel.img       unpacked kernel image, loaded at 0x80010000
    kernel_data.bin  the plain block between kernel and 2BL
    unpack.json      offsets, sizes, checks and SHA-256 of every output

Every offset and size is read from the binary; where each one comes from is
documented in docs/decomp/Rom.md. Keys are never printed or written to the
JSON file.
"""

import argparse
import hashlib
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lzx  # noqa: E402

BANK = 0x40000           # one 256 KiB copy of the image (xboxdevwiki, BIOS)
BL_OFFSET = 0x39E00      # 2BL in the bank (xboxdevwiki, BIOS)
BL_SIZE = 0x6000         # MCPX 0xFFFFFE00+: PRGA loop count
MCPX_KEY_OFFSET = 0x1A5  # MCPX ROM offset of the 2BL key (xboxdevwiki, Boot Process)
MCPX_MD5 = "d49c52a4102f6df7bcf8d0617ac475ed"  # MCPX v1.0 (XboxBiosTool README)
BL_MAGIC = 0x7854794A    # MCPX 0xFFFFFF86: cmp eax, 0x7854794A

# Fields in the decrypted 2BL, read by the loader at 2BL 0x4007D0.
BL_ENTRY = 0x0000        # dword: entry point, MCPX 0xFFFFFF8D
BL_KERNEL_KEY = 0x008C   # 16 bytes: argument of 0x4007D0, pushed in 0x40061D
BL_DATA_SIZE = 0x5FDC    # dword: size of the plain block below the 2BL
BL_HEAD_SIZE = 0x5FE0    # dword: bytes hashed from the start of the ROM
BL_MAGIC_AT = 0x5FE4     # dword: checked by the MCPX
BL_KERNEL_SIZE = 0x5FE8  # dword: size of the packed kernel
BL_DIGEST = 0x5FEC       # 20 bytes: SHA-1 compared in 0x4007D0
BL_GAP = 0x6200          # 0x4007D0: data block ends 0x6200 below the ROM end

KERNEL_BASE = 0x80010000  # 0x4008F7: push 0x80010000 as the output address
WINDOW_BITS = 17          # 0x4006A5: window 0x20000


def rc4(key, data):
    s = list(range(256))
    j = 0
    for i in range(256):
        j = (j + s[i] + key[i % len(key)]) & 0xFF
        s[i], s[j] = s[j], s[i]
    i = j = 0
    out = bytearray(len(data))
    for n, c in enumerate(data):
        i = (i + 1) & 0xFF
        j = (j + s[i]) & 0xFF
        s[i], s[j] = s[j], s[i]
        out[n] = c ^ s[(s[i] + s[j]) & 0xFF]
    return bytes(out)


def dword(b, off):
    return struct.unpack_from("<I", b, off)[0]


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def open_rom(rom, mcpx):
    if len(rom) % BANK:
        raise ValueError("ROM size is not a multiple of 256 KiB")
    banks = [rom[i:i + BANK] for i in range(0, len(rom), BANK)]
    if any(b != banks[0] for b in banks):
        raise ValueError("ROM banks differ")
    bank = banks[-1]
    if len(mcpx) != 512 or hashlib.md5(mcpx).hexdigest() != MCPX_MD5:
        raise ValueError("MCPX dump is not the expected v1.0 image")

    bl = rc4(mcpx[MCPX_KEY_OFFSET:MCPX_KEY_OFFSET + 16], bank[BL_OFFSET:BL_OFFSET + BL_SIZE])
    if dword(bl, BL_MAGIC_AT) != BL_MAGIC:
        raise ValueError("2BL magic mismatch, wrong MCPX key?")

    kernel_size = dword(bl, BL_KERNEL_SIZE)
    data_size = dword(bl, BL_DATA_SIZE)
    head_size = dword(bl, BL_HEAD_SIZE)
    data_off = BANK - BL_GAP - data_size
    kernel_off = data_off - kernel_size
    key = bl[BL_KERNEL_KEY:BL_KERNEL_KEY + 16]

    # 0x4007D0: SHA-1 over key, then (size, bytes) of kernel, data block and
    # ROM head, then SHA-1 over key and that digest.
    p = lambda v: struct.pack("<I", v)
    inner = hashlib.sha1(key + p(kernel_size) + bank[kernel_off:kernel_off + kernel_size]
                         + p(data_size) + bank[data_off:data_off + data_size]
                         + p(head_size) + bank[:head_size]).digest()
    digest_ok = hashlib.sha1(key + inner).digest() == bl[BL_DIGEST:BL_DIGEST + 20]

    packed = rc4(key, bank[kernel_off:kernel_off + kernel_size])
    image, dec = lzx.decompress_chunks(packed, WINDOW_BITS)

    outputs = {
        "2bl.bin": bl,
        "kernel.lzx": packed,
        "kernel.img": image,
        "kernel_data.bin": bank[data_off:data_off + data_size],
    }
    report = {
        "rom_sha256": sha256(rom),
        "rom_banks": len(banks),
        "mcpx_md5": hashlib.md5(mcpx).hexdigest(),
        "bl": {
            "bank_offset": hex(BL_OFFSET),
            "size": hex(BL_SIZE),
            "entry": hex(dword(bl, BL_ENTRY)),
            "magic": hex(dword(bl, BL_MAGIC_AT)),
            "digest_ok": digest_ok,
        },
        "kernel": {
            "bank_offset": hex(kernel_off),
            "packed_size": hex(kernel_size),
            "chunks": dec.chunks,
            "e8_translation_size": None if dec.e8_size is None else hex(dec.e8_size),
            "matches_of_length_257": dec.long_matches,
            "image_size": hex(len(image)),
            "load_address": hex(KERNEL_BASE),
        },
        "kernel_data": {"bank_offset": hex(data_off), "size": hex(data_size)},
        "rom_head_hashed": hex(head_size),
        "sha256": {name: sha256(b) for name, b in outputs.items()},
    }
    return outputs, report


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.normpath(os.path.join(here, "..", ".."))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", default=os.path.join(root, "rom", "bios_complex_4627_retail.bin"))
    ap.add_argument("--mcpx", default=os.path.join(root, "rom", "mcpx_1.0.bin"))
    ap.add_argument("--out", default=os.path.join(root, "out", "rom"))
    a = ap.parse_args()

    with open(a.rom, "rb") as f:
        rom = f.read()
    with open(a.mcpx, "rb") as f:
        mcpx = f.read()
    outputs, report = open_rom(rom, mcpx)
    os.makedirs(a.out, exist_ok=True)
    for name, b in outputs.items():
        with open(os.path.join(a.out, name), "wb") as f:
            f.write(b)
    with open(os.path.join(a.out, "unpack.json"), "w") as f:
        json.dump(report, f, indent=2)
    print(json.dumps(report, indent=2))
    return 0 if report["bl"]["digest_ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
