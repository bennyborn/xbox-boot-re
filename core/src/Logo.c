/* Logo, lettering and trade marks, docs/decomp/Logo.md.
 *
 * Set-up: the seven meshes decoded from the ROM as logo_setup leaves them
 * in the GPU buffers (Logo.md 2, 3). They are generated on request into
 * host buffers; the state only holds what logo_update writes (4, 6).
 * Draw: what logo_draw hands the GPU for a camera and t (5).
 *
 * Numerics as in Overview.c: double for the x87, r32f() where the code
 * stores. The order of the sums follows the disassembly as written down in
 * Logo.md and in tools/xemu/logo_model.py.
 */
#include "core_internal.h"

/* float32 constants of the kernel, address of each in the comment.
 * core/test/check_k4.py compares them with out/rom/kernel.img. */
#define K_POS_MUL   0.00987599976f  /* 0x80012984 */
#define K_POS_ADD   161.715363f     /* 0x80012988 */
#define K_UV_MUL    5.80000014e-05f /* 0x8001298C */
#define K_UV_ADD    0.947660983f    /* 0x80012990 */
#define K_E_MUL     0.00250800001f  /* 0x80066974 */
#define K_E_ADD     41.0653687f     /* 0x80066978 */
#define K_ONE       1.0f            /* 0x8001288C */
#define K_CENT      0.00999999978f  /* 0x80012980 */
#define K_MINUS1    -1.0f           /* 0x80012894 */
#define K_ZERO      0.0f            /* 0x80012880 */
#define K_END       8.0f            /* 0x80066964 */
#define K_TAIL      2.0f            /* 0x80066968 */
#define K_FADE      0.800000012f    /* 0x8006696C */
#define K_SLIDE     0.25f           /* 0x80066970 */
#define K_HALF      0.5f            /* 0x80010A18 */
#define K_ALPHA     255.0f          /* 0x80012908 */
#define K_PI        3.14159274f     /* 0x8006695C */
#define K_COL_A     0.815680027f    /* 0x8001299C */
#define K_COL_B     0.592100024f    /* 0x80012998 */
#define K_COL_C     0.529399991f    /* 0x80012994 */

/* Immediates in logo_draw, address of the four bytes in the code. */
#define B_D0        0x3F4BC6A8u     /* 0x8005C104 */
#define B_D1        0x3F5FDF3Bu     /* 0x8005C10E */
#define B_D2        0x3B7F9724u     /* 0x8005C117 */
#define B_E0        0x3E04816Fu     /* 0x8005C121 */
#define B_E1        0x3ED566CFu     /* 0x8005C12A */
#define B_E2        0x3DB88659u     /* 0x8005C134 */
#define B_TEXT      0xFF62CA13u     /* 0x8005C2C4 */

#define LOGO_KEYS   0x80073FB8u     /* two points of three float32 (Logo.md 2.4) */
#define LOGO_END    0x800743D0u     /* end of PACK_LOGO */

/* Logo.md 2.1: vertices, their count, indices, their count; five int16
 * per vertex except mesh E with three. */
static const struct { uint32_t va, nv, ia, ni; } meshes[LOGO_MESHES] = {
    { 0x80072AA0u, 210, 0x800732D8u, 336 },     /* A */
    { 0x800735F0u, 77, 0x800738F8u, 225 },      /* B */
    { 0x80073428u, 34, 0x80073580u, 105 },      /* C */
    { 0x80071008u, 501, 0x800723A0u, 1008 },    /* D */
    { 0x800739DCu, 4, 0x80073A04u, 6 },         /* Q1 */
    { 0x80073A0Cu, 4, 0x80073A34u, 6 },         /* Q2 */
    { 0x80073A40u, 156, 0x80073DE8u, 462 },     /* E */
};

void logo_mesh_info(uint32_t mesh, xb_logo_mesh_desc *out)
{
    out->vertices = meshes[mesh].nv;
    out->indices = meshes[mesh].ni;
    out->floats = mesh == XB_LOGO_E ? 3 : 5;
}

/* decode_vertex5 0x8005B3D4, decode_i16_scaled 0x8005B398 (Logo.md 2.2):
 * fild, fmul m32, fadd m32, fstp m32. The product is exact in double. */
static float unpack(int16_t i, float mul, float add)
{
    return r32f((double)i * mul + add);
}

