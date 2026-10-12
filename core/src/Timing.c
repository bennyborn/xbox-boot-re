/* Time keeping, docs/decomp/Timing.md.
 *
 * Numerics as in Timing.md 4.2: every x87 operation is one double
 * operation, r32() marks where the original stores a float32. The order
 * of every sum and product follows the model tools/xemu/vectors_c3.py,
 * which meets both runs of vectors/c3_timing.json bit for bit.
 */
#include "core_internal.h"

/* float32 constants of the kernel, address of each in the comment.
 * core/test/check_k1.py compares them with out/rom/kernel.img. */
#define K_MS    0.001f   /* 0x800128A0 */
#define K_ONE   1.0f     /* 0x8001288C */
#define K_PI    3.14159265358979f /* 0x80065E4C */
#define K_END   8.0f     /* 0x80065E54 */
#define K_TWO   2.0f     /* 0x80065E58 */
#define K_08    0.8f     /* 0x80065E5C */
#define K_06    0.6f     /* 0x80065E60 */
#define K_BASE  0.0f     /* 0x80065E64 */
#define K_HALF  0.5f     /* 0x80010A18 */
#define K_003   0.03f    /* 0x800128C0 */
#define K_1_13  0.0769230769230769f /* 0x800128BC */
#define K_12    1.2f     /* 0x800128B8 */
#define K_005   0.05f    /* 0x800128B4 */
#define K_02    0.2f     /* 0x800128B0 */
#define K_04    0.4f     /* 0x800128AC */
#define K_01    0.1f     /* 0x80012888 */

/* 0x80056A60 and 0x80056A82: one draw, low 16 bits scaled. */
static double u01(uint32_t *rng)
{
    *rng = rng_step(*rng);
    return (double)(*rng & 0xFFFFu) * (1.0 / 65536.0);
}

static double pm1(uint32_t *rng)
{
    *rng = rng_step(*rng);
    return (double)(*rng & 0xFFFFu) * (1.0 / 32768.0) - 1.0;
}

/* 0x80056EF6: the pulse table at this+0x30 (Timing.md 4). */
static void pulse_table(timing_state *ts, uint32_t *rng)
{
    int i;

    for (i = 1; i <= PULSE_COUNT; i++) {
        float *e = ts->pulse[i - 1];
        double x, y, y32, q, w, a, t0, lo, u;

        x = pm1(rng) * K_003 + (double)i * K_1_13;
        y = 1.0 - ((x + 1.0) * x) * 0.5;
        y32 = r32(y);
        q = r32(K_12 - y);                 /* from y before rounding */
        u = u01(rng);
        w = (((u + 2.0) * q) * q) * K_005;
        if (K_01 > w)
            w = K_01;
        w = r32(w);
        u = u01(rng);
        a = r32(((u + 1.0) * (y32 + 0.5)) * K_02);
        t0 = r32(((((double)K_END - K_TWO) - K_04) - K_06) * y32 + K_06);
        lo = K_06 + w;
        e[0] = (float)(lo < t0 ? t0 : lo);
        e[1] = (float)w;
        e[2] = (float)a;
    }
    /* The last entry is overwritten (0x80056FF0-0x8005700D). */
    ts->pulse[PULSE_COUNT - 1][0] = (float)((double)K_06 + ts->pulse[PULSE_COUNT - 1][1]);
    ts->pulse[PULSE_COUNT - 1][2] = (float)((double)ts->pulse[PULSE_COUNT - 1][2] * 3.0);
}

