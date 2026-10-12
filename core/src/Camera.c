/* Camera and camera path, docs/decomp/Camera.md.
 *
 * Set-up: projection (Camera.md 2), path keys from the table, the path
 * chosen with one draw from the generator, eight own keys and the tangents
 * (5.1-5.4). Per frame the camera is a function of t alone (4, 5.5, 3), so
 * camera_at() evaluates it for any t without touching the state.
 *
 * Numerics as in Overview.c: double for the x87, r32f() where the code
 * stores. The order of every sum follows the disassembly as written down
 * in Camera.md and in tools/xemu/chamber_model.py.
 */
#include "core_internal.h"

/* float32 constants of the kernel, address of each in the comment.
 * core/test/check_k2.py compares them with out/rom/kernel.img. */
#define K_PI        3.1415927f    /* 0x80065E4C */
#define K_PI_B      3.1415927f    /* 0x80066754 */
#define K_QUARTER   0.25f         /* 0x8001289C */
#define K_ASPECT    0.75f         /* 0x80012898 */
#define K_ASPECT_W  0.5625f       /* 0x800128E0 */
#define K_NEAR      0.4f          /* 0x800128AC */
#define K_FAR       800.0f        /* 0x800128DC */
#define K_S_RATE    0.2833f       /* 0x800128CC */
#define K_PI_2      1.5707964f    /* 0x800128C8 */
#define K_S_LIN     0.2f          /* 0x800128B0 */
#define K_SIXTH     0.16666667f   /* 0x800128D4 */
#define K_S_SQ      0.8f          /* 0x800128D0 */
#define K_END       8.0f          /* 0x8006675C */
#define K_TWO       2.0f          /* 0x80066760 */
#define K_LEAD      0.8f          /* 0x80066764 */
#define K_CENT      0.01f         /* 0x80012980 */
#define K_SEVENTH   0.14285715f   /* 0x80012978 */
#define K_ONE       1.0f          /* 0x8001288C */
#define K_ZERO      0.0f          /* 0x80012880 */
#define K_HALF      0.5f          /* 0x80010A18 */
#define K_REACH     0.7f          /* 0x80012958 */
#define K_GROW      1.2f          /* 0x800128B8 */
#define K_M95       -95.0f        /* 0x80012974 */
#define K_MIN_R     195.0f        /* 0x80012970 */
#define K_95        95.0f         /* 0x8001296C */
#define K_MINUS1    -1.0f         /* 0x80012894 */
#define K_M132      -132.14f      /* 0x80012968 */
#define K_132       132.14f       /* 0x80012964 */
#define K_MS        0.001f        /* 0x800128A0 */
#define K_THREE     3.0f          /* 0x800128A8 */
#define K_MHALF     -0.5f         /* 0x80012890 */
#define K_ZERO_B    0.0f          /* 0x80065E50 */
#define K_P01       0.1f          /* 0x80012888 */
#define K_M01       -0.1f         /* 0x80012884 */
#define K_WRAP      1000.0f       /* 0x800128C4 */

/* Immediate values on the stack of 0x8005A6CB (Camera.md 5.3). */
static const float own_x[7] = { 0.322f, 1.821f, 2.323f, -11.926f, -39.973f, -60.774f, -90.795f };
static const float own_y[7] = { 30.548f, -70.819f, -150.298f, -220.64f, -243.021f, -261.441f,
                                -287.773f };

#define CAMPATH_TABLE 0x80070DB8u
#define CAMPATH_STRIDE 0x1Cu

/* 0x8005B18C, Camera.md 2. */
void camera_perspective(float fov, float aspect, float near, float far, float m[16])
{
    double s, c, cot, q;
    int i;

    x87_fsincos((double)fov * 0.5, &s, &c);
    cot = (double)r32f(c) / r32f(s);       /* stays in the register */
    q = (double)far / ((double)far - near);  /* stored as m[10], unrounded for m[14] */
    for (i = 0; i < 16; i++)
        m[i] = 0.0f;
    m[0] = r32f(cot * aspect);
    m[5] = r32f(cot);
    m[10] = r32f(q);
    m[11] = 1.0f;
    m[14] = r32f(-(q * near));
}

