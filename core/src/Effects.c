/* Veil and glow, docs/decomp/Effects.md.
 *
 * The object fx = anim_object + 0x17EC draws, in every frame with the
 * chamber, a quad and the chamber's depth into a target (section 3), then
 * over the frame the veil from that target and three noise layers (4) and
 * a glow (5). The core computes what the two functions hand the GPU; the
 * target itself is drawn by the host.
 *
 * Numerics as in Overview.c: x87 with a 53-bit mantissa, r32f() marks a
 * store to float32. The order of every operation follows Effects.md and
 * tools/xemu/effects_model.py, which meets the guest's stream word for
 * word.
 */
#include "core_internal.h"

/* float32 constants of the kernel, address of each in the comment.
 * core/test/check_k6.py compares them with out/rom/kernel.img. */
#define K_ZERO      0.0f          /* 0x80012880 */
#define K_ONE       1.0f          /* 0x8001288C */
#define K_MINUS1    -1.0f         /* 0x80012894 */
#define K_HALF      0.5f          /* 0x80010A18 */
#define K_TWO       2.0f          /* 0x80010A24 */
#define K_END       8.0f          /* 0x8006673C */
#define K_TAIL      2.0f          /* 0x80066740 */
#define K_FADE      0.8f          /* 0x80066744 */
#define K_T_GLOW    0.6f          /* 0x80066748 */
#define K_DIST      0.005f        /* 0x8006674C */
#define K_DEPTH     40.0f         /* 0x80066750 */
#define K_GREEN     0.7f          /* 0x80012958 */
#define K_GREEN_OFF 0.1f          /* 0x80012888 */
#define K_FLASH_T   0.3f          /* 0x800128FC */
#define K_FLASH_K   4.0f          /* 0x80012900 */
#define K_FLASH_G   0.75f         /* 0x80012898 */
#define K_FLASH_R   0.625f        /* 0x80012954 */
#define K_FLASH_B   0.4f          /* 0x800128AC */
#define K_TEX_Y     1.875f        /* 0x80012950 */
#define K_TEX_X     2.5f          /* 0x8001294C */
#define K_THIRD     0.33333334f   /* 0x80012948 */
#define K_FIFTH     0.2f          /* 0x800128B0 */
#define K_LAYER     0.6f          /* 0x80012930 */
#define K_GLOW_K    3.3333333f    /* 0x80012944 */
#define K_255       255.0f        /* 0x80012908 */
#define K_GLOW_T    8.0f          /* 0x8001293C */
#define K_GLOW_S    0.33f         /* 0x80012940 */
#define K_QUAD_P    -1.875f       /* 0x8001295C */
#define K_QUAD_Q    -2.5f         /* 0x80012960 */
#define K_T_BLOB    0.6f          /* 0x80065E60 */

/* Immediates in the code, address of the bytes in the comment. */
#define B_GLOW_RGB  0x00A0FF60u   /* 0x80059FF4, imm32 */
#define B_GLOW_MAX  0x000000C4u   /* 0x80059FDE, imm32 */

static void set4(xb_real o[4], double a, double b, double c, double d)
{
    o[0] = r32f(a);
    o[1] = r32f(b);
    o[2] = r32f(c);
    o[3] = r32f(d);
}

/* ((8 - 2) - 0.8) - 0.5, unrounded (0x80059F29-0x80059F3B). */
static double t_flash(void)
{
    return (((double)K_END - K_TAIL) - K_FADE) - K_HALF;
}

/* The clamp of 0x80059D9A-0x80059DC5 and 0x80059F49-0x80059F74: 1 for
 * values above 1, 0 below 0; NaN stays. */
static double clamp01(double x)
{
    if (!(x <= 1.0))
        return x > 1.0 ? 1.0 : x;
    return x < 0.0 ? 0.0 : x;
}

/* The place of the origin on the screen: (0, 0, 0, 1) * view * projection
 * (0x80059A2E), times f32(1 / w) (0x80058196). */
