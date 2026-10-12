/* Host test of milestone K4: the logo against docs/vectors/c7_logo.json.
 *
 *     test_k4 <pack> <c7_logo.json>
 *     test_k4 <pack> --mesh <dir>
 *     test_k4 <pack> --replay <commands.txt>
 *     test_k4 <pack> --digest
 *
 * First form:
 *   - meshes: vertex and index count of each of the seven meshes, the
 *     number of triangles, and the largest index = vertex count - 1
 *     (Logo.md 2.1);
 *   - for each run, every draw of mesh D in the method stream: its t (the
 *     frame or the texture draw the vector matched it with) goes through
 *     xb_logo_update in the order of the stream, then xb_logo_draw with the
 *     core's camera at t gives vertex constants 0..8, which must equal the
 *     stream bit for bit. A texture draw must be one in the core too
 *     (xb_logo.rtt), with the vector's t, and a frame draw must not; the
 *     camera's switch this+0xE must be set at every such t;
 *   - the factor colour before E must equal the core's; the one before Q1
 *     and Q2 must be a value the core gives for some t in [6, 6.25] (the
 *     vector has no t for these draws; check_k4.py compares each with its
 *     frame), and the values must not fall in the order of the stream;
 *   - a run through xb_advance at 60 Hz: logo_update from the first frame
 *     with this+0xE, two draws into the texture at the first t >= 6 and
 *     the first t >= 6.25, at the end the lettering at key 1 and TM opaque.
 * Then a run over the whole eight seconds, twice from a fresh state, must
 * give the same digest (see --digest).
 *
 * --mesh writes each mesh as raw little-endian files <dir>/<name>.{vtx,idx}
 * for check_k4.py.
 *
 * --replay reads commands, one per line, and prints bit patterns in hex:
 *   i                 a fresh state (xb_init)
 *   u <t>             xb_logo_update, then "s" as for S
 *   S                 "s" with the logo state: tm, texture, phase, rtt,
 *                     updates, rtt_count, pos 3, alpha
 *   d <t> <r>         "d" with xb_logo_draw for the core's camera at t
 *   D <t> <r> <view 16> <proj 16>   the same for a camera given by the caller
 *   c <t>             "c" with logo_on of the camera and view, proj at t
 *
 * --digest runs from tick 0 at 60 Hz (17, 16, 17 ms) until XB_DONE and
 * folds at every frame t, the logo's state and the drawn values (on the
 * screen, and into the texture where the frame drew there) into an FNV-1a
 * digest, before that the seven meshes. Prints "digest <16 hex digits>
 * <frames>". The wasm build prints the same line (core/test/wasm_k4.mjs).
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

static uint32_t hex(const json_doc *d, int node)
{
    char buf[16];
    size_t n = d->nodes[node].len < 15 ? d->nodes[node].len : 15;

    memcpy(buf, d->nodes[node].text, n);
    buf[n] = 0;
    return (uint32_t)strtoul(buf, NULL, 16);
}

static const char *const mesh_name[XB_LOGO_MESHES] = { "A", "B", "C", "D", "Q1", "Q2", "E" };

static xb_real vtx[5 * 501];
static uint16_t idx[1008];

/* ---- meshes ------------------------------------------------------------ */

static void check_meshes(const xb_state *s, const json_doc *d)
{
    int m, bad = 0;
    char what[256], why[96] = "";

    for (m = 0; m < XB_LOGO_MESHES; m++) {
        int e = json_get(d, json_get(d, 0, "meshes"), mesh_name[m]);
        xb_logo_mesh_desc in;
        uint32_t i, top = 0;

        xb_logo_mesh_info(s, (uint32_t)m, &in);
        if (xb_logo_mesh(s, (uint32_t)m, vtx, idx) != XB_OK) {
            snprintf(why, sizeof why, "mesh %s not decoded", mesh_name[m]);
            bad++;
            continue;
        }
        for (i = 0; i < in.indices; i++)
            if (idx[i] > top)
                top = idx[i];
        if (in.vertices != (uint32_t)json_num(d, json_get(d, e, "vertices")) ||
            in.indices != (uint32_t)json_num(d, json_get(d, e, "indices")) ||
            in.indices / 3 != (uint32_t)json_num(d, json_get(d, e, "triangles")) || in.indices % 3 ||
            top != in.vertices - 1 || in.floats != (m == XB_LOGO_E ? 3u : 5u)) {
            snprintf(why, sizeof why, "mesh %s: %u vertices, %u indices, largest %u", mesh_name[m], in.vertices,
                     in.indices, top);
            bad++;
        }
    }
    snprintf(what, sizeof what, "seven meshes: vertex and index counts, triangles and the largest index as in "
             "the vector (%d differ)%s%s", bad, why[0] ? ", first: " : "", why);
    check(bad == 0, what);
}

