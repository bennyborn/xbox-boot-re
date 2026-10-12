/* Small helpers shared by the subsystems: docs/decomp/Overview.md, "Kleine
 * Helfer", and the SSE functions of Chamber.md 11.
 *
 * Two kinds of arithmetic, as in the original:
 *   - x87 helpers (vectors, matrices, quaternions) compute in double and
 *     store float32 where the code stores (Timing.md 4.2). Products of two
 *     float32 are exact in double, so only the order of the sums matters;
 *     it is the order of the disassembly, given at each function.
 *   - SSE helpers (sqrt, sin, asin, atan, acos) compute in float, one
 *     rounding per instruction. rsqrtss and rcpss are taken as exact
 *     1/sqrt(x) and 1/x in float32, which is what xemu does
 *     (target/i386/ops_sse.h; Chamber.md 11).
 */
#include "core_internal.h"

/* SSE constants, float32 at the address in the comment. core/test/check_k2.py
 * compares every K_* line of this file with out/rom/kernel.img. */
#define K_HALF_S    0.5f          /* 0x80065EA0 */
#define K_THREE_2   1.5f          /* 0x80065EB0 */
#define K_2_PI      0.63661975f   /* 0x80065EF0 */
#define K_ONE_S     1.0f          /* 0x80065E80 */
#define K_SIN_C0    1.5707964f    /* 0x80065F50 */
#define K_SIN_C1    -0.6459641f   /* 0x80065F54 */
#define K_SIN_C2    0.079692625f  /* 0x80065F58 */
#define K_SIN_C3    -0.004681754f /* 0x80065F5C */
#define K_AT_84     2.7692308f    /* 0x80065F84 */
#define K_AT_88     2.2727273f    /* 0x80065F88 */
#define K_AT_8C     25.0f         /* 0x80065F8C */
#define K_AT_90     16.0f         /* 0x80065F90 */
#define K_AT_94     11.0f         /* 0x80065F94 */
#define K_AT_98     9.0f          /* 0x80065F98 */
#define K_AT_9C     7.0f          /* 0x80065F9C */
#define K_AT_A0     5.0f          /* 0x80065FA0 */
#define K_AT_A4     4.0f          /* 0x80065FA4 */
#define K_AT_A8     3.0f          /* 0x80065FA8 */
#define K_AT_AC     1.0f          /* 0x80065FAC */
#define K_AS_STOP   1e-06f        /* 0x80065FB8 */
#define K_SLERP_EPS 1e-05f        /* 0x800129A0 */

#define BITS_PI_2      0x3FC90FDBu   /* pi/2, returned by asin, atan, acos */
#define BITS_MINUS_PI_2 0xBFC90FDBu
#define BITS_ONE       0x3F800000u
#define BITS_MINUS_ONE 0xBF800000u
#define BITS_SQRT_HALF 0x3F3504F3u   /* 0.7071, border of the asin series */
#define DEFAULT_NAN    0xFFC00000u   /* what x86 returns for an invalid operation */

/* ---- SSE ------------------------------------------------------------ */

static float rsqrt(float x)
{
    float r = 1.0f / __builtin_sqrtf(x);
    return r != r ? from_bits(DEFAULT_NAN) : r;
}

static float rcp(float x)
{
    return 1.0f / x;
}

/* cvttss2si: truncation; NaN and values out of range give 0x80000000. */
static int32_t cvtt(float x)
{
    if (!(x > -2147483648.0f && x < 2147483648.0f))
        return INT32_MIN;
    return (int32_t)x;
}

float sse_sqrt(float x)
{
    float r, a;

    if (fbits(x) == 0)
        return 0.0f;
    r = rsqrt(x);
    a = x * r * r * r * K_HALF_S;
    return (r * K_THREE_2 - a) * x;
}

float sse_sin(float x)
{
    uint32_t xb = fbits(x), sgn;
    float a = from_bits(xb & 0x7FFFFFFFu) * K_2_PI, fr, f2, p;
    int32_t k = cvtt(a);

    sgn = (((uint32_t)k << 30) & 0x80000000u) ^ (xb & 0x80000000u);
    fr = a - (float)k;
    if (k & 1)
        fr = K_ONE_S - fr;
    f2 = fr * fr;
    p = f2 * K_SIN_C3 + K_SIN_C2;
    p = p * f2 + K_SIN_C1;
    p = p * f2 + K_SIN_C0;
    return p * from_bits(fbits(fr) | sgn);
}

static float tail(float x1, float x2)
{
    float x7;

    x1 = x1 * x2 + K_AT_AC;
    x2 = rcp(x1);
    x7 = x2 * x2 * x1;
    return (x2 + x2) - x7;
}

