/* Host test of milestone K6: effects and the frame description against
 * docs/vectors/c8_gpu.json.
 *
 *     test_k6 <pack> <c8_gpu.json>
 *     test_k6 <pack> <c8_gpu.json> --dump <dir>
 *     test_k6 <pack> --digest
 *
 * First form. The vector holds the run c8_main2: the step of every frame in
 * whole milliseconds with t after it (section "timeline"), for 21 recorded
 * frames every draw call of the stream with its step of FrameOrder.md,
 * primitive, count, program, combiners, textures and targets ("draws",
 * "frames") and the render states ("kinds"), and for 20 frames the track
 * values and what the veil and the glow sent ("effects").
 *   - Set-up: xb_frame_setup, passes 0 to 6, against frame 0.
 *   - The core replays the timeline with xb_advance; t after every step
 *     must equal the guest's, bit for bit. Stops the vector lists as
 *     repeated are no frames and are skipped.
 *   - At every recorded frame xb_frame against the vector's draws: the
 *     same number of draws in the same order, each with the same step,
 *     primitive, count, program, combiners, the textures the draw reads,
 *     both targets and the render states in effect.
 *   - At every frame of "effects": the track values (anim + 0x690, 0x698,
 *     0x6A0, 0x6A8), the veil's two colours packed as 0x800410B0 packs
 *     them (GPU.md 4.2) and alpha, factor colour, s and 1 / s of the glow.
 * What the comparison leaves out, because the stream shows something the
 * animation does not set: program and combiners of the library's own draw
 * (A1, D5), textures on stages the draw does not read, targets the vector
 * could not name (null), a render state no call sent in the frame.
 * Functions and factors count only while their switch is on.
 *
 * --dump writes every recorded frame as <dir>/frame_<n>.json with all
 * constants, colours and matrices as bit patterns, for check_k6.py, which
 * compares them with the guest's stream.
 * --digest runs 8 s at 60 Hz, folds the set-up and every frame's draws and
 * values into an FNV-1a digest, NaN folded as one pattern, and prints
 * "digest <16 hex digits> <frames>".
 * The wasm build prints the same line (core/test/wasm_k6.mjs).
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

static uint32_t bits(float f)
{
    uint32_t u;

    memcpy(&u, &f, 4);
    return u;
}

static void str(const json_doc *d, int node, char *out, size_t cap)
{
    size_t n = 0;

    if (node >= 0 && d->nodes[node].type == JSON_STR)
        n = d->nodes[node].len < cap - 1 ? d->nodes[node].len : cap - 1;
    if (n)
        memcpy(out, d->nodes[node].text, n);
    out[n] = 0;
}

static uint32_t hex(const json_doc *d, int node)
{
    char buf[24];

    str(d, node, buf, sizeof buf);
    return (uint32_t)strtoul(buf, NULL, 16);
}

static xb_draw draws[XB_FRAME_DRAWS_MAX];
static xb_real params[XB_FRAME_PARAMS_MAX];

static void buffers(xb_frame_out *o)
{
    memset(o, 0, sizeof *o);
    o->draws = draws;
    o->draw_cap = XB_FRAME_DRAWS_MAX;
    o->params = params;
    o->param_cap = XB_FRAME_PARAMS_MAX;
}

/* ---- names of the vector -------------------------------------------------- */

/* Name of a surface in the vector. Textures the probe saw being made have
 * a label; the others appear as their physical address, which the test
 * learns at the first use and then holds the core to. */
static char surf_name[XB_SURFACES][24];

static void surf_init(void)
{
    static const char *const tex[XB_TEXTURES] = {
        "normal_cube128", "normal_cube64", "fractal128", "falloff256", "gradient_green", "radial256",
        "gradient_grey", "tm16", "noise_a", "noise_b", "noise_c"
    };
    int i;

    memset(surf_name, 0, sizeof surf_name);
    strcpy(surf_name[XB_SURF_NONE], "0x0");
    for (i = 0; i < XB_TEXTURES; i++)
        strcpy(surf_name[XB_SURF_TEX + i], tex[i]);
    strcpy(surf_name[XB_SURF_LIGHT0], "light_view_a");
    strcpy(surf_name[XB_SURF_LIGHT1], "light_view_b");
    /* The labels name the fields the other way round: the texture made at
     * 0x8005A445 goes to fx+0x18 (Effects.md 1). */
    strcpy(surf_name[XB_SURF_FX_A], "fx_target_14");
    strcpy(surf_name[XB_SURF_FX_B], "fx_target_18");
    strcpy(surf_name[XB_SURF_FX_DEPTH], "fx_depth");
    strcpy(surf_name[XB_SURF_LOGO], "logo_target");
    strcpy(surf_name[XB_SURF_CUBE], "chamber_cube256");
    strcpy(surf_name[XB_SURF_SCREEN], "0x3628000");
    strcpy(surf_name[XB_SURF_SCREEN_DEPTH], "0x33d0000");
    strcpy(surf_name[XB_SURF_DISPLAY], "0x32a4000");
}