/* ---- the draws of one run ---------------------------------------------- */

/* Range of t over which the core gives each factor word before Q1 and Q2:
 * words are alpha << 24, so 256 entries. */
static uint32_t q_lo[256], q_hi[256], e_colour;
static int q_seen[256];

static void sweep_quad_words(xb_state *s, const void *pack, size_t pack_size)
{
    xb_config cfg = { 0, 0 };
    xb_logo_params p;
    xb_camera cam;
    uint32_t u, last = bits(6.25f);

    memset(q_seen, 0, sizeof q_seen);
    xb_init(s, pack, pack_size, &cfg);
    xb_camera_at(s, 6.0f, &cam);
    for (u = bits(6.0f); u <= last; u++) {
        int to_tex;

        xb_logo_update(s, from_bits(u));
        /* below 6.25 on the screen; at 6.25 in the draw into the texture */
        to_tex = u == last;
        xb_logo_draw(s, &cam, from_bits(u), (uint32_t)to_tex, &p);
        if (p.text) {
            uint32_t w = p.quad_colour >> 24;

            e_colour = p.text_colour;
            if (!q_seen[w])
                q_lo[w] = u;
            q_seen[w] = 1;
            q_hi[w] = u;
        }
    }
}

static void check_run(xb_state *s, const void *pack, size_t pack_size, const json_doc *d, int run,
                      const char *name)
{
    int draws = json_path(d, run, "stream/draws"), tex = json_path(d, run, "texture_draws/t");
    int n = d->nodes[draws].count, i, n_d = 0, d_ok = 0, n_e = 0, e_ok = 0, n_q = 0, q_ok = 0, n_tex = 0;
    int tex_bad = 0, prev_q = -1;
    xb_config cfg = { 0, 0 };
    xb_logo_params p;
    xb_logo lg;
    xb_camera cam;
    char what[400], why[160] = "";

    xb_init(s, pack, pack_size, &cfg);
    for (i = 0; i < n; i++) {
        int row = json_at(d, draws, i), mesh = json_get(d, row, "mesh");

        if (json_str_eq(d, mesh, "D")) {
            int mt = json_get(d, row, "matched"), c = json_get(d, row, "constants_0_8"), r, k, ok = 1;
            int to_tex = json_str_eq(d, json_get(d, mt, "by"), "texture");
            float t = (float)json_num(d, json_get(d, mt, "t"));

            n_d++;
            xb_logo_update(s, t);
            xb_get_logo(s, &lg);
            xb_camera_at(s, t, &cam);
            if (to_tex) {
                float want = n_tex < d->nodes[tex].count ? (float)json_num(d, json_at(d, tex, n_tex)) : -1.0f;
                tex_bad += !lg.rtt || bits(want) != bits(t);
                n_tex++;
            } else {
                tex_bad += lg.rtt;
            }
            xb_logo_draw(s, &cam, t, (uint32_t)to_tex, &p);
            ok = cam.logo_on && p.solid;
            for (r = 0; r < 9 && ok; r++)
                for (k = 0; k < 4 && ok; k++)
                    if (hex(d, json_at(d, json_at(d, c, r), k)) != bits(p.d_vs[r][k])) {
                        ok = 0;
                        if (!why[0])
                            snprintf(why, sizeof why, "D at t = %.4f (%s), constant %d.%d: %08X, stream %08X", t,
                                     to_tex ? "texture" : "frame", r, k, (unsigned)bits(p.d_vs[r][k]),
                                     (unsigned)hex(d, json_at(d, json_at(d, c, r), k)));
                    }
            d_ok += ok;
        } else if (json_str_eq(d, mesh, "E")) {
            n_e++;
            e_ok += hex(d, json_get(d, row, "factor")) == e_colour;
        } else if (json_str_eq(d, mesh, "Q1") || json_str_eq(d, mesh, "Q2")) {
            uint32_t f = hex(d, json_get(d, row, "factor")), w = f >> 24;

            n_q++;
            if ((f & 0xFFFFFFu) == 0 && q_seen[w] && (int)w >= prev_q) {
                q_ok++;
                prev_q = (int)w;
            } else if (!why[0]) {
                snprintf(why, sizeof why, "factor %08X before %.*s", (unsigned)f, (int)d->nodes[mesh].len,
                         d->nodes[mesh].text);
            }
        }
    }
    xb_get_logo(s, &lg);
    snprintf(what, sizeof what, "%s: %d draws of D, constants 0..8 equal in %d with the core's camera; "
             "%d of them into the texture, as in the core and at the vector's t (%d wrong)%s%s", name, n_d, d_ok,
             n_tex, tex_bad, why[0] ? "; first: " : "", why);
    check(n_d > 0 && d_ok == n_d && tex_bad == 0 && n_tex == d->nodes[tex].count && lg.rtt_count == (uint32_t)n_tex,
          what);
    snprintf(what, sizeof what, "%s: factor colour before E in %d of %d draws as in the core; before Q1 and Q2 "
             "in %d of %d a value of the core between t = 6 and 6.25, not falling", name, e_ok, n_e, q_ok, n_q);
    check(e_ok == n_e && q_ok == n_q, what);
}