float sse_atan(float x)
{
    uint32_t u = fbits(x);
    float x0, x1, x2, x6;

    if (u >= BITS_ONE) {        /* also every negative x: unsigned compare */
        x0 = rcp(x);
        x1 = x0 * x0;
        x2 = rcp(x1 * K_AT_84 + K_AT_94);
        x2 = rcp(x2 * (x1 * K_AT_8C) + K_AT_98);
        x2 = rcp(x2 * (x1 * K_AT_90) + K_AT_9C);
        x2 = rcp(x2 * (x1 * K_AT_98) + K_AT_A0);
        x2 = rcp(x2 * (x1 * K_AT_A4) + K_AT_A8);
        return from_bits(u >= BITS_MINUS_ONE ? BITS_MINUS_PI_2 : BITS_PI_2) - x0 * tail(x1, x2);
    }
    x0 = x;
    x1 = x0 * x0;
    x2 = x1 * K_AT_88 + K_AT_98;
    x6 = rcp(x2) * (x1 * K_AT_90) + K_AT_9C;
    x2 = rcp(x6) * (x1 * K_AT_98) + K_AT_A0;
    x6 = rcp(x2) * (x1 * K_AT_A4) + K_AT_A8;
    x2 = rcp(x6);
    return x0 * tail(x1, x2);
}

float sse_asin(const xb_state *s, float x)
{
    const uint8_t *fac = pack_at(s, PACK_ASIN, ASIN_FACTORS);
    uint32_t u = fbits(x);
    float cf = 1.0f, sum = 1.0f, p = 1.0f, x2, term;
    int k;

    if (u & 0x80000000u) {
        /* The code compares the masked value with the bounds for -1 and
         * -0.7071, which never holds: negative x always take the series. */
        if ((u & 0x7FFFFFFFu) == 0)
            return 0.0f;
    } else {
        if (u == 0)
            return 0.0f;
        if (u >= BITS_ONE)
            return from_bits(BITS_PI_2);
        if (u >= BITS_SQRT_HALF) {
            float y = 1.0f - x * x, r = rsqrt(y), a = y * r * r * r * K_HALF_S;
            return sse_atan((r * K_THREE_2 - a) * x);
        }
    }
    x2 = x * x;
    for (k = 0x30;;) {
        cf = cf * from_bits(rd_u32(fac + 4 * k));
        p = p * x2;
        term = cf * p;
        sum = sum + term;
        k--;
        if (!(term >= K_AS_STOP) || k == 0)
            break;
    }
    return sum * x;
}

/* Returns pi/2 - asin(x) unrounded, as the caller stores it. */
double sse_acos(const xb_state *s, float x)
{
    uint32_t u = fbits(x);

    if ((u & 0x7FFFFFFFu) == 0)
        return from_bits(BITS_PI_2);
    if (!(u & 0x80000000u) && u >= BITS_ONE)
        return 0.0;
    return (double)from_bits(BITS_PI_2) - sse_asin(s, x);
}

/* ---- vectors (x87) ---------------------------------------------------- */

float v3_len(const float v[3])
{
    return sse_sqrt(r32f((double)v[0] * v[0] + (double)v[1] * v[1] + (double)v[2] * v[2]));
}

void v3_norm(float v[3])
{
    double k = 1.0 / v3_len(v);
    int i;

    for (i = 0; i < 3; i++)
        v[i] = r32f(k * v[i]);
}

void v3_cross(const float a[3], const float b[3], float out[3])
{
    float o[3];

    o[0] = r32f((double)b[2] * a[1] - (double)a[2] * b[1]);
    o[1] = r32f((double)a[2] * b[0] - (double)a[0] * b[2]);
    o[2] = r32f((double)a[0] * b[1] - (double)b[0] * a[1]);
    out[0] = o[0];
    out[1] = o[1];
    out[2] = o[2];
}

void v3_sub(const float a[3], const float b[3], float out[3])
{
    int i;

    for (i = 0; i < 3; i++)
        out[i] = r32f((double)a[i] - b[i]);
}

void v3_add(const float a[3], const float b[3], float out[3])
{
    int i;

    for (i = 0; i < 3; i++)
        out[i] = r32f((double)a[i] + b[i]);
}

void v3_scale(float a[3], double k)
{
    int i;

    for (i = 0; i < 3; i++)
        a[i] = r32f((double)a[i] * k);
}

void v3_madd(float a[3], const float b[3], double k)
{
    int i;

    for (i = 0; i < 3; i++)
        a[i] = r32f((double)a[i] + (double)b[i] * k);
}

/* 0x8005667A, order of 0x80056682-0x800566C9. */
void v3_mul_m3(const float v[3], const float m[16], float out[3])
{
    float o[3];

    o[0] = r32f(((double)m[8] * v[2] + (double)m[4] * v[1]) + (double)v[0] * m[0]);
    o[1] = r32f(((double)m[1] * v[0] + (double)m[9] * v[2]) + (double)m[5] * v[1]);
    o[2] = r32f(((double)m[2] * v[0] + (double)m[10] * v[2]) + (double)m[6] * v[1]);
    out[0] = o[0];
    out[1] = o[1];
    out[2] = o[2];
}