/* 1 equal, 0 different, -1 the vector has no name (null). */
static int surf_eq(const json_doc *d, int node, unsigned id)
{
    char got[24];
    int i;

    if (node < 0 || d->nodes[node].type != JSON_STR)
        return -1;
    str(d, node, got, sizeof got);
    if (id >= XB_SURFACES)
        return 0;
    if (!surf_name[id][0]) {
        for (i = 0; i < XB_SURFACES; i++)
            if (!strcmp(surf_name[i], got))
                return 0;                    /* the address belongs to another surface */
        if (strncmp(got, "0x", 2))
            return 0;
        strcpy(surf_name[id], got);
        return 1;
    }
    return !strcmp(surf_name[id], got);
}

static void step_name(unsigned step, char out[4])
{
    out[0] = (step >> 8) == 0x0A ? 'A' : (step >> 8) == 0x0B ? 'U' : 'D';
    out[1] = (char)('0' + (step & 0xFF));
    out[2] = 0;
}

/* ---- one frame against the vector ----------------------------------------- */

typedef struct tally {
    int draws, order, objects, textures, targets, states;
    int unnamed, unsent;             /* left out: null targets, states not sent */
} tally;

/* A state of the kind's snapshot: 1 and *v if the frame sent it. */
static int sent(const json_doc *d, int st, const char *key, uint32_t *v)
{
    int n = json_get(d, st, key);

    if (n < 0)
        return 0;
    *v = hex(d, n);
    return 1;
}

static int state_differs(const json_doc *d, int st, const xb_draw *w, tally *ty)
{
    const xb_render_state *r = &w->state;
    uint32_t v;
    int bad = 0;

#define ONE(key, want) do { if (sent(d, st, key, &v)) bad += v != (uint32_t)(want); else ty->unsent++; } while (0)
    ONE("0x0300", r->alpha_test);
    if (r->alpha_test) {
        ONE("0x033c", r->alpha_func);
        ONE("0x0340", r->alpha_ref);
    }
    ONE("0x0304", r->blend);
    if (r->blend) {
        ONE("0x0344", r->blend_src);
        ONE("0x0348", r->blend_dst);
    }
    ONE("0x030c", r->depth_test);
    if (r->depth_test)
        ONE("0x0354", r->depth_func);
    ONE("0x035c", r->depth_write);
    /* 0x308 of another object class turns up in the snapshot after the
     * copy into the logo's texture (0x8003EBC0); only 0 and 1 are the
     * cull switch. */
    if (sent(d, st, "0x0308", &v) && v <= 1) {
        bad += v != r->cull;
        if (r->cull)
            ONE("0x039c", r->cull_face);
    } else {
        ty->unsent++;
    }
    ONE("0x0358", r->colour_mask);
    /* SET_SHADOW_DEPTH_FUNC carries the function without 0x200 */
    if (r->shadow_func)
        ONE("0x1e6c", r->shadow_func - 0x200u);
#undef ONE
    return bad != 0;
}

