/* The glowing blob, docs/decomp/Blob.md.
 *
 * Numerics as in Blob.md 9: x87 code in double, r32f() where the original
 * stores a float32; sine and square root through the SSE helpers of
 * Overview.c. Where Blob.md gives a formula without the order of its
 * operations, the order is that of tools/xemu/blob_model.py, which meets
 * all five runs of vectors/c6_blob.json bit for bit.
 */
#include "core_internal.h"

/* float32 constants of the kernel, address of each in the comment.
 * core/test/check_k3.py compares them with out/rom/kernel.img. */
#define K_PI        3.14159265358979f /* 0x80066574 */
#define K_FADE_END  8.0f        /* 0x8006657C */
#define K_FADE_TWO  2.0f        /* 0x80066580 */
#define K_FADE_08   0.8f        /* 0x80066584 */
#define K_START     0.6f        /* 0x80066588 */
#define K_HALF      0.5f        /* 0x80010A18 */
#define K_ONE       1.0f        /* 0x8001288C */
#define K_QUARTER   0.25f       /* 0x8001289C */
#define K_MINUS1    -1.0f       /* 0x80012894 */
#define K_TWO       2.0f        /* 0x80010A24 */
#define K_UNIT      1.52587890625e-05f  /* 0x800708C0 */
#define K_SIGNED    3.0517578125e-05f   /* 0x800708C4 */
#define K_LEN_M     0.34375f    /* 0x800128F4 */
#define K_LEN_K     1.0433885f  /* 0x800128F0 */
#define K_LEN_MIN   1e-06f      /* 0x800128F8 */
#define K_FACE      0.57735f    /* 0x80012914 */
#define K_FACE_NEG  -0.57735f   /* 0x80012918 */
#define K_SHORT     0.001f      /* 0x800128A0 */
#define K_04        0.4f        /* 0x800128AC */
#define K_02        0.2f        /* 0x800128B0 */
#define K_08        0.8f        /* 0x800128D0 */
#define K_03        0.3f        /* 0x800128FC */
#define K_RESTART_T -0.3f       /* 0x8001292C */
#define K_06B       0.6f        /* 0x80012930 */
#define K_DROP_FIVE 5.0f        /* 0x80012934 */
#define K_PERIOD    1.6666666f  /* 0x80012928 */
#define K_NEAR      0.9f        /* 0x80012938 */
#define K_SPRING_LO 0.95f       /* 0x80012924 */
#define K_SPRING_HI 1.05f       /* 0x80012920 */
#define K_SPRING    1000.0f     /* 0x80066590 */
#define K_01        0.1f        /* 0x80012888 */
#define K_14        1.4f        /* 0x8001291C */
#define K_GRAD      -4.0f       /* 0x80012910 */
#define K_PULSE     1.3f        /* 0x8001290C */
#define K_GLOW      5.2f        /* 0x80012904 */
#define K_255       255.0f      /* 0x80012908 */
#define K_12        1.2f        /* 0x800128B8 */
#define K_FOUR      4.0f        /* 0x80012900 */
#define K_BRIGHT    0.3f        /* 0x8006658C */

/* Values the code sets as immediates (blob_construct 0x80059270, spawn
 * 0x80059320); the address is that of the four bytes of the immediate.
 * check_k3.py compares them with the image as well. */
#define B_RADIUS    2.3f        /* 0x8005931B: 0x40133333 */
#define B_BLUE      0.15f       /* 0x800592F5: 0x3E19999A */
#define B_SCALE     1.2f        /* 0x800594FB: 0x3F99999A */
#define B_SEED      0x76543210u /* 0x800592A2 */

#define NONE (-1)

/* ---- generator and helpers, Blob.md 4; K_UNIT = 2^-16, K_SIGNED = 2^-15 ---- */

static uint32_t draw16(blob_state *b)
{
    b->rng = rng_step(b->rng);
    b->draws++;
    return b->rng & 0xFFFFu;
}

static double unit(blob_state *b)
{
    return (double)draw16(b) * K_UNIT;
}

static double signed1(blob_state *b)
{
    return (double)draw16(b) * K_SIGNED - 1.0;
}

