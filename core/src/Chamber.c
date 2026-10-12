/* The chamber around the blob, docs/decomp/Chamber.md.
 *
 * Set-up (chamber_setup 0x8005FC65): quaternions, positions and track
 * index lists unpacked from the ROM (Chamber.md 3), every part placed
 * (5). Per frame the tracks are a function of t alone (6), and so is the
 * world matrix of every part (7); chamber_at() and chamber_world()
 * evaluate them for any t. The geometry of the twelve groups (4) is
 * generated on request into buffers of the host, once, the way
 * prim_group_build fills its vertex and index buffers.
 *
 * Numerics as in Overview.c. The order of every sum follows Chamber.md
 * and tools/xemu/chamber_model.py, which meets the guest bit for bit.
 */
#include "core_internal.h"

/* float32 constants of the kernel, address of each in the comment.
 * core/test/check_k2.py compares them with out/rom/kernel.img. */
#define K_PI        3.1415927f    /* 0x8006697C */
#define K_ONE       1.0f          /* 0x8001288C */
#define K_MINUS1    -1.0f         /* 0x80012894 */
#define K_HALF      0.5f          /* 0x80010A18 */
#define K_QUAT      3.0534353e-05f /* 0x800129A8 */
#define K_TRK_KX    0.002755f     /* 0x8006699C */
#define K_TRK_KY    0.002755f     /* 0x800669A0 */
#define K_TRK_KZ    0.00244f      /* 0x800669A4 */
#define K_TRK_OX    -0.159046f    /* 0x800669A8 */
#define K_TRK_OY    -0.741611f    /* 0x800669AC */
#define K_TRK_OZ    2.155624f     /* 0x800669B0 */
#define K_PLC_KX    0.004131f     /* 0x80066984 */
#define K_PLC_KY    0.008252f     /* 0x80066988 */
#define K_PLC_KZ    0.004421f     /* 0x8006698C */
#define K_PLC_OX    -27.844984f   /* 0x80066990 */
#define K_PLC_OY    -0.228729f    /* 0x80066994 */
#define K_PLC_OZ    0.497086f     /* 0x80066998 */
#define K_SIDE      0.0f          /* 0x800669C8 */
#define K_T_START   0.6f          /* 0x800669C0 */
#define K_T_QUARTER 0.25f         /* 0x8001289C */
#define K_T_SPAN    4.5f          /* 0x800669C4 */
#define K_T_KEYS    28.0f         /* 0x800129B0 */
#define K_LOD_END   8.0f          /* 0x800669B4 */
#define K_LOD_TWO   2.0f          /* 0x800669B8 */
#define K_LOD_LEAD  0.8f          /* 0x800669BC */
#define K_CYL_U     4.0f          /* 0x80012900 */
#define K_CYL_V     32.0f         /* 0x800129A4 */
#define K_LATHE_UV  10.0f         /* 0x800128D8 */
#define K_LV_FOV    0.6666667f    /* 0x800129B8 */
#define K_LV_FAR    500.0f        /* 0x800129BC */
#define K_LV_TEXEL  256.5f        /* 0x800129B4 */
#define K_LIGHT_D   0.13f         /* 0x800128A4 */
#define K_LIGHT_A   0.001f        /* 0x800128A0 */
#define K_GROUP_R   0.2079f       /* 0x800129AC */
#define K_GROUP_B   0.1f          /* 0x80012888 */

/* ROM layout, Chamber.md 2. */
#define ROT_TRACKS    0x800743D0u
#define POS_TRACKS    0x80074E08u
#define TRACK_STRIDE  0x5Au
#define N_POSITIONS   0x80075948u
#define POSITIONS     0x80075950u
#define N_QUATS       0x800760D0u
#define QUATS         0x800760D8u
#define QUAT_SIGNS    0x80076A88u

/* chamber_build_groups 0x8005F7B8: per slot the primitive type, its forms
 * and placements, the vertex format (Chamber.md 2, 4.1). */
static const struct group_def {
    uint8_t  type, format;
    uint16_t form_size, place_size;
    uint32_t forms, n_forms, places, n_places;
} groups[XB_CHAMBER_GROUPS] = {
    { 0, 1, 0x01, 0x10, 0x80076C80u, 2, 0x80076AC0u, 28 },
    { 1, 0, 0x02, 0x14, 0x80077034u, 6, 0x80076C88u, 47 },
    { 2, 0, 0x00, 0x18, 0,           1, 0x80077040u, 72 },
    { 3, 0, 0x08, 0x10, 0x80077C40u, 7, 0x80077700u, 84 },
    { 4, 0, 0x10, 0x0C, 0x80077E10u, 3, 0x80077C78u, 34 },
    { 5, 1, 0x14C, 0x0C, 0x80077E88u, 5, 0x80077E40u, 6 },
};

#define SCENE(s, va) pack_at(s, PACK_SCENE, va)

/* ---- unpacking, Chamber.md 3 -------------------------------------------- */

/* decode_delta16_7f 0x8005DF27: 30 signed byte differences; 0x7F introduces
 * a 16-bit difference, high byte first. 16-bit arithmetic. */
static void expand(const uint8_t *p, int16_t out[CH_TRACK_LEN])
{
    uint16_t v = 0;
    int k;

    for (k = 0; k < CH_TRACK_LEN; k++) {
        if (*p == 0x7F) {
            v = (uint16_t)(v + (uint16_t)((p[1] << 8) | p[2]));
            p += 3;
        } else {
            v = (uint16_t)(v + (uint16_t)(int8_t)*p);
            p += 1;
        }
        out[k] = (int16_t)v;
    }
}

