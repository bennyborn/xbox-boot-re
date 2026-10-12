/* Host test of milestone K3: the blob against docs/vectors/c6_blob.json.
 *
 *     test_k3 <pack> <c6_blob.json>
 *     test_k3 <pack> --mesh <dir>
 *     test_k3 <pack> --replay <commands.txt>
 *     test_k3 <pack> --digest
 *
 * First form, for each of the five runs:
 *   - replay: the core gets the guest's arguments (t, dt) of every call of
 *     blob_update through xb_blob_update, blob_restart where t goes back
 *     (c6_loop) and the generator value of c6_poke where the probe wrote
 *     it. After a restart the generator must equal the vector's. At every
 *     capture: all bulges and drops, k, colour and quad of the glow, the
 *     constants the vector holds (vs c10, both c0, c10 to c13 of each
 *     drop) and the range of the height in the deformed mesh, with the
 *     vector's t, x1c, x20 and the core's camera at t. At the end the
 *     number of generator steps and of drops;
 *   - run: for the runs without a restart, the same through
 *     xb_advance_free with the guest's dt: t after each step must equal
 *     the guest's t, the core must call blob_update in exactly these
 *     frames, and at the captures x1c and x20 of the core must equal the
 *     guest's; the drawn values are then taken with the core's own values.
 * Then a run over the whole eight seconds, twice from a fresh state, must
 * give the same digest (see --digest).
 *
 * --mesh writes both meshes as raw little-endian files <dir>/{big,small}.
 * {unit,idx} for check_k3.py.
 *
 * --replay reads commands, one per line, and prints bit patterns in hex:
 *   u <t> <dt>      prints "s" with the state before the call (generator,
 *                   its steps, both counts, centre, radius, colour, 32
 *                   bulges of 16 words, 8 drops of 14 words; drop of a
 *                   bulge as index, -1 none), then xb_blob_update
 *   S               only the "s" line
 *   v <t> <dt>      only xb_blob_update
 *   r               xb_blob_restart
 *   g <value>       xb_blob_set_rng
 *   m               "m" with the deformed mesh, 4 words per vertex
 *   d <t> <x1c> <x20>   "d" with xb_blob_draw for the camera at t
 *   D <t> <x1c> <x20> <eye 3> <view_inv 16> <view 16> <proj 16>
 *                   the same for a camera given by the caller
 *   w <x20> <x> <y> <z> "w" with both outputs of xb_blob_center
 *   l               "l" with both xb_blob_light_view matrices
 *
 * --digest runs from tick 0 at 60 Hz (17, 16, 17 ms) until XB_DONE and
 * folds at every frame t, the blob's state, the deformed mesh and the drawn
 * values into an FNV-1a digest. Prints "digest <16 hex digits> <frames>".
 * The wasm build prints the same line (core/test/wasm_k3.mjs).
 */
#include "xboot.h"
#include "json.h"

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

static float num(const json_doc *d, int node)
{
    return (float)json_num(d, node);
}

static int eq(const json_doc *d, int node, float v)
{
    return node >= 0 && bits(num(d, node)) == bits(v);
}

static int list_eq(const json_doc *d, int arr, const float *v, int n)
{
    int i;

    if (arr < 0 || d->nodes[arr].count != n)
        return 0;
    for (i = 0; i < n; i++)
        if (!eq(d, json_at(d, arr, i), v[i]))
            return 0;
    return 1;
}

static xb_real deformed[XB_BLOB_VERTICES][4];

