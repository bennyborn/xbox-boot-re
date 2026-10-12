/* Textures the set-up computes on the CPU, docs/decomp/Textures.md.
 *
 * Ten textures from six generators and TM from the ROM (Textures.md 1, 4),
 * eleven in all, generated on request into a host buffer in the layout the GPU
 * reads (2): levels one after another, cube faces aligned to 128 bytes,
 * texels in the order of the swizzled copy 0x800AAB46 (3).
 *
 * The original computes each image row by row into a buffer and copies it
 * swizzled. The core writes every texel straight to its swizzled place, so
 * it needs no buffer of its own: the height fields of 4.7 and 4.8 live in
 * the host buffer, addressed through the swizzle, and the normal map of 4.7
 * reads only texels that come later in row order, as the original does in
 * its own buffer.
 *
 * Numerics as in Overview.c: double for the x87, r32f() where the code
 * stores, trunc_ss() for cvttss2si.
 */
#include "core_internal.h"

/* float32 constants of the kernel, address of each in the comment.
 * core/test/check_k5.py compares them with out/rom/kernel.img. */
#define K_ARGB      127.5f          /* 0x800129C0 */
#define K_ONE       1.0f            /* 0x8001288C */
#define K_BYTE      0.00392156886f  /* 0x800129CC, 1/255 */
#define K_255       255.0f          /* 0x80012908 */
#define K_ANGLE     0.00390625f     /* 0x800129C8, 1/256 */
#define K_INDEX     256.0f          /* 0x800129C4 */
#define K_TENTH     0.100000001f    /* 0x80012888 */
#define K_BUMP      0.001953125f    /* 0x800128E4, 1/512, argument of texgen_fractal */
#define K_HALF      0.5f            /* 0x80010A18, both factors of texgen_radial */

/* Immediates, address of their bytes in the code; imm8 are sign-extended.
 * core/test/check_k5.py compares them with the code bytes. */
/* set-up 0x8005768C, arguments (Textures.md 1) */
#define B_CUBE_A    0x80u           /* 0x800576CE, imm32: mov edi, n of the first cube */
#define B_CUBE_B    0x40u           /* 0x800576D9, imm8: n of the second cube */
#define B_FR_THR    0x32u           /* 0x800576EC, imm8: thr of texgen_fractal */
#define B_FR_MAX    0xFFu           /* 0x800576F4, imm32: mov eax, start and max */
#define B_FR_MASK   0xFFFFFFu       /* 0x800576FE, imm32 */
#define B_FR_AMP    0x200u          /* 0x80057704, imm32 */
#define B_FR_NORM   0x1u            /* 0x8005770D, imm8: normals */
#define B_FO_SEED   0x3039u         /* 0x80057715, imm32 */
#define B_FO_N      0x100u          /* 0x8005771D, imm32: mov ebx, w and h */
/* fx_noise_textures 0x8005A14F */
#define B_NZ_MAX    0x55u           /* 0x8005A16D, imm8 */
#define B_NZ_START  0x3F00u         /* 0x8005A16F, imm32; only its low byte is used (0x80060E76) */
#define B_NZ_AMP    0x1A9u          /* 0x8005A17F, imm32 */
#define B_NZ_N      0x100u          /* 0x8005A184, imm32 */
#define B_NZ_K      0x3u            /* 0x8005A189, imm8 */
/* logo set-up 0x8005B712 */
#define B_GA_C1     0xFF4B9B4Bu     /* 0x8005B772, imm32 */
#define B_GA_C0     0xFF000100u     /* 0x8005B777, imm32 */
#define B_GA_H      0x80u           /* 0x8005B77E, imm32 */
#define B_GA_W      0x10u           /* 0x8005B786, imm8 */
#define B_RA_P      0x6u            /* 0x8005B806, imm8 */
#define B_RA_N      0x100u          /* 0x8005B808, imm32 */
#define B_GC_C1     0xFFFFFFFFu     /* 0x8005B8C0, imm8 */
#define B_GC_C0     0xFF000000u     /* 0x8005B8C2, imm32 */
#define B_GC_H      0x80u           /* 0x8005B8C7, imm32 */
#define B_GC_W      0x10u           /* 0x8005B8CC, imm8 */
/* inside the generators */
#define B_ALPHA     0x437F0000u     /* 0x80060387, imm32: 255.0, alpha of normal_to_argb */
#define B_RA_STEPS  0x100u          /* 0x80060464, imm32: mov edx, steps of the table */
#define B_FO_SCALE  0x1000u         /* 0x8006066B, imm32 */
#define B_FO_ONE    0x1000000u      /* 0x800606E9, imm32 */
#define B_FO_ROT    0xDu            /* 0x80060705, imm8: rcl ecx, 13 */
#define B_FO_SUB    0xBu            /* 0x80060708, imm8 */
#define B_FO_SHL    0xFu            /* 0x80060712, imm8 */
#define B_FO_MASK   0x1FF0000u      /* 0x8006071B, imm32 */
#define B_FO_BYTE   0xFF00u         /* 0x80060737, imm32 */
#define B_FR_BYTE   0xFFu           /* 0x80060A83, imm32: mov esi, 0xFF */
#define B_FR_HUNDRED 0x64u          /* 0x80060AC9, imm8 */
#define B_CUBE_M1_5 0xBF800000u     /* 0x800611A1, imm32: -1.0 of face 5 */
#define B_CUBE_M1_3 0xBF800000u     /* 0x800611C7, imm32: -1.0 of face 3 */
#define B_CUBE_M1_1 0xBF800000u     /* 0x800611E6, imm32: -1.0 of face 1 */