static void decode(chamber_state *ch, const xb_state *s)
{
    static const float tk[3] = { K_TRK_KX, K_TRK_KY, K_TRK_KZ };
    static const float to[3] = { K_TRK_OX, K_TRK_OY, K_TRK_OZ };
    const uint8_t *q = SCENE(s, QUATS), *signs = SCENE(s, QUAT_SIGNS), *p = SCENE(s, POSITIONS);
    int i, a;

    /* decode_quats 0x8005DDE2: z stays unrounded in the register. */
    for (i = 0; i < CH_QUATS; i++) {
        double x = r32f(rd_i16(q + 6 * i) * (double)K_QUAT);
        double y = r32f(rd_i16(q + 6 * i + 2) * (double)K_QUAT);
        double z = rd_i16(q + 6 * i + 4) * (double)K_QUAT;
        float w = sse_sqrt(r32f(((K_ONE - x * x) - y * y) - z * z));

        if (!((rd_u32(signs + 4 * (i >> 5)) >> (i & 31)) & 1))
            w = r32f((double)w * K_MINUS1);
        /* Quaternion 238 gives the root of a negative number; the guest
         * holds the pattern 0xFFC00000 there (Chamber.md 3). The sign of a
         * NaN after the multiplication is not defined in C, so it is set. */
        if (w != w)
            w = from_bits(0xFFC00000u);
        ch->quats[i][0] = (float)x;
        ch->quats[i][1] = (float)y;
        ch->quats[i][2] = r32f(z);
        ch->quats[i][3] = w;
    }
    /* decode_positions 0x8005DEB1 */
    for (i = 0; i < CH_POSITIONS; i++)
        for (a = 0; a < 3; a++)
            ch->positions[i][a] = r32f(rd_i16(p + 6 * i + 2 * a) * (double)tk[a] + to[a]);
    for (i = 0; i < CH_ROT_TRACKS; i++)
        expand(SCENE(s, ROT_TRACKS + TRACK_STRIDE * i), ch->rot_tracks[i]);
    for (i = 0; i < CH_POS_TRACKS; i++)
        expand(SCENE(s, POS_TRACKS + TRACK_STRIDE * i), ch->pos_tracks[i]);
}

/* ---- placement, Chamber.md 5 -------------------------------------------- */

static void diag(const float sc[4], float m[16])
{
    m4_ident(m);
    m[0] = sc[0];
    m[5] = sc[1];
    m[10] = sc[2];
}

static int place(chamber_state *ch, const xb_state *s, const struct group_def *g, uint32_t gi,
                 uint32_t j, chamber_part *pp)
{
    static const float pk[3] = { K_PLC_KX, K_PLC_KY, K_PLC_KZ };
    static const float po[3] = { K_PLC_OX, K_PLC_OY, K_PLC_OZ };
    const uint8_t *r = SCENE(s, g->places + g->place_size * j);
    const uint8_t *ip = g->type == 0 ? r : r + 2;      /* the three int16 of the position */
    const uint8_t *ib = g->type == 0 ? r + 6 : r + 8;  /* form, position track, rotation track */
    float pos[3], S[16], z;
    int a;

    pp->type = g->type;
    pp->group = (uint8_t)gi;
    pp->index = (uint16_t)j;
    pp->pad = 0;
    for (a = 0; a < 3; a++)
        pos[a] = r32f(rd_i16(ip + 2 * a) * (double)pk[a] + po[a]);
    pp->form = g->type == 2 ? 0 : (int8_t)ib[0];
    pp->pos_track = (int8_t)ib[1];
    pp->rot_track = (int8_t)ib[2];
    if (pp->pos_track >= CH_POS_TRACKS || pp->rot_track >= CH_ROT_TRACKS ||
        pp->form >= (int)g->n_forms || pp->form < 0)
        return XB_ERR_PACK_LAYOUT;

    pp->scale[3] = 1.0f;
    switch (g->type) {
    case 0:                                             /* radius r: (r, r, r) */
        pp->scale[0] = pp->scale[1] = pp->scale[2] = rd_f32(r + 0xC);
        break;
    case 1:                                             /* (r, r, 2h) */
        pp->scale[0] = pp->scale[1] = rd_f32(r + 0xC);
        pp->scale[2] = r32f((double)rd_f32(r + 0x10) + rd_f32(r + 0x10));
        break;
    case 2:                                             /* (+0x14, +0xC, +0x10) */
        pp->scale[0] = rd_f32(r + 0x14);
        pp->scale[1] = rd_f32(r + 0xC);
        pp->scale[2] = rd_f32(r + 0x10);
        break;
    case 3:
        pp->scale[0] = pp->scale[1] = pp->scale[2] = rd_f32(r + 0xC);
        break;
    default:                                            /* cone, lathe: S = 1 */
        pp->scale[0] = pp->scale[1] = pp->scale[2] = 1.0f;
        break;
    }
    if (g->type == 0) {
        m4_ident(pp->L);
    } else {
        int qi = (int8_t)r[0];
        if (qi < 0)
            return XB_ERR_PACK_LAYOUT;
        quat_to_m4(ch->quats[qi], pp->L);
    }
    pp->L[12] = pos[0];
    pp->L[13] = pos[1];
    pp->L[14] = pos[2];
    if (g->type >= 4) {
        for (a = 0; a < 16; a++)
            pp->SL[a] = pp->L[a];
    } else {
        diag(pp->scale, S);
        m4_mul(S, pp->L, pp->SL);
    }

    /* Side +0x118: height of the first state of the part's tracks. */
    z = pos[2];
    if (pp->rot_track >= 0) {
        float R[16], v[3];
        quat_to_m4(ch->quats[ch->rot_tracks[pp->rot_track][0]], R);
        v3_mul_m3(pos, R, v);
        z = v[2];
    }
    if (pp->pos_track >= 0)
        z = r32f((double)z + ch->positions[ch->pos_tracks[pp->pos_track][0]][2]);
    pp->side = z > K_SIDE;
    return XB_OK;
}