static void compare_draws(const json_doc *d, int frame, const xb_draw *w, uint32_t n, uint32_t at,
                          tally *ty)
{
    int list = json_get(d, frame, "draws"), kinds = json_path(d, 0, "draws/kinds"), node, tex;
    uint32_t i;
    char a[40], b[40];

    node = json_at(d, list, (int)at);
    for (i = 0; i < n; i++, node = node < 0 ? -1 : d->nodes[node].next) {
        const xb_draw *x = &w[i];
        int bad, r, k;

        ty->draws++;
        if (node < 0) {
            ty->order++;
            continue;
        }
        /* step, primitive, count */
        step_name(x->step, a);
        str(d, json_get(d, node, "step"), b, sizeof b);
        bad = strcmp(a, b) != 0;
        bad |= (uint32_t)json_num(d, json_get(d, node, "prim")) != x->prim;
        /* the library's triangle goes inline, four words per vertex */
        bad |= (uint32_t)json_num(d, json_get(d, node, "count")) !=
               (x->what == XB_DRAW_PRESENT ? 4u * x->count : x->count);
        ty->order += bad;
        /* program and combiners */
        if (x->what != XB_DRAW_PRESENT) {
            if (x->program < 0x10000u)
                snprintf(a, sizeof a, "fixed function 0x%x", (unsigned)x->program);
            else
                snprintf(a, sizeof a, "0x%08x", (unsigned)x->program);
            str(d, json_get(d, node, "program"), b, sizeof b);
            bad = strcmp(a, b) != 0;
            if (x->combiners)
                snprintf(a, sizeof a, "0x%08x", (unsigned)x->combiners);
            else
                strcpy(a, "from texture stages");
            str(d, json_get(d, node, "combiners"), b, sizeof b);
            bad |= strcmp(a, b) != 0;
            ty->objects += bad;
        }
        /* textures the draw reads */
        tex = json_get(d, node, "textures");
        for (k = 0, bad = 0; k < 4; k++) {
            char key[2] = { (char)('0' + k), 0 };

            if (x->tex[k] == XB_SURF_NONE)
                continue;
            if (json_get(d, tex, key) < 0) {
                /* The set-up's presentation: the stage's texture was set
                 * before the probe's first stop, the vector has only its
                 * address in the snapshot (SET_TEXTURE_OFFSET). */
                int kd = json_at(d, kinds, (int)json_num(d, json_get(d, node, "kind")));
                uint32_t off;
                char reg[8];

                snprintf(reg, sizeof reg, "0x%04x", 0x1b00 + 0x40 * k);
                if (sent(d, json_get(d, kd, "state"), reg, &off)) {
                    snprintf(b, sizeof b, "0x%x", (unsigned)off);
                    bad |= strcmp(b, surf_name[x->tex[k]]) != 0;
                    continue;
                }
            }
            if (surf_eq(d, json_get(d, tex, key), x->tex[k]) != 1) {
                str(d, json_get(d, tex, key), b, sizeof b);
                if (!bad && ty->textures < 3)
                    printf("      draw %u (%s): stage %d is surface %u (%s) in the core, \"%s\" in the vector\n",
                           (unsigned)(at + i), a, k, x->tex[k], surf_name[x->tex[k]], b);
                bad = 1;
            }
        }
        ty->textures += bad;
        /* targets */
        r = surf_eq(d, json_get(d, node, "color"), x->color);
        bad = r == 0;
        ty->unnamed += r < 0;
        r = surf_eq(d, json_get(d, node, "zeta"), x->zeta);
        bad |= r == 0;
        ty->unnamed += r < 0;
        if (bad && ty->targets < 3) {
            str(d, json_get(d, node, "color"), a, sizeof a);
            str(d, json_get(d, node, "zeta"), b, sizeof b);
            printf("      draw %u: targets %u (%s) and %u (%s) in the core, \"%s\" and \"%s\" in the vector\n",
                   (unsigned)(at + i), x->color, surf_name[x->color], x->zeta, surf_name[x->zeta], a, b);
        }
        ty->targets += bad;
        /* render states */
        k = json_at(d, kinds, (int)json_num(d, json_get(d, node, "kind")));
        ty->states += state_differs(d, json_get(d, k, "state"), x, ty);
    }
}

static void report(const char *what, int n_vector, const tally *ty)
{
    char line[400];

    snprintf(line, sizeof line, "%s: %d draws, vector %d; step, primitive or count differ in %d, "
             "program or combiners in %d, textures in %d, targets in %d, states in %d "
             "(left out: %d targets without a name, %d states not sent)", what, ty->draws, n_vector,
             ty->order, ty->objects, ty->textures, ty->targets, ty->states, ty->unnamed, ty->unsent);
    check(ty->draws == n_vector && !ty->order && !ty->objects && !ty->textures && !ty->targets &&
          !ty->states, line);
}

/* ---- effects --------------------------------------------------------------- */

/* 0x800410B0 (GPU.md 4.2): clamp to [0, 1], trunc(f32(x * 255 + 0.5)),
 * A << 24 | R << 16 | G << 8 | B. */