#define TM_TEXELS   0x80073FD0u     /* 16 x 16 ARGB, Logo.md 2.4 */
#define TM_SIZE     16u
#define FACE_ALIGN  128u            /* NV2A_CUBEMAP_FACE_ALIGNMENT, Textures.md 2 */
#define SWZ_MAX     256u

/* Colour formats of SET_TEXTURE_FORMAT (Textures.md 2). */
#define FMT_A8R8G8B8 6u
#define FMT_X8R8G8B8 7u
#define FMT_A8       0x19u

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* cvttss2si on a float32; NaN and values out of range give 0x80000000. */
static int32_t trunc_ss(float x)
{
    if (!(x > -2147483648.0f && x < 2147483648.0f))
        return INT32_MIN;
    return (int32_t)x;
}

/* ---- swizzle, Textures.md 3 --------------------------------------------- */

/* Offset in texels of column x and row y: sw.x[x] | sw.y[y]. */
typedef struct swz {
    uint32_t x[SWZ_MAX], y[SWZ_MAX];
} swz;

static uint32_t deposit(uint32_t v, uint32_t mask)
{
    uint32_t out = 0, bit;

    for (bit = 1; mask; bit <<= 1)
        if (mask & bit) {
            if (v & 1)
                out |= bit;
            v >>= 1;
            mask ^= bit;
        }
    return out;
}

/* 0x800AAB46: from bit 0 upward one bit of x while 2^k < w, then one of y
 * while 2^k < h; w < 3 or h < 2 is copied unchanged (0x800AAB8C-0x800AABB9). */
static void swz_init(swz *z, uint32_t w, uint32_t h)
{
    uint32_t mx = 0, my = 0, k, mbit = 1, i;

    if (w < 3 || h < 2) {
        for (i = 0; i < w; i++)
            z->x[i] = i;
        for (i = 0; i < h; i++)
            z->y[i] = i * w;
        return;
    }
    for (k = 1; k < w || k < h; k <<= 1) {
        if (k < w) {
            mx |= mbit;
            mbit <<= 1;
        }
        if (k < h) {
            my |= mbit;
            mbit <<= 1;
        }
    }
    for (i = 0; i < w; i++)
        z->x[i] = deposit(i, mx);
    for (i = 0; i < h; i++)
        z->y[i] = deposit(i, my);
}

/* ---- helpers ------------------------------------------------------------- */

/* normal_to_argb 0x8006036C (Textures.md 4.1): (v + 1) * 127.5 in the
 * register, one store per channel; alpha is cvttss2si of the immediate 255. */