/* Compares the core with one capture of the vector file. */
static int capture_bad(const xb_state *s, const json_doc *d, int c, float t, float x1c, float x20,
                       int quad, char *why, size_t n_why)
{
    static xb_blob b;
    static xb_blob_params dr;
    int bl = json_get(d, c, "bulges"), dl = json_get(d, c, "drops"), i, k;
    int glow = json_get(d, c, "glow"), con = json_get(d, c, "constants"), q;
    char col[16];
    float lo, hi;
    xb_camera cam;

    xb_get_blob(s, &b);
    xb_camera_at(s, t, &cam);
    xb_blob_draw(s, &cam, t, x1c, x20, &dr);
    if (d->nodes[bl].count != (int)b.n_bulges || d->nodes[dl].count != (int)b.n_drops) {
        snprintf(why, n_why, "counts %u, %u", b.n_bulges, b.n_drops);
        return 1;
    }
    for (i = 0; i < (int)b.n_bulges; i++) {
        const xb_bulge *g = &b.bulge[i];
        int e = json_at(d, bl, i), dn = json_get(d, e, "drop");
        int drop = d->nodes[dn].type == JSON_NULL ? -1 : (int)json_num(d, dn);

        if (!eq(d, json_get(d, e, "size"), g->size) || !eq(d, json_get(d, e, "h"), g->h) ||
            !list_eq(d, json_get(d, e, "dir"), g->dir, 3) ||
            (uint32_t)json_num(d, json_get(d, e, "mask")) != g->mask ||
            !eq(d, json_get(d, e, "t0"), g->t0) || !eq(d, json_get(d, e, "freq"), g->freq) ||
            !eq(d, json_get(d, e, "amp"), g->amp) || drop != g->drop) {
            snprintf(why, n_why, "bulge %d", i);
            return 1;
        }
    }
    for (i = 0; i < (int)b.n_drops; i++) {
        const xb_drop *p = &b.drop[i];
        int e = json_at(d, dl, i);

        if (!eq(d, json_get(d, e, "size"), p->size) || !list_eq(d, json_get(d, e, "pos"), p->pos, 3) ||
            !list_eq(d, json_get(d, e, "dir"), p->dir, 3) || !eq(d, json_get(d, e, "disp"), p->disp) ||
            !eq(d, json_get(d, e, "scale"), p->scale) || !eq(d, json_get(d, e, "rate"), p->rate) ||
            !eq(d, json_get(d, e, "t0"), p->t0) || !eq(d, json_get(d, e, "freq"), p->freq) ||
            !eq(d, json_get(d, e, "amp"), p->amp)) {
            snprintf(why, n_why, "drop %d", i);
            return 1;
        }
    }
    snprintf(col, sizeof col, "0x%08x", dr.glow_colour);
    q = json_get(d, glow, "quad");
    for (i = 0; quad && i < 4; i++)
        if (!list_eq(d, json_at(d, q, i), dr.glow[i], 5)) {
            snprintf(why, n_why, "glow vertex %d", i);
            return 1;
        }
    if (!eq(d, json_get(d, glow, "k"), dr.k) || !json_str_eq(d, json_get(d, glow, "colour"), col)) {
        snprintf(why, n_why, "glow k %08X colour %s", (unsigned)bits(dr.k), col);
        return 1;
    }
    if (!list_eq(d, json_get(d, con, "vs_c10"), dr.mesh_vs[10], 4) ||
        !list_eq(d, json_get(d, con, "ps1_c0"), dr.mesh_ps[0], 4) ||
        !list_eq(d, json_get(d, con, "ps2_c0"), dr.drop_ps[0], 4)) {
        snprintf(why, n_why, "mesh or drop colour constants");
        return 1;
    }
    q = json_get(d, con, "drops_c10_c13");
    if (d->nodes[q].count != (int)dr.drops) {
        snprintf(why, n_why, "%u drops drawn", dr.drops);
        return 1;
    }
    for (i = 0; i < (int)dr.drops; i++)
        if (!list_eq(d, json_at(d, q, i), dr.drop[i][0], 16)) {
            snprintf(why, n_why, "constants of drop %d", i);
            return 1;
        }
    xb_blob_deformed(s, deformed);
    lo = hi = deformed[0][3];
    for (k = 1; k < XB_BLOB_VERTICES; k++) {
        if (deformed[k][3] < lo)
            lo = deformed[k][3];
        if (deformed[k][3] > hi)
            hi = deformed[k][3];
    }
    q = json_get(d, c, "mesh_extent");
    if (!eq(d, json_at(d, q, 0), lo) || !eq(d, json_at(d, q, 1), hi)) {
        snprintf(why, n_why, "height in the mesh %08X .. %08X", (unsigned)bits(lo), (unsigned)bits(hi));
        return 1;
    }
    return 0;
}

static int event_at(const json_doc *d, int run, int frame, const char *what)
{
    int ev = json_get(d, run, "events"), i;

    for (i = 0; i < d->nodes[ev].count; i++) {
        int e = json_at(d, ev, i);
        if ((int)json_num(d, json_get(d, e, "frame")) == frame && json_str_eq(d, json_get(d, e, "event"), what))
            return e;
    }
    return -1;
}

static uint32_t hex(const json_doc *d, int node)
{
    char buf[16];
    size_t n = d->nodes[node].len < 15 ? d->nodes[node].len : 15;

    memcpy(buf, d->nodes[node].text, n);
    buf[n] = 0;
    return (uint32_t)strtoul(buf, NULL, 16);
}

