/* Stand-ins for x87 instructions the core has no library for.
 *
 * fsincos (Timing.md 4, pulse_sum 0x80057011; Overview.md, "Kleine
 * Helfer"). xemu evaluates it as sin and cos of the host's C library in
 * double precision (target/i386/tcg/fpu_helper.c, helper_fsincos; see
 * docs/core.md 4). Every caller in the animation stores the result as
 * float32. So the core needs sin and cos accurate to well below one ulp of
 * double; the rounding to float32 then matches except for arguments whose
 * value lies almost exactly between two floats. How often that happens is
 * measured over every float32 argument the animation can pass
 * (core/test/sweep_k1.c, docs/core.md 4).
 *
 * Method: reduce x by the nearest multiple n of pi/2, then a Taylor series
 * on |r| <= pi/4. Constants from core/test/gen_trig.py.
 */
#include "core_internal.h"

/* pi/2 in three parts; hi and mid have 33 significant bits, so n*hi and
 * n*mid are exact for |n| < 2^20. 2/pi is the nearest double. */
#define PIO2_HI  0x1.921fb54400000p+0
#define PIO2_MID 0x1.0b4611a600000p-34
#define PIO2_LO  0x1.3198a2e037073p-69
#define INV_PIO2 0x1.45f306dc9c883p-1

/* Above this the reduction loses exactness (|n| >= 2^20). */
#define REDUCE_MAX 1.6e6

/* sin r for |r| <= pi/4: r + r^3 * P(r^2), Taylor terms up to r^21. The
 * first omitted term is below (pi/4)^23 / 23! < 4e-26. */
static double sin_kernel(double r)
{
    double z = r * r;
    double p = -1.0 / 51090942171709440000.0;            /* -1/21! */
    p = p * z + 1.0 / 121645100408832000.0;               /*  1/19! */
    p = p * z - 1.0 / 355687428096000.0;                  /* -1/17! */
    p = p * z + 1.0 / 1307674368000.0;                    /*  1/15! */
    p = p * z - 1.0 / 6227020800.0;                       /* -1/13! */
    p = p * z + 1.0 / 39916800.0;                         /*  1/11! */
    p = p * z - 1.0 / 362880.0;                           /* -1/9!  */
    p = p * z + 1.0 / 5040.0;                             /*  1/7!  */
    p = p * z - 1.0 / 120.0;                              /* -1/5!  */
    p = p * z + 1.0 / 6.0;                                /*  1/3!  */
    return r - (r * z) * p;
}

/* cos r for |r| <= pi/4: 1 - r^2/2 + r^4 * Q(r^2), Taylor terms up to
 * r^20 (first omitted term < 5e-24). The leading 1 - r^2/2 is formed with
 * its rounding error carried along, so the sum stays within an ulp. */
static double cos_kernel(double r)
{
    double z = r * r;
    double h = 0.5 * z;
    double w = 1.0 - h;
    double q = 1.0 / 2432902008176640000.0;               /*  1/20! */
    q = q * z - 1.0 / 6402373705728000.0;                 /* -1/18! */
    q = q * z + 1.0 / 20922789888000.0;                   /*  1/16! */
    q = q * z - 1.0 / 87178291200.0;                      /* -1/14! */
    q = q * z + 1.0 / 479001600.0;                        /*  1/12! */
    q = q * z - 1.0 / 3628800.0;                          /* -1/10! */
    q = q * z + 1.0 / 40320.0;                            /*  1/8!  */
    q = q * z - 1.0 / 720.0;                              /* -1/6!  */
    q = q * z + 1.0 / 24.0;                               /*  1/4!  */
    return w + (((1.0 - w) - h) + (z * z) * q);
}

int x87_fsincos(double x, double *s, double *c)
{
    double fn, r, sr, cr;
    int32_t n;

    if (!(x > -REDUCE_MAX && x < REDUCE_MAX)) {   /* also NaN; no caller gets here */
        double zero = 0.0;
        *s = *c = zero / zero;
        return -1;
    }
    n = (int32_t)(x * INV_PIO2 + (x < 0.0 ? -0.5 : 0.5));
    fn = (double)n;
    r = ((x - fn * PIO2_HI) - fn * PIO2_MID) - fn * PIO2_LO;
    sr = sin_kernel(r);
    cr = cos_kernel(r);
    switch (n & 3) {
    case 0:  *s = sr;  *c = cr;  break;
    case 1:  *s = cr;  *c = -sr; break;
    case 2:  *s = -sr; *c = -cr; break;
    default: *s = -cr; *c = sr;  break;
    }
    return 0;
}