/* 0x8005B093, Camera.md 3. */
void camera_view_toward(const float eye[3], const float target[3], const float up[3], float m[16])
{
    float x[3], y[3], z[3], ne[3];

    v3_sub(target, eye, z);
    v3_norm(z);
    v3_cross(z, up, x);
    v3_norm(x);
    v3_cross(x, z, y);
    m[0] = x[0]; m[1] = y[0]; m[2] = z[0];  m[3] = 0.0f;
    m[4] = x[1]; m[5] = y[1]; m[6] = z[1];  m[7] = 0.0f;
    m[8] = x[2]; m[9] = y[2]; m[10] = z[2]; m[11] = 0.0f;
    m[12] = m[13] = m[14] = 0.0f;
    m[15] = 1.0f;
    ne[0] = -eye[0];
    ne[1] = -eye[1];
    ne[2] = -eye[2];
    v3_mul_m3(ne, m, m + 12);
}

/* The eight own keys and the tangents, campath_select 0x8005A6CB with the
 * path already chosen (Camera.md 5.2-5.4). */
static void own_keys(camera_state *cs)
{
    campath_key *k = cs->keys, *last = &k[cs->n_table - 1], *own = &k[cs->n_table];
    double lead = ((double)K_END - K_TWO) - K_LEAD;
    float v[3] = { 0.0f, 0.0f, 0.0f }, e[3], d[3], m[3], x[3], o[3], up[3] = { 0.0f, 0.0f, 1.0f };
    float len, rho;
    uint32_t i, j, n;

    for (j = 0; j < CAM_OWN_KEYS; j++) {
        campath_key *q = &own[j];
        int a;

        q->t = r32f((double)j * K_LEAD * K_SEVENTH + lead);
        for (a = 0; a < 3; a++)
            q->eye[a] = q->eye_tan[a] = q->target[a] = q->target_tan[a] = 0.0f;
        q->cut = q->split = 0.0f;
    }
    if (cs->n_table >= 2) {
        v3_sub(last->eye, last[-1].eye, v);
        v3_scale(v, r32f(1.0 / ((double)last->t - last[-1].t)));
    }
    e[0] = last->eye[0];
    e[1] = last->eye[1];
    e[2] = last->eye[2];
    v3_madd(e, v, r32f(((double)own[0].t - last->t) * K_REACH));
    len = v3_len(e);
    v3_scale(e, r32f(1.0 / len));
    rho = r32f((double)len * K_GROW - K_M95);
    if (rho < K_MIN_R)
        rho = K_MIN_R;
    d[0] = e[0];
    d[1] = e[1];
    d[2] = e[2];
    v3_scale(e, r32f((double)rho - K_95));
    own[0].eye[0] = e[0];
    own[0].eye[1] = e[1];
    own[0].eye[2] = e[2];

    for (i = 0; i < 3; i++)
        m[i] = r32f((double)d[i] * K_MINUS1);
    v3_cross(m, up, x);
    v3_norm(x);
    v3_cross(x, m, o);
    m4_ident(cs->frame);
    for (i = 0; i < 3; i++) {
        cs->frame[i] = x[i];
        cs->frame[4 + i] = m[i];
        cs->frame[8 + i] = o[i];
        cs->frame[12 + i] = m[i];
    }
    /* Row 3 = m * -rho, i.e. d * rho (0x8005A9FC-0x8005AA0F); the logo
     * draws with it (Logo.md 1), the camera only uses rows 0 to 2. */
    v3_scale(cs->frame + 12, -(double)rho);
    cs->origin[0] = m[0];
    cs->origin[1] = m[1];
    cs->origin[2] = m[2];
    v3_scale(cs->origin, r32f((double)K_M132 - rho));
    for (j = 0; j < 7; j++) {
        float loc[3], p[3];

        loc[0] = 0.0f;
        loc[1] = r32f((double)own_y[j] + K_132);
        loc[2] = own_x[j];
        v3_mul_m3(loc, cs->frame, p);
        v3_add(p, cs->origin, own[j + 1].eye);
    }
    {
        float loc[3] = { 0.0f, K_132, 25.0f }, p[3];

        v3_mul_m3(loc, cs->frame, p);
        v3_add(p, cs->origin, cs->look);
    }
    cs->fade_t0 = own[2].t;
    cs->fade_k = r32f(1.0 / ((double)own[5].t - own[2].t));

    /* Tangents of all n + 8 keys (Camera.md 5.4). */
    n = cs->n_keys;
    for (i = 0; i < n; i++) {
        campath_key *q = &k[i];
        float dlt[3];
        int a;

        for (a = 0; a < 3; a++)
            q->eye_tan[a] = q->target_tan[a] = 0.0f;
        if (i > 0) {
            float w = r32f(((double)q->split + K_ONE) * ((double)K_ONE - q->cut) * K_HALF);
            v3_sub(q->eye, q[-1].eye, dlt);
            v3_madd(q->eye_tan, dlt, w);
            v3_sub(q->target, q[-1].target, dlt);
            v3_madd(q->target_tan, dlt, w);
        }
        if (i < n - 1) {
            float w = r32f(((double)K_ONE - q->split) * ((double)K_ONE - q->cut) * K_HALF);
            v3_sub(q[1].eye, q->eye, dlt);
            v3_madd(q->eye_tan, dlt, w);
            v3_sub(q[1].target, q->target, dlt);
            v3_madd(q->target_tan, dlt, w);
        }
    }
}