static uint32_t pack_colour(const xb_real c[4])
{
    uint32_t b[4];
    int i;

    for (i = 0; i < 4; i++) {
        double x = c[i] > 1.0f ? 1.0 : (c[i] < 0.0f ? 0.0 : (double)c[i]);

        b[i] = (uint32_t)(int32_t)(float)(x * 255.0 + 0.5) & 0xFFu;
    }
    return (b[3] << 24) | (b[0] << 16) | (b[1] << 8) | b[2];
}

static void check_effects(const xb_state *s, const json_doc *d, int rec, int frame)
{
    int tr = json_get(d, rec, "track"), col = json_get(d, rec, "colours"), gl = json_get(d, rec, "glow");
    xb_timing tm;
    xb_camera cam;
    xb_track trk;
    xb_fx_params fx;
    char what[400];
    int ok;

    xb_get_timing(s, &tm);
    xb_camera_at(s, tm.t, &cam);
    xb_get_track(s, &trk);
    xb_fx_draw(s, &cam, tm.t, tm.x20, &trk, &fx);
    ok = bits(trk.dist) == hex(d, json_at(d, tr, 0)) && bits(trk.azimuth) == hex(d, json_at(d, tr, 1)) &&
         bits(trk.elevation) == hex(d, json_at(d, tr, 2)) &&
         bits(trk.blob_dist) == hex(d, json_at(d, tr, 3));
    snprintf(what, sizeof what, "frame %d t=%.3f: track |eye| %.4f, azimuth %.5f, elevation %.5f, D %.4f "
             "equal the guest's (%08X %08X %08X %08X)", frame, (double)tm.t, (double)trk.dist,
             (double)trk.azimuth, (double)trk.elevation, (double)trk.blob_dist, (unsigned)bits(trk.dist),
             (unsigned)bits(trk.azimuth), (unsigned)bits(trk.elevation), (unsigned)bits(trk.blob_dist));
    check(ok && (float)json_num(d, json_get(d, rec, "x20")) == tm.x20 &&
          (float)json_num(d, json_get(d, rec, "t")) == tm.t, what);

    if (col >= 0) {
        uint32_t k0 = pack_colour(fx.veil_ps[0]), k1 = pack_colour(fx.veil_ps[1]);

        snprintf(what, sizeof what, "frame %d: veil colours %08X %08X, vector %08X %08X; fx_draw %s",
                 frame, (unsigned)k0, (unsigned)k1, (unsigned)hex(d, json_at(d, col, 0)),
                 (unsigned)hex(d, json_at(d, col, 1)), fx.veil ? "called" : "not called");
        check(fx.veil && k0 == hex(d, json_at(d, col, 0)) && k1 == hex(d, json_at(d, col, 1)), what);
    } else {
        snprintf(what, sizeof what, "frame %d: no veil in the vector, fx_draw %s", frame,
                 fx.veil ? "called" : "not called");
        check(!fx.veil, what);
    }
    if (gl >= 0 && d->nodes[gl].type == JSON_OBJ) {
        ok = fx.glow && fx.glow_alpha == (uint32_t)json_num(d, json_get(d, gl, "alpha")) &&
             fx.glow_colour == (uint32_t)json_num(d, json_get(d, gl, "factor")) &&
             fx.glow_s == (float)json_num(d, json_get(d, gl, "s")) &&
             fx.glow_world[0] == (float)json_num(d, json_get(d, gl, "inv_s")) &&
             fx.glow_world[5] == fx.glow_world[0];
        snprintf(what, sizeof what, "frame %d: glow alpha %u, factor %08X, s %.6g, 1/s %.6g as in the vector",
                 frame, (unsigned)fx.glow_alpha, (unsigned)fx.glow_colour, (double)fx.glow_s,
                 (double)fx.glow_world[0]);
        check(ok, what);
    } else {
        snprintf(what, sizeof what, "frame %d: no glow in the vector, alpha %u in the core", frame,
                 (unsigned)fx.glow_alpha);
        check(!fx.glow, what);
    }
}

/* ---- dump for check_k6.py --------------------------------------------------- */

static void dump_words(FILE *f, const xb_real *v, uint32_t n)
{
    uint32_t i;

    fputc('[', f);
    for (i = 0; i < n; i++)
        fprintf(f, "%s%u", i ? "," : "", (unsigned)bits(v[i]));
    fputc(']', f);
}