/* decode_delta16_tilde 0x8005B473 (Logo.md 2.3). */
static int unpack_indices(const xb_state *s, uint32_t at, uint32_t n, uint16_t *idx)
{
    const uint8_t *p = pack_at(s, PACK_LOGO, at), *end = pack_at(s, PACK_LOGO, LOGO_END);
    uint16_t v;
    uint32_t i;

    v = (uint16_t)(int16_t)(int8_t)*p++;
    idx[0] = v;
    for (i = 1; i < n; i++) {
        if (p >= end)
            return XB_ERR_PACK_LAYOUT;
        if (*p == 0x7E) {
            if (end - p < 3)
                return XB_ERR_PACK_LAYOUT;
            v = (uint16_t)(v + (uint16_t)((uint32_t)p[1] << 8 | p[2]));
            p += 3;
        } else {
            v = (uint16_t)(v + (uint16_t)(int16_t)(int8_t)*p);
            p += 1;
        }
        idx[i] = v;
    }
    return XB_OK;
}

int logo_mesh(const xb_state *s, uint32_t mesh, float *vtx, uint16_t *idx)
{
    const uint8_t *p = pack_at(s, PACK_LOGO, meshes[mesh].va);
    uint32_t i, a, nf = mesh == XB_LOGO_E ? 3 : 5;

    for (i = 0; i < meshes[mesh].nv; i++, p += 2 * nf) {
        float *o = vtx + nf * i;

        for (a = 0; a < nf; a++) {
            int16_t q = rd_i16(p + 2 * a);
            if (mesh == XB_LOGO_E)
                o[a] = unpack(q, K_E_MUL, K_E_ADD);
            else
                o[a] = a < 3 ? unpack(q, K_POS_MUL, K_POS_ADD) : unpack(q, K_UV_MUL, K_UV_ADD);
        }
        /* Mesh C: every v with |f32(v - 1)| <= 0.01 becomes -1
         * (0x8005B87F-0x8005B8BD; the comparison holds on equality). */
        if (mesh == XB_LOGO_C) {
            float d = r32f((double)o[4] - K_ONE);
            if (d < 0.0f)
                d = -d;
            if (d <= K_CENT)
                o[4] = K_MINUS1;
        }
    }
    return unpack_indices(s, meshes[mesh].ia, meshes[mesh].ni, idx);
}

void logo_start(logo_state *lg)
{
    int i;

    /* logo_build_mesh_e: M is the identity, so its row 3 is 0
     * (0x8005B997); +0x4C = 0 (0x8005B99F), +0xC4 = 0 (0x8005BAC1);
     * +0xD0 and +0xD4 null. */
    lg->tm = lg->texture = lg->phase = lg->rtt = lg->rtt_d4 = 0;
    for (i = 0; i < 3; i++)
        lg->pos[i] = 0.0f;
    lg->alpha = 0.0f;
    lg->updates = lg->rtt_count = 0;
}

static double clamp01(double x)
{
    /* the two comparisons of 0x8005C5E6-0x8005C623 and 0x8005BFAE-0x8005BFDB;
     * NaN passes both */
    if (x > 1.0)
        return 1.0;
    if (x < 0.0)
        return 0.0;
    return x;
}

/* logo_update 0x8005C4A5 with logo_render_to_texture 0x8005C366 (Logo.md 4, 6). */
void logo_update(const xb_state *s, logo_state *lg, float t)
{
    float T = r32f((double)K_END - K_TAIL), inv32, F;
    double inv, dt;
    int32_t k;
    uint8_t phase;
    int i;

    lg->updates++;
    lg->rtt = 0;
    if (!((double)t >= T)) {            /* test ah, 1: below or unordered */
        lg->tm = 0;
        lg->alpha = 0.0f;
        return;
    }
    lg->tm = 1;
    inv = (double)K_ONE / K_SLIDE;
    inv32 = r32f(inv);
    dt = (double)t - T;
    F = r32f(dt * inv);
    k = (int32_t)F;                     /* cvttss2si; F is in [0, 1) where k is used */
    if (F < K_ZERO || F == K_ZERO) {
        for (i = 0; i < 3; i++)
            lg->pos[i] = rd_f32(pack_at(s, PACK_LOGO, LOGO_KEYS + 4 * i));
    } else if (!(F < K_ONE)) {
        for (i = 0; i < 3; i++)
            lg->pos[i] = rd_f32(pack_at(s, PACK_LOGO, LOGO_KEYS + 12 + 4 * i));
    } else {
        double f = (double)F - k, w = 1.0 - f;

        for (i = 0; i < 3; i++) {
            float k0 = rd_f32(pack_at(s, PACK_LOGO, LOGO_KEYS + 12 * (uint32_t)k + 4 * i));
            float k1 = rd_f32(pack_at(s, PACK_LOGO, LOGO_KEYS + 12 * (uint32_t)k + 12 + 4 * i));
            lg->pos[i] = r32f(w * k0 + f * k1);
        }
    }
    lg->alpha = r32f(clamp01(r32f(dt * inv32)) * K_ALPHA);

    /* logo_render_to_texture: phase = (6 + 0.25 <= t), drawn when the
     * texture does not exist yet or the phase changed. */
    phase = (double)t >= ((double)K_END - K_TAIL) + K_SLIDE;    /* unordered: 0 */
    if (!lg->texture || phase != lg->phase) {
        lg->rtt = 1;
        lg->rtt_d4 = lg->phase;         /* +0xD4 while logo_draw runs */
        lg->rtt_count++;
        lg->texture = 1;
        lg->phase = phase;
    }
}