static float fade(double t)
{
    double k = (double)K_ONE / ((((double)K_FADE_END - K_FADE_TWO) - K_FADE_08) - ((double)K_START + K_HALF));
    double f = (t - K_START) * k;

    return r32f(f >= 0.0 ? f : 0.0);
}

static double fabs_d(double x)
{
    return x < 0.0 ? -x : x;
}

/* 0x8005820E over the sorted magnitudes. */
static double len_approx(const float v[3])
{
    double a = fabs_d(v[0]), m = fabs_d(v[1]), c = fabs_d(v[2]), x;

    if (a > m) { x = a; a = m; m = x; }
    if (m > c) { x = m; m = c; c = x; }
    if (a > m) { x = a; a = m; m = x; }
    return ((a * 0.25 + m * K_LEN_M) + c) * K_LEN_K;
}

/* 0x80058290: unchanged below an approximate length of 1e-6. */
static void norm_approx(float v[3])
{
    double n = len_approx(v), k;
    int i;

    if (n < K_LEN_MIN)
        return;
    k = r32f(1.0 / n);
    for (i = 0; i < 3; i++)
        v[i] = r32f(v[i] * k);
}

/* 0x80058FFC */
static void face_mask(xb_bulge *g)
{
    uint32_t m = 0;
    int k;

    for (k = 0; k < 3; k++) {
        if ((double)g->dir[k] - g->size < K_FACE_NEG)
            m |= 1u << k;
        if ((double)g->size + g->dir[k] > K_FACE)
            m |= 8u << k;
    }
    g->mask = m;
}

/* ---- meshes, Blob.md 3 ------------------------------------------------------ */

static uint32_t sphere(uint32_t n, float *unit_out, uint16_t *idx)
{
    uint32_t m = n / 2 < 1 ? 1 : n / 2, f, i, j, nv = 0, ni = 0;
    float step = r32f(2.0 / m);

    for (f = 0; f < 6; f++)
        for (j = 0; j <= m; j++)
            for (i = 0; i <= m; i++) {
                float a = i == m ? 1.0f : r32f((double)i * step - 1.0);
                float c = j == m ? 1.0f : r32f((double)j * step - 1.0);
                float v[3];

                switch (f) {
                case 0: v[0] = -1.0f; v[1] = -a; v[2] = c; break;
                case 1: v[0] = c; v[1] = -1.0f; v[2] = -a; break;
                case 2: v[0] = -a; v[1] = c; v[2] = -1.0f; break;
                case 3: v[0] = 1.0f; v[1] = a; v[2] = c; break;
                case 4: v[0] = c; v[1] = 1.0f; v[2] = a; break;
                default: v[0] = a; v[1] = c; v[2] = 1.0f; break;
                }
                v3_norm(v);
                if (unit_out) {
                    unit_out[3 * nv] = v[0];
                    unit_out[3 * nv + 1] = v[1];
                    unit_out[3 * nv + 2] = v[2];
                }
                nv++;
            }
    if (idx)
        for (f = 0; f < 6; f++)
            ni += strip_indices(idx + ni, m, m, f > 0, f < 5, f * (m + 1) * (m + 1));
    return nv;
}

uint32_t blob_mesh(uint32_t small, float *unit_out, uint16_t *idx)
{
    return sphere(small ? 8 : 32, unit_out, idx);
}

/* ---- bulges and drops, Blob.md 5, 6 ----------------------------------------- */

static int step(blob_state *b, xb_bulge *g, double t, double dt, int slot);