int chamber_start(chamber_state *ch, const xb_state *s)
{
    uint32_t gi, j, n = 0;
    int i, k;

    if (rd_u32(SCENE(s, N_POSITIONS)) != CH_POSITIONS || rd_u32(SCENE(s, N_QUATS)) != CH_QUATS)
        return XB_ERR_PACK_LAYOUT;
    decode(ch, s);
    for (i = 0; i < CH_ROT_TRACKS; i++)
        for (k = 0; k < CH_TRACK_LEN; k++)
            if ((uint16_t)ch->rot_tracks[i][k] >= CH_QUATS)
                return XB_ERR_PACK_LAYOUT;
    for (i = 0; i < CH_POS_TRACKS; i++)
        for (k = 0; k < CH_TRACK_LEN; k++)
            if ((uint16_t)ch->pos_tracks[i][k] >= CH_POSITIONS)
                return XB_ERR_PACK_LAYOUT;
    for (gi = 0; gi < XB_CHAMBER_GROUPS; gi++)
        for (j = 0; j < groups[gi].n_places; j++) {
            int rc = place(ch, s, &groups[gi], gi, j, &ch->parts[n++]);
            if (rc != XB_OK)
                return rc;
        }
    return n == CH_PARTS ? XB_OK : XB_ERR_PACK_LAYOUT;
}

/* ---- motion, Chamber.md 6 ------------------------------------------------ */

void chamber_at(const xb_state *s, float t, xb_chamber_pose *out)
{
    const chamber_state *ch = &s->chamber;
    /* u is stored (fst, 0x8005FA12); the product takes the quotient from
     * the register, unrounded (0x8005FA15) */
    double uq = ((double)t - ((double)K_T_START + K_T_QUARTER)) / K_T_SPAN;
    float u = r32f(uq);
    float k = r32f(uq * K_T_KEYS), fr = 0.0f;
    int i = 0, n, a;

    if (u > 0.0f && u < 1.0f) {
        i = (int)k;
        fr = r32f((double)k - i);
    }
    out->t = t;
    out->u = u;
    out->k = k;
    for (n = 0; n < CH_POS_TRACKS; n++) {
        const int16_t *tr = ch->pos_tracks[n];
        const float *p = ch->positions[tr[u > 0.0f ? (u >= 1.0f ? 29 : i) : 0]];

        if (u > 0.0f && u < 1.0f) {
            const float *b = ch->positions[tr[i + 1]];
            double v = 1.0 - fr;
            for (a = 0; a < 3; a++)
                out->pos[n][a] = r32f((double)fr * b[a] + v * p[a]);
        } else {
            for (a = 0; a < 3; a++)
                out->pos[n][a] = p[a];
        }
    }
    for (n = 0; n < CH_ROT_TRACKS; n++) {
        const int16_t *tr = ch->rot_tracks[n];
        float q[4];

        if (u > 0.0f && u < 1.0f) {
            quat_slerp(s, ch->quats[tr[i]], ch->quats[tr[i + 1]], fr, q);
        } else {
            const float *src = ch->quats[tr[u > 0.0f ? 29 : 0]];
            for (a = 0; a < 4; a++)
                q[a] = src[a];
        }
        quat_to_m4(q, out->rot[n]);
    }
    out->detail = (double)t >= ((double)K_LOD_END - K_LOD_TWO) - K_LOD_LEAD;
}

/* prim_group_draw 0x8005EE08, 0x8005F057-0x8005F0C3 (Chamber.md 7). */
void chamber_world(const xb_state *s, const xb_chamber_pose *pose, uint32_t part, float w[16])
{
    const chamber_part *pp = &s->chamber.parts[part];
    float L[16], S[16];
    int a;

    if (pp->pos_track < 0 && pp->rot_track < 0) {
        for (a = 0; a < 16; a++)
            w[a] = pp->SL[a];
        return;
    }
    if (pp->rot_track >= 0)
        m4_mul(pp->L, pose->rot[pp->rot_track], L);
    else
        for (a = 0; a < 16; a++)
            L[a] = pp->L[a];
    if (pp->pos_track >= 0)
        v3_add(L + 12, pose->pos[pp->pos_track], L + 12);
    diag(pp->scale, S);
    m4_mul(S, L, w);
}

/* chamber_light_view 0x8005FDF9 without the drawing (Chamber.md 8). */
void chamber_light_view(const float center[3], uint32_t side, float out[16])
{
    float tgt[3], up[3], view[16], proj[16], tex[16], vp[16];

    tgt[0] = center[0];
    tgt[1] = center[1];
    tgt[2] = r32f((double)center[2] + (side ? 1.0 : (double)K_MINUS1));
    up[0] = 0.0f;
    up[1] = side ? K_MINUS1 : 1.0f;
    up[2] = 0.0f;
    camera_view_toward(center, tgt, up, view);
    camera_perspective(r32f((double)K_PI * K_LV_FOV), 1.0f, 1.0f, K_LV_FAR, proj);
    m4_ident(tex);
    tex[0] = 256.0f;
    tex[5] = -256.0f;
    tex[10] = 65535.0f;                  /* 0x477FFF00 */
    tex[12] = tex[13] = K_LV_TEXEL;
    m4_mul(view, proj, vp);
    m4_mul(vp, tex, out);
}

/* ---- geometry, Chamber.md 4 ---------------------------------------------- */

#define TWO_PI ((double)K_PI + K_PI)        /* 0x8006697C doubled, exact */
#define MAX_SEG 64

typedef struct mesh_out {
    float    *pos, *attr;
    uint16_t *idx;
    uint32_t  nv, ni, stride, uv;
} mesh_out;

static void vert(mesh_out *m, const double p[3], const double b[9], double u, double v)
{
    if (m->pos) {
        float *pp = m->pos + 3 * m->nv, *aa = m->attr + m->stride * m->nv;
        int i;

        for (i = 0; i < 3; i++)
            pp[i] = (float)p[i];
        if (m->uv) {
            *aa++ = (float)u;
            *aa++ = (float)v;
        }
        for (i = 0; i < 9; i++)
            aa[i] = (float)b[i];
    }
    m->nv++;
}

static void index1(mesh_out *m, uint32_t v)
{
    if (m->idx)
        m->idx[m->ni] = (uint16_t)v;
    m->ni++;
}

static void cs32(double a, float *c, float *s)
{
    double sn, cn;

    x87_fsincos(a, &sn, &cn);
    *c = (float)cn;
    *s = (float)sn;
}