/* campath_construct 0x8005AFB6: the table into keys of 0x3C bytes. Counts
 * the paths into cs->n_paths. */
static int load_table(camera_state *cs, const xb_state *s, campath_key *table)
{
    const uint8_t *tab = pack_at(s, PACK_CAMPATH, CAMPATH_TABLE);
    double key_time = ((double)K_END - K_TWO) - K_LEAD;
    uint32_t i, j;

    cs->n_paths = 0;
    for (i = 0; i < CAM_TABLE_KEYS; i++) {
        const uint8_t *r = tab + i * CAMPATH_STRIDE;
        campath_key *q = &table[i];

        q->t = r32f((double)r[0] * key_time * K_CENT);
        q->cut = r32f((double)(int8_t)r[1] * K_CENT);
        q->split = r32f((double)(int8_t)r[2] * K_CENT);
        for (j = 0; j < 3; j++) {
            q->eye[j] = rd_f32(r + 4 + 4 * j);
            q->target[j] = rd_f32(r + 0x10 + 4 * j);
            q->eye_tan[j] = q->target_tan[j] = 0.0f;
        }
        if (q->t == 0.0f)
            cs->n_paths++;
    }
    if (cs->n_paths == 0 || table[0].t != 0.0f)
        return XB_ERR_PACK_LAYOUT;
    return XB_OK;
}

/* campath_select 0x8005A6CB after its draw: path `path`, its keys from the
 * table, then the own keys (Camera.md 5.2). */
static void choose_path(camera_state *cs, const campath_key *table, uint32_t path)
{
    uint32_t i, j;

    cs->path = path;
    for (i = 0;; i++)
        if (table[i].t == 0.0f && path-- == 0)
            break;
    cs->first = i;
    for (j = i + 1; j < CAM_TABLE_KEYS && table[j].t != 0.0f; j++)
        ;
    cs->n_table = j - i;
    cs->n_keys = cs->n_table + CAM_OWN_KEYS;
    for (j = 0; j < cs->n_table; j++)
        cs->keys[j] = table[i + j];
    own_keys(cs);
}

int camera_start(camera_state *cs, const xb_state *s, uint32_t *rng, uint32_t flags)
{
    campath_key table[CAM_TABLE_KEYS];
    int rc;

    /* Set-up 0x8005768C: camera_set_projection 0x8005B318. */
    cs->aspect = (flags & XB_START_WIDE) ? K_ASPECT_W : K_ASPECT;
    camera_perspective(r32f((double)K_PI * K_QUARTER), cs->aspect, K_NEAR, K_FAR, cs->proj);

    rc = load_table(cs, s, table);
    if (rc != XB_OK)
        return rc;
    /* campath_select 0x8005A6CB(-1): the first draw of the generator. */
    *rng = rng_step(*rng);
    choose_path(cs, table, (*rng & 0x7FFFFFFFu) % cs->n_paths);
    return XB_OK;
}