static uint32_t normal_to_argb(const float v[3])
{
    uint32_t c[3];
    int i;

    for (i = 0; i < 3; i++)
        c[i] = (uint32_t)trunc_ss(r32f(((double)v[i] + K_ONE) * K_ARGB));
    return (uint32_t)trunc_ss(from_bits(B_ALPHA)) << 24 | c[0] << 16 | c[1] << 8 | c[2];
}

/* rand_below 0x8005A620 (Textures.md 4.6): a step of 0x8005A645, then the
 * high word of s' * n. */
static uint32_t rand_below(uint32_t *s, uint32_t n)
{
    *s = rng_step(*s);
    return (uint32_t)(((uint64_t)*s * n) >> 32);
}

/* ---- 4.2 cube of directions, 0x800610DD ---------------------------------- */

static void normal_cube(uint32_t n, uint8_t *out)
{
    swz z;
    uint32_t face_bytes = (n * n * 4 + FACE_ALIGN - 1) & ~(FACE_ALIGN - 1), f, u, v;
    double d = r32f((double)(n - 1));               /* 0x8006113D-0x80061148 */

    swz_init(&z, n, n);
    for (f = 0; f < 6; f++) {
        uint8_t *face = out + f * face_bytes;

        for (u = 0; u < face_bytes; u++)
            face[u] = 0;
        for (v = 0; v < n; v++) {
            float fy = r32f(((double)v / d) * 2.0 - K_ONE);
            for (u = 0; u < n; u++) {
                /* fx stays in the register; every case stores it */
                float fx = r32f(((double)u / d) * 2.0 - K_ONE), vec[3];

                switch (f) {                        /* 0x8006118B-0x80061205 */
                case 0: vec[0] = 1.0f; vec[1] = -fy; vec[2] = -fx; break;
                case 1: vec[0] = from_bits(B_CUBE_M1_1); vec[1] = -fy; vec[2] = fx; break;
                case 2: vec[0] = fx; vec[1] = 1.0f; vec[2] = fy; break;
                case 3: vec[0] = fx; vec[1] = from_bits(B_CUBE_M1_3); vec[2] = -fy; break;
                case 4: vec[0] = fx; vec[1] = -fy; vec[2] = 1.0f; break;
                default: vec[0] = -fx; vec[1] = -fy; vec[2] = from_bits(B_CUBE_M1_5); break;
                }
                v3_norm(vec);                       /* vec3_normalize 0x8005816D */
                wr_u32(face + 4 * (z.x[u] | z.y[v]), normal_to_argb(vec));
            }
        }
    }
}

/* ---- 4.3 gradient, 0x800607A1 ------------------------------------------- */

static void gradient(uint32_t w, uint32_t h, uint32_t c0, uint32_t c1, uint8_t *out)
{
    swz z;
    float a[4], b[4], inv;
    uint32_t x, y;
    int i;

    swz_init(&z, w, h);
    for (i = 0; i < 4; i++) {                       /* A, R, G, B; 0x800607DC-0x800608C1 */
        a[i] = r32f((double)((c0 >> (24 - 8 * i)) & 0xFF) * K_BYTE);
        b[i] = r32f((double)((c1 >> (24 - 8 * i)) & 0xFF) * K_BYTE);
    }
    inv = r32f((double)K_ONE / (double)(h - 1));    /* fdivr 0x800608D6 */
    for (y = 0; y < h; y++) {                       /* 0x800608EE-0x80060972 */
        double s = (double)y * inv, r = 1.0 - s;    /* fld1 */
        uint32_t ch[4], col;

        for (i = 0; i < 4; i++)
            ch[i] = (uint32_t)trunc_ss(r32f((r * a[i] + s * b[i]) * K_255));
        col = ch[0] << 24 | ch[1] << 16 | ch[2] << 8 | ch[3];
        for (x = 0; x < w; x++)
            wr_u32(out + 4 * (z.x[x] | z.y[y]), col);
    }
}

/* ---- 4.4 radial brightness, 0x800603D8 ----------------------------------- */

