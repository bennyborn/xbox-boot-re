"""Runs Ghidra headless for this line and refuses runs that went wrong.

Ghidra keeps analysing with its defaults when a prescript fails, which would
bring back names from its signature databases. So every run is checked: no
script error in the log, and the prescript must have switched Function ID off.
"""

import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
HEADLESS = os.path.join(ROOT, ".tools", "ghidra-headless.bat")


class GhidraRunError(Exception):
    pass


def run(project_dir, project_name, import_args, post_scripts, log):
    """import_args: loader options after -import <file>.
    post_scripts: list of (script name, [args])."""
    os.makedirs(project_dir, exist_ok=True)
    cmd = [HEADLESS, project_dir, project_name] + import_args + [
        "-overwrite", "-scriptPath", HERE, "-preScript", "NoForeignNames.java"]
    for name, args in post_scripts:
        cmd += ["-postScript", name] + list(args)
    with open(log, "w") as f:
        rc = subprocess.call(cmd, stdout=f, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    with open(log, errors="replace") as f:
        text = f.read()
    if rc != 0:
        raise GhidraRunError("Ghidra exit %d, see %s" % (rc, log))
    if "SCRIPT ERROR" in text:
        raise GhidraRunError("script error, see %s" % log)
    if "analyzer: Function ID = false" not in text:
        raise GhidraRunError("Function ID was not switched off, see %s" % log)
