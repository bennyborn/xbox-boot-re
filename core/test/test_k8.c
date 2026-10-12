/*
 * Host test for milestone K8: the core as a whole.
 *
 *     test_k8 <pack>
 *     test_k8 <pack> --digest
 *
 * K0 to K7 test each subsystem against its vector. This test runs
 * everything together, the way a host does (hosts/native/run.c): set-up,
 * every frame of a run at 60 Hz with its draw list, the deformed mesh and
 * the samples that fall into the frame. It checks what only shows in a
 * whole run:
 *
 *   - the run ends where Timing.md 3 says, with as many frames and samples
 *     as the steps give, and twice from the start it is the same run
 *   - picture and sound do not touch each other: the draw lists are the
 *     same with and without sound, and the samples are those of xb_audio
 *     alone
 *   - the draw lists are those test_k6 folds (its digest, printed as
 *     "frames <hex>" for core/build.py to compare)
 *   - the core stays inside the memory it asked for: guard bytes behind
 *     state, sound memory and every buffer are untouched, and the pack is
 *     unchanged
 *   - xb_frame changes nothing: called again after the run it gives the
 *     same list
 *   - other hosts: 30, 50 and 1000 Hz, the short version, the wide
 *     picture; each ends within one step after t = 8
 *
 * Prints "digest <16 hex digits> <frames> <samples>" of the run with
 * sound at 60 Hz; wasm_k8.mjs and hosts/native/xboot_run print the same.
 */
#include "run.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GUARD 64u
#define GUARD_BYTE 0xA5u

static int checks, failed;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok)
        failed++;
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
}

/* A block of `size` bytes aligned to XB_ALIGN with GUARD bytes behind it. */
static void *guarded(size_t size)
{
    unsigned char *base = malloc(size + XB_ALIGN + GUARD), *p;

    if (!base)
        return NULL;
    p = (unsigned char *)(((uintptr_t)base + XB_ALIGN - 1) & ~(uintptr_t)(XB_ALIGN - 1));
    memset(p + size, GUARD_BYTE, GUARD);
    return p;
}

static int guard_ok(const void *p, size_t size)
{
    const unsigned char *g = (const unsigned char *)p + size;
    unsigned i;

    for (i = 0; i < GUARD; i++)
        if (g[i] != GUARD_BYTE)
            return 0;
    return 1;
}

static unsigned char *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = guarded(*size);
    if (buf && fread(buf, 1, *size, f) != *size)
        buf = NULL;
    fclose(f);
    return buf;
}

/* ---- sinks ------------------------------------------------------------------ */

static void fold(uint64_t *h, uint32_t u)
{
    int i;

    for (i = 0; i < 4; i++) {
        *h ^= (u >> (8 * i)) & 0xFFu;
        *h *= 0x100000001B3ull;
    }
}

/* The digest of test_k6.c over the draw lists, and all samples of the run. */
typedef struct seen {
    uint64_t frames;
    int16_t *samples;        /* room for `cap` stereo frames, or NULL */
    uint32_t cap, n;
    uint32_t done_at;        /* index of the frame that returned XB_DONE */
    uint32_t n_done;
    xb_real  t_before_done;  /* t of the frame before it */
    uint32_t last_draws, last_params;
    uint64_t last;           /* digest of the last frame's list alone */
} seen;

static void fold_list(uint64_t *h, const xb_frame_out *o)
{
    uint32_t i, k, w;

    fold(h, o->draw_count);
    fold(h, o->param_count);
    fold(h, o->clear_depth);
    for (i = 0; i < 16; i++) {
        memcpy(&w, &o->view[i], 4);
        fold(h, w);
        memcpy(&w, &o->proj[i], 4);
        fold(h, w);
    }
    for (i = 0; i < o->draw_count; i++)
        for (k = 0; k < sizeof(xb_draw) / 4; k++) {
            memcpy(&w, (const unsigned char *)&o->draws[i] + 4 * k, 4);
            fold(h, w);
        }
    for (i = 0; i < o->param_count; i++) {
        memcpy(&w, &o->params[i], 4);
        fold(h, o->params[i] != o->params[i] ? 0x7FC00000u : w);
    }
}

static int seen_setup(void *user, uint32_t pass, const xb_frame_out *o)
{
    seen *s = user;

    (void)pass;
    fold_list(&s->frames, o);
    return 0;
}