static void check_run(xb_state *s, const void *pack, size_t pack_size, const json_doc *d, int run)
{
    const char *name = "";
    int steps = json_get(d, run, "steps_t_dt"), caps = json_get(d, run, "captures");
    int frames = d->nodes[steps].count, n, ci, bad, cap_ok, cap_n, restarts, restart_bad, no_quad;
    int integrated = 1;
    float prev = -1.0f;
    xb_config cfg = { 0, 0 };
    xb_blob b;
    xb_timing tm;
    char what[320], why[96];
    int nm = json_get(d, run, "run");
    static char runname[32];

    snprintf(runname, sizeof runname, "%.*s", (int)d->nodes[nm].len, d->nodes[nm].text);
    name = runname;

    /* replay through xb_blob_update */
    xb_init(s, pack, pack_size, &cfg);
    ci = cap_ok = cap_n = restarts = restart_bad = no_quad = 0;
    why[0] = 0;
    for (n = 0; n < frames; n++) {
        int st = json_at(d, steps, n), e;
        float t = num(d, json_at(d, st, 0)), dt = num(d, json_at(d, st, 1));

        if (t < prev) {
            xb_blob_restart(s);
            xb_get_blob(s, &b);
            e = event_at(d, run, n, "blob_restart");
            restarts++;
            restart_bad += e < 0 || b.rng != hex(d, json_get(d, e, "rand_after"));
            integrated = 0;
        }
        prev = t;
        if ((e = event_at(d, run, n, "generator set")) >= 0)
            xb_blob_set_rng(s, hex(d, json_get(d, e, "value")));
        xb_blob_update(s, t, dt);
        if (ci < d->nodes[caps].count) {
            int c = json_at(d, caps, ci);
            if ((int)json_num(d, json_get(d, c, "frame")) == n) {
                cap_n++;
                no_quad += restarts > 0;
                cap_ok += !capture_bad(s, d, c, num(d, json_get(d, c, "t")), num(d, json_get(d, c, "x1c")),
                                       num(d, json_get(d, c, "x20")), restarts == 0, why, sizeof why);
                ci++;
            }
        }
    }
    xb_get_blob(s, &b);
    snprintf(what, sizeof what, "%s replay: %d calls, %d of %d captures equal%s%s; %d restarts with the "
             "guest's generator after it (%d differ)", name, frames, cap_ok, cap_n, why[0] ? ", first: " : "",
             why, restarts, restart_bad);
    check(cap_ok == cap_n && ci == d->nodes[caps].count && restart_bad == 0, what);
    if (no_quad)
        printf("      %s: glow quad of %d captures after a restart not compared: the guest's camera follows "
               "another path there (Camera.md 7); check_k3.py compares it with the guest's camera\n",
               name, no_quad);
    snprintf(what, sizeof what, "%s replay: %u generator steps, guest %d; %u drops, guest %d", name, b.draws,
             (int)json_num(d, json_get(d, run, "rand_steps")), b.n_drops,
             (int)json_num(d, json_get(d, run, "drops")));
    check(b.draws == (uint32_t)json_num(d, json_get(d, run, "rand_steps")) &&
          b.n_drops == (uint32_t)json_num(d, json_get(d, run, "drops")), what);
    if (!integrated) {
        printf("      %s: the core does not loop at t >= 8 (docs/core.md 2), replay only\n", name);
        return;
    }

    /* the same through xb_advance_free */
    xb_init(s, pack, pack_size, &cfg);
    ci = cap_ok = cap_n = bad = 0;
    why[0] = 0;
    for (n = 0; n < frames; n++) {
        int st = json_at(d, steps, n), e;
        float t = num(d, json_at(d, st, 0)), dt = num(d, json_at(d, st, 1));

        if ((e = event_at(d, run, n, "generator set")) >= 0)
            xb_blob_set_rng(s, hex(d, json_get(d, e, "value")));
        xb_advance_free(s, dt);
        xb_get_blob(s, &b);
        bad += bits(xb_time(s)) != bits(t) || b.updates != (uint32_t)n + 1;
        if (ci < d->nodes[caps].count) {
            int c = json_at(d, caps, ci);
            if ((int)json_num(d, json_get(d, c, "frame")) == n) {
                xb_get_timing(s, &tm);
                cap_n++;
                if (!eq(d, json_get(d, c, "x1c"), tm.x1c) || !eq(d, json_get(d, c, "x20"), tm.x20))
                    snprintf(why, sizeof why, "x1c %08X x20 %08X at frame %d", (unsigned)bits(tm.x1c),
                             (unsigned)bits(tm.x20), n);
                else
                    cap_ok += !capture_bad(s, d, c, tm.t, tm.x1c, tm.x20, 1, why, sizeof why);
                ci++;
            }
        }
    }
    snprintf(what, sizeof what, "%s run: %d steps of xb_advance_free, t and blob_update as in the guest in "
             "%d; %d of %d captures equal with the core's t, x1c, x20%s%s", name, frames, frames - bad, cap_ok,
             cap_n, why[0] ? ", first: " : "", why);
    check(bad == 0 && cap_ok == cap_n, what);
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

static void fold_w(digest *g, const void *p, size_t bytes)
{
    const uint8_t *q = p;
    size_t i;

    for (i = 0; i + 4 <= bytes; i += 4)
        fold(g, (uint32_t)q[i] | (uint32_t)q[i + 1] << 8 | (uint32_t)q[i + 2] << 16 | (uint32_t)q[i + 3] << 24);
}

static int run_digest(xb_state *s, const void *pack, size_t pack_size, uint64_t *out, int *frames)
{
    static const uint32_t step[3] = { 17, 16, 17 };
    static xb_blob b;
    static xb_blob_params dr;
    static xb_real unit[3 * XB_BLOB_VERTICES];
    static uint16_t idx[XB_BLOB_INDICES];
    digest g = { 0xCBF29CE484222325ull };
    xb_config cfg = { 0, 0 };
    uint32_t tick = 0, k;
    int rc = XB_OK, n = 0;
    xb_camera cam;
    xb_timing tm;
    xb_real lv[16];

    if (xb_init(s, pack, pack_size, &cfg) != XB_OK)
        return -1;
    for (k = 0; k < 2; k++) {
        xb_blob_mesh(s, k, unit, idx);
        fold_w(&g, unit, 12 * (k ? XB_BLOB_SMALL_VERTICES : XB_BLOB_VERTICES));
        fold_w(&g, idx, 2 * (k ? XB_BLOB_SMALL_INDICES : XB_BLOB_INDICES));
    }
    while (rc == XB_OK) {
        tick += step[n % 3];
        rc = xb_advance(s, tick);
        xb_get_timing(s, &tm);
        xb_camera_at(s, tm.t, &cam);
        xb_get_blob(s, &b);
        xb_blob_deformed(s, deformed);
        xb_blob_draw(s, &cam, tm.t, tm.x1c, tm.x20, &dr);
        fold(&g, bits(tm.t));
        fold_w(&g, &b, sizeof b);
        fold_w(&g, deformed, sizeof deformed);
        fold_w(&g, &dr, sizeof dr);
        for (k = 0; k < 2; k++) {
            xb_blob_light_view(s, k, lv);
            fold_w(&g, lv, sizeof lv);
        }
        n++;
    }
    *out = g.h;
    *frames = n;
    return rc == XB_DONE ? 0 : -1;
}

/* ---- dumps for check_k3.py ---------------------------------------------- */

static int dump_mesh(const xb_state *s, const char *dir)
{
    static xb_real unit[3 * XB_BLOB_VERTICES];
    static uint16_t idx[XB_BLOB_INDICES];
    char path[512];
    uint32_t k;

    for (k = 0; k < 2; k++) {
        size_t nv = k ? XB_BLOB_SMALL_VERTICES : XB_BLOB_VERTICES, ni = k ? XB_BLOB_SMALL_INDICES : XB_BLOB_INDICES;
        FILE *f;

        if (xb_blob_mesh(s, k, unit, idx) != XB_OK)
            return 2;
        snprintf(path, sizeof path, "%s/%s.unit", dir, k ? "small" : "big");
        f = fopen(path, "wb");
        if (!f || fwrite(unit, 12, nv, f) != nv)
            return 2;
        fclose(f);
        snprintf(path, sizeof path, "%s/%s.idx", dir, k ? "small" : "big");
        f = fopen(path, "wb");
        if (!f || fwrite(idx, 2, ni, f) != ni)
            return 2;
        fclose(f);
    }
    return 0;
}

static void words(const void *p, size_t n)
{
    const uint32_t *w = p;
    size_t i;

    for (i = 0; i < n; i++)
        printf(" %08X", (unsigned)w[i]);
}

static void print_words(const char *tag, const void *p, size_t n)
{
    fputs(tag, stdout);
    words(p, n);
    putchar('\n');
}

static int replay(xb_state *s, const char *path)
{
    static xb_blob b;
    static xb_blob_params dr;
    FILE *f = fopen(path, "r");
    char line[1024];

    if (!f)
        return 2;
    while (fgets(line, sizeof line, f)) {
        unsigned long a = 0, c = 0, e = 0, h = 0;
        xb_camera cam;

        switch (line[0]) {
        case 'u':
        case 'S':
            xb_get_blob(s, &b);
            printf("s %08X %08X %08X %08X", b.rng, b.draws, b.n_bulges, b.n_drops);
            words(b.center, 8);              /* center, radius, colour */
            words(b.bulge, sizeof b.bulge / 4);
            print_words("", b.drop, sizeof b.drop / 4);
            if (line[0] == 'S')
                break;
            /* fall through */
        case 'v':
            sscanf(line + 1, "%lx %lx", &a, &c);
            xb_blob_update(s, from_bits((uint32_t)a), from_bits((uint32_t)c));
            break;
        case 'r':
            xb_blob_restart(s);
            break;
        case 'g':
            sscanf(line + 1, "%lx", &a);
            xb_blob_set_rng(s, (uint32_t)a);
            break;
        case 'm':
            xb_blob_deformed(s, deformed);
            print_words("m", deformed, 4 * XB_BLOB_VERTICES);
            break;
        case 'd':
            sscanf(line + 1, "%lx %lx %lx", &a, &c, &e);
            xb_camera_at(s, from_bits((uint32_t)a), &cam);
            xb_blob_draw(s, &cam, from_bits((uint32_t)a), from_bits((uint32_t)c), from_bits((uint32_t)e), &dr);
            print_words("d", &dr, sizeof dr / 4);
            break;
        case 'D': {
            const char *p = line + 1;
            uint32_t v[3 + 3 + 48];
            int i, used;

            for (i = 0; i < 54 && sscanf(p, "%lx%n", &a, &used) == 1; i++, p += used)
                v[i] = (uint32_t)a;
            memset(&cam, 0, sizeof cam);
            for (i = 0; i < 3; i++)
                cam.eye[i] = from_bits(v[3 + i]);
            for (i = 0; i < 16; i++) {
                cam.view_inv[i] = from_bits(v[6 + i]);
                cam.view[i] = from_bits(v[22 + i]);
                cam.proj[i] = from_bits(v[38 + i]);
            }
            xb_blob_draw(s, &cam, from_bits(v[0]), from_bits(v[1]), from_bits(v[2]), &dr);
            print_words("d", &dr, sizeof dr / 4);
            break;
        }
        case 'w': {
            float ref[3], out[4];
            sscanf(line + 1, "%lx %lx %lx %lx", &a, &c, &e, &h);
            ref[0] = from_bits((uint32_t)c);
            ref[1] = from_bits((uint32_t)e);
            ref[2] = from_bits((uint32_t)h);
            xb_blob_center(s, ref, from_bits((uint32_t)a), out, out + 3);
            print_words("w", out, 4);
            break;
        }
        case 'l': {
            xb_real lv[32];
            xb_blob_light_view(s, 0, lv);
            xb_blob_light_view(s, 1, lv + 16);
            print_words("l", lv, 32);
            break;
        }
        default:
            break;
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
    int r, n, frames[2], c;
    uint64_t dg[2];
    char what[256];

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: test_k3 <pack> <c6_blob.json> | --mesh <dir> | --replay <file> | --digest\n");
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
    if (argc == 4 && strcmp(argv[2], "--replay") == 0)
        return replay(s, argv[3]);
    if (argc == 3 && strcmp(argv[2], "--digest") == 0) {
        if (run_digest(s, pack, pack_size, &dg[0], &frames[0]) != 0)
            return 2;
        printf("digest %016llX %d\n", (unsigned long long)dg[0], frames[0]);
        return 0;
    }

    printf("state %u bytes, pack %u bytes; xb_blob %u, xb_blob_params %u, xb_timing %u, xb_camera %u bytes\n",
           (unsigned)xb_state_size(), (unsigned)pack_size, (unsigned)sizeof(xb_blob),
           (unsigned)sizeof(xb_blob_params), (unsigned)sizeof(xb_timing), (unsigned)sizeof(xb_camera));
    if (json_load(&d, argv[2]) != 0)
        return 2;
    {
        xb_blob b;
        xb_get_blob(s, &b);
        snprintf(what, sizeof what, "after set-up: %u bulges, %u drops, %u generator steps (Blob.md 4: 208), "
                 "mesh undeformed", b.n_bulges, b.n_drops, b.draws);
        check(b.n_bulges == 32 && b.n_drops == 8 && b.draws == 208 && b.flat == 1, what);
    }
    n = d.nodes[json_get(&d, 0, "runs")].count;
    check(n == 5, "c6_blob.json has five runs");
    for (r = 0; r < n; r++)
        check_run(s, pack, pack_size, &d, json_at(&d, json_get(&d, 0, "runs"), r));

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