static void radial(uint32_t n, uint32_t p, int alpha, float A, float B, uint8_t *out)
{
    swz z;
    uint8_t tab[B_RA_STEPS];
    int32_t m = ((int32_t)n - 1) / 2, x, y;
    float inv = r32f((double)K_ONE / (double)((int32_t)n / 2));    /* 0x80060416-0x8006043F */
    uint32_t i, k;

    swz_init(&z, n, n);
    for (i = 0; i < B_RA_STEPS; i++) {              /* 0x80060468-0x800604D1 */
        double sn, cs;
        float c;

        x87_fsincos(r32f((double)i * K_ANGLE), &sn, &cs);
        c = r32f(cs);
        for (k = 0; k < p; k++)
            c = r32f((double)c * c);
        tab[i] = (uint8_t)trunc_ss(r32f(((double)(B_RA_STEPS - i) * K_ANGLE * A + (double)c * B) * K_255));
    }
    /* Row y writes only its first y texels (0x800604EB); the rest keeps
     * what was in the guest's memory, 0 in every run. The core writes 0. */
    for (i = 0; i < 4 * n * n; i++)
        out[i] = 0;
    for (y = 0; y < (int32_t)n; y++)
        for (x = 0; x < y; x++) {
            double r = (double)sse_sqrt(r32f((double)((x - m) * (x - m) + (y - m) * (y - m)))) * inv;
            uint32_t L, texel = 0;

            if (r < K_ONE) {                        /* fcom, test ah, 5: below 1 */
                L = tab[trunc_ss(r32f(r * K_INDEX))];
                texel = (alpha ? L : 0xFFu) << 24 | L << 16 | L << 8 | L;
            }
            wr_u32(out + 4 * (z.x[x] | z.y[y]), texel);
        }
}

/* ---- 4.5 fall-off with mip levels, 0x800605D2 --------------------------- */

static uint32_t bit_length(uint32_t v)
{
    uint32_t n = 0;

    while (v) {
        n++;
        v >>= 1;
    }
    return n;
}

static uint32_t falloff_levels(uint32_t w, uint32_t h)
{
    uint32_t lw = bit_length(w >> 1), lh = bit_length(h >> 1);    /* 0x800605DE-0x8006061B */

    return lw > lh ? lw : lh;
}

/* rcl r32, n: rotates the 33 bits CF:v left by n. */
static uint32_t rcl32(uint32_t v, uint32_t n, uint32_t cf)
{
    uint64_t x = (uint64_t)cf << 32 | v;
    uint32_t i;

    for (i = 0; i < n; i++)
        x = ((x << 1) | (x >> 32)) & 0x1FFFFFFFFull;
    return (uint32_t)x;
}

static void falloff_mips(uint32_t w, uint32_t h, uint32_t noise, uint32_t seed, uint8_t *out)
{
    swz z;
    uint32_t levels = falloff_levels(w, h), lv, s = seed;

    for (lv = 0; lv < levels; lv++) {
        int32_t W = (int32_t)w >> lv, H = (int32_t)h >> lv, q = (int32_t)B_FO_SCALE / W, x, y;
        int32_t cx = (W - 1) / 2, cy = (H - 1) / 2;
        uint32_t sh = 1;

        while (q != 1) {                            /* 0x8006067F-0x80060686 */
            sh++;
            q >>= 1;
        }
        swz_init(&z, (uint32_t)W, (uint32_t)H);
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {               /* 0x800606CD-0x80060749 */
                uint32_t ax = (uint32_t)(x - cx) << sh, by = (uint32_t)(y - cy) << sh;
                uint32_t d = ax * ax + by * by, v, hi, nz, e, e2, b, a;

                v = d <= B_FO_ONE ? B_FO_ONE - d : 0;
                hi = (uint32_t)(((uint64_t)v * noise) >> 32);
                {   /* the generator: rcl with the carry of the mul, minus (s - 11) */
                    uint32_t r = rcl32(s, B_FO_ROT, hi != 0);
                    s = r - (s - B_FO_SUB);
                }
                nz = (uint32_t)(((uint64_t)hi * s) >> 32) << B_FO_SHL;
                e = (int32_t)v >= (int32_t)nz ? v - nz : 0;    /* sub; jge */
                e &= B_FO_MASK;
                e2 = (e & 0xFF0000u) << 8;          /* rcl ebx, 8; sbb ebx, 0 (0x8006071F) */
                if (e & 0x1000000u)
                    e2 -= 1;
                b = e2 >> 24;
                a = b * b;
                a = a * a;
                a >>= 16;
                a = a * a;
                a = (a >> 16) & B_FO_BYTE;
                wr_u32(out + 4 * (z.x[x] | z.y[y]), (a >> 8 | a) << 16 | (a >> 8 | a));
            }
        out += 4 * (uint32_t)W * (uint32_t)H;
    }
}

