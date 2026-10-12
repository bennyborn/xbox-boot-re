/* Host test of milestone K2: chamber and camera against docs/vectors/c5_chamber.json.
 *
 *     test_k2 <pack> <c5_chamber.json>
 *     test_k2 <pack> --mesh <dir>
 *     test_k2 <pack> --dump <times.txt>
 *     test_k2 <pack> --digest
 *
 * First form:
 *   - static part: for all twelve groups (six types, two detail levels)
 *     the counts of vertices and indices, the form table, and the SHA-256
 *     of the guest's index buffer, position buffer and attribute buffer.
 *     Cylinders and cones count two vertices per form that the original
 *     never writes; the guest holds left-over memory there, so for those
 *     four groups only the indices are compared by hash here, the vertices
 *     value by value against the model in core/test/check_k2.py;
 *   - at each of the five captured times: path time, section, eye, target,
 *     up, view and projection of the camera, the detail switch, the world
 *     matrix of every drawn part (rows 0 to 3, three values each, NaN
 *     where the guest has NaN), the drawn detail level, and both light
 *     views from the centre the guest used;
 *   - a run over the whole eight seconds, twice from a fresh state, must
 *     give the same digest (see --digest).
 *
 * --mesh writes the geometry of every group as raw little-endian files
 * <dir>/g<group>_d<detail>.{pos,attr,idx,forms} for check_k2.py.
 *
 * --dump first prints the keys of the chosen path ("k" lines, 15 bit
 * patterns each) and an "f" line with path, first key, keys in the table,
 * frame, origin, look and fade. Then it reads one float32 bit pattern of t
 * per line (hex), followed by "c" or "cw", and prints for each a "c" line
 * with the camera, and with "cw" also a "p" line with the track state and
 * one "w" line of 16 bit patterns per part (NaN as 7FC00000).
 *
 * --digest runs from tick 0 at 60 Hz (17, 16, 17 ms) until XB_DONE and
 * folds at every frame t, camera, track state and all 271 world matrices
 * into an FNV-1a digest, NaN folded as one pattern; before that the
 * geometry of all twelve groups. Prints "digest <16 hex digits> <frames>".
 * The wasm build prints the same line (core/test/wasm_k2.mjs).
 */
#include "xboot.h"
#include "json.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failed;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok)
        failed++;
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
}

static uint32_t bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static float from_bits(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static void *aligned_alloc16(size_t size, void **base)
{
    *base = malloc(size + XB_ALIGN);
    if (!*base)
        return NULL;
    return (void *)(((uintptr_t)*base + XB_ALIGN - 1) & ~(uintptr_t)(XB_ALIGN - 1));
}

static unsigned char *read_file(const char *path, size_t *size, void **base)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = aligned_alloc16(*size, base);
    if (buf && fread(buf, 1, *size, f) != *size)
        buf = NULL;
    fclose(f);
    return buf;
}

/* A vector value: a number, or the string "NaN". */
static float vnum(const json_doc *d, int node)
{
    if (json_str_eq(d, node, "NaN"))
        return from_bits(0x7FC00000u);
    return (float)json_num(d, node);
}

/* Equal bits, or both NaN (the vector file keeps no NaN pattern). */
static int same(float a, float b)
{
    return bits(a) == bits(b) || (a != a && b != b);
}

static int list_equal(const json_doc *d, int arr, const float *v, int n)
{
    int i;

    if (arr < 0 || d->nodes[arr].count != n)
        return 0;
    for (i = 0; i < n; i++)
        if (!same(vnum(d, json_at(d, arr, i)), v[i]))
            return 0;
    return 1;
}

typedef struct mesh {
    xb_mesh_info info;
    float *pos, *attr;
    uint16_t *idx;
    xb_form forms[16];
} mesh;

static int mesh_get(const xb_state *s, uint32_t g, uint32_t lod, mesh *m)
{
    if (xb_chamber_mesh_info(s, g, lod, &m->info) != XB_OK || m->info.forms > 16)
        return -1;
    m->pos = malloc(sizeof(float) * 3 * m->info.vertices);
    m->attr = malloc(sizeof(float) * m->info.attr_floats * m->info.vertices);
    m->idx = malloc(sizeof(uint16_t) * m->info.indices);
    memset(m->pos, 0xEE, sizeof(float) * 3 * m->info.vertices);   /* the core must write all */
    memset(m->attr, 0xEE, sizeof(float) * m->info.attr_floats * m->info.vertices);
    return xb_chamber_mesh(s, g, lod, m->pos, m->attr, m->idx, m->forms);
}