static void dump_draws(FILE *f, const xb_frame_out *o, int *first)
{
    uint32_t i;
    char st[4];
    int k;

    for (i = 0; i < o->draw_count; i++) {
        const xb_draw *x = &o->draws[i];

        step_name(x->step, st);
        fprintf(f, "%s{\"step\":\"%s\",\"what\":%u,\"mesh\":%u,\"part\":%u,\"prim\":%u,\"detail\":%u,"
                "\"first\":%u,\"count\":%u,\"flags\":%u,\"program\":%u,\"combiners\":%u,\"factor\":%u,"
                "\"color\":%u,\"zeta\":%u,\"tex\":[%u,%u,%u,%u]",
                *first ? "" : ",\n", st, x->what, x->mesh, x->part, x->prim, x->detail,
                (unsigned)x->first, (unsigned)x->count, (unsigned)x->flags, (unsigned)x->program,
                (unsigned)x->combiners, (unsigned)x->factor, x->color, x->zeta, x->tex[0], x->tex[1],
                x->tex[2], x->tex[3]);
        *first = 0;
        fputs(",\"vs\":[", f);
        for (k = 0; k < 2; k++)
            if (x->vs[k].count) {
                fprintf(f, "%s[%u,", k && x->vs[0].count ? "," : "", x->vs[k].base);
                dump_words(f, o->params + x->vs[k].first, 4u * x->vs[k].count);
                fputc(']', f);
            }
        fputs("],\"ps\":", f);
        dump_words(f, o->params + x->ps_first, 4u * x->ps_count);
        if (x->flags & XB_DRAW_MATRICES) {
            fputs(",\"world\":", f);
            dump_words(f, o->params + x->world, 16);
            fputs(",\"view\":", f);
            dump_words(f, o->params + x->view, 16);
            fputs(",\"proj\":", f);
            dump_words(f, o->params + x->proj, 16);
        }
        if (x->vtx_count) {
            fputs(",\"vtx\":", f);
            dump_words(f, o->params + x->vtx_first, x->vtx_count);
        }
        fputc('}', f);
    }
}

static FILE *dump_open(const char *dir, int frame)
{
    char path[600];
    FILE *f;

    snprintf(path, sizeof path, "%s/frame_%d.json", dir, frame);
    f = fopen(path, "wb");
    if (f)
        fputs("[\n", f);
    return f;
}

/* ---- digest ----------------------------------------------------------------- */

typedef struct digest {
    uint64_t h;
} digest;

static void fold(digest *g, uint32_t u)
{
    int i;

    for (i = 0; i < 4; i++) {
        g->h ^= (u >> (8 * i)) & 0xFFu;
        g->h *= 0x100000001B3ull;
    }
}

static void fold_frame(digest *g, const xb_frame_out *o)
{
    uint32_t i, k, w;

    fold(g, o->draw_count);
    fold(g, o->param_count);
    fold(g, o->clear_depth);
    for (i = 0; i < 16; i++) {
        fold(g, bits(o->view[i]));
        fold(g, bits(o->proj[i]));
    }
    /* xb_draw has no padding the core leaves unset: 112 bytes, 28 words */
    for (i = 0; i < o->draw_count; i++)
        for (k = 0; k < sizeof(xb_draw) / 4; k++) {
            memcpy(&w, (const unsigned char *)&o->draws[i] + 4 * k, 4);
            fold(g, w);
        }
    /* NaN as one pattern: which NaN an operation gives differs between
     * the builds (the parts of quaternion 238, Chamber.md 6) */
    for (i = 0; i < o->param_count; i++)
        fold(g, o->params[i] != o->params[i] ? 0x7FC00000u : bits(o->params[i]));
}