static void basis3(double b[9], const float a[3], const float c[3], const float n[3])
{
    int i;

    for (i = 0; i < 3; i++) {
        b[i] = a[i];
        b[3 + i] = c[i];
        b[6 + i] = n[i];
    }
}

/* 0x800612D3: strip over a grid of (cols + 1) x (rows + 1) vertices, in
 * blocks of 14 columns (Chamber.md 4.3). */
static void strip(mesh_out *m, uint32_t cols, uint32_t rows, int lead, int trail,
                  uint32_t start, uint32_t row, uint32_t step)
{
    uint32_t i, r;

    if (!row)
        row = cols + 1;
    if (cols >= 15) {
        strip(m, 14, rows, lead, 1, start, row, step);
        strip(m, cols - 14, rows, 1, trail, step * 14 + start, row, step);
        return;
    }
    if (lead)
        index1(m, start);
    index1(m, start);
    for (i = 1; i <= cols; i++) {
        index1(m, step * i + start);
        index1(m, step * i + start);
    }
    for (r = 0; r < rows; r++) {
        index1(m, r * row + start);
        for (i = 0; i <= cols; i++) {
            index1(m, r * row + i * step + start);
            index1(m, i * step + (r + 1) * row + start);
        }
        if (r + 1 < rows)
            index1(m, cols * step + start + (r + 1) * row);
    }
    if (trail)
        index1(m, cols * step + start + rows * row);
}

uint32_t strip_indices(uint16_t *idx, uint32_t cols, uint32_t rows, int lead, int trail,
                       uint32_t start)
{
    mesh_out m = { 0, 0, 0, 0, 0, 0, 0 };

    m.idx = idx;
    strip(&m, cols, rows, lead, trail, start, 0, 1);
    return m.ni;
}

/* 0x8006127E */
static uint32_t strip_count(uint32_t cols, uint32_t rows, int lead, int trail)
{
    if (cols < 15)
        return (lead != 0) + ((cols + 2) * rows + cols) * 2 + (trail != 0);
    return strip_count(cols - 14, rows, 1, trail) + strip_count(14, rows, lead, 1);
}

static void ring_table(uint32_t n, float tb[MAX_SEG + 1][3])
{
    uint32_t j;

    for (j = 0; j <= n; j++) {
        float a = r32f((double)j * (TWO_PI / n));
        cs32(a, &tb[j][0], &tb[j][1]);
        tb[j][2] = r32f((1.0 / TWO_PI) * a);
    }
}