static void mesh_free(mesh *m)
{
    free(m->pos);
    free(m->attr);
    free(m->idx);
}

static void check_static(const xb_state *s, const json_doc *d)
{
    int groups = json_path(d, 0, "static/groups"), gi;
    char what[256];

    check(groups >= 0 && d->nodes[groups].count == 12, "c5_chamber.json has twelve groups");
    for (gi = 0; gi < 12; gi++) {
        int G = json_at(d, groups, gi), f, k, forms_ok = 1;
        uint32_t g = (uint32_t)json_num(d, json_get(d, G, "slot")) % 6;
        uint32_t lod = (uint32_t)json_num(d, json_get(d, G, "detail"));
        int unwritten = (int)json_num(d, json_get(d, G, "vertices_not_written"));
        int fl = json_get(d, G, "forms");
        char h[65];
        mesh m;
        int rc = mesh_get(s, g, lod, &m);

        snprintf(what, sizeof what, "group %d (type %u, detail %u): %u vertices, %u indices, guest %d, %d",
                 gi, g, lod, m.info.vertices, m.info.indices,
                 (int)json_num(d, json_get(d, G, "vertices")), (int)json_num(d, json_get(d, G, "indices")));
        check(rc == XB_OK && m.info.vertices == (uint32_t)json_num(d, json_get(d, G, "vertices")) &&
              m.info.indices == (uint32_t)json_num(d, json_get(d, G, "indices")), what);
        if (rc != XB_OK)
            continue;
        if (d->nodes[fl].count != (int)m.info.forms)
            forms_ok = 0;
        for (f = 0; forms_ok && f < (int)m.info.forms; f++) {
            const uint32_t *o = &m.forms[f].first_vertex;
            for (k = 0; k < 6; k++)
                forms_ok &= o[k] == (uint32_t)json_num(d, json_at(d, json_at(d, fl, f), k));
        }
        snprintf(what, sizeof what, "group %d: form table (%u forms) equals the guest's", gi, m.info.forms);
        check(forms_ok, what);
        sha256_hex(m.idx, sizeof(uint16_t) * m.info.indices, h);
        snprintf(what, sizeof what, "group %d: indices, SHA-256 %.16s...", gi, h);
        check(json_str_eq(d, json_path(d, G, "sha256/indices"), h), what);
        if (unwritten == 0) {
            sha256_hex(m.pos, sizeof(float) * 3 * m.info.vertices, h);
            snprintf(what, sizeof what, "group %d: vertex positions, SHA-256 %.16s...", gi, h);
            check(json_str_eq(d, json_path(d, G, "sha256/positions"), h), what);
            sha256_hex(m.attr, sizeof(float) * m.info.attr_floats * m.info.vertices, h);
            snprintf(what, sizeof what, "group %d: vertex attributes, SHA-256 %.16s...", gi, h);
            check(json_str_eq(d, json_path(d, G, "sha256/attributes"), h), what);
        } else {
            printf("      group %d: %d vertices the guest never wrote, vertices in check_k2.py\n",
                   gi, unwritten);
        }
        mesh_free(&m);
    }
}

