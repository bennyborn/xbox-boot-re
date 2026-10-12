"""Imports the decrypted 2BL into a fresh Ghidra project.

    python tools/ghidra/import_2bl.py [--image out/rom/2bl.bin]

Run tools/rom/unpack.py first. The 2BL is a raw image without headers. It
is loaded at 0x400000, where it copies itself and runs from 0x40061D on
(2BL 0x902DA..0x903B1). The stub before paging runs from 0x90000 instead;
in this project its code lies at the same offset above 0x400000.

The decompilation of every function goes to out/ghidra/2bl.c for local
reading only; it is never committed. Every function must keep Ghidra's
default name; anything else means a name came from outside the binary.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import headless  # noqa: E402

BASE = 0x400000
ENTRIES = [0x40024C, 0x40061D]  # stub (dword 0 of the 2BL, minus 0x90000) and main


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--image", default=os.path.join(headless.ROOT, "out", "rom", "2bl.bin"))
    ap.add_argument("--project", default=os.path.join(headless.ROOT, "out", "ghidra"))
    a = ap.parse_args()

    project = os.path.abspath(a.project)
    listing = os.path.join(project, "2bl.c")
    if os.path.exists(listing):
        os.remove(listing)
    headless.run(project, "bl2",
                 ["-import", os.path.abspath(a.image),
                  "-loader", "BinaryLoader", "-loader-baseAddr", hex(BASE),
                  "-processor", "x86:LE:32:default", "-cspec", "windows"],
                 [("DumpDecomp.java", [listing] + [hex(e) for e in ENTRIES])],
                 os.path.join(project, "import_2bl.log"))
    with open(listing) as f:
        heads = [line.split()[2:] for line in f if line.startswith("// ==== ")]
    foreign = [h for h in heads if not h[1].startswith("FUN_")]
    if foreign:
        print("FOREIGN NAMES at: " + ", ".join(h[0] for h in foreign))
        return 1
    print("OK: %d functions, all with default names; decompilation in %s" % (len(heads), listing))
    return 0


if __name__ == "__main__":
    sys.exit(main())