/* ---- 4.7 and 4.8, subdivision -------------------------------------------- */

/* The visiting order of 0x800609D1 and 0x80060DDC (Textures.md 4.7): centres
 * of the squares, then the edges in two passes, then the next level. */
typedef struct walk {
    int32_t n, half, step, x, y, level;
    int centre, second;
} walk;

static void walk_start(walk *w, int32_t n)
{
    w->n = n;
    w->half = n >> 1;
    w->step = n;
    w->x = w->y = w->half;
    w->level = 0;
    w->centre = 1;
    w->second = 0;
}

static void walk_next(walk *w)
{
    w->x += w->step;
    if (w->centre) {
        if (w->x >= w->n) {
            w->y += w->step;
            w->x = w->half;
            if (w->y >= w->n) {
                w->y = 0;
                w->centre = 0;
            }
        }
    } else if (w->x >= w->n) {
        w->y += w->step;
        if (w->y < w->n) {
            w->x = w->second ? 0 : w->half;
        } else {
            if (w->second) {
                w->step = w->half;
                w->half >>= 1;
                w->level++;
                w->x = w->half;
                w->centre = 1;
            } else {
                w->x = 0;
            }
            w->y = w->half;
            w->second = !w->second;
        }
    }
}

/* Offsets of the four values averaged at the current point: the corners
 * (x +- half, y +- half) of a centre, the neighbours (x, y +- half),
 * (x +- half, y) of an edge, modulo n. */
static void walk_corners(const walk *w, const swz *z, uint32_t o[4])
{
    int32_t n = w->n, xp = w->x + w->half, yp = w->y + w->half, xm = w->x - w->half, ym = w->y - w->half;

    if (xm < 0)
        xm += n;
    if (xp >= n)
        xp -= n;
    if (ym < 0)
        ym += n;
    if (yp >= n)
        yp -= n;
    if (w->centre) {
        o[0] = z->y[ym] | z->x[xp];
        o[1] = z->y[ym] | z->x[xm];
        o[2] = z->y[yp] | z->x[xp];
        o[3] = z->y[yp] | z->x[xm];
    } else {
        o[0] = z->y[ym] | z->x[w->x];
        o[1] = z->y[yp] | z->x[w->x];
        o[2] = z->y[w->y] | z->x[xp];
        o[3] = z->y[w->y] | z->x[xm];
    }
}

static uint32_t grey(uint32_t v)
{
    return ((v << 8 | v) << 8) | v;                 /* v * 0x010101, 0x80060A50-0x80060A5A */
}