static void check_capture(const xb_state *s, const json_doc *d, int c)
{
    int cam = json_get(d, c, "camera"), parts = json_get(d, c, "parts"), lv = json_get(d, c, "light_views");
    float t = (float)json_num(d, json_get(d, c, "t"));
    static xb_real world[XB_CHAMBER_PARTS][16];
    static xb_chamber_pose pose;
    int group_first[7], i, n, bad = 0, nan = 0, lod_bad = 0, shown = 0;
    char what[256];
    xb_camera cm;
    xb_part pt;

    xb_camera_at(s, t, &cm);
    snprintf(what, sizeof what, "t=%.4f: s %08X, guest %08X; section %d, guest %d", (double)t,
             (unsigned)bits(cm.s), (unsigned)bits(vnum(d, json_get(d, cam, "s"))), cm.key,
             (int)json_num(d, json_get(d, cam, "key")));
    check(same(cm.s, vnum(d, json_get(d, cam, "s"))) && cm.key == (int)json_num(d, json_get(d, cam, "key")),
          what);
    snprintf(what, sizeof what, "t=%.4f: eye (%.3f, %.3f, %.3f), target, up equal the guest's", (double)t,
             (double)cm.eye[0], (double)cm.eye[1], (double)cm.eye[2]);
    check(list_equal(d, json_get(d, cam, "eye"), cm.eye, 3) &&
          list_equal(d, json_get(d, cam, "target"), cm.target, 3) &&
          list_equal(d, json_get(d, cam, "up"), cm.up, 3), what);
    snprintf(what, sizeof what, "t=%.4f: view and projection equal the guest's (32 values)", (double)t);
    check(list_equal(d, json_get(d, cam, "view"), cm.view, 16) &&
          list_equal(d, json_get(d, cam, "projection"), cm.proj, 16), what);
    snprintf(what, sizeof what, "t=%.4f: chamber drawn (this+0xD = %u), logo %u", (double)t,
             cm.chamber_on, cm.logo_on);
    check(cm.chamber_on == 1, what);

    xb_chamber_at(s, t, &pose);
    snprintf(what, sizeof what, "t=%.4f: detail %u, guest %d", (double)t, pose.detail,
             (int)json_num(d, json_get(d, c, "detail")));
    check(pose.detail == (uint32_t)json_num(d, json_get(d, c, "detail")), what);
    xb_chamber_world(s, &pose, 0, XB_CHAMBER_PARTS, world);

    for (i = 0, n = 0; i < XB_CHAMBER_PARTS; i++) {
        xb_chamber_part(s, (uint32_t)i, &pt);
        if (pt.index == 0)
            group_first[pt.group] = i;
    }
    n = d->nodes[parts].count;
    for (i = 0; i < n; i++) {
        int p = json_at(d, parts, i), r, k;
        int gi = (int)json_num(d, json_get(d, p, "group"));
        int j = (int)json_num(d, json_get(d, p, "index"));
        const float *w = world[group_first[gi % 6] + j];
        int pos = json_get(d, p, "position"), ax = json_get(d, p, "axes"), ok = 1;

        for (k = 0; k < 3; k++)
            ok &= same(w[12 + k], vnum(d, json_at(d, pos, k)));
        for (r = 0; r < 3; r++)
            for (k = 0; k < 3; k++)
                ok &= same(w[4 * r + k], vnum(d, json_at(d, ax, 3 * r + k)));
        if (!ok && shown++ < 3)
            printf("      part %d/%d: core position %08X %08X %08X\n", gi, j, (unsigned)bits(w[12]),
                   (unsigned)bits(w[13]), (unsigned)bits(w[14]));
        bad += !ok;
        lod_bad += (uint32_t)(gi / 6) != pose.detail;
        for (k = 0; k < 16; k++)
            if (w[k] != w[k]) {
                nan++;
                break;
            }
    }
    snprintf(what, sizeof what, "t=%.4f: world matrices of %d parts (%d values), %d differ, %d with NaN, guest %d",
             (double)t, n, 12 * n, bad, nan, (int)json_num(d, json_get(d, c, "parts_nan")));
    check(n == XB_CHAMBER_PARTS && bad == 0 && nan == (int)json_num(d, json_get(d, c, "parts_nan")), what);
    snprintf(what, sizeof what, "t=%.4f: the guest drew the detail level of the core (%d parts off)",
             (double)t, lod_bad);
    check(lod_bad == 0, what);

    for (i = 0; i < 2; i++) {
        int v = json_get(d, lv, i ? "up" : "down"), ctr = json_get(d, v, "center");
        float center[3], T[16];

        center[0] = vnum(d, json_at(d, ctr, 0));
        center[1] = vnum(d, json_at(d, ctr, 1));
        center[2] = vnum(d, json_at(d, ctr, 2));
        xb_light_view(center, (uint32_t)i, T);
        snprintf(what, sizeof what, "t=%.4f: light view %s from (%.3f, %.3f, %.3f) equals the guest's",
                 (double)t, i ? "up" : "down", (double)center[0], (double)center[1], (double)center[2]);
        check(list_equal(d, json_get(d, v, "matrix"), T, 16), what);
    }
}

/* ---- digest of a run --------------------------------------------------- */

typedef struct digest {
    uint64_t h;
} digest;