/* campath_select 0x8005A6CB with an argument >= 0: no draw, the path is
 * the argument (0x8005A6D4-0x8005A6FC, Camera.md 5.2). For the workbench;
 * the generator is left as it is. */
int camera_select(camera_state *cs, const xb_state *s, uint32_t path)
{
    campath_key table[CAM_TABLE_KEYS];
    int rc = load_table(cs, s, table);

    if (rc != XB_OK)
        return rc;
    if (path >= cs->n_paths)
        return XB_ERR_ARG;
    choose_path(cs, table, path);
    return XB_OK;
}

/* anim_update 0x800571AD in path mode: s from t (Camera.md 4). */
static float path_time(float t)
{
    double l8 = r32f((double)t * t * K_SIXTH * K_S_SQ);
    float a = r32f((double)t * K_S_RATE);

    if (K_PI_2 < a)
        a = K_PI_2;
    return r32f((double)sse_sin(a) * t * K_S_LIN + l8);
}

/* campath_eval 0x8005AC32 (Camera.md 5.5). */
static void path_eval(const camera_state *cs, float s, xb_camera *out)
{
    const campath_key *k = cs->keys;
    uint32_t n = cs->n_keys, i, a;

    out->key = -1;
    if (!((double)s > (double)K_END - K_TWO)) {
        for (i = 1; i < n && !(s < k[i].t); i++)
            ;
        if (i != n) {
            const campath_key *k0 = &k[i - 1], *k1 = &k[i];
            double dt = r32f((double)k1->t - k0->t), prev, st, u, h, uu, u2, u3;
            double U, U2, U3, T2, T3, h00, h10, h11, h01, fz, w;
            double sn, cn;

            if (dt < K_MS)
                dt = K_MS;
            prev = i > 1 ? (double)k0->t - k[i - 2].t : dt;
            if (prev < K_MS)
                prev = K_MS;
            st = (double)s - k0->t;
            u = st / dt;
            if (u < 0.0)
                u = 0.0;
            if (u > 1.0)
                u = 1.0;
            h = 3.0 * (u * u) - (u * u * u + u * u * u);
            uu = st / ((1.0 - h) * prev + h * dt);
            u2 = uu * uu;
            u3 = u2 * uu;
            U = r32f(uu);
            U2 = r32f(u2);
            U3 = r32f(u3);
            T2 = r32f(u3 + u3);
            T3 = r32f(u2 * 3.0);
            h00 = (T2 - T3) + 1.0;
            h10 = (U3 - (U2 + U2)) + U;
            h11 = U3 - U2;
            h01 = T3 - T2;
            for (a = 0; a < 3; a++) {
                out->eye[a] = r32f(((h01 * k1->eye[a] + h11 * k1->eye_tan[a]) +
                                    h10 * k0->eye_tan[a]) + h00 * k0->eye[a]);
                out->target[a] = r32f(((h01 * k1->target[a] + h11 * k1->target_tan[a]) +
                                       h10 * k0->target_tan[a]) + h00 * k0->target[a]);
            }
            fz = ((double)s - cs->fade_t0) * cs->fade_k;
            if (fz < 0.0)
                fz = 0.0;
            if (fz > 1.0)
                fz = 1.0;
            x87_fsincos(r32f((double)K_PI_B * fz), &sn, &cn);
            w = r32f((1.0 - r32f(cn)) * K_HALF);
            v3_scale(out->target, r32f(1.0 - w));
            v3_madd(out->target, cs->look, w);
            out->key = (int32_t)i;
            out->chamber_on = i < n - 1;              /* i < n_table + 7 */
            out->logo_on = i > cs->n_table - 1;
            return;
        }
        for (a = 0; a < 3; a++)
            out->eye[a] = k[n - 1].eye[a];
    } else {
        for (a = 0; a < 3; a++)
            out->eye[a] = k[n - 1].eye[a];       /* own key 7, path+0x1A8 */
    }
    /* The branch for i = 0 (eye (0, -90, 0), 0x8005ACAD-0x8005ACE0) is never
     * taken: the search starts at i = 1. */
    for (a = 0; a < 3; a++)
        out->target[a] = cs->look[a];
    out->chamber_on = 0;
    out->logo_on = 1;
}