void timing_start(timing_state *ts, uint32_t *rng, uint32_t flags, uint32_t tick)
{
    /* Constructor 0x80056ACA: t = 0, or 7.5 (0x40F00000) if the byte at
     * 0x8003AEB8 is not 0 (0x80056B5B-0x80056B6E). Timing.md 3. The
     * derived values start at 0, as both runs show in their first step. */
    ts->t = (flags & XB_START_SHORT) ? 7.5f : 0.0f;
    ts->x18 = ts->x1c = ts->x20 = ts->x24 = 0.0f;
    ts->arg = 0.0f;
    ts->steps = 0;

    /* Generator: seed 0x76543210 (0x80056B01); one draw for the camera
     * path (0x8005AFB6 -> 0x8005A6CB(-1), camera_start, before this call),
     * 36 for the table, one in the set-up (0x8005A41D -> 0x8005A14F).
     * Timing.md 4.1. */
    pulse_table(ts, rng);
    *rng = rng_step(*rng);

    /* Set-up 0x8005768C stores the current tick in this+0x28 and this+0x2C
     * at its end (0x80057932-0x8005793B). Timing.md 2. */
    ts->last_tick = tick;
    ts->setup_tick = tick;
}

/* 0x80057011: sum of a * cos(pi/2 * |t - t0| / w) over |t - t0| <= w. */
static double pulses(const timing_state *ts, float t)
{
    double sum = 0.0;
    int i;

    for (i = 0; i < PULSE_COUNT; i++) {
        double t0 = ts->pulse[i][0], w = ts->pulse[i][1], a = ts->pulse[i][2];
        double d = r32((double)t - t0), ph, s, c;

        if (d < 0.0)
            d = -d;                        /* 0x80057F89 */
        if (d > w)                         /* fcom; NaN would take the sum */
            continue;
        ph = r32(((double)K_PI / w) * d * K_HALF);
        x87_fsincos(ph, &s, &c);
        sum = r32(r32(c) * a + sum);
    }
    return sum;
}

/* The ramp of 0x800571AD (0x800571E5-0x80057245), on t before rounding. */
static double ramp(double t)
{
    double start = (double)K_06 + K_HALF, u;

    if (t < start)
        return 0.0;
    u = 1.0 / ((((double)K_END - K_TWO) - K_08) - start) * (t - start);
    return r32(K_BASE + (1.0 - K_BASE) * (((u + 1.0) * u) * 0.5));
}

/* 0x800571AD for the time fields. `arg` is the stored float32 argument. */
static int update(timing_state *ts, double arg)
{
    double t_reg, x20, h;

    if (arg > K_ONE)                       /* 0x800571BC */
        arg = K_MS;
    t_reg = arg + ts->t;                   /* fadd; fst keeps st0 unrounded */
    ts->t = (float)t_reg;
    /* this+0xC is 0 from the constructor (0x80056B52) on; no writer found. */
    ts->x1c = (float)ramp(t_reg);
    x20 = pulses(ts, ts->t) + ts->x1c;     /* argument of 0x80057011 is float32 */
    h = arg * 0.5;
    ts->x24 = (float)((1.0 - h) * ts->x24 + x20 * h);
    ts->x18 = (float)(x20 * arg + ts->x18);
    ts->x20 = (float)x20;
    ts->arg = (float)arg;
    ts->steps++;
    return ts->t >= K_END ? XB_DONE : XB_OK;
}

int timing_frame(timing_state *ts, uint32_t tick)
{
    uint32_t d, sign;
    double dm;

    /* Loop 0x800579B8 steps only while t < 8.0. */
    if (ts->t >= K_END)
        return XB_DONE;
    /* 0x80057952-0x80057961: d = |last - tick| as signed 32-bit (cdq,
     * xor, sub), then fild. Spelled out without signed overflow. */
    d = ts->last_tick - tick;
    sign = (d >> 31) ? 0xFFFFFFFFu : 0u;
    d = (d ^ sign) - sign;
    dm = (d >> 31) ? (double)d - 4294967296.0 : (double)d;
    /* arg = f32(dm * factor[this+0x1818] + this+0x14); both are 0 in every
     * frame of both runs, factor[0] = 1.0 (Timing.md 2). */
    dm = r32(dm * K_MS);
    ts->last_tick = tick;                  /* 0x800579B1, after update and draw */
    return update(ts, dm);
}

int timing_free(timing_state *ts, double dt)
{
    if (ts->t >= K_END)
        return XB_DONE;
    return update(ts, r32(dt));
}