static void fold(digest *g, uint32_t u)
{
    int i;

    for (i = 0; i < 4; i++) {
        g->h ^= (u >> (8 * i)) & 0xFF;
        g->h *= 0x100000001B3ull;
    }
}

static void fold_f(digest *g, const float *v, int n)
{
    int i;

    for (i = 0; i < n; i++)
        fold(g, v[i] != v[i] ? 0x7FC00000u : bits(v[i]));
}

static int run_digest(xb_state *s, const void *pack, size_t pack_size, uint64_t *out, int *frames)
{
    static const uint32_t step[3] = { 17, 16, 17 };
    static xb_real world[XB_CHAMBER_PARTS][16];
    static xb_chamber_pose pose;
    digest g = { 0xCBF29CE484222325ull };
    xb_config cfg = { 0, 0 };
    uint32_t tick = 0, gi, lod;
    int rc = XB_OK, n = 0;
    xb_camera cm;

    if (xb_init(s, pack, pack_size, &cfg) != XB_OK)
        return -1;
    for (gi = 0; gi < XB_CHAMBER_GROUPS; gi++)
        for (lod = 0; lod < 2; lod++) {
            mesh m;
            uint32_t i;
            if (mesh_get(s, gi, lod, &m) != XB_OK)
                return -1;
            fold_f(&g, m.pos, (int)(3 * m.info.vertices));
            fold_f(&g, m.attr, (int)(m.info.attr_floats * m.info.vertices));
            for (i = 0; i < m.info.indices; i++)
                fold(&g, m.idx[i]);
            mesh_free(&m);
        }
    while (rc == XB_OK) {
        float t;

        tick += step[n % 3];
        rc = xb_advance(s, tick);
        t = xb_time(s);
        xb_camera_at(s, t, &cm);
        xb_chamber_at(s, t, &pose);
        xb_chamber_world(s, &pose, 0, XB_CHAMBER_PARTS, world);
        fold_f(&g, &t, 1);
        fold_f(&g, &cm.s, 1);
        fold(&g, (uint32_t)cm.key);
        fold(&g, cm.chamber_on | (uint32_t)cm.logo_on << 8);
        fold_f(&g, cm.eye, 3);
        fold_f(&g, cm.target, 3);
        fold_f(&g, cm.view, 16);
        fold_f(&g, cm.view_inv, 16);
        fold_f(&g, cm.proj, 16);
        fold_f(&g, &pose.u, 2);
        fold(&g, pose.detail);
        fold_f(&g, pose.rot[0], 16 * XB_ROT_TRACKS);
        fold_f(&g, pose.pos[0], 3 * XB_POS_TRACKS);
        fold_f(&g, world[0], 16 * XB_CHAMBER_PARTS);
        n++;
    }
    *out = g.h;
    *frames = n;
    return rc == XB_DONE ? 0 : -1;
}

/* ---- dumps for check_k2.py ---------------------------------------------- */

static int dump_mesh(const xb_state *s, const char *dir)
{
    uint32_t g, lod;
    char path[512];

    for (g = 0; g < XB_CHAMBER_GROUPS; g++)
        for (lod = 0; lod < 2; lod++) {
            static const char *ext[4] = { "pos", "attr", "idx", "forms" };
            mesh m;
            int e;

            if (mesh_get(s, g, lod, &m) != XB_OK)
                return 2;
            for (e = 0; e < 4; e++) {
                const void *p[4] = { m.pos, m.attr, m.idx, m.forms };
                size_t n[4] = { 12 * (size_t)m.info.vertices,
                                4 * (size_t)m.info.attr_floats * m.info.vertices,
                                2 * (size_t)m.info.indices, sizeof(xb_form) * m.info.forms };
                FILE *f;

                snprintf(path, sizeof path, "%s/g%u_d%u.%s", dir, g, lod, ext[e]);
                f = fopen(path, "wb");
                if (!f || fwrite(p[e], 1, n[e], f) != n[e])
                    return 2;
                fclose(f);
            }
            mesh_free(&m);
        }
    return 0;
}

