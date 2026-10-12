/*
 * Runs the GP program of the sound hardware outside of xemu, on xemu's
 * DSP56300 core (hw/xbox/mcpx/apu/dsp/{dsp.c,dsp_cpu.c,dsp_dma.c}, commit
 * fc24584, compiled by tools/dsp/build.py from ref/xemu/, not part of the
 * repo). This file and tools/dsp/stub/ are own code.
 *
 *   gp run PROGRAM IN OUT FRAMES [XDUMP]   process FRAMES frames
 *
 * PROGRAM: the program words as in the kernel (32-bit little endian, 24
 * bits used), data/audio/gp_program.bin. Like the kernel, the harness puts
 * them at offset 0 of the GP scratch memory; the bootstrap copies 0x800
 * words from there into P memory (dsp.c, dsp_bootstrap).
 *
 * IN: per frame 32 mix bins x 32 samples, float32 in [-1, 1), written to
 * X:0x1400 + bin * 32 + i as 24 bit like xemu (gp_ep.c, mcpx_apu_dsp_frame;
 * fpconv.h, float_to_24b). OUT: per frame 32 samples of X:0x1400 and 32 of
 * X:0x1420 (what xemu plays as left and right with audio.use_dsp on), as
 * float32 (24-bit value / 2^23). XDUMP: X memory 0..0x17FF after the last
 * frame, as 32-bit words (0x1000..0x13FF, outside X RAM, as 0).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsp.h"
#include "dsp_state.h"
#include "fpconv.h"

#define SCRATCH_SIZE (16u << 20)
static uint8_t *scratch;
static unsigned long fifo_bytes;

static void scratch_rw(void *opaque, uint8_t *ptr, uint32_t addr, size_t len, bool dir)
{
    (void)opaque;
    if ((size_t)addr + len > SCRATCH_SIZE) {
        fprintf(stderr, "scratch access out of range: %#x + %#zx\n", addr, len);
        exit(2);
    }
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
    fifo_bytes += len;
}

static DSPState *boot(const char *path, unsigned *nwords)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(1);
    }
    scratch = calloc(1, SCRATCH_SIZE);
    size_t n = fread(scratch, 1, 0x800 * 4, f);
    fclose(f);
    *nwords = (unsigned)(n / 4);
    DSPState *dsp = dsp_init(NULL, scratch_rw, fifo_rw);
    for (int i = 0; i < DSP_PRAM_SIZE; i++) {
        dsp->core.pram[i] = 0xCACACACA;
    }
    dsp->is_gp = true;
    dsp_reset(dsp);
    dsp_bootstrap(dsp);
    return dsp;
}

static int run_frame(DSPState *dsp)
{
    dsp_start_frame(dsp);
    dsp->core.is_idle = false;
    dsp->core.cycle_count = 0;
    for (int k = 0; k < 100000 && !dsp->core.is_idle; k++) {
        dsp_run(dsp, 1000);
    }
    return dsp->core.is_idle ? (int)dsp->core.cycle_count : -1;
}

static float from24(uint32_t v)
{
    int32_t s = (int32_t)(v << 8) >> 8;
    return (float)s / 8388608.0f;
}

int main(int argc, char **argv)
{
    if (argc >= 6 && !strcmp(argv[1], "run")) {
        unsigned n;
        DSPState *dsp = boot(argv[2], &n);
        FILE *in = fopen(argv[3], "rb"), *out = fopen(argv[4], "wb");
        if (!in || !out) {
            perror("in/out");
            return 1;
        }
        long frames = atol(argv[5]);
        static float bins[32][32];
        long maxc = 0, minc = -1;
        for (long fr = 0; fr < frames; fr++) {
            if (fread(bins, sizeof bins, 1, in) != 1) {
                memset(bins, 0, sizeof bins);
            }
            for (int b = 0; b < 32; b++) {
                for (int i = 0; i < 32; i++) {
                    dsp_write_memory(dsp, 'X', 0x1400 + b * 32 + i, float_to_24b(bins[b][i]));
                }
            }
            int c = run_frame(dsp);
            if (c < 0) {
                fprintf(stderr, "frame %ld did not reach idle\n", fr);
                return 3;
            }
            if (c > maxc) maxc = c;
            if (minc < 0 || c < minc) minc = c;
            float o[64];
            for (int i = 0; i < 32; i++) {
                o[i] = from24(dsp_read_memory(dsp, 'X', 0x1400 + i));
                o[32 + i] = from24(dsp_read_memory(dsp, 'X', 0x1420 + i));
            }
            fwrite(o, sizeof o, 1, out);
        }
        fclose(in);
        fclose(out);
        if (argc >= 7) {
            FILE *x = fopen(argv[6], "wb");
            for (int a = 0; a < 0x1800; a++) {
                /* X RAM ends at 0xFFF, the mix buffer starts at 0x1400 */
                uint32_t v = (a < 0x1000 || a >= 0x1400) ? dsp_read_memory(dsp, 'X', a) : 0;
                fwrite(&v, 4, 1, x);
            }
            fclose(x);
        }
        fprintf(stderr, "%ld frames, cycles per frame %ld..%ld, fifo bytes %lu\n",
                frames, minc, maxc, fifo_bytes);
        return 0;
    }
    fprintf(stderr, "usage: gp run PROGRAM IN OUT FRAMES [XDUMP]\n");
    return 1;
}