/* texgen_fractal 0x800609D1 (Textures.md 4.7). */
static void fractal(uint32_t n, int normals, float S, uint32_t amp, uint32_t seed, uint32_t mask,
                    uint32_t start, int use_start, uint32_t max, int32_t thr, uint8_t *out)
{
    swz z;
    uint32_t s = seed, i, a, o[4];
    int32_t level = 0, r, x;
    walk w;

    swz_init(&z, n, n);
    for (i = 0; i < 4 * n * n; i++)
        out[i] = 0;
    wr_u32(out, grey(use_start ? start : rand_below(&s, max)));
    a = (uint32_t)((int32_t)amp >> 1);
    for (walk_start(&w, (int32_t)n); w.half > 0; walk_next(&w)) {
        uint32_t v = 0;
        int k;

        while (level < w.level) {
            a = (uint32_t)((int32_t)a >> 1);        /* sar [ebp-0x10], 1 */
            level++;
        }
        walk_corners(&w, &z, o);
        for (k = 0; k < 4; k++)
            v += rd_u32(out + 4 * o[k]) & B_FR_BYTE;
        v >>= 2;
        /* 0x80060B01-0x80060B33 */
        if ((int32_t)rand_below(&s, B_FR_HUNDRED) > thr) {
            v += rand_below(&s, a);
            if (v > max)
                v = max;
        } else {
            v -= rand_below(&s, a);
            if (v > B_FR_BYTE)
                v = 0;
        }
        wr_u32(out + 4 * (z.y[w.y] | z.x[w.x]), grey(v));
    }
    for (i = 0; i < n * n; i++) {                   /* 0x80060C77-0x80060C90 */
        uint32_t d = rd_u32(out + 4 * i);
        wr_u32(out + 4 * i, (d & mask) | d << 24);
    }
    if (!normals)
        return;
    /* 0x80060C9C-0x80060DB4. Texel (x, r) reads the next word and the word
     * one row lower; both come later in row order and are still heights.
     * Past the buffer the guest had 0 (Textures.md 4.7). */
    for (r = 0; r < (int32_t)n; r++) {
        double R = r, R1 = r32f(R + K_TENTH);
        for (x = 0; x < (int32_t)n; x++) {
            uint8_t *here = out + 4 * (z.y[r] | z.x[x]);
            uint32_t hc = here[2], hr = 0, hb = 0;
            double X = x, A;
            float B, C, v1[3], v2[3], nv[3];

            if (x + 1 < (int32_t)n)
                hr = out[4 * (z.y[r] | z.x[x + 1]) + 2];
            else if (r + 1 < (int32_t)n)
                hr = out[4 * (z.y[r + 1] | z.x[0]) + 2];
            if (r + 1 < (int32_t)n)
                hb = out[4 * (z.y[r + 1] | z.x[x]) + 2];
            A = (double)hc * S;
            B = r32f((double)hr * S);
            C = r32f((double)hb * S);
            v1[0] = r32f((X + K_TENTH) - X);
            v1[1] = r32f(R - R);
            v1[2] = r32f((double)B - A);
            v2[0] = r32f(X - X);
            v2[1] = r32f((double)R1 - R);
            v2[2] = r32f((double)C - r32f(A));
            v3_cross(v1, v2, nv);                   /* vec3_cross 0x8005A662 */
            v3_norm(nv);
            wr_u32(here, normal_to_argb(nv));
        }
    }
}

/* texgen_fractal_multi 0x80060DDC (Textures.md 4.8). The final copy takes
 * layer 0 for every texture, so only layer 0 is kept here; layers 1 to k-1
 * still draw their numbers, which do not depend on their values. */
static void fractal_multi(uint32_t k, uint32_t n, uint32_t amp, uint32_t seed, uint32_t start, int32_t max,
                          uint8_t *out)
{
    swz z;
    uint32_t s = seed, i, l, o[4];
    int32_t a, level = 0;
    walk w;

    swz_init(&z, n, n);
    for (i = 0; i < n * n; i++)
        out[i] = 0;
    out[0] = (uint8_t)start;                        /* mov al, [ebp+0x1C]; 0x80060E80 */
    a = (int32_t)amp >> 1;
    for (walk_start(&w, (int32_t)n); w.half > 0; walk_next(&w)) {
        while (level < w.level) {
            a >>= 1;                                /* sar ebx, 1 (0x80061072) */
            level++;
        }
        walk_corners(&w, &z, o);
        for (l = 0; l < k; l++) {                   /* 0x80060F1E-0x80060F5E */
            int32_t r = (int32_t)rand_below(&s, (uint32_t)(a + a)) - a, v;

            if (l)
                continue;
            v = (int32_t)(out[o[0]] + out[o[1]] + out[o[2]] + out[o[3]]) >> 2;
            v += r;
            out[z.y[w.y] | z.x[w.x]] = (uint8_t)(v < 0 ? 0 : v > max ? max : v);
        }
    }
}

/* ---- the textures ------------------------------------------------------- */

static uint32_t log2u(uint32_t v)
{
    return bit_length(v) - 1;
}

