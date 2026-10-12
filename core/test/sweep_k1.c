/* Measures the core's fsincos (core/src/x87.c) over every float32 argument
 * the pulses of Timing.md 4 can pass, native x86-64 only:
 *
 *     sweep_k1 [last_bits]
 *
 * The argument ph = f32(pi / w * d * 0.5) with 0 <= d <= w lies in
 * [0, f32(pi/2)] = bit patterns 0 .. 0x3FC90FDB; the default sweep goes a
 * little beyond, to 0x3FC90FE0. For each argument, sin and cos rounded to
 * float32 (the pulses store them so, 0x80057077-0x80057079) from:
 *
 *   core  x87_fsincos in double,
 *   crt   sin and cos of the C library of this host in double, which is
 *         what xemu does for fsincos (helper_fsincos, docs/core.md 4),
 *   x87   the fsincos instruction of this CPU, stored with fstp m32.
 *
 * Counts the arguments where two of them differ, and prints the first few.
 */
#include "core_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static void x87(float x, float *s, float *c)
{
    __asm__ volatile("flds %2\n\tfsincos\n\tfstps %1\n\tfstps %0"
                     : "=m"(*s), "=m"(*c) : "m"(x) : "st", "st(1)");
}

int main(int argc, char **argv)
{
    uint32_t last = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 0) : 0x3FC90FE0u, u;
    unsigned long long n = 0;
    unsigned long long d_core_crt[2] = { 0, 0 }, d_core_x87[2] = { 0, 0 }, d_crt_x87[2] = { 0, 0 };
    int shown = 0;
    clock_t t0 = clock();

    for (u = 0; u <= last; u++) {
        float x = from_bits(u), xs, xc;
        double s, c;
        float cs, cc, rs, rc;

        x87_fsincos(x, &s, &c);
        cs = (float)s;
        cc = (float)c;
        rs = (float)sin((double)x);
        rc = (float)cos((double)x);
        x87(x, &xs, &xc);
        n++;
        d_core_crt[0] += bits(cs) != bits(rs);
        d_core_crt[1] += bits(cc) != bits(rc);
        d_core_x87[0] += bits(cs) != bits(xs);
        d_core_x87[1] += bits(cc) != bits(xc);
        d_crt_x87[0] += bits(rs) != bits(xs);
        d_crt_x87[1] += bits(rc) != bits(xc);
        if ((bits(cc) != bits(rc) || bits(cc) != bits(xc) || bits(cs) != bits(rs) ||
             bits(cs) != bits(xs)) && shown++ < 12)
            printf("  x = %08X (%.9g): cos core %08X crt %08X x87 %08X | sin core %08X crt %08X x87 %08X\n",
                   (unsigned)u, (double)x, (unsigned)bits(cc), (unsigned)bits(rc), (unsigned)bits(xc),
                   (unsigned)bits(cs), (unsigned)bits(rs), (unsigned)bits(xs));
        if (u == last)
            break;
    }
    printf("arguments 0x00000000 .. 0x%08X: %llu, %.1f s\n", (unsigned)last, n,
           (double)(clock() - t0) / CLOCKS_PER_SEC);
    printf("                 cos differs   sin differs\n");
    printf("core vs crt      %11llu   %11llu\n", d_core_crt[1], d_core_crt[0]);
    printf("core vs x87      %11llu   %11llu\n", d_core_x87[1], d_core_x87[0]);
    printf("crt  vs x87      %11llu   %11llu\n", d_crt_x87[1], d_crt_x87[0]);
    return 0;
}