static void check_advance(xb_state *s, const void *pack, size_t pack_size)
{
    static const uint32_t step[3] = { 17, 16, 17 };
    xb_config cfg = { 0, 0 };
    xb_camera cam;
    xb_timing tm;
    xb_logo lg;
    uint32_t tick = 0, updates = 0, keys[3];
    int rc = XB_OK, n = 0, bad = 0, first = -1, rtt[2] = { -1, -1 }, nr = 0, i;
    float rtt_t[2] = { 0, 0 };
    char what[320];

    xb_init(s, pack, pack_size, &cfg);
    while (rc == XB_OK) {
        tick += step[n % 3];
        rc = xb_advance(s, tick);
        xb_get_timing(s, &tm);
        xb_camera_at(s, tm.t, &cam);
        xb_get_logo(s, &lg);
        updates += cam.logo_on;
        if (cam.logo_on && first < 0)
            first = n;
        bad += lg.updates != updates;
        if (lg.rtt && nr < 2) {
            rtt[nr] = n;
            rtt_t[nr++] = tm.t;
        }
        n++;
    }
    /* key 1 of the lettering, 0x80073FC4 (Logo.md 2.4) */
    keys[0] = 0xBE6AE89Fu;    /* 0x80073FC4, compared by check_k4.py */
    keys[1] = 0xC30D0DC3u;
    keys[2] = 0xC259C22Du;
    for (i = 0; i < 3; i++)
        bad += bits(lg.pos[i]) != keys[i];
    snprintf(what, sizeof what, "run at 60 Hz: logo_update from frame %d on in %u frames (%d wrong), into the "
             "texture at t = %.4f and %.4f (%d draws), at the end tm %u, phase %u, alpha %.1f, lettering at key 1",
             first, updates, bad, rtt_t[0], rtt_t[1], lg.rtt_count, lg.tm, lg.phase, lg.alpha);
    check(bad == 0 && rc == XB_DONE && lg.rtt_count == 2 && rtt_t[0] >= 6.0f && rtt[0] > 0 && rtt_t[1] >= 6.25f &&
          lg.tm == 1 && lg.phase == 1 && lg.alpha == 255.0f, what);
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
    digest g = { 0xCBF29CE484222325ull };
    xb_config cfg = { 0, 0 };
    uint32_t tick = 0, m;
    int rc = XB_OK, n = 0;
    xb_logo_params p;
    xb_camera cam;
    xb_timing tm;
    xb_logo lg;

    if (xb_init(s, pack, pack_size, &cfg) != XB_OK)
        return -1;
    for (m = 0; m < XB_LOGO_MESHES; m++) {
        xb_logo_mesh_desc in;

        xb_logo_mesh_info(s, m, &in);
        xb_logo_mesh(s, m, vtx, idx);
        fold_w(&g, vtx, 4 * in.floats * in.vertices);
        fold_w(&g, idx, 2 * in.indices);
    }
    while (rc == XB_OK) {
        tick += step[n % 3];
        rc = xb_advance(s, tick);
        xb_get_timing(s, &tm);
        xb_camera_at(s, tm.t, &cam);
        xb_get_logo(s, &lg);
        fold(&g, bits(tm.t));
        fold_w(&g, &lg, sizeof lg);
        if (lg.rtt) {
            xb_logo_draw(s, &cam, tm.t, 1, &p);
            fold_w(&g, &p, sizeof p);
        }
        if (cam.logo_on) {
            xb_logo_draw(s, &cam, tm.t, 0, &p);
            fold_w(&g, &p, sizeof p);
        }
        n++;
    }
    *out = g.h;
    *frames = n;
    return rc == XB_DONE ? 0 : -1;
}

/* ---- dumps for check_k4.py ---------------------------------------------- */