/* mat4_rot_x 0x800570A3 with fsincos: rows (1,0,0,0), (0,c,s,0), (0,-s,c,0), (0,0,0,1). */
static void rot_x(float a, float m[16])
{
    double sn, cs;

    x87_fsincos(a, &sn, &cs);
    m4_ident(m);
    m[5] = r32f(cs);
    m[6] = r32f(sn);
    m[9] = -r32f(sn);
    m[10] = r32f(cs);
}

/* logo_draw 0x8005BC63 (Logo.md 5). `r` is 1 for the draw into the texture;
 * tm and d4 are +0x4C and +0xD4 as logo_draw sees them. */
void logo_draw(const xb_state *s, const logo_state *lg, const xb_camera *cam, float t, int r,
               xb_logo_params *out)
{
    const float *W = s->camera.frame;   /* this+0x1774 = 0x800707C4 */
    int tm = r ? lg->phase : lg->tm, d4 = r ? lg->rtt_d4 : lg->phase;
    float vp[16], wvp[16];
    int i, j;

    for (i = 0; i < 16; i++) {
        out->world[i] = W[i];
        out->text_world[i] = 0.0f;
    }
    for (i = 0; i < 9; i++)
        for (j = 0; j < 4; j++)
            out->d_vs[i][j] = 0.0f;
    out->quad_colour = out->text_colour = 0;

    /* 0x8005BC86-0x8005BCA6: meshes when r or t < 8 - 2 (unrounded) */
    out->solid = r || ((double)K_END - K_TAIL) > (double)t;
    if (out->solid) {
        /* 0x8005BF6D-0x8005C16C */
        double t6 = (double)K_END - K_TAIL, t0 = (t6 - K_FADE) - K_HALF;
        double rr = (double)K_ONE / (t6 - t0), sl = ((double)t - t0) * rr;
        double l = (sl + sl) - K_ONE;
        float lf = r32f(l), p = r32f(clamp01(-l)), q = r32f(clamp01((1.0 - p) - clamp01(l)));

        m4_mul(cam->view, cam->proj, vp);   /* camera_view_proj 0x8005B06A */
        m4_mul(W, vp, wvp);                 /* mat4_mul 0x800566CF */
        for (i = 0; i < 4; i++)             /* mat4_transpose 0x8005808F */
            for (j = 0; j < 4; j++)
                out->d_vs[i][j] = wvp[j * 4 + i];
        for (i = 4; i < 9; i++)
            out->d_vs[i][1] = out->d_vs[i][3] = 1.0f;
        if (lf < K_ZERO) {
            for (i = 4; i < 6; i++) {
                out->d_vs[i][0] = K_COL_A;
                out->d_vs[i][2] = K_COL_B;
            }
            for (i = 6; i < 8; i++) {
                out->d_vs[i][0] = K_COL_A;
                out->d_vs[i][2] = K_COL_C;
            }
            out->d_vs[8][0] = out->d_vs[8][1] = out->d_vs[8][2] = p;
        } else {
            for (i = 4; i < 6; i++) {
                out->d_vs[i][0] = K_COL_A;
                out->d_vs[i][2] = K_COL_C;
            }
            out->d_vs[6][0] = from_bits(B_D0);
            out->d_vs[6][1] = from_bits(B_D1);
            out->d_vs[6][2] = from_bits(B_D2);
            out->d_vs[7][0] = from_bits(B_E0);
            out->d_vs[7][1] = from_bits(B_E1);
            out->d_vs[7][2] = from_bits(B_E2);
            out->d_vs[8][0] = out->d_vs[8][1] = out->d_vs[8][2] = q;
        }
    }

    /* 0x8005C181-0x8005C193: lettering and TM when +0x4C and not +0xD4 */
    out->text = tm && !d4;
    if (out->text) {
        float rx[16], m[16], rm[16];

        out->quad_colour = (uint32_t)(int32_t)lg->alpha << 24;   /* 0x8005C1BB */
        out->text_colour = B_TEXT;
        rot_x(r32f((double)K_PI * K_HALF), rx);                    /* 0x8005C2DF-0x8005C317 */
        m4_ident(m);
        for (i = 0; i < 3; i++)
            m[12 + i] = lg->pos[i];
        m4_mul(rx, m, rm);
        m4_mul(rm, W, out->text_world);
    }
}