/* ---- matrices (x87) ----------------------------------------------------- */

void m4_ident(float m[16])
{
    int i;

    for (i = 0; i < 16; i++)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

/* 0x800566CF: out = a * b. The four products of each element are added in
 * the order of k read from the disassembly (Chamber.md 11; MUL_ORDER in
 * tools/xemu/chamber_model.py). */
static const uint8_t mul_order[16][4] = {
    {0, 2, 1, 3}, {2, 0, 1, 3}, {0, 2, 1, 3}, {1, 3, 0, 2},
    {1, 2, 3, 0}, {0, 1, 2, 3}, {1, 2, 3, 0}, {0, 3, 2, 1},
    {1, 2, 3, 0}, {0, 1, 3, 2}, {1, 3, 2, 0}, {0, 3, 2, 1},
    {1, 2, 3, 0}, {0, 1, 3, 2}, {1, 3, 2, 0}, {0, 3, 2, 1}
};

void m4_mul(const float a[16], const float b[16], float out[16])
{
    float o[16];
    int e, j;

    for (e = 0; e < 16; e++) {
        int r = e / 4, c = e % 4, k = mul_order[e][0];
        double acc = (double)a[r * 4 + k] * b[k * 4 + c];

        for (j = 1; j < 4; j++) {
            k = mul_order[e][j];
            acc += (double)a[r * 4 + k] * b[k * 4 + c];
        }
        o[e] = r32f(acc);
    }
    for (e = 0; e < 16; e++)
        out[e] = o[e];
}

/* 0x8005B22E: inverse of a rotation with translation. */
void m4_inverse_rigid(const float m[16], float out[16])
{
    float o[16], t[3];

    o[0] = m[0]; o[1] = m[4]; o[2] = m[8];  o[3] = 0.0f;
    o[4] = m[1]; o[5] = m[5]; o[6] = m[9];  o[7] = 0.0f;
    o[8] = m[2]; o[9] = m[6]; o[10] = m[10]; o[11] = 0.0f;
    o[12] = o[13] = o[14] = 0.0f;
    o[15] = 1.0f;
    t[0] = -m[12];
    t[1] = -m[13];
    t[2] = -m[14];
    v3_mul_m3(t, o, o + 12);
    for (int i = 0; i < 16; i++)
        out[i] = o[i];
}

/* ---- quaternions (x87), q = (x, y, z, w) -------------------------------- */

/* 0x8005C790: rotation matrix, Chamber.md 5; 0x8005C665 is its transpose. */
void quat_to_m4(const float q[4], float m[16])
{
    double x = q[0], y = q[1], z = q[2], w = q[3];
    int i;

    for (i = 0; i < 16; i++)
        m[i] = 0.0f;
    m[0] = r32f(((w * w + x * x) - y * y) - z * z);
    m[4] = r32f((x * y + z * w) * 2.0);
    m[8] = r32f(z * x * 2.0 - w * y * 2.0);
    m[1] = r32f(x * y * 2.0 - z * w * 2.0);
    m[5] = r32f((y * y + (w * w - x * x)) - z * z);
    m[9] = r32f((z * y + x * w) * 2.0);
    m[2] = r32f((w * y + z * x) * 2.0);
    m[6] = r32f(z * y * 2.0 - x * w * 2.0);
    m[10] = r32f(z * z + ((w * w - x * x) - y * y));
    m[15] = 1.0f;
}

/* 0x8005E0B1, Chamber.md 6. */
void quat_slerp(const xb_state *s, const float a[4], const float b_in[4], float u, float out[4])
{
    double d = (double)a[3] * b_in[3] + (double)a[2] * b_in[2] + (double)a[1] * b_in[1] +
               (double)a[0] * b_in[0];
    float b[4], d32, th, sa, sb, o[4];
    double k;
    int i;

    for (i = 0; i < 4; i++)
        b[i] = b_in[i];
    if (d < 0.0) {
        for (i = 0; i < 4; i++)
            b[i] = -b[i];
        d = -d;
    }
    d32 = (float)d;
    if (__builtin_fabsf(r32f((double)d32 - 1.0)) <= K_SLERP_EPS) {
        double v = 1.0 - u;
        for (i = 0; i < 4; i++)
            out[i] = r32f((double)b[i] * u + v * a[i]);
        return;
    }
    th = (float)sse_acos(s, d32);
    sa = sse_sin(r32f((1.0 - u) * th));
    sb = sse_sin(r32f((double)th * u));
    for (i = 0; i < 4; i++)
        o[i] = r32f((double)sb * b[i] + (double)sa * a[i]);
    k = 1.0 / sse_sin(th);
    for (i = 0; i < 4; i++)
        out[i] = r32f(k * o[i]);
}