/* drop_update 0x800590A6 */
static void drop_update(blob_state *b, xb_drop *d, double t, double dt)
{
    double v = dt * d->rate + d->scale, arg, s, w, e;
    float fd, u;
    int move;

    if (v < K_HALF)
        v = K_HALF;
    else if (v > K_TWO)
        v = K_TWO;
    d->scale = r32f(v);
    if (d->rate > 0.0f)
        move = v < K_SPRING_LO || v > 1.0;
    else
        move = v < 1.0 || v > K_SPRING_HI;
    if (move)
        d->rate = r32f(d->rate - (v - 1.0) * K_SPRING * dt);
    fd = fade(t);
    arg = r32f((t - d->t0) * d->freq * (t * K_01 + 1.0) * K_14);
    s = sse_sin((float)arg);
    u = r32f(1.0 - fabs_d(s));
    w = 1.0 - (double)sse_sqrt(u) * u;
    if (!(s > 0.0))
        w = -w;
    e = w * d->amp * fd;
    d->disp = r32f(e);
    d->neg = e < 0.0;
    d->pos[0] = r32f(b->center[0] + (double)d->dir[0] * d->disp);
    d->pos[1] = r32f(b->center[1] + (double)d->dir[1] * d->disp);
    d->pos[2] = r32f(b->center[2] + (double)d->dir[2] * d->disp);
}

/* spawn 0x80059320; returns whether the bulge has a drop. */
static int spawn(blob_state *b, xb_bulge *g, double t, int slot)
{
    double q, s, p, r;
    xb_drop *d;
    float fd;

    if (t < 0.0)
        g->drop = NONE;
    g->dir[0] = r32f(signed1(b));
    g->dir[1] = r32f(signed1(b));
    g->dir[2] = r32f(signed1(b));
    if (((double)g->dir[0] * g->dir[0] + (double)g->dir[1] * g->dir[1]) +
        (double)g->dir[2] * g->dir[2] < K_SHORT) {
        g->dir[0] = r32f(signed1(b));
        g->dir[1] = r32f(signed1(b));
        g->dir[2] = 1.0f;
    }
    g->c[0] = g->c[1] = g->c[2] = 0.0f;
    norm_approx(g->dir);
    fd = fade(t);
    q = unit(b);
    s = (q + K_ONE) * K_04;
    g->size = r32f(s);
    g->size2 = r32f(s * s);
    g->inv2 = r32f(1.0 / (s * s));
    g->h = 0.0f;
    face_mask(g);
    g->t0 = r32f(unit(b) * K_04 + t);
    g->amp = r32f((((1.0 - q) * K_HALF + K_02) * ((double)fd + K_ONE)) * K_HALF);
    if (g->drop == NONE) {
        g->drop = slot;
        if (slot == NONE) {
            double x = (unit(b) + g->amp) * K_03;

            g->freq = r32f(((double)fd * K_02 + K_08) / x * K_PI);
            if (t < K_MINUS1)              /* no caller reaches it (Blob.md 5.2) */
                g->t0 = r32f(-(unit(b) * ((double)K_PI / g->freq)));
            return 0;
        }
    }
    d = &b->drop[g->drop];
    d->size = r32f((unit(b) + K_ONE) * g->size * B_RADIUS * K_QUARTER);
    d->dir[0] = g->dir[0];
    d->dir[1] = g->dir[1];
    d->dir[2] = g->dir[2];
    r = signed1(b);
    d->amp = r32f((r + r + K_DROP_FIVE) * B_RADIUS);
    d->amp = r32f((double)d->amp * K_06B);
    d->t0 = t < K_MINUS1 ? r32f(unit(b) * K_RESTART_T) : (float)t;
    p = (unit(b) * K_03 + K_08) * K_PERIOD;
    d->freq = r32f(K_PI / p * 2.0);
    d->scale = B_SCALE;
    d->rate = 0.0f;
    g->flag = p * K_04 > t - g->t0;
    drop_update(b, d, t, 0.0);
    step(b, g, t, 0.0, NONE);
    return 1;
}

