"""Imports the unpacked kernel into a fresh Ghidra project and checks it.

    python tools/ghidra/import_kernel.py [--image out/rom/kernel.img]

Run tools/rom/unpack.py first. The project lands in out/ghidra/ (Ghidra
refuses project paths with a folder that starts with a dot, so not .tools/).
The prescript NoForeignNames.java turns off the analyzers that name things
from Ghidra's signature databases. The postscript DumpLayout.java writes
out/ghidra/kernel_layout.json, which this script compares with the PE
headers: image base, and one block per section at base + RVA with the
section's size and permissions.
"""

import argparse
import json
import os
import sys

import pefile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import headless  # noqa: E402

ROOT = headless.ROOT

IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_READ = 0x40000000
IMAGE_SCN_MEM_WRITE = 0x80000000


def expected_layout(path):
    pe = pefile.PE(path, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    sections = []
    for s in pe.sections:
        c = s.Characteristics
        sections.append({
            "name": s.Name.rstrip(b"\0").decode("ascii"),
            "start": base + s.VirtualAddress,
            "size": s.Misc_VirtualSize,
            "perm": ("r" if c & IMAGE_SCN_MEM_READ else "-")
                    + ("w" if c & IMAGE_SCN_MEM_WRITE else "-")
                    + ("x" if c & IMAGE_SCN_MEM_EXECUTE else "-"),
        })
    return base, sections


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--image", default=os.path.join(ROOT, "out", "rom", "kernel.img"))
    ap.add_argument("--project", default=os.path.join(ROOT, "out", "ghidra"))
    a = ap.parse_args()

    project = os.path.abspath(a.project)
    layout_json = os.path.join(project, "kernel_layout.json")
    if os.path.exists(layout_json):
        os.remove(layout_json)
    headless.run(project, "kernel",
                 ["-import", os.path.abspath(a.image), "-loader", "PeLoader"],
                 [("DumpLayout.java", [layout_json])],
                 os.path.join(project, "import_kernel.log"))
    with open(layout_json) as f:
        got = json.load(f)

    base, sections = expected_layout(a.image)
    blocks = {b["name"]: b for b in got["blocks"]}
    problems = []
    if int(got["image_base"], 16) != base:
        problems.append("image base %s, expected 0x%x" % (got["image_base"], base))
    for s in sections:
        b = blocks.get(s["name"])
        if b is None:
            problems.append("no block for section %s" % s["name"])
            continue
        if int(b["start"], 16) != s["start"]:
            problems.append("%s starts at %s, expected 0x%x" % (s["name"], b["start"], s["start"]))
        if int(b["size"], 16) < s["size"]:
            problems.append("%s has 0x%x bytes, expected at least 0x%x"
                            % (s["name"], int(b["size"], 16), s["size"]))
        if b["perm"] != s["perm"]:
            problems.append("%s has %s, expected %s" % (s["name"], b["perm"], s["perm"]))

    print(json.dumps(got, indent=2))
    if problems:
        print("MISMATCH:\n  " + "\n  ".join(problems))
        return 1
    print("OK: image base 0x%x and %d sections match the PE headers" % (base, len(sections)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