static int run_digest(xb_state *s, const void *pack, size_t pack_size, uint64_t *out, int *frames,
                      uint32_t *max_draws, uint32_t *max_params)
{
    static const uint32_t steps[3] = { 17, 17, 16 };
    xb_config cfg = { 0, 0 };
    xb_frame_out o;
    digest g;
    uint32_t tick = 0, p;
    int n = 0, rc;

    g.h = 0xCBF29CE484222325ull;
    *max_draws = *max_params = 0;
    if (xb_init(s, pack, pack_size, &cfg) != XB_OK)
        return -1;
    for (p = 0; p < XB_SETUP_PASSES; p++) {
        buffers(&o);
        if (xb_frame_setup(s, p, &o) != XB_OK)
            return -1;
        fold_frame(&g, &o);
        if (o.draw_count > *max_draws)
            *max_draws = o.draw_count;
        if (o.param_count > *max_params)
            *max_params = o.param_count;
    }
    do {
        tick += steps[n % 3];
        rc = xb_advance(s, tick);
        if (rc < 0)
            return -1;
        buffers(&o);
        if (xb_frame(s, &o) != XB_OK)
            return -1;
        fold_frame(&g, &o);
        if (o.draw_count > *max_draws)
            *max_draws = o.draw_count;
        if (o.param_count > *max_params)
            *max_params = o.param_count;
        n++;
    } while (rc == XB_OK && n < 100000);
    *out = g.h;
    *frames = n;
    return 0;
}