/* step 0x800595B6 */
static int step(blob_state *b, xb_bulge *g, double t, double dt, int slot)
{
    double ph;

    (void)dt;
    if (g->drop != NONE) {
        xb_drop *d = &b->drop[g->drop];
        double q = (fabs_d(d->disp) + d->size) / B_RADIUS, h = q - K_ONE;

        if (h > K_TWO)
            h = K_TWO;
        else if (h < 0.0)
            h = 0.0;
        g->h = r32f(h);
        if (g->flag) {
            double p, h2;

            if (!(h > K_08)) {
                double dot = ((double)g->dir[2] * d->dir[2] + (double)g->dir[1] * d->dir[1]) +
                             (double)g->dir[0] * d->dir[0];
                uint32_t neg = dot < 0.0;

                if (neg == d->neg)
                    return 0;
                g->dir[0] = -g->dir[0];
                g->dir[1] = -g->dir[1];
                g->dir[2] = -g->dir[2];
                g->c[0] = g->dir[0];
                g->c[1] = g->dir[1];
                g->c[2] = g->dir[2];
                face_mask(g);
                return 0;
            }
            g->amp = r32f(h);
            g->flag = 0;
            p = K_03 * h;
            g->freq = r32f(K_PI / p * 2.0);
            g->t0 = r32f(t - p * K_QUARTER);
            h2 = h - K_HALF;
            if (!(K_08 < h2) && h2 < K_06B)
                h2 = K_06B;
            else if (K_08 < h2)
                h2 = K_08;
            d->scale = r32f(h2);
            d->rate = 0.0f;
        }
        if (!g->flag && q < K_NEAR)
            g->flag = 1;
    }
    ph = (t - g->t0) * g->freq;
    if (ph > K_PI) {
        if (g->drop == NONE)
            return spawn(b, g, t, slot);
        g->h = 0.0f;
        return 0;
    }
    if (ph < 0.0)
        return 0;
    g->h = r32f((double)sse_sin(r32f(ph)) * g->amp);
    g->c[0] = g->dir[0];
    g->c[1] = g->dir[1];
    g->c[2] = g->dir[2];
    face_mask(g);
    return 0;
}

/* ---- the object ---------------------------------------------------------------- */

/* blob_restart 0x80059868 */
void blob_restart(blob_state *b)
{
    b->n_drops = b->n_bulges = 0;
    while (b->n_bulges < BLOB_BULGES) {
        int slot = b->n_drops < BLOB_DROPS ? (int)b->n_drops : NONE;
        xb_bulge *g = &b->bulge[b->n_bulges++];

        if (spawn(b, g, K_RESTART_T, slot))
            b->n_drops++;
    }
    b->flat = 1;
}

/* blob_construct 0x80059270 and blob_setup 0x800598E6 */
void blob_start(blob_state *b)
{
    int i;

    b->rng = B_SEED;
    b->draws = 0;
    b->updates = 0;
    b->center[0] = b->center[1] = b->center[2] = 0.0f;
    b->radius = B_RADIUS;
    b->colour[0] = 0.25f;
    b->colour[1] = 1.0f;
    b->colour[2] = B_BLUE;
    b->colour[3] = 1.0f;
    for (i = 0; i < BLOB_BULGES; i++) {
        xb_bulge *g = &b->bulge[i];
        int a;

        g->size = g->size2 = g->inv2 = g->h = 0.0f;
        for (a = 0; a < 3; a++)
            g->c[a] = g->dir[a] = 0.0f;
        g->mask = 0;
        g->t0 = g->freq = g->amp = 0.0f;
        g->drop = NONE;
        g->flag = 0;
    }
    for (i = 0; i < BLOB_DROPS; i++) {
        xb_drop *d = &b->drop[i];
        int a;

        d->size = 0.0f;
        for (a = 0; a < 3; a++)
            d->pos[a] = d->dir[a] = 0.0f;
        d->neg = 0;
        d->t0 = d->freq = d->amp = d->disp = 0.0f;
        d->scale = 1.0f;
        d->rate = 0.0f;
    }
    sphere(32, b->unit[0], 0);
    blob_restart(b);
}

/* blob_update 0x80059789. The deformation is not stored: it depends only on
 * the bulges, and blob_deformed computes it when the host asks. */
void blob_update(blob_state *b, float t, float dt)
{
    uint32_t i;

    b->updates++;
    if (!(t < K_START)) {
        for (i = 0; i < b->n_drops; i++)
            drop_update(b, &b->drop[i], t, dt);
        for (i = 0; i < b->n_bulges; i++) {
            int slot = b->n_drops < BLOB_DROPS ? (int)b->n_drops : NONE;

            if (step(b, &b->bulge[i], t, dt, slot))
                b->n_drops++;
        }
        b->flat = 0;
    } else if ((double)t - dt > K_START) {
        b->flat = 1;
    }
}