static int dump(const xb_state *s, const char *path)
{
    static xb_real world[XB_CHAMBER_PARTS][16];
    static xb_chamber_pose pose;
    FILE *f = fopen(path, "r");
    unsigned long u;
    char what[8];
    xb_camera cm;
    xb_campath cp;
    int i, k;

    if (!f)
        return 2;
    xb_get_campath(s, &cp);
    for (i = 0; i < (int)cp.n_keys; i++) {
        printf("k");
        for (k = 0; k < 15; k++)
            printf(" %08X", (unsigned)bits(cp.key[i][k]));
        printf("\n");
    }
    printf("f %u %u %u", cp.path, cp.first, cp.n_table);
    for (k = 0; k < 16; k++)
        printf(" %08X", (unsigned)bits(cp.frame[k]));
    for (k = 0; k < 3; k++)
        printf(" %08X", (unsigned)bits(cp.origin[k]));
    for (k = 0; k < 3; k++)
        printf(" %08X", (unsigned)bits(cp.look[k]));
    printf(" %08X %08X\n", (unsigned)bits(cp.fade_t0), (unsigned)bits(cp.fade_k));
    while (fscanf(f, "%lx %7s", &u, what) == 2) {
        float t = from_bits((uint32_t)u);

        xb_camera_at(s, t, &cm);
        printf("c %08X %d %u %u", (unsigned)bits(cm.s), cm.key, cm.chamber_on, cm.logo_on);
        for (k = 0; k < 3; k++)
            printf(" %08X", (unsigned)bits(cm.eye[k]));
        for (k = 0; k < 3; k++)
            printf(" %08X", (unsigned)bits(cm.target[k]));
        for (k = 0; k < 16; k++)
            printf(" %08X", (unsigned)bits(cm.view[k]));
        printf("\n");
        if (strcmp(what, "cw") != 0)
            continue;
        xb_chamber_at(s, t, &pose);
        printf("p %08X %08X %u\n", (unsigned)bits(pose.u), (unsigned)bits(pose.k), pose.detail);
        xb_chamber_world(s, &pose, 0, XB_CHAMBER_PARTS, world);
        for (i = 0; i < XB_CHAMBER_PARTS; i++) {
            printf("w");
            for (k = 0; k < 16; k++)
                printf(" %08X", world[i][k] != world[i][k] ? 0x7FC00000u : (unsigned)bits(world[i][k]));
            printf("\n");
        }
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    void *pack_base, *state_base;
    size_t pack_size;
    unsigned char *pack;
    xb_state *s;
    xb_config cfg = { 0, 0 };
    json_doc d;
    int c, n, frames[2];
    uint64_t dg[2];
    char what[256];

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: test_k2 <pack> <c5_chamber.json> | --mesh <dir> | --dump <times> | --digest\n");
        return 2;
    }
    pack = read_file(argv[1], &pack_size, &pack_base);
    s = aligned_alloc16(xb_state_size(), &state_base);
    if (!pack || !s) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    if (xb_init(s, pack, pack_size, &cfg) != XB_OK)
        return 2;
    if (argc == 4 && strcmp(argv[2], "--mesh") == 0)
        return dump_mesh(s, argv[3]);
    if (argc == 4 && strcmp(argv[2], "--dump") == 0)
        return dump(s, argv[3]);
    if (argc == 3 && strcmp(argv[2], "--digest") == 0) {
        if (run_digest(s, pack, pack_size, &dg[0], &frames[0]) != 0)
            return 2;
        printf("digest %016llX %d\n", (unsigned long long)dg[0], frames[0]);
        return 0;
    }

    printf("state %u bytes, pack %u bytes\n", (unsigned)xb_state_size(), (unsigned)pack_size);
    if (json_load(&d, argv[2]) != 0)
        return 2;
    check_static(s, &d);
    n = d.nodes[json_get(&d, 0, "captures")].count;
    check(n == 5, "c5_chamber.json has five captures");
    for (c = 0; c < n; c++)
        check_capture(s, &d, json_at(&d, json_get(&d, 0, "captures"), c));

    c = run_digest(s, pack, pack_size, &dg[0], &frames[0]);
    c |= run_digest(s, pack, pack_size, &dg[1], &frames[1]);
    snprintf(what, sizeof what, "run over 8 s at 60 Hz, twice: %d frames, digest %016llX and %016llX",
             frames[0], (unsigned long long)dg[0], (unsigned long long)dg[1]);
    check(c == 0 && dg[0] == dg[1] && frames[0] == frames[1], what);
    printf("digest %016llX %d\n", (unsigned long long)dg[0], frames[0]);

    json_free(&d);
    free(pack_base);
    free(state_base);
    printf("%d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
