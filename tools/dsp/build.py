"""Builds out/dsp/gp.exe: the GP harness on xemu's DSP56300 core.

    python tools/dsp/build.py

Copies the DSP core of xemu (commit fc24584, from ref/xemu/, see
docs/provenance.md) to out/dsp/src/ under its original names and compiles
it with gcc against the stand-in headers in tools/dsp/stub/ and the own
harness tools/dsp/gp_harness.c, and out/dsp/gpdis.exe (tools/dsp/gp_disasm.c). Nothing of xemu goes into the repo.
"""

import os
import shutil
import subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
REF = os.path.join(ROOT, 'ref', 'xemu')
SRC = os.path.join(ROOT, 'out', 'dsp', 'src')
FILES = ['dsp.c', 'dsp_cpu.c', 'dsp_dma.c', 'dsp.h', 'dsp_cpu.h', 'dsp_dma.h', 'dsp_state.h',
         'dsp_cpu_regs.h', 'dsp_dma_regs.h', 'dsp_emu.c.inc', 'dsp_dis.c.inc']


def main():
    os.makedirs(SRC, exist_ok=True)
    for f in FILES:
        shutil.copyfile(os.path.join(REF, 'hw_xbox_mcpx_apu_dsp_' + f), os.path.join(SRC, f))
    shutil.copyfile(os.path.join(REF, 'hw_xbox_mcpx_apu_fpconv.h'), os.path.join(SRC, 'fpconv.h'))
    shutil.copyfile(os.path.join(REF, 'hw_xbox_mcpx_apu_dsp_debug.h'), os.path.join(SRC, 'debug.h'))
    # dsp_dma.c compares count * item_size (unsigned) with a static ssize_t
    # that starts at -1; with a 32-bit ssize_t (this MinGW) the comparison is
    # unsigned and the buffer is never allocated. Widen the variable.
    dma = os.path.join(SRC, 'dsp_dma.c')
    text = open(dma).read()
    old = 'static ssize_t scratch_buf_size = -1;'
    assert old in text
    open(dma, 'w').write(text.replace(old, 'static long long scratch_buf_size = -1;'))
    stub = os.path.join(ROOT, 'tools', 'dsp', 'stub')
    exe = os.path.join(ROOT, 'out', 'dsp', 'gp.exe')
    cmd = ['gcc', '-std=gnu11', '-O2', '-I' + stub, '-I' + SRC,
           os.path.join(ROOT, 'tools', 'dsp', 'gp_harness.c')] + \
          [os.path.join(SRC, f) for f in ('dsp.c', 'dsp_cpu.c', 'dsp_dma.c')] + ['-o', exe, '-lm']
    subprocess.check_call(cmd)
    dis = os.path.join(ROOT, 'out', 'dsp', 'gpdis.exe')
    subprocess.check_call(['gcc', '-std=gnu11', '-O2', '-I' + stub, '-I' + SRC,
                           os.path.join(ROOT, 'tools', 'dsp', 'gp_disasm.c'),
                           os.path.join(SRC, 'dsp.c'), os.path.join(SRC, 'dsp_dma.c'),
                           '-o', dis, '-lm'])
    print(os.path.relpath(exe, ROOT), os.path.relpath(dis, ROOT))


if __name__ == '__main__':
    main()