static int seen_frame(void *user, const run_frame *f)
{
    seen *s = user;

    fold_list(&s->frames, f->frame);
    s->last = 0xCBF29CE484222325ull;
    fold_list(&s->last, f->frame);
    s->last_draws = f->frame->draw_count;
    s->last_params = f->frame->param_count;
    if (f->rc == XB_DONE) {
        if (!s->n_done)
            s->done_at = f->index;
        s->n_done++;
    } else {
        s->t_before_done = f->timing.t;
    }
    if (s->samples && f->stereo_frames) {
        if (s->n + f->stereo_frames > s->cap)
            return -200;
        memcpy(s->samples + 2 * s->n, f->samples, (size_t)f->stereo_frames * 4);
    }
    s->n += f->stereo_frames;
    return 0;
}

static void seen_reset(seen *s)
{
    int16_t *keep = s->samples;
    uint32_t cap = s->cap;

    memset(s, 0, sizeof *s);
    s->frames = 0xCBF29CE484222325ull;
    s->samples = keep;
    s->cap = cap;
}

/* ---- main ------------------------------------------------------------------- */

#define RUN_SAMPLES 384000u          /* 8 s at 48 kHz */

int main(int argc, char **argv)
{
    static const uint32_t rates[] = { 30, 50, 1000 };
    size_t pack_size = 0, state_size = xb_state_size(), sound_size = xb_audio_size();
    size_t draws_size = sizeof(xb_draw) * XB_FRAME_DRAWS_MAX, params_size = sizeof(xb_real) * XB_FRAME_PARAMS_MAX;
    size_t mesh_size = sizeof(xb_real) * 4 * XB_BLOB_VERTICES, samples_size = sizeof(int16_t) * 2 * RUN_SAMPLES_MAX;
    unsigned char *pack;
    char pack_sha[2][65];
    run_memory m;
    run_config cfg = { 0, 0, RUN_HZ_DEFAULT, 1 };
    run_result r[2], q;
    run_sink sink;
    seen sn;
    int16_t *all, *alone;
    xb_frame_out o;
    xb_config xc = { 0, 0 };
    uint64_t with_sound, again;
    char what[400];
    uint32_t i, k, differ;
    int rc[2];

    if (argc < 2) {
        fprintf(stderr, "usage: test_k8 <pack> [--digest]\n");
        return 2;
    }
    pack = read_file(argv[1], &pack_size);
    m.state = guarded(state_size);
    m.sound = guarded(sound_size);
    m.draws = guarded(draws_size);
    m.params = guarded(params_size);
    m.deformed = guarded(mesh_size);
    m.samples = guarded(samples_size);
    all = malloc(sizeof(int16_t) * 2 * RUN_SAMPLES);
    alone = malloc(sizeof(int16_t) * 2 * RUN_SAMPLES);
    if (!pack || !m.state || !m.sound || !m.draws || !m.params || !m.deformed || !m.samples || !all || !alone) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    if (argc == 3 && !strcmp(argv[2], "--digest")) {
        if (run_whole(&m, pack, pack_size, &cfg, NULL, &q) != XB_OK)
            return 1;
        printf("digest %016llX %u %u\n", (unsigned long long)q.digest, q.frames, q.samples);
        return 0;
    }
    printf("state %u bytes, sound %u bytes, pack %u bytes; buffers of a frame %u bytes, deformed mesh %u bytes\n",
           (unsigned)state_size, (unsigned)sound_size, (unsigned)pack_size, (unsigned)(draws_size + params_size),
           (unsigned)mesh_size);
    sha256_hex(pack, pack_size, pack_sha[0]);
    sink.user = &sn;
    sink.setup = seen_setup;
    sink.frame = seen_frame;

    /* the run with sound, twice */
    sn.samples = all;
    sn.cap = RUN_SAMPLES;
    seen_reset(&sn);
    rc[0] = run_whole(&m, pack, pack_size, &cfg, &sink, &r[0]);
    with_sound = sn.frames;
    snprintf(what, sizeof what,
             "run at 60 Hz with sound: %u frames, XB_DONE at frame %u and only there (t = %.6f before, %.6f at it), "
             "%u samples", r[0].frames, sn.done_at, (double)sn.t_before_done, (double)r[0].t_end, r[0].samples);
    check(rc[0] == XB_OK && r[0].frames == 480 && sn.done_at == 479 && sn.n_done == 1 &&
          sn.t_before_done < 8.0f && r[0].t_end >= 8.0f && r[0].samples == RUN_SAMPLES && sn.n == RUN_SAMPLES, what);
    snprintf(what, sizeof what,
             "set-up %u draws and %u values, frames %u draws and %u values; largest frame %u with %u draws of %u, "
             "%u values of %u; the blob's mesh in %u frames",
             r[0].setup_draws, r[0].setup_params, r[0].draws, r[0].params, r[0].max_frame, r[0].max_draws,
             XB_FRAME_DRAWS_MAX, r[0].max_params, XB_FRAME_PARAMS_MAX, r[0].blob_frames);
    check(r[0].setup_draws == 2599 && r[0].max_draws <= XB_FRAME_DRAWS_MAX &&
          r[0].max_params <= XB_FRAME_PARAMS_MAX && r[0].blob_frames > 0, what);

    /* xb_frame after the run: the same list again, nothing changed */
    memset(&o, 0, sizeof o);
    o.draws = m.draws;
    o.draw_cap = XB_FRAME_DRAWS_MAX;
    o.params = m.params;
    o.param_cap = XB_FRAME_PARAMS_MAX;
    again = 0xCBF29CE484222325ull;
    rc[1] = xb_frame(m.state, &o);
    fold_list(&again, &o);
    k = (uint32_t)xb_advance(m.state, 100000u);
    snprintf(what, sizeof what, "after the run: xb_frame gives the last list again (%u draws, %u values), "
             "xb_advance XB_DONE with t unchanged (%.6f)", o.draw_count, o.param_count, (double)xb_time(m.state));
    check(rc[1] == XB_OK && again == sn.last && o.draw_count == sn.last_draws && o.param_count == sn.last_params &&
          k == XB_DONE && xb_time(m.state) == r[0].t_end, what);

    seen_reset(&sn);
    sn.samples = NULL;
    rc[1] = run_whole(&m, pack, pack_size, &cfg, &sink, &r[1]);
    snprintf(what, sizeof what, "the same run again: digest %016llX and %016llX",
             (unsigned long long)r[0].digest, (unsigned long long)r[1].digest);
    check(rc[1] == XB_OK && r[0].digest == r[1].digest && r[1].frames == 480, what);

    /* picture without sound */
    cfg.sound = 0;
    seen_reset(&sn);
    rc[1] = run_whole(&m, pack, pack_size, &cfg, &sink, &q);
    snprintf(what, sizeof what, "draw lists with sound %016llX, without %016llX (the digest of test_k6)",
             (unsigned long long)with_sound, (unsigned long long)sn.frames);
    check(rc[1] == XB_OK && with_sound == sn.frames && q.samples == 0, what);
    printf("frames %016llX %u\n", (unsigned long long)sn.frames, q.frames);

    /* sound without picture */
    differ = 0;
    if (xb_init(m.state, pack, pack_size, &xc) != XB_OK ||
        xb_audio_init(m.state, m.sound, sound_size) != XB_OK || xb_audio(m.state, alone, RUN_SAMPLES) != XB_OK)
        differ = RUN_SAMPLES;
    for (i = 0; i < 2 * RUN_SAMPLES && differ < RUN_SAMPLES; i++)
        differ += all[i] != alone[i];
    k = 0;
    for (i = 0; i < 2 * RUN_SAMPLES; i++)
        k += all[i] != 0;
    snprintf(what, sizeof what, "samples of the run against xb_audio alone in one call: %u of %u values differ; "
             "%u are not 0", differ, 2 * RUN_SAMPLES, k);
    check(differ == 0 && k > RUN_SAMPLES / 2, what);

    /* the queries change no byte of the state: at frames with the chamber,
     * with the blob deformed, with the logo's texture drawn and at the end */
    {
        static const uint32_t at[] = { 20, 200, 320, 361, 376, 479 };
        static xb_blob blob;
        static xb_blob_params bp;
        static xb_chamber_pose pose;
        static xb_fx_params fx;
        static xb_logo_params lp;
        static xb_audio_state au;
        unsigned char *copy = malloc(state_size + sound_size);
        xb_timing tm;
        xb_camera cam;
        xb_track tr;
        xb_logo lg;
        xb_real light[16];
        uint32_t tick = 0, n = 0, changed = 0, calls = 0;

        differ = copy == NULL || xb_init(m.state, pack, pack_size, &xc) != XB_OK ||
                 xb_audio_init(m.state, m.sound, sound_size) != XB_OK;
        for (k = 0; k < sizeof at / sizeof at[0] && !differ; k++) {
            for (; n <= at[k]; n++) {
                tick += run_step_ms(RUN_HZ_DEFAULT, n);
                xb_advance(m.state, tick);
            }
            xb_audio(m.state, m.samples, 800);
            memcpy(copy, m.state, state_size);
            memcpy(copy + state_size, m.sound, sound_size);
            o.draw_count = o.param_count = 0;
            differ |= xb_frame(m.state, &o) != XB_OK || xb_get_timing(m.state, &tm) != XB_OK ||
                      xb_camera_at(m.state, tm.t, &cam) != XB_OK || xb_get_track(m.state, &tr) != XB_OK ||
                      xb_get_logo(m.state, &lg) != XB_OK || xb_get_blob(m.state, &blob) != XB_OK ||
                      xb_blob_deformed(m.state, m.deformed) != XB_OK ||
                      xb_blob_draw(m.state, &cam, tm.t, tm.x1c, tm.x20, &bp) != XB_OK ||
                      xb_blob_light_view(m.state, 0, light) != XB_OK ||
                      xb_chamber_at(m.state, tm.t, &pose) != XB_OK ||
                      xb_fx_draw(m.state, &cam, tm.t, tm.x20, &tr, &fx) != XB_OK ||
                      xb_logo_draw(m.state, &cam, tm.t, 0, &lp) != XB_OK || xb_get_audio(m.state, &au) != XB_OK ||
                      xb_texture(m.state, XB_TEX_TM, m.params, 1024) != XB_OK;
            calls += 14;
            for (i = 0; i < XB_SETUP_PASSES; i++)
                differ |= xb_frame_setup(m.state, i, &o) != XB_OK;
            changed += memcmp(copy, m.state, state_size) != 0;
            changed += memcmp(copy + state_size, m.sound, sound_size) != 0;
        }
        snprintf(what, sizeof what, "at %u frames %u queries and the set-up's seven passes: state or sound memory "
                 "changed %u times", (unsigned)(sizeof at / sizeof at[0]), calls, changed);
        check(!differ && changed == 0, what);
        free(copy);
    }

    /* other hosts */
    for (i = 0; i < sizeof rates / sizeof rates[0]; i++) {
        xb_real step = (xb_real)(run_step_ms(rates[i], 0) * 0.001);

        cfg.flags = 0;
        cfg.hz = rates[i];
        cfg.sound = 1;
        rc[1] = run_whole(&m, pack, pack_size, &cfg, NULL, &q);
        snprintf(what, sizeof what, "%u Hz: %u frames to t = %.6f, %u samples, largest frame %u draws, digest %016llX",
                 rates[i], q.frames, (double)q.t_end, q.samples, q.max_draws, (unsigned long long)q.digest);
        check(rc[1] == XB_OK && q.t_end >= 8.0f && q.t_end < 8.0f + step && q.max_draws <= XB_FRAME_DRAWS_MAX &&
              q.samples >= RUN_SAMPLES && q.samples <= RUN_SAMPLES + 48u * run_step_ms(rates[i], 0), what);
    }
    cfg.hz = RUN_HZ_DEFAULT;
    cfg.flags = XB_START_SHORT;
    seen_reset(&sn);
    sn.samples = alone;
    rc[1] = run_whole(&m, pack, pack_size, &cfg, &sink, &q);
    k = 0;
    for (i = 0; i < 2 * sn.n; i++)
        k += alone[i] != 0;
    snprintf(what, sizeof what, "short version: %u frames to t = %.6f, largest frame %u draws, %u samples, %u not 0",
             q.frames, (double)q.t_end, q.max_draws, q.samples, k);
    check(rc[1] == XB_OK && q.frames == 30 && q.t_end >= 8.0f && k == 0 && q.max_draws < 16, what);
    cfg.flags = XB_START_WIDE;
    rc[1] = run_whole(&m, pack, pack_size, &cfg, NULL, &q);
    snprintf(what, sizeof what, "wide picture: %u frames, %u draws, digest %016llX (other than %016llX)", q.frames,
             q.draws, (unsigned long long)q.digest, (unsigned long long)r[0].digest);
    check(rc[1] == XB_OK && q.frames == 480 && q.draws == r[0].draws && q.digest != r[0].digest, what);

    /* memory */
    sha256_hex(pack, pack_size, pack_sha[1]);
    check(guard_ok(m.state, state_size) && guard_ok(m.sound, sound_size) && guard_ok(m.draws, draws_size) &&
          guard_ok(m.params, params_size) && guard_ok(m.deformed, mesh_size) && guard_ok(m.samples, samples_size) &&
          guard_ok(pack, pack_size),
          "guard bytes behind state, sound memory, draws, values, mesh, samples and pack untouched after all runs");
    check(strcmp(pack_sha[0], pack_sha[1]) == 0, "the pack is unchanged after all runs (SHA-256)");

    printf("digest %016llX %u %u\n", (unsigned long long)r[0].digest, r[0].frames, r[0].samples);
    printf("%d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
