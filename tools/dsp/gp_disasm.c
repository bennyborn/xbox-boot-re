/*
 * Static disassembly of the GP program with xemu's own disassembler
 * (dsp_dis.c.inc, reached through the static functions of dsp_cpu.c,
 * which this file includes). Own code; xemu's files come from out/dsp/src
 * (tools/dsp/build.py).
 *
 *   gpdis PROGRAM    one line per instruction: address, words, text
 */
#include "dsp_cpu.c"

#include "dsp.h"
#include "dsp_state.h"

static uint8_t *scratch;

static void scratch_rw(void *opaque, uint8_t *ptr, uint32_t addr, size_t len, bool dir)
{
    (void)opaque;
    if (dir) {
        memcpy(scratch + addr, ptr, len);
    } else {
        memcpy(ptr, scratch + addr, len);
    }
}

static void fifo_rw(void *opaque, uint8_t *ptr, unsigned int index, size_t len, bool dir)
{
    (void)opaque;
    (void)index;
    if (!dir) {
        memset(ptr, 0, len);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: gpdis PROGRAM\n");
        return 1;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    scratch = calloc(1, 0x2000 * 4);
    size_t n = fread(scratch, 1, 0x800 * 4, f) / 4;
    fclose(f);
    DSPState *dsp = dsp_init(NULL, scratch_rw, fifo_rw);
    dsp_bootstrap(dsp);
    dsp_core_t *core = &dsp->core;
    for (uint32_t pc = 0; pc < n;) {
        core->pc = pc;
        core->disasm_prev_inst_pc = 0xFFFFFFFF;
        uint16_t len = disasm_instruction(core, DSP_DISASM_MODE);
        if (len == 0) {
            len = 1;
        }
        printf("%04x  %s\n", pc, disasm_get_instruction_text(core));
        pc += len;
    }
    return 0;
}