static int dump_mesh(const xb_state *s, const char *dir)
{
    char path[512];
    uint32_t m;

    for (m = 0; m < XB_LOGO_MESHES; m++) {
        xb_logo_mesh_desc in;
        FILE *f;

        xb_logo_mesh_info(s, m, &in);
        if (xb_logo_mesh(s, m, vtx, idx) != XB_OK)
            return 2;
        snprintf(path, sizeof path, "%s/%s.vtx", dir, mesh_name[m]);
        f = fopen(path, "wb");
        if (!f || fwrite(vtx, 4 * in.floats, in.vertices, f) != in.vertices)
            return 2;
        fclose(f);
        snprintf(path, sizeof path, "%s/%s.idx", dir, mesh_name[m]);
        f = fopen(path, "wb");
        if (!f || fwrite(idx, 2, in.indices, f) != in.indices)
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

static int replay(xb_state *s, const void *pack, size_t pack_size, const char *path)
{
    FILE *f = fopen(path, "r");
    char line[2048];
    xb_config cfg = { 0, 0 };

    if (!f)
        return 2;
    while (fgets(line, sizeof line, f)) {
        unsigned long a = 0, c = 0;
        xb_logo_params p;
        xb_camera cam;
        xb_logo lg;

        switch (line[0]) {
        case 'i':
            xb_init(s, pack, pack_size, &cfg);
            break;
        case 'u':
            sscanf(line + 1, "%lx", &a);
            xb_logo_update(s, from_bits((uint32_t)a));
            /* fall through */
        case 'S':
            xb_get_logo(s, &lg);
            print_words("s", &lg, sizeof lg / 4);
            break;
        case 'd':
            sscanf(line + 1, "%lx %lx", &a, &c);
            xb_camera_at(s, from_bits((uint32_t)a), &cam);
            xb_logo_draw(s, &cam, from_bits((uint32_t)a), (uint32_t)c, &p);
            print_words("d", &p, sizeof p / 4);
            break;
        case 'D': {
            const char *q = line + 1;
            uint32_t v[2 + 32];
            int i, used;

            for (i = 0; i < 34 && sscanf(q, "%lx%n", &a, &used) == 1; i++, q += used)
                v[i] = (uint32_t)a;
            memset(&cam, 0, sizeof cam);
            for (i = 0; i < 16; i++) {
                cam.view[i] = from_bits(v[2 + i]);
                cam.proj[i] = from_bits(v[18 + i]);
            }
            xb_logo_draw(s, &cam, from_bits(v[0]), v[1], &p);
            print_words("d", &p, sizeof p / 4);
            break;
        }
        case 'c':
            sscanf(line + 1, "%lx", &a);
            xb_camera_at(s, from_bits((uint32_t)a), &cam);
            printf("c %u", cam.logo_on);
            words(cam.view, 16);
            print_words("", cam.proj, 16);
            break;
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
        fprintf(stderr, "usage: test_k4 <pack> <c7_logo.json> | --mesh <dir> | --replay <file> | --digest\n");
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
        return replay(s, pack, pack_size, argv[3]);
    if (argc == 3 && strcmp(argv[2], "--digest") == 0) {
        if (run_digest(s, pack, pack_size, &dg[0], &frames[0]) != 0)
            return 2;
        printf("digest %016llX %d\n", (unsigned long long)dg[0], frames[0]);
        return 0;
    }

    printf("state %u bytes, pack %u bytes; xb_logo %u, xb_logo_params %u, xb_camera %u bytes\n",
           (unsigned)xb_state_size(), (unsigned)pack_size, (unsigned)sizeof(xb_logo),
           (unsigned)sizeof(xb_logo_params), (unsigned)sizeof(xb_camera));
    if (json_load(&d, argv[2]) != 0)
        return 2;
    check((int)json_num(&d, json_get(&d, 0, "failures")) == 0, "c7_logo.json: the model met the guest (0 failures)");
    check_meshes(s, &d);
    sweep_quad_words(s, pack, pack_size);
    {
        int w, k = 0;
        for (w = 0; w < 256; w++)
            k += q_seen[w];
        snprintf(what, sizeof what, "factor colours of Q1, Q2 for every float32 t in [6, 6.25]: %d values, "
                 "0x00000000 at t = %.6f, 0xFF000000 from t = %.6f", k, from_bits(q_lo[0]), from_bits(q_lo[255]));
        check(q_seen[0] && q_seen[255] && k == 256, what);
    }
    n = d.nodes[json_get(&d, 0, "runs")].count;
    check(n == 2, "c7_logo.json has two runs");
    for (r = 0; r < 2; r++) {
        const char *name = r ? "c7_b" : "c7_main";
        check_run(s, pack, pack_size, &d, json_get(&d, json_get(&d, 0, "runs"), name), name);
    }
    check_advance(s, pack, pack_size);

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