/* deform 0x80058B93 */
void blob_deformed(const blob_state *b, float (*out)[4])
{
    uint32_t f, v, per_face = BLOB_VERTS / 6;
    int near[BLOB_BULGES], n, j;

    for (f = 0; f < 6; f++) {
        n = 0;
        for (j = 0; j < (int)b->n_bulges; j++)
            if (b->bulge[j].mask & (1u << f))
                near[n++] = j;
        for (v = f * per_face; v < (f + 1) * per_face; v++) {
            const float *p = b->unit[v];
            float *o = out[v];

            o[0] = p[0];
            o[1] = p[1];
            o[2] = p[2];
            o[3] = b->flat ? 1.0f : 0.0f;
            if (b->flat)
                continue;
            for (j = n - 1; j >= 0; j--) {
                const xb_bulge *g = &b->bulge[near[j]];
                float d[3], q[3];
                double dd, gg, k;
                float w;
                int a;

                for (a = 0; a < 3; a++)
                    d[a] = r32f((double)p[a] - g->c[a]);
                dd = ((double)d[2] * d[2] + (double)d[1] * d[1]) + (double)d[0] * d[0];
                if (!(dd < g->size2))
                    continue;
                gg = dd * g->inv2 - 1.0;
                w = r32f((double)B_RADIUS * g->h * gg * gg);
                k = r32f((double)g->h * g->inv2 * gg * K_GRAD);
                for (a = 0; a < 3; a++)
                    q[a] = r32f(p[a] + d[a] * k);
                norm_approx(q);
                o[0] = r32f((double)q[0] + o[0]);
                o[1] = r32f((double)q[1] + o[1]);
                o[2] = r32f((double)q[2] + o[2]);
                o[3] = r32f((double)w + o[3]);
            }
        }
    }
}

/* ---- blob_draw 0x800583BF, Blob.md 7 ----------------------------------------- */

static void set4(xb_real o[4], double a, double b, double c, double d)
{
    o[0] = (float)a;
    o[1] = (float)b;
    o[2] = (float)c;
    o[3] = (float)d;
}