static void origin(const float vp[16], float *px, float *py)
{
    static const float o[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float r[4], k;

    v4_mul_m4(o, vp, r);
    k = r32f(1.0 / (double)r[3]);
    *px = r32f((double)k * r[0]);
    *py = r32f((double)k * r[1]);
}

/* The switch v of anim_draw (0x80056D75-0x80056DA8): the chamber is on and
 * x20 > 0 or t < 0.6. this+0x181D is 1 (Effects.md 2). */
int fx_veil_on(int chamber_on, float t, float x20)
{
    return chamber_on && (x20 > K_ZERO || t < K_T_BLOB);
}

void fx_quads(xb_real (*veil)[7], xb_real (*quad)[3])
{
    static const float xy[4][2] = { { -1, -1 }, { -1, 1 }, { 1, 1 }, { 1, -1 } };
    static const float uv[4][2] = { { 0, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 } };
    int i;

    for (i = 0; i < 4; i++) {
        if (veil) {
            veil[i][0] = xy[i][0];
            veil[i][1] = xy[i][1];
            veil[i][2] = K_ONE;
            veil[i][3] = uv[i][0];
            veil[i][4] = uv[i][1];
            /* 0x8005A48F-0x8005A558: p from v, q from u */
            veil[i][5] = r32f(((double)K_TWO * uv[i][1] + K_MINUS1) * K_QUAD_P);
            veil[i][6] = r32f(((double)K_TWO * uv[i][0] + K_MINUS1) * K_QUAD_Q);
        }
        if (quad) {
            quad[i][0] = xy[i][0];
            quad[i][1] = xy[i][1];
            quad[i][2] = K_ONE;
        }
    }
}

void fx_draw(const xb_camera *cam, float aspect, float t, float x20, const xb_track *trk,
             xb_fx_params *out)
{
    double D = trk->blob_dist, az = trk->azimuth, el = trk->elevation;
    double X, Y, g, f, G, h, s;
    float vp[16], px, py, kx, ky, inv, off, c6c, c70, g32, s32, is;
    int i, n;

    m4_mul(cam->view, cam->proj, vp);            /* camera_view_proj 0x8005B06A */
    origin(vp, &px, &py);

    /* 3: fx_render_offscreen 0x8005A194 */
    kx = r32f(D * K_DIST);
    ky = r32f(((double)aspect * D) * K_DIST);
    set4(out->quad_vs[0], 1.0, 1.0, 1.0, 1.0);
    set4(out->quad_vs[1], 0.0, 0.0, 0.0, 1.0);
    set4(out->quad_vs[2], kx, ky, 0.0, 1.0);
    set4(out->quad_vs[3], (double)K_HALF - (double)px * kx, (double)K_HALF - (double)py * ky,
         K_HALF, 0.0);
    set4(out->quad_m[0], 1.0, 0.0, 0.0, 0.0);
    set4(out->quad_m[1], 0.0, 1.0, 0.0, 0.0);
    set4(out->quad_m[2], 0.0, 0.0, 1.0, 0.0);
    set4(out->quad_m[3], 0.0, 0.0, 1.0, 0.0);
    inv = r32f(1.0 / (double)r32f((double)K_DEPTH + K_DEPTH));
    off = r32f((double)K_DEPTH - D);
    set4(out->depth_vs[0], inv, inv, inv, 1.0);
    set4(out->depth_vs[1], off, off, off, 1.0);

    /* 4: the veil, 0x80059BCD-0x80059F03 */
    out->veil = (uint32_t)fx_veil_on(cam->chamber_on, t, x20);
    out->depth_func = cam->logo_on ? 0x202u : 0x207u;
    g = (double)x20 * K_GREEN - K_GREEN_OFF;
    if (!(0.0 < g))
        g = 0.0;
    set4(out->veil_ps[0], (double)K_ZERO * g, g, (double)K_ZERO * g, 1.0);
    f = ((double)t - (t_flash() + K_FLASH_T)) * K_FLASH_K;
    f = clamp01(f) * K_FLASH_G;
    set4(out->veil_ps[1], (double)K_FLASH_R * f, f, f * K_FLASH_B, 0.0);
    X = (D * K_DIST) * K_HALF;
    Y = ((double)aspect * D) * K_DIST;
    c6c = r32f((double)py * Y * K_TEX_Y);
    c70 = r32f((double)px * X * K_TEX_X);
    for (i = 0; i < 3; i++) {
        double q = ((double)(2 - i) * K_THIRD - K_FIFTH) * K_LAYER;

        set4(out->veil_vs[2 * i], (double)c6c - el * q, az * q - (double)c70, 0.0, 0.0);
        set4(out->veil_vs[2 * i + 1], -Y, -X, 1.0, 1.0);
    }

    /* 5: the glow, 0x80059F23-0x8005A100 */
    G = clamp01(((double)t - t_flash()) * K_GLOW_K);
    if (t < K_T_GLOW) {
        h = (double)K_T_GLOW * K_FIFTH;
        G = (double)t < h ? (double)t / h : (double)K_ONE - ((double)t - h) / K_T_GLOW;
    }
    g32 = r32f(G);
    n = (int)((double)g32 * K_255);              /* 0x80055F8C, truncated */
    n = n > 0xFF ? 0xFF : (n < 0 ? 0 : n);       /* 0x80059FC4-0x80059FD9 */
    n += n;                                      /* 0x80059FDB, then at most 0xC4 */
    if (n > (int)B_GLOW_MAX)
        n = (int)B_GLOW_MAX;
    for (i = 0; i < 16; i++)
        out->glow_world[i] = 0.0f;
    out->glow = n != 0;
    out->glow_alpha = (uint32_t)n;
    out->glow_colour = 0;
    out->glow_s = 0.0f;
    if (!n)
        return;
    /* 0x80059FF0-0x80059FF9: shl 24, or 0xA0FF60, the argument of 0x8003CED0 */
    out->glow_colour = ((uint32_t)n << 24) | B_GLOW_RGB;
    if (t < K_T_GLOW)
        s = (D * K_T_GLOW) / ((double)t * K_GLOW_T);
    else
        s = (double)K_GLOW_S / g32;
    s32 = r32f(s);
    is = r32f(1.0 / (double)s32);
    out->glow_s = s32;
    out->glow_world[0] = is;                      /* mat4_diag3 0x80059ABB */
    out->glow_world[5] = is;
    out->glow_world[14] = 1.0f;                   /* 0x8005A0C2 */
    out->glow_world[15] = 1.0f;
}