/* ---- main ------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    void *pack_base = NULL, *state_base = NULL;
    size_t pack_size = 0;
    unsigned char *pack;
    xb_state *s;
    xb_config cfg = { 0, 0 };
    xb_frame_out o;
    json_doc d;
    const char *dump = NULL;
    char what[400];
    uint64_t dg[2];
    uint32_t tick = 0, p, md, mp, wrong_t = 0;
    int frames[2], list, ms, after, n, i, rec, eff, n_eff, n_rec, c, skipped = 0;
    tally ty;

    if (argc < 3) {
        fprintf(stderr, "usage: test_k6 <pack> <c8_gpu.json> [--dump <dir>] | <pack> --digest\n");
        return 2;
    }
    pack = read_file(argv[1], &pack_size, &pack_base);
    s = aligned_alloc16(xb_state_size(), &state_base);
    if (!pack || !s) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    if (!strcmp(argv[2], "--digest")) {
        if (run_digest(s, pack, pack_size, &dg[0], &frames[0], &md, &mp) != 0)
            return 1;
        printf("digest %016llX %d\n", (unsigned long long)dg[0], frames[0]);
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[3], "--dump"))
        dump = argv[4];
    if (json_load(&d, argv[2]) != 0)
        return 2;
    printf("state %u bytes, xb_draw %u bytes\n", (unsigned)xb_state_size(), (unsigned)sizeof(xb_draw));
    surf_init();
    check(xb_init(s, pack, pack_size, &cfg) == XB_OK, "xb_init");

    /* ---- set-up: frame 0 of the vector ---- */
    list = json_path(&d, 0, "draws/frames");
    rec = json_at(&d, list, 0);
    {
        FILE *f = dump ? dump_open(dump, 0) : NULL;
        uint32_t at = 0;
        int first = 1, rc = XB_OK;

        memset(&ty, 0, sizeof ty);
        for (p = 0; p < XB_SETUP_PASSES && rc == XB_OK; p++) {
            buffers(&o);
            rc = xb_frame_setup(s, p, &o);
            compare_draws(&d, rec, o.draws, o.draw_count, at, &ty);
            at += o.draw_count;
            if (f)
                dump_draws(f, &o, &first);
        }
        if (f) {
            fputs("\n]\n", f);
            fclose(f);
        }
        check(rc == XB_OK && (int)json_num(&d, json_get(&d, rec, "frame")) == 0,
              "xb_frame_setup: seven passes, frame 0 of the vector is the set-up");
        report("set-up (A1, A2)", d.nodes[json_get(&d, rec, "draws")].count, &ty);
    }

    /* ---- the run, frame by frame ---- */
    ms = json_path(&d, 0, "timeline/ms");
    after = json_path(&d, 0, "timeline/t_after");
    n = d.nodes[ms].count;
    check(n > 0 && n == d.nodes[after].count && (int)json_num(&d, json_path(&d, 0, "timeline/first_frame")) == 1 &&
          (int)json_num(&d, json_path(&d, 0, "timeline/not_replayable")) == 0,
          "c8_gpu.json: a timeline in whole milliseconds, every frame replayable");
    eff = json_path(&d, 0, "effects/frames");
    n_eff = d.nodes[eff].count;
    n_rec = d.nodes[list].count;
    {
        int im = json_at(&d, ms, 0), ia = json_at(&d, after, 0), ir = 1, ie = 0, seen = 0, seen_e = 0;

        int reps = json_path(&d, 0, "timeline/repeated_stops"), rp = 0;

        for (i = 1; i <= n; i++, im = d.nodes[im].next, ia = d.nodes[ia].next) {
            /* a stop of the probe without a call (vectors_c8.py, timeline) */
            if (rp < d.nodes[reps].count && (int)json_num(&d, json_at(&d, reps, rp)) == i) {
                wrong_t += (uint32_t)json_num(&d, im) != 0 || bits(xb_time(s)) != hex(&d, ia);
                rp++;
                skipped++;
                continue;
            }
            tick += (uint32_t)json_num(&d, im);
            if (xb_advance(s, tick) < 0)
                wrong_t++;
            wrong_t += bits(xb_time(s)) != hex(&d, ia);
            rec = ir < n_rec ? json_at(&d, list, ir) : -1;
            if (rec >= 0 && (int)json_num(&d, json_get(&d, rec, "frame")) == i) {
                buffers(&o);
                c = xb_frame(s, &o);
                memset(&ty, 0, sizeof ty);
                compare_draws(&d, rec, o.draws, o.draw_count, 0, &ty);
                snprintf(what, sizeof what, "frame %d t=%.3f", i, (double)xb_time(s));
                if (c != XB_OK)
                    ty.order++;
                report(what, d.nodes[json_get(&d, rec, "draws")].count, &ty);
                if (dump) {
                    FILE *f = dump_open(dump, i);
                    int first = 1;

                    if (f) {
                        dump_draws(f, &o, &first);
                        fputs("\n]\n", f);
                        fclose(f);
                    }
                }
                ir++;
                seen++;
            }
            rec = ie < n_eff ? json_at(&d, eff, ie) : -1;
            if (rec >= 0 && (int)json_num(&d, json_get(&d, rec, "frame")) == i) {
                check_effects(s, &d, rec, i);
                ie++;
                seen_e++;
            }
        }
        snprintf(what, sizeof what, "replay of %d stops, %d of them repeated and skipped: t after each step "
                 "equals the guest's (%u differ); %d of %d recorded frames and %d of %d effect frames met",
                 n, skipped, (unsigned)wrong_t, seen, n_rec - 1, seen_e, n_eff);
        check(!wrong_t && seen == n_rec - 1 && seen_e == n_eff, what);
    }

    /* ---- buffers too small ---- */
    {
        xb_frame_out small;
        uint32_t nd, np;

        buffers(&o);
        xb_frame(s, &o);
        nd = o.draw_count;
        np = o.param_count;
        memset(&small, 0, sizeof small);
        small.draws = draws;
        small.draw_cap = nd - 1;
        small.params = params;
        small.param_cap = np;
        c = xb_frame(s, &small) == XB_ERR_SPACE && small.draw_count == nd && small.param_count == np;
        small.draw_cap = nd;
        small.param_cap = np - 1;
        c = c && xb_frame(s, &small) == XB_ERR_SPACE && small.draw_count == nd && small.param_count == np;
        small.param_cap = np;
        c = c && xb_frame(s, &small) == XB_OK;
        check(c && xb_frame_setup(s, XB_SETUP_PASSES, &o) == XB_ERR_ARG,
              "xb_frame: XB_ERR_SPACE with the sizes needed when a buffer is one entry short; "
              "xb_frame_setup rejects pass 7");
    }

    /* ---- determinism and the largest frame ---- */
    c = run_digest(s, pack, pack_size, &dg[0], &frames[0], &md, &mp);
    c |= run_digest(s, pack, pack_size, &dg[1], &frames[1], &md, &mp);
    snprintf(what, sizeof what, "run over 8 s at 60 Hz, twice: %d frames, digest %016llX and %016llX; "
             "largest frame %u draws of %u, %u values of %u", frames[0], (unsigned long long)dg[0],
             (unsigned long long)dg[1], (unsigned)md, (unsigned)XB_FRAME_DRAWS_MAX, (unsigned)mp,
             (unsigned)XB_FRAME_PARAMS_MAX);
    check(c == 0 && dg[0] == dg[1] && frames[0] == frames[1] && md <= XB_FRAME_DRAWS_MAX &&
          mp <= XB_FRAME_PARAMS_MAX, what);
    printf("digest %016llX %d\n", (unsigned long long)dg[0], frames[0]);

    printf("%d checks, %d failed\n", checks, failed);
    json_free(&d);
    free(pack_base);
    free(state_base);
    return failed ? 1 : 0;
}