void camera_at(const xb_state *s, float t, xb_camera *out)
{
    const camera_state *cs = &s->camera;
    int i;

    out->t = t;
    out->s = path_time(t);
    out->path = cs->path;
    out->pad[0] = out->pad[1] = 0;
    path_eval(cs, out->s, out);
    out->up[0] = 0.0f;
    out->up[1] = 0.0f;
    out->up[2] = 1.0f;
    camera_view_toward(out->eye, out->target, out->up, out->view);
    m4_inverse_rigid(out->view, out->view_inv);
    for (i = 0; i < 16; i++)
        out->proj[i] = cs->proj[i];
}

/* ---- track values, Camera.md 6 ------------------------------------------- */

/* angle_from_sin_cos 0x80056969: the angle of (cs, sn) with the limits +-1
 * taken first, the arc sine near 0 and the arc cosine elsewhere. The value
 * stays in the register; the caller stores it. */
static double angle_sin_cos(const xb_state *s, float sn, float cs)
{
    double r;

    if (sn <= K_MINUS1)
        return (double)K_PI * K_MHALF;
    if (sn >= K_ONE)
        return (double)K_PI * K_HALF;
    if (cs <= K_MINUS1)
        return K_PI;
    if (cs >= K_ONE)
        return K_ZERO_B;
    if (sn < K_P01 && sn > K_M01) {
        r = sse_asin(s, sn);
        if (cs < K_ZERO) {
            r = (double)K_PI - r;
            if (r > (double)K_PI)
                r = r - ((double)K_PI + K_PI);
        }
        return r;
    }
    r = sse_acos(s, cs);
    return sn < K_ZERO ? -r : r;
}

/* anim_update 0x80057435-0x80057587 in path mode: +0x690, +0x6A0, +0x698
 * from the eye, +0x6A8 from the eye and the blob's centre (anim+0xBB0). */
void camera_track(const xb_state *s, track_state *tr, const float eye[3], const float centre[3])
{
    double x = eye[0], y = eye[1], z = eye[2], inv, lo, hi, v, two = (double)K_PI + K_PI;
    double dx = (double)centre[0] - x, dy = (double)centre[1] - y, dz = (double)centre[2] - z;
    float len, q, cs, sn, prev = tr->azimuth, hi32;

    len = sse_sqrt(r32f((x * x + y * y) + z * z));      /* 0x800568E5, 0x80057F42 */
    tr->dist = len;
    tr->elevation = sse_asin(s, r32f(z * (1.0 / (double)len)));
    q = sse_sqrt(r32f(y * y + x * x));
    inv = (double)K_ONE / q;
    cs = r32f(x * inv);
    sn = r32f(y * inv);
    tr->azimuth = r32f(angle_sin_cos(s, sn, cs));
    /* 0x800574B2-0x8005755E: whole turns towards the previous value */
    if ((prev < 0.0f ? -prev : prev) < K_WRAP) {
        float d = r32f((double)tr->azimuth - prev);

        if ((d < 0.0f ? -d : d) > K_PI) {
            lo = (double)prev - K_PI;
            if (lo > (double)tr->azimuth) {
                v = tr->azimuth;
                do
                    v = v + two;
                while (lo > v);
                tr->azimuth = r32f(v);
            }
            hi = (double)prev + K_PI;
            hi32 = r32f(hi);
            if (hi < (double)tr->azimuth) {
                v = tr->azimuth;
                do
                    v = v - two;
                while ((double)hi32 < v);
                tr->azimuth = r32f(v);
            }
        }
    }
    tr->blob_dist = sse_sqrt(r32f((dz * dz + dy * dy) + dx * dx));   /* 0x8005691F */
}