void blob_draw(const blob_state *b, const xb_camera *cam, float t, float x1c, float x20,
               xb_blob_params *out)
{
    const float *colour = b->colour;
    const float *ax = cam->view_inv, *ay = cam->view_inv + 4;
    float k, kq, f, g, vp[16], m[16];
    double a, br, ramp;
    uint32_t i, r, c;

    k = r32f(((double)sse_sqrt(r32f((double)x20 - x1c)) * K_PULSE + 1.0) * B_RADIUS);
    out->k = k;

    /* 7.1 */
    a = x20 < 1.0f ? x20 : 1.0;
    out->glow_colour = ((uint32_t)(int32_t)r32f(a * K_255) << 24) | 0xA0FF40u;
    kq = r32f((double)k * K_GLOW);
    for (i = 0; i < 3; i++) {
        out->glow[0][i] = r32f((double)r32f((double)ay[i] - ax[i]) * kq);
        out->glow[1][i] = r32f((double)r32f((double)ay[i] + ax[i]) * kq);
        out->glow[2][i] = r32f((double)r32f((double)ax[i] - ay[i]) * kq);
        out->glow[3][i] = r32f((double)r32f(-(double)ax[i] - ay[i]) * kq);
    }
    out->glow[0][3] = 0.0f; out->glow[0][4] = 1.0f;
    out->glow[1][3] = 1.0f; out->glow[1][4] = 1.0f;
    out->glow[2][3] = 1.0f; out->glow[2][4] = 0.0f;
    out->glow[3][3] = 0.0f; out->glow[3][4] = 0.0f;

    /* 7.2: constants 0..3 the transpose of diag(k, k, k, 1) with the centre
     * in row 3, 4..7 the transpose of view * projection. */
    m4_ident(m);
    m[0] = m[5] = m[10] = k;
    m[12] = b->center[0];
    m[13] = b->center[1];
    m[14] = b->center[2];
    m4_mul(cam->view, cam->proj, vp);
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++) {
            out->mesh_vs[r][c] = m[c * 4 + r];
            out->mesh_vs[4 + r][c] = vp[c * 4 + r];
            out->drop_vs[r][c] = vp[c * 4 + r];
        }
    set4(out->mesh_vs[8], 0.0, 1.0, 2.0, 0.5);
    set4(out->mesh_vs[9], cam->eye[0], cam->eye[1], cam->eye[2], 0.0);
    set4(out->mesh_vs[10], k, k, k, 1.0);
    set4(out->mesh_vs[11], r32f(1.0 / k), r32f(1.0 / k), r32f(1.0 / k), 0.0);
    set4(out->mesh_vs[12], b->center[0], b->center[1], b->center[2], 0.0);
    br = (((double)x20 - x1c) * K_08 + (double)x1c * K_12) * 4.0 + K_BRIGHT;
    ramp = (double)t * K_FOUR;
    if (ramp > 1.0)
        ramp = 1.0;
    f = r32f(ramp * br);
    set4(out->mesh_ps[0], r32f(colour[0] * (double)f), r32f(colour[1] * (double)f),
         r32f(colour[2] * (double)f), 1.0);
    set4(out->mesh_ps[1], 0.0, 0.0, 0.0, 1.0);

    /* 7.3 */
    set4(out->drop_vs[4], 0.0, 1.0, 2.0, 0.5);
    set4(out->drop_vs[5], cam->eye[0], cam->eye[1], cam->eye[2], 1.0);
    g = r32f((double)x20 * K_BRIGHT);
    set4(out->drop_ps[0], r32f(colour[0] * (double)g), r32f(colour[1] * (double)g),
         r32f(colour[2] * (double)g), 1.0);
    set4(out->drop_ps[1], r32f(colour[0] * (double)K_02), r32f(colour[1] * (double)K_02),
         r32f(colour[2] * (double)K_02), 1.0);
    set4(out->drop_ps[2], 2.0, 2.0, 2.0, 2.0);
    out->drops = b->n_drops;
    for (i = 0; i < BLOB_DROPS; i++) {
        const xb_drop *d = &b->drop[i];
        double sz, z;

        if (i >= b->n_drops) {
            for (r = 0; r < 4; r++)
                set4(out->drop[i][r], 0.0, 0.0, 0.0, 0.0);
            continue;
        }
        sz = d->size / (double)sse_sqrt(d->scale);
        z = r32f((double)d->scale * d->size - sz);
        set4(out->drop[i][0], d->pos[0], d->pos[1], d->pos[2], 0.0);
        set4(out->drop[i][1], d->dir[0], d->dir[1], d->dir[2], 1.0);
        set4(out->drop[i][2], r32f(sz), r32f(sz), r32f(sz), 1.0);
        set4(out->drop[i][3], r32f(z * d->dir[0]), r32f(z * d->dir[1]), r32f(z * d->dir[2]), 1.0);
    }
}

/* ---- weighted centre 0x80058D1F, Blob.md 8 ----------------------------------- */

static double inv_dist2(const float ref[3], const float p[3])
{
    double dx = (double)ref[0] - p[0], dy = (double)ref[1] - p[1], dz = (double)ref[2] - p[2];

    return 1.0 / ((dz * dz + dy * dy) + dx * dx);
}

void blob_weighted_center(const blob_state *b, const float ref[3], float x20, float out[3],
                          float *second)
{
    double w = inv_dist2(ref, b->center), inv;
    float tot = r32f(w), sec = r32f((double)x20 * w * 4.0), acc[3], w32 = r32f(w);
    uint32_t i, a;

    for (a = 0; a < 3; a++)
        acc[a] = r32f(0.0 + (double)b->center[a] * w32);
    for (i = 0; i < b->n_drops; i++) {
        const xb_drop *d = &b->drop[i];

        w = inv_dist2(ref, d->pos);
        w32 = r32f(w);
        sec = r32f((double)x20 * w + sec);
        for (a = 0; a < 3; a++)
            acc[a] = r32f((double)acc[a] + (double)d->pos[a] * w32);
        tot = r32f((double)w32 + tot);
    }
    inv = 1.0 / tot;
    for (a = 0; a < 3; a++)
        out[a] = r32f(acc[a] * inv);
    if (second)
        *second = r32f(inv * sec);
}