void texture_info(uint32_t tex, xb_texture_desc *d)
{
    uint32_t lv, size = 0;

    d->levels = 1;
    d->faces = 1;
    d->texel_bytes = 4;
    d->format = FMT_A8R8G8B8;
    switch (tex) {
    case XB_TEX_CUBE128:
    case XB_TEX_CUBE64:
        d->width = d->height = tex == XB_TEX_CUBE128 ? B_CUBE_A : B_CUBE_B;
        d->faces = 6;
        d->format = FMT_X8R8G8B8;
        break;
    case XB_TEX_BUMP:
        d->width = d->height = B_CUBE_A;            /* edi still holds 0x80 */
        break;
    case XB_TEX_FALLOFF:
        d->width = d->height = B_FO_N;
        d->levels = falloff_levels(B_FO_N, B_FO_N);
        break;
    case XB_TEX_GRADIENT_A:
    case XB_TEX_GRADIENT_C:
        d->width = B_GA_W;
        d->height = B_GA_H;
        break;
    case XB_TEX_RADIAL:
        d->width = d->height = B_RA_N;
        break;
    case XB_TEX_TM:
        d->width = d->height = TM_SIZE;
        break;
    default:                                        /* the three noise textures */
        d->width = d->height = B_NZ_N;
        d->texel_bytes = 1;
        d->format = FMT_A8;
        break;
    }
    for (lv = 0; lv < d->levels; lv++)
        size += (d->width >> lv) * (d->height >> lv) * d->texel_bytes;
    if (d->faces == 6)
        size = 6 * ((size + FACE_ALIGN - 1) & ~(FACE_ALIGN - 1));
    d->bytes = size;
    /* Textures.md 2: context DMA 2, cube, border bit, dimension 2, colour
     * format, levels, log2 of width and height, as in every measured word. */
    d->format_word = 2u | (d->faces == 6 ? 1u : 0u) << 2 | 1u << 3 | 2u << 4 | d->format << 8 |
                     d->levels << 16 | log2u(d->width) << 20 | log2u(d->height) << 24;
}

void texture_make(const xb_state *s, uint32_t tex, uint8_t *out)
{
    switch (tex) {
    case XB_TEX_CUBE128:
        normal_cube(B_CUBE_A, out);
        break;
    case XB_TEX_CUBE64:
        normal_cube(B_CUBE_B, out);
        break;
    case XB_TEX_BUMP:
        /* seed: xor ebx, ebx (0x800576F9); use_start: ebx; start and max: eax */
        fractal(B_CUBE_A, (int)B_FR_NORM, K_BUMP, B_FR_AMP, 0, B_FR_MASK, B_FR_MAX, 0, B_FR_MAX,
                (int32_t)B_FR_THR, out);
        break;
    case XB_TEX_FALLOFF:
        falloff_mips(B_FO_N, B_FO_N, 0, B_FO_SEED, out);     /* noise: ebx = 0 (0x80057719) */
        break;
    case XB_TEX_GRADIENT_A:
        gradient(B_GA_W, B_GA_H, B_GA_C0, B_GA_C1, out);
        break;
    case XB_TEX_RADIAL:
        radial(B_RA_N, B_RA_P, 0, K_HALF, K_HALF, out);      /* a_flag: edi = 0 (0x8005B7F5) */
        break;
    case XB_TEX_GRADIENT_C:
        gradient(B_GC_W, B_GC_H, B_GC_C0, B_GC_C1, out);
        break;
    case XB_TEX_TM: {
        swz z;
        const uint8_t *src = pack_at(s, PACK_LOGO, TM_TEXELS);
        uint32_t x, y, b;

        swz_init(&z, TM_SIZE, TM_SIZE);
        for (y = 0; y < TM_SIZE; y++)
            for (x = 0; x < TM_SIZE; x++)
                for (b = 0; b < 4; b++)
                    out[4 * (z.x[x] | z.y[y]) + b] = src[4 * (y * TM_SIZE + x) + b];
        break;
    }
    default:
        fractal_multi(B_NZ_K, B_NZ_N, B_NZ_AMP, s->noise_seed, B_NZ_START & 0xFF, (int32_t)B_NZ_MAX, out);
        break;
    }
}