static const double ident3[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
static const double down3[9] = { 1, 0, 0, 0, -1, 0, 0, 0, -1 };

/* prim_gen_sphere 0x8005C9DC */
static void gen_sphere(mesh_out *m, const uint8_t *f, uint32_t lod, uint32_t base)
{
    uint32_t n = (uint32_t)(int8_t)f[0] >> lod, rings = n / 2, i, j, r, start, cur;
    float dlat = r32f((double)K_PI / (rings - 1.0));

    for (i = 0; i < rings; i++) {
        float lat = r32f((double)i * dlat), c1, s1;

        cs32(lat, &c1, &s1);
        for (j = 0; j < n; j++) {
            float lon = r32f((double)j * (TWO_PI / n)), c2, s2, p[3], t[3], c[3];
            double pd[3], b[9];

            cs32(lon, &c2, &s2);
            p[0] = c1;
            p[1] = r32f((double)s1 * c2);
            p[2] = r32f((double)s2 * s1);
            t[0] = 0.0f;
            t[1] = -s2;
            t[2] = c2;
            v3_cross(t, p, c);
            basis3(b, t, c, p);
            pd[0] = p[0];
            pd[1] = p[1];
            pd[2] = p[2];
            vert(m, pd, b, lon / TWO_PI, (double)r32f(1.0 / K_PI) * lat);
        }
    }
    start = base;
    for (r = 0; r + 1 < rings; r++) {
        cur = start;
        for (j = 0; j <= n; j++) {
            index1(m, n + cur);
            index1(m, cur);
            cur = j + 1 < n ? cur + 1 : start;
        }
        start += n;
    }
}

static void caps_and_strip(mesh_out *m, uint32_t n, uint32_t rows, uint32_t base)
{
    uint32_t i;

    for (i = base; i < base + 2 * n; i++)
        index1(m, i);
    strip(m, n, rows, 0, 0, base + 2 * n, 0, 1);
}

/* prim_gen_cylinder 0x8005CBC8 */
static void gen_cylinder(mesh_out *m, const uint8_t *f, uint32_t lod, uint32_t base)
{
    uint32_t rows = (uint32_t)(int8_t)f[0], n = (uint32_t)(int8_t)f[1] >> lod, r, j;
    float tb[MAX_SEG + 1][3], dz;
    double p[3], b[9];
    int i;

    ring_table(n, tb);
    for (i = (int)n - 1; i >= 0; i--) {
        p[0] = tb[i][0]; p[1] = tb[i][1]; p[2] = 1.0;
        vert(m, p, ident3, r32f((double)tb[i][0] + 0.5), r32f((double)tb[i][1] + 0.5));
    }
    for (j = 0; j < n; j++) {
        p[0] = tb[j][0]; p[1] = tb[j][1]; p[2] = 0.0;
        vert(m, p, down3, r32f((double)tb[j][0] + 0.5), r32f((double)tb[j][1] + 0.5));
    }
    dz = r32f(1.0 / rows);
    for (r = 0; r <= rows; r++) {
        float z = r32f((double)r * dz);
        for (j = 0; j <= n; j++) {
            float x = tb[j][0], y = tb[j][1];
            p[0] = x; p[1] = y; p[2] = z;
            b[0] = -y; b[1] = x; b[2] = 0.0;
            b[3] = 0.0; b[4] = 0.0; b[5] = 1.0;
            b[6] = x; b[7] = y; b[8] = 0.0;
            vert(m, p, b, (double)tb[j][2] * K_CYL_U, (double)K_CYL_V * z);
        }
    }
    caps_and_strip(m, n, rows, base);
}

/* prim_gen_cone 0x8005CF51 */
static void gen_cone(mesh_out *m, const uint8_t *f, uint32_t lod, uint32_t base)
{
    float r0 = rd_f32(f), r1 = rd_f32(f + 4), h = rd_f32(f + 8);
    uint32_t rows = (uint32_t)(int8_t)f[0xC], n = (uint32_t)(int8_t)f[0xD] >> lod, r, j;
    float tb[MAX_SEG + 1][3], A[MAX_SEG + 1][2], B[MAX_SEG + 1][2], df;
    double p[3], b[9];
    int i;

    ring_table(n, tb);
    for (j = 0; j <= n; j++) {
        A[j][0] = r32f((double)tb[j][0] * r0);
        A[j][1] = r32f((double)tb[j][1] * r0);
        B[j][0] = r32f((double)tb[j][0] * r1);
        B[j][1] = r32f((double)tb[j][1] * r1);
    }
    for (i = (int)n - 1; i >= 0; i--) {
        p[0] = B[i][0]; p[1] = B[i][1]; p[2] = h;
        vert(m, p, ident3, r32f((double)B[i][0] + 0.5), r32f((double)B[i][1] + 0.5));
    }
    for (j = 0; j < n; j++) {
        p[0] = A[j][0]; p[1] = A[j][1]; p[2] = 0.0;
        vert(m, p, down3, r32f((double)A[j][0] + 0.5), r32f((double)A[j][1] + 0.5));
    }
    df = r32f(1.0 / rows);
    for (r = 0; r <= rows; r++) {
        float fz = r32f((double)r * df);
        for (j = 0; j <= n; j++) {
            float t[3], sv[3], c[3];

            p[0] = ((double)B[j][0] - A[j][0]) * fz + A[j][0];
            p[1] = ((double)B[j][1] - A[j][1]) * fz + A[j][1];
            p[2] = (double)fz * h;
            t[0] = -B[j][1];
            t[1] = B[j][0];
            t[2] = 0.0f;
            v3_norm(t);
            sv[0] = r32f((double)B[j][0] - A[j][0]);
            sv[1] = r32f((double)B[j][1] - A[j][1]);
            sv[2] = h;
            v3_norm(sv);
            v3_cross(t, sv, c);
            basis3(b, t, sv, c);
            vert(m, p, b, tb[j][2], fz);
        }
    }
    caps_and_strip(m, n, rows, base);
}

/* prim_gen_box 0x8005D398: per side four corners in units of 0.5 and the
 * vectors a, b, n as the code sets them (Chamber.md 4.3). */
static const int8_t box[6][4][3] = {
    { {1, -1, 1}, {1, 1, 1}, {1, 1, -1}, {1, -1, -1} },
    { {1, 1, 1}, {-1, 1, 1}, {-1, 1, -1}, {1, 1, -1} },
    { {-1, 1, 1}, {-1, -1, 1}, {-1, -1, -1}, {-1, 1, -1} },
    { {-1, -1, 1}, {1, -1, 1}, {1, -1, -1}, {-1, -1, -1} },
    { {-1, 1, 1}, {1, 1, 1}, {1, -1, 1}, {-1, -1, 1} },
    { {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1} },
};
static const int8_t box_basis[6][9] = {
    { 0, 1, 0, 0, 0, 1, 1, 0, 0 },
    { -1, 0, 0, 0, 0, 1, 0, 1, 0 },
    { 0, -1, 0, 0, 0, 1, 0, -1, 0 },
    { 1, 0, 0, 0, 0, 1, 0, -1, 0 },
    { 1, 0, 0, 0, 1, 0, 0, 0, 1 },
    { 1, 0, 0, 0, -1, 0, 0, 0, -1 },
};
static const uint8_t box_uv[4][2] = { {0, 1}, {1, 1}, {1, 0}, {0, 0} };

static void gen_box(mesh_out *m, uint32_t base)
{
    uint32_t k, i;

    for (k = 0; k < 6; k++) {
        double b[9], p[3];

        for (i = 0; i < 9; i++)
            b[i] = box_basis[k][i];
        for (i = 0; i < 4; i++) {
            p[0] = 0.5 * box[k][i][0];
            p[1] = 0.5 * box[k][i][1];
            p[2] = 0.5 * box[k][i][2];
            vert(m, p, b, box_uv[i][0], box_uv[i][1]);
        }
        index1(m, base + 4 * k);
        index1(m, base + 4 * k + 1);
        index1(m, base + 4 * k + 2);
        index1(m, base + 4 * k);
        index1(m, base + 4 * k + 2);
        index1(m, base + 4 * k + 3);
    }
}

/* prim_gen_torus 0x8005D74F */
static void gen_torus(mesh_out *m, const uint8_t *f, uint32_t lod, uint32_t base)
{
    float rt = rd_f32(f);
    uint32_t n = (uint32_t)(int8_t)f[4] >> lod, k = (uint32_t)(int8_t)f[5] >> lod, i, j;
    uint32_t cur, nxt, a, b;

    for (i = 0; i < k; i++) {
        float al = r32f((double)i * (TWO_PI / k)), c1, s1, rad;

        cs32(al, &c1, &s1);
        rad = r32f((double)c1 * rt + 1.0);
        for (j = 0; j < n; j++) {
            float be = r32f((double)j * (TWO_PI / n)), c2, s2, t[3], sv[3], c[3];
            double p[3], bs[9];

            cs32(be, &c2, &s2);
            t[0] = -s2;
            t[1] = c2;
            t[2] = 0.0f;
            sv[0] = r32f(-((double)c2 * s1));
            sv[1] = r32f(-((double)s2 * s1));
            sv[2] = c1;
            v3_cross(t, sv, c);
            basis3(bs, t, sv, c);
            p[0] = (double)c2 * rad;
            p[1] = (double)s2 * rad;
            p[2] = (double)s1 * rt;
            vert(m, p, bs, (1.0 / TWO_PI) * be, (1.0 / TWO_PI) * al);
        }
    }
    cur = base;
    nxt = base + n;
    for (i = 0; i < k; i++) {
        a = cur;
        b = nxt;
        for (j = 0; j <= n; j++) {
            index1(m, a);
            index1(m, b);
            if (j + 1 < n) {
                a++;
                b++;
            } else {
                a = cur;
                b = nxt;
            }
        }
        cur += n;
        nxt = i + 2 < k ? nxt + n : base;
    }
}

/* 0x8005C665: the rotation matrix of q, not transposed. */
static void quat_to_m4_row(const float q[4], float m[16])
{
    float t[16];

    quat_to_m4(q, t);
    m4_ident(m);
    m[0] = t[0]; m[1] = t[4]; m[2] = t[8];
    m[4] = t[1]; m[5] = t[5]; m[6] = t[9];
    m[8] = t[2]; m[9] = t[6]; m[10] = t[10];
}

#define LATHE_POINTS 19

/* prim_gen_lathe 0x8005E210 */
static void gen_lathe(mesh_out *m, const uint8_t *f, uint32_t lod, uint32_t base)
{
    float A[3], C[3], P[2 * LATHE_POINTS][3], N[2 * LATHE_POINTS][3], N2[2 * LATHE_POINTS][3];
    uint8_t K[2 * LATHE_POINTS];
    uint32_t np = (uint32_t)(int8_t)f[0x149], nv = 0, i, v, r, segs, a, b, j;
    float dang, vstep;

    for (i = 0; i < 3; i++) {
        A[i] = rd_f32(f + 0x130 + 4 * i);
        C[i] = rd_f32(f + 0x13C + 4 * i);
    }
    for (i = 0; i < np; i++) {
        const uint8_t *pt = f + 16 * i, *qt = f + 16 * ((i + 1) % np);
        float p[3], q[3], d1[3], d2[3], c1[3];
        int crease = !(rd_u32(pt + 12) & 1);

        for (j = 0; j < 3; j++) {
            p[j] = rd_f32(pt + 4 * j);
            q[j] = rd_f32(qt + 4 * j);
        }
        if (crease) {
            P[nv][0] = p[0]; P[nv][1] = p[1]; P[nv][2] = p[2];
            N[nv][0] = N[nv][1] = N[nv][2] = 0.0f;
            K[nv++] = 0;
        }
        P[nv][0] = p[0]; P[nv][1] = p[1]; P[nv][2] = p[2];
        K[nv] = crease ? 1 : 2;
        v3_sub(q, C, d1);
        v3_cross(A, d1, c1);
        v3_sub(q, p, d2);
        v3_cross(c1, d2, N[nv]);
        v3_norm(N[nv]);
        nv++;
    }
    for (v = 0; v < nv; v++) {
        const float *prev = N[(v + nv - 1) % nv];
        if (K[v] == 0) {
            N2[v][0] = prev[0]; N2[v][1] = prev[1]; N2[v][2] = prev[2];
        } else if (K[v] == 1) {
            N2[v][0] = N[v][0]; N2[v][1] = N[v][1]; N2[v][2] = N[v][2];
        } else {
            v3_add(prev, N[v], N2[v]);
            v3_norm(N2[v]);
        }
    }
    segs = (uint32_t)(int8_t)f[0x148] >> lod;
    dang = r32f(TWO_PI / segs);
    vstep = r32f(1.0 / (np - 1.0));
    for (r = 0; r <= segs; r++) {
        float ang = r32f((double)r * dang), q[4], M[16], u;
        double h = (double)ang * K_HALF, sn, cn;
        uint32_t k = 0;

        x87_fsincos(h, &sn, &cn);
        for (i = 0; i < 3; i++)
            q[i] = r32f((double)r32f(sn) * A[i]);
        q[3] = r32f(cn);
        quat_to_m4_row(q, M);
        u = r32f(ang / TWO_PI);
        for (v = 0; v < nv; v++) {
            float vv = r32f((double)k * vstep), d[3], p[3], t[3], c[3], nn[3], bt[3];
            double pd[3], bs[9];

            if (K[v])
                k++;
            v3_sub(P[v], C, d);
            v3_mul_m3(d, M, p);
            v3_add(p, C, p);
            v3_cross(A, d, c);
            v3_mul_m3(c, M, t);
            v3_norm(t);
            v3_mul_m3(N2[v], M, nn);
            v3_cross(nn, t, bt);
            basis3(bs, t, bt, nn);
            pd[0] = p[0];
            pd[1] = p[1];
            pd[2] = p[2];
            vert(m, pd, bs, (double)u * K_LATHE_UV, (double)vv * K_LATHE_UV);
        }
    }
    for (r = 0; r < segs; r++) {
        uint32_t cur = base + r * nv, nxt = base + (r + 1) * nv;
        a = cur;
        b = nxt;
        for (j = 0; j <= nv; j++) {
            index1(m, b);
            index1(m, a);
            if (j + 1 < nv) {
                a++;
                b++;
            } else {
                a = cur;
                b = nxt;
            }
        }
    }
}

/* prim_count_* 0x8005DBB3-0x8005DD58: vertices, primitives, indices and the
 * length of a cap fan of one form. */
static void form_counts(uint32_t type, const uint8_t *f, uint32_t lod, xb_form *o)
{
    uint32_t n, rows, k, nc = 0, np, i;

    o->fan = 0;
    switch (type) {
    case 0:
        n = (uint32_t)(int8_t)f[0] >> lod;
        o->prim_count = ((n >> 1) - 1) * (n + 1) * 2 - 2;
        o->vertex_count = (n >> 1) * n;
        o->index_count = o->prim_count + 2;
        break;
    case 1:
    case 4:
        rows = (uint32_t)(int8_t)f[type == 1 ? 0 : 0xC];
        n = (uint32_t)(int8_t)f[type == 1 ? 1 : 0xD] >> lod;
        o->vertex_count = (rows + 1) * (n + 1) + 2 + 2 * n;
        o->prim_count = 0;
        o->index_count = strip_count(n, rows, 0, 0) + 2 * n;
        o->fan = n;
        break;
    case 2:
        o->vertex_count = 24;
        o->prim_count = 12;
        o->index_count = 36;
        break;
    case 3:
        n = (uint32_t)(int8_t)f[4] >> lod;
        k = (uint32_t)(int8_t)f[5] >> lod;
        o->prim_count = (n + 1) * k * 2 - 2;
        o->vertex_count = n * k;
        o->index_count = o->prim_count + 2;
        break;
    default:
        np = (uint32_t)(int8_t)f[0x149];
        for (i = 0; i < np; i++)
            nc += !(rd_u32(f + 16 * i + 12) & 1);
        n = (uint32_t)(int8_t)f[0x148] >> lod;
        o->prim_count = (np + 1 + nc) * n * 2 - 2;
        o->vertex_count = (np + nc) * (n + 1);
        o->index_count = o->prim_count + 2;
        break;
    }
}

static int form_ok(uint32_t type, const uint8_t *f, uint32_t lod)
{
    uint32_t n;

    switch (type) {
    case 0:
        n = (uint32_t)(int8_t)f[0] >> lod;
        return n >= 6 && n <= MAX_SEG;
    case 1:
    case 4:
        n = (uint32_t)(int8_t)f[type == 1 ? 1 : 0xD] >> lod;
        return n >= 3 && n <= MAX_SEG && (int8_t)f[type == 1 ? 0 : 0xC] > 0;
    case 3:
        return ((uint32_t)(int8_t)f[4] >> lod) >= 3 && ((uint32_t)(int8_t)f[5] >> lod) >= 3;
    case 5:
        n = (uint32_t)(int8_t)f[0x149];
        return n >= 2 && n <= LATHE_POINTS && ((uint32_t)(int8_t)f[0x148] >> lod) >= 1;
    default:
        return 1;
    }
}

/* prim_group_build 0x8005F3DB: count all forms, then generate them one
 * after another into the same buffers. With null buffers only counts. */
int chamber_mesh(const xb_state *s, uint32_t gi, uint32_t lod, xb_mesh_info *info,
                 float *pos, float *attr, uint16_t *idx, xb_form *forms)
{
    const struct group_def *g;
    mesh_out m;
    uint32_t fi, vb = 0, ib = 0;

    if (gi >= XB_CHAMBER_GROUPS || lod > 1)
        return XB_ERR_ARG;
    g = &groups[gi];
    m.pos = pos;
    m.attr = attr;
    m.idx = idx;
    m.nv = m.ni = 0;
    m.uv = g->format == 1;
    m.stride = m.uv ? 11 : 9;
    for (fi = 0; fi < g->n_forms; fi++) {
        const uint8_t *f = g->type == 2 ? 0 : SCENE(s, g->forms + g->form_size * fi);
        xb_form o;

        if (f && !form_ok(g->type, f, lod))
            return XB_ERR_PACK_LAYOUT;
        form_counts(g->type, f, lod, &o);
        o.first_vertex = vb;
        o.first_index = ib;
        if (pos) {
            switch (g->type) {
            case 0: gen_sphere(&m, f, lod, vb); break;
            case 1: gen_cylinder(&m, f, lod, vb); break;
            case 2: gen_box(&m, vb); break;
            case 3: gen_torus(&m, f, lod, vb); break;
            case 4: gen_cone(&m, f, lod, vb); break;
            default: gen_lathe(&m, f, lod, vb); break;
            }
            /* Counted but not written (cylinder, cone): 0 here. */
            while (m.nv < vb + o.vertex_count) {
                double z[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
                vert(&m, z, z, 0.0, 0.0);
            }
            if (m.nv != vb + o.vertex_count || m.ni != ib + o.index_count)
                return XB_ERR_PACK_LAYOUT;
        }
        if (forms)
            forms[fi] = o;
        vb += o.vertex_count;
        ib += o.index_count;
    }
    info->type = g->type;
    info->format = g->format;
    info->attr_floats = m.stride;
    info->prim = g->type == 2 ? 5 : 6;
    info->forms = g->n_forms;
    info->vertices = vb;
    info->indices = ib;
    return XB_OK;
}

/* ---- values of a draw, Chamber.md 7 --------------------------------------- */

/* light_params 0x8006F8B4 as anim_construct leaves it (0x80056BE5-0x80056C69)
 * and the three colour factors of every group (+0x28, +0x38, +0x48). */
static const float light_diffuse[3] = { K_LIGHT_D, K_LIGHT_D, K_LIGHT_D };   /* +0x04 */
static const float light_specular[3] = { K_ONE, K_ONE, K_ONE };              /* +0x14 */
static const float light_ambient[3] = { 0.0f, 0.0f, 0.0f };                  /* +0x24 */
/* +0x34: three zeros, written at 0x80056BF6, 0x80056C00, 0x80056C05; read
 * at 0x8005F24E, as the point vec3_transform takes into constant 4 */
static const float light_34[3] = { 0.0f, 0.0f, 0.0f };
static const float light_att[3] = { K_ONE, K_LIGHT_A, K_LIGHT_A };           /* +0x54 */
static const float group_colour[3] = { K_GROUP_R, K_ONE, K_GROUP_B };

/* chamber_build_groups 0x8005F7B8, last argument: the cube of directions
 * of the group, anim+0x934 (64) or anim+0x938 (128). */
static const uint8_t group_cube128[XB_CHAMBER_GROUPS] = { 0, 1, 0, 0, 0, 1 };

int chamber_group_cube128(uint32_t group)
{
    return group_cube128[group];
}

int chamber_group_format(uint32_t group)
{
    return groups[group].format;
}

/* vec3_transform 0x8005C8BB: v * M with the translation, sums in the order
 * of the code. */
static void v3_transform(const float v[3], const float m[16], float out[3])
{
    double x = v[0], y = v[1], z = v[2];

    out[0] = r32f(((m[8] * z + m[4] * y) + x * m[0]) + m[12]);
    out[1] = r32f(((m[1] * x + m[9] * z) + m[5] * y) + m[13]);
    out[2] = r32f(((m[2] * x + m[10] * z) + m[6] * y) + m[14]);
}

/* vec4_mul_mat4 0x80059A2E. */
void v4_mul_m4(const float v[4], const float m[16], float out[4])
{
    double x = v[0], y = v[1], z = v[2], w = v[3];
    int j;

    out[0] = r32f(((m[12] * w + m[4] * y) + m[8] * z) + x * m[0]);
    for (j = 1; j < 4; j++)
        out[j] = r32f(((m[j] * x + m[12 + j] * w) + m[4 + j] * y) + m[8 + j] * z);
}

static void transpose4(const float m[16], xb_real out[4][4])
{
    int r, c;

    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            out[r][c] = m[c * 4 + r];
}

/* prim_group_draw 0x8005EE08 for one part in the modes 0 and 1: W and
 * the inverse of L' (0x8005F057-0x8005F0C3), then the constants and
 * colours (0x8005EF41-0x8005EFCF once per group without the switch,
 * 0x8005F13A-0x8005F2FA per part). */
void chamber_part_draw(const xb_state *s, const xb_camera *cam, const xb_chamber_pose *pose,
                       float x20, const float (*light)[16], int lit, uint32_t part,
                       xb_part_params *out)
{
    const chamber_part *pp = &s->chamber.parts[part];
    float L[16], inv[16], vp[16], m[16], p4[4], c4[4];
    int a;

    if (pp->pos_track < 0 && pp->rot_track < 0) {
        for (a = 0; a < 16; a++)
            L[a] = pp->L[a];
    } else {
        if (pp->rot_track >= 0)
            m4_mul(pp->L, pose->rot[pp->rot_track], L);
        else
            for (a = 0; a < 16; a++)
                L[a] = pp->L[a];
        if (pp->pos_track >= 0)
            v3_add(L + 12, pose->pos[pp->pos_track], L + 12);
    }
    m4_inverse_rigid(L, inv);                 /* part+0x48 without tracks */
    chamber_world(s, pose, part, out->world);

    m4_mul(cam->view, cam->proj, vp);         /* camera_view_proj 0x8005B06A */
    m4_mul(out->world, vp, m);
    transpose4(m, out->vs);
    if (lit) {
        double mm = (double)x20 + x20, im;

        /* the weighted centre for the part's position, row 3 of W */
        blob_weighted_center(&s->blob, out->world + 12, x20, p4, 0);
        p4[3] = 1.0f;
        for (a = 0; a < 3; a++) {
            out->ps[0][a] = r32f(((double)light_ambient[a] * group_colour[a]) * mm);
            out->ps[1][a] = r32f(((double)group_colour[a] * light_diffuse[a]) * mm);
            out->ps[2][a] = r32f(((double)group_colour[a] * light_specular[a]) * mm);
        }
        v4_mul_m4(p4, inv, c4);
        im = (double)K_ONE / r32f(mm);
        out->vs[7][0] = light_att[0];
        out->vs[7][1] = r32f(im * light_att[1]);
        out->vs[7][2] = r32f(im * light_att[2]);
    } else {
        for (a = 0; a < 3; a++) {
            out->ps[0][a] = r32f((double)light_ambient[a] * group_colour[a]);
            out->ps[1][a] = r32f((double)group_colour[a] * light_diffuse[a]);
            out->ps[2][a] = r32f((double)group_colour[a] * light_specular[a]);
        }
        v3_transform(light_34, inv, c4);      /* 0x8005F243-0x8005F252 */
        for (a = 0; a < 3; a++)
            out->vs[7][a] = light_att[a];
    }
    out->ps[0][3] = out->ps[1][3] = out->ps[2][3] = 1.0f;
    out->vs[7][3] = 1.0f;
    for (a = 0; a < 3; a++)
        out->vs[4][a] = c4[a];
    out->vs[4][3] = 1.0f;
    v3_transform(cam->view_inv + 12, inv, out->vs[5]);   /* the eye, cam+0xEC */
    out->vs[5][3] = 1.0f;
    for (a = 0; a < 4; a++)
        out->vs[6][a] = pp->scale[a];
    m4_mul(out->world, light[pp->side ? 1 : 0], m);
    transpose4(m, out->vs + 8);
}

/* The camera chamber_light_view sets (0x8005FE43-0x8005FEC4). */
void chamber_light_camera(const float center[3], uint32_t side, float view[16], float proj[16])
{
    float tgt[3], up[3];

    tgt[0] = center[0];
    tgt[1] = center[1];
    tgt[2] = r32f((double)center[2] + (side ? 1.0 : (double)K_MINUS1));
    up[0] = 0.0f;
    up[1] = side ? K_MINUS1 : 1.0f;
    up[2] = 0.0f;
    camera_view_toward(center, tgt, up, view);
    camera_perspective(r32f((double)K_PI * K_LV_FOV), 1.0f, 1.0f, K_LV_FAR, proj);
}

/* The draws of one part (0x8005F31A-0x8005F3B6): two cap fans and a strip
 * for cylinder and cone, else one draw of the group's primitive. Returns
 * the number of draws, each as prim, first index, count. */
uint32_t chamber_part_prims(uint32_t type, const xb_form *f, uint32_t out[3][3])
{
    if (type == 1 || type == 4) {
        uint32_t n = 0, fan = f->fan;

        if (fan) {
            out[0][0] = 7; out[0][1] = f->first_index; out[0][2] = fan;
            out[1][0] = 7; out[1][1] = f->first_index + fan; out[1][2] = fan;
            n = 2;
        }
        out[n][0] = 6;
        out[n][1] = f->first_index + 2 * fan;
        out[n][2] = f->index_count - 2 * fan;
        return n + 1;
    }
    if (type == 2) {                          /* table 0x800655B8: triangles (3, 0) */
        out[0][0] = 5; out[0][1] = f->first_index; out[0][2] = 3 * f->prim_count;
    } else {                                  /* strip (1, 2) */
        out[0][0] = 6; out[0][1] = f->first_index; out[0][2] = f->prim_count + 2;
    }
    return 1;
}
