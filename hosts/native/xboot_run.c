/*
 * xboot_run - the native host: writes one whole run of the core to files.
 *
 *     xboot_run <pack> <dir> [--hz N] [--short] [--wide] [--start-tick N]
 *                            [--no-sound] [--no-assets]
 *     xboot_run <pack> --digest [--hz N] [--short] [--wide] [--no-sound]
 *
 * Into <dir> (docs/core.md 5 has the formats):
 *
 *   run.json       configuration, sizes, counts and the digest of the run
 *   states.csv     one line per frame: time values, camera, track values,
 *                  blob, logo, number of draws and values
 *   frames.bin     the draw lists: the seven passes of the set-up, then
 *                  every frame with its draws, values and, where the blob
 *                  is drawn, its deformed mesh
 *   sound.wav      48 kHz, 16 bit, stereo, as long as the run
 *   assets.json    list of the files in assets/
 *   assets/<x>.bin what a renderer loads once: the chamber's meshes in
 *                  both detail levels, the blob's two meshes, the logo's
 *                  seven, the two quads of the effects, the eleven textures
 *
 * Everything comes from calls of xboot.h; the host adds nothing of its own
 * and reads nothing but the pack. Files are little-endian, written from
 * memory as it is; the host must be little-endian. --digest writes no
 * files and prints "digest <16 hex digits> <frames> <samples>".
 */
#include "run.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "xboot_run writes its files from memory and needs a little-endian host"
#endif

#define FRAMES_MAGIC 0x4E524258u    /* "XBRN" */
#define FRAMES_VERSION 1u
#define REC_SETUP 0x50544553u       /* "SETP" */
#define REC_FRAME 0x4D415246u       /* "FRAM" */

static void *aligned_alloc16(size_t size)
{
    void *base = malloc(size + XB_ALIGN);

    if (!base)
        return NULL;
    return (void *)(((uintptr_t)base + XB_ALIGN - 1) & ~(uintptr_t)(XB_ALIGN - 1));
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
    buf = aligned_alloc16(*size);
    if (buf && fread(buf, 1, *size, f) != *size)
        buf = NULL;
    fclose(f);
    return buf;
}

static FILE *open_in(const char *dir, const char *name, const char *mode)
{
    char path[1024];

    if (snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path)
        return NULL;
    return fopen(path, mode);
}

static uint32_t bits(xb_real f)
{
    uint32_t u;

    memcpy(&u, &f, 4);
    return u;
}

/* ---- sinks ------------------------------------------------------------------ */

typedef struct files {
    FILE *states, *frames, *wav;
    uint32_t wav_frames;
} files;

static int put(FILE *f, const void *p, size_t n)
{
    return n == 0 || fwrite(p, n, 1, f) == 1 ? 0 : -100;
}

static int put_u32(FILE *f, uint32_t v)
{
    return put(f, &v, 4);
}

/* A record of frames.bin: tag, index, tick, bits of t, draw_count,
 * param_count, clear_depth, blob flag; view, proj; the draws; the values;
 * with the blob flag XB_BLOB_VERTICES rows of four float32. */
static int put_record(FILE *f, uint32_t tag, uint32_t index, uint32_t tick, uint32_t t,
                      const xb_frame_out *o, const xb_real (*deformed)[4])
{
    int rc = put_u32(f, tag) | put_u32(f, index) | put_u32(f, tick) | put_u32(f, t) |
             put_u32(f, o->draw_count) | put_u32(f, o->param_count) | put_u32(f, o->clear_depth) |
             put_u32(f, deformed != NULL);

    rc |= put(f, o->view, sizeof o->view) | put(f, o->proj, sizeof o->proj);
    rc |= put(f, o->draws, (size_t)o->draw_count * sizeof(xb_draw));
    rc |= put(f, o->params, (size_t)o->param_count * sizeof(xb_real));
    if (deformed)
        rc |= put(f, deformed, (size_t)XB_BLOB_VERTICES * 4 * sizeof(xb_real));
    return rc;
}

static int on_setup(void *user, uint32_t pass, const xb_frame_out *o)
{
    files *w = user;

    return put_record(w->frames, REC_SETUP, pass, 0, 0, o, NULL);
}

static int on_frame(void *user, const run_frame *f)
{
    files *w = user;
    const xb_camera *c = &f->camera;

    /* %.9g is enough to read a float32 back exactly; the bits of t are
     * there as well, for joining with a recording */
    fprintf(w->states,
            "%u,%u,%d,%08X,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,"
            "%.9g,%.9g,%.9g,%.9g,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.9g,%.9g,%.9g,%.9g,%u,%u,%u,%u\n",
            f->index, f->tick, f->rc, bits(f->timing.t), (double)f->timing.t, (double)f->timing.x18,
            (double)f->timing.x1c, (double)f->timing.x20, (double)f->timing.x24, (double)c->s, c->key,
            c->chamber_on, c->logo_on, (double)c->eye[0], (double)c->eye[1], (double)c->eye[2],
            (double)c->target[0], (double)c->target[1], (double)c->target[2], (double)f->track.dist,
            (double)f->track.azimuth, (double)f->track.elevation, (double)f->track.blob_dist,
            f->track.fx_draws, f->blob_bulges, f->blob_drops, f->blob_draws, f->blob_flat, f->logo.tm,
            f->logo.texture, f->logo.phase, f->logo.rtt, (double)f->logo.pos[0], (double)f->logo.pos[1],
            (double)f->logo.pos[2], (double)f->logo.alpha, f->frame->draw_count, f->frame->param_count,
            f->frame->clear_depth, f->stereo_frames);
    if (put_record(w->frames, REC_FRAME, f->index, f->tick, bits(f->timing.t), f->frame,
                   f->blob_drawn ? f->deformed : NULL))
        return -100;
    if (w->wav && put(w->wav, f->samples, (size_t)f->stereo_frames * 4))
        return -100;
    w->wav_frames += f->stereo_frames;
    return ferror(w->states) ? -100 : 0;
}

static void wav_head(FILE *f, uint32_t frames)
{
    uint32_t bytes = frames * 4, v;
    uint16_t h;

    fwrite("RIFF", 4, 1, f);
    v = 36 + bytes;
    fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 8, 1, f);
    v = 16;
    fwrite(&v, 4, 1, f);
    h = 1;                              /* PCM */
    fwrite(&h, 2, 1, f);
    h = 2;                              /* stereo */
    fwrite(&h, 2, 1, f);
    v = XB_AUDIO_RATE;
    fwrite(&v, 4, 1, f);
    v = XB_AUDIO_RATE * 4;
    fwrite(&v, 4, 1, f);
    h = 4;
    fwrite(&h, 2, 1, f);
    h = 16;
    fwrite(&h, 2, 1, f);
    fwrite("data", 4, 1, f);
    fwrite(&bytes, 4, 1, f);
}

/* ---- assets ----------------------------------------------------------------- */

typedef struct assets {
    const char *dir;
    FILE *list;
    int n;
    size_t bytes;
} assets;

/* Writes one file of assets/ and its line in assets.json. `what` is the
 * rest of the JSON object. */
static int asset(assets *a, const char *name, const void *p, size_t n, const char *what)
{
    char rel[80];
    FILE *f;
    int rc;

    snprintf(rel, sizeof rel, "assets/%s.bin", name);
    f = open_in(a->dir, rel, "wb");
    if (!f)
        return -100;
    rc = put(f, p, n);
    if (fclose(f) != 0)
        rc = -100;
    fprintf(a->list, "%s\n    {\"file\": \"%s\", \"bytes\": %u, %s}", a->n ? "," : "", rel, (unsigned)n, what);
    a->n++;
    a->bytes += n;
    return rc;
}

static const char *const tex_name[XB_TEXTURES] = {
    "cube128", "cube64", "bump", "falloff", "gradient_a", "radial", "gradient_c", "tm", "noise0", "noise1",
    "noise2"
};
static const char *const logo_name[XB_LOGO_MESHES] = { "a", "b", "c", "d", "q1", "q2", "e" };

static int write_assets(const xb_state *s, const char *dir, size_t *bytes, int *count)
{
    /* the largest mesh of the chamber has well under 4096 vertices and
     * 8192 indices (docs/core.md 6); xb_chamber_mesh_info says so first */
    enum { VMAX = 4096, IMAX = 8192, FMAX = 512, TEXMAX = 393216 };
    static xb_real pos[VMAX * 3], attr[VMAX * 11], unit[XB_BLOB_VERTICES * 3], veil[4][7], quad[4][3];
    static uint16_t idx[IMAX];
    static xb_form forms[FMAX];
    static unsigned char texels[TEXMAX];
    assets a = { 0 };
    char name[40], what[240];
    uint32_t g, d, m, t;
    int rc = 0;

    a.dir = dir;
    a.list = open_in(dir, "assets.json", "w");
    if (!a.list)
        return -100;
    fprintf(a.list, "{\n  \"note\": \"little-endian; float32 and uint16 as xboot.h describes each call\",\n"
                    "  \"files\": [");
    for (d = 0; d < 2; d++)
        for (g = 0; g < XB_CHAMBER_GROUPS; g++) {
            xb_mesh_info i;

            if (xb_chamber_mesh_info(s, g, d, &i) != XB_OK || i.vertices > VMAX || i.indices > IMAX ||
                i.forms > FMAX || xb_chamber_mesh(s, g, d, pos, attr, idx, forms) != XB_OK)
                return -101;
            snprintf(what, sizeof what,
                     "\"call\": \"xb_chamber_mesh\", \"group\": %u, \"detail\": %u, \"part\": \"pos\", "
                     "\"vertices\": %u, \"floats\": 3", g, d, i.vertices);
            snprintf(name, sizeof name, "chamber_%u_%u_pos", g, d);
            rc |= asset(&a, name, pos, (size_t)i.vertices * 12, what);
            snprintf(what, sizeof what,
                     "\"call\": \"xb_chamber_mesh\", \"group\": %u, \"detail\": %u, \"part\": \"attr\", "
                     "\"vertices\": %u, \"floats\": %u, \"format\": %u", g, d, i.vertices, i.attr_floats, i.format);
            snprintf(name, sizeof name, "chamber_%u_%u_attr", g, d);
            rc |= asset(&a, name, attr, (size_t)i.vertices * i.attr_floats * 4, what);
            snprintf(what, sizeof what,
                     "\"call\": \"xb_chamber_mesh\", \"group\": %u, \"detail\": %u, \"part\": \"idx\", "
                     "\"indices\": %u, \"prim\": %u", g, d, i.indices, i.prim);
            snprintf(name, sizeof name, "chamber_%u_%u_idx", g, d);
            rc |= asset(&a, name, idx, (size_t)i.indices * 2, what);
            snprintf(what, sizeof what,
                     "\"call\": \"xb_chamber_mesh\", \"group\": %u, \"detail\": %u, \"part\": \"forms\", "
                     "\"forms\": %u, \"words\": 6", g, d, i.forms);
            snprintf(name, sizeof name, "chamber_%u_%u_forms", g, d);
            rc |= asset(&a, name, forms, (size_t)i.forms * sizeof(xb_form), what);
        }
    for (m = 0; m < 2; m++) {
        uint32_t nv = m ? XB_BLOB_SMALL_VERTICES : XB_BLOB_VERTICES;
        uint32_t ni = m ? XB_BLOB_SMALL_INDICES : XB_BLOB_INDICES;

        if (xb_blob_mesh(s, m, unit, idx) != XB_OK)
            return -101;
        snprintf(what, sizeof what, "\"call\": \"xb_blob_mesh\", \"small\": %u, \"part\": \"unit\", "
                 "\"vertices\": %u, \"floats\": 3", m, nv);
        snprintf(name, sizeof name, "blob_%u_unit", m);
        rc |= asset(&a, name, unit, (size_t)nv * 12, what);
        snprintf(what, sizeof what, "\"call\": \"xb_blob_mesh\", \"small\": %u, \"part\": \"idx\", "
                 "\"indices\": %u, \"prim\": 6", m, ni);
        snprintf(name, sizeof name, "blob_%u_idx", m);
        rc |= asset(&a, name, idx, (size_t)ni * 2, what);
    }
    for (m = 0; m < XB_LOGO_MESHES; m++) {
        xb_logo_mesh_desc i;

        if (xb_logo_mesh_info(s, m, &i) != XB_OK || i.vertices * i.floats > VMAX * 3 || i.indices > IMAX ||
            xb_logo_mesh(s, m, pos, idx) != XB_OK)
            return -101;
        snprintf(what, sizeof what, "\"call\": \"xb_logo_mesh\", \"mesh\": %u, \"part\": \"vertices\", "
                 "\"vertices\": %u, \"floats\": %u", m, i.vertices, i.floats);
        snprintf(name, sizeof name, "logo_%s_vtx", logo_name[m]);
        rc |= asset(&a, name, pos, (size_t)i.vertices * i.floats * 4, what);
        snprintf(what, sizeof what, "\"call\": \"xb_logo_mesh\", \"mesh\": %u, \"part\": \"idx\", "
                 "\"indices\": %u, \"prim\": 5", m, i.indices);
        snprintf(name, sizeof name, "logo_%s_idx", logo_name[m]);
        rc |= asset(&a, name, idx, (size_t)i.indices * 2, what);
    }
    if (xb_fx_quads(veil, quad) != XB_OK)
        return -101;
    rc |= asset(&a, "fx_veil", veil, sizeof veil,
                "\"call\": \"xb_fx_quads\", \"part\": \"veil\", \"vertices\": 4, \"floats\": 7");
    rc |= asset(&a, "fx_quad", quad, sizeof quad,
                "\"call\": \"xb_fx_quads\", \"part\": \"quad\", \"vertices\": 4, \"floats\": 3");
    for (t = 0; t < XB_TEXTURES; t++) {
        xb_texture_desc i;

        if (xb_texture_info(s, t, &i) != XB_OK || i.bytes > TEXMAX || xb_texture(s, t, texels, i.bytes) != XB_OK)
            return -101;
        snprintf(what, sizeof what,
                 "\"call\": \"xb_texture\", \"tex\": %u, \"width\": %u, \"height\": %u, \"levels\": %u, "
                 "\"faces\": %u, \"format\": %u, \"texel_bytes\": %u, \"format_word\": \"0x%08X\"",
                 t, i.width, i.height, i.levels, i.faces, i.format, i.texel_bytes, i.format_word);
        snprintf(name, sizeof name, "tex_%s", tex_name[t]);
        rc |= asset(&a, name, texels, i.bytes, what);
    }
    fprintf(a.list, "\n  ]\n}\n");
    if (fclose(a.list) != 0)
        rc = -100;
    *bytes = a.bytes;
    *count = a.n;
    return rc;
}

/* ---- main ------------------------------------------------------------------- */

static int usage(void)
{
    fprintf(stderr, "usage: xboot_run <pack> <dir> [--hz N] [--short] [--wide] [--start-tick N] "
                    "[--no-sound] [--no-assets]\n"
                    "       xboot_run <pack> --digest [--hz N] [--short] [--wide] [--no-sound]\n");
    return 2;
}

int main(int argc, char **argv)
{
    run_config cfg = { 0, 0, RUN_HZ_DEFAULT, 1 };
    run_memory m;
    run_result r;
    run_sink sink = { 0 };
    files w = { 0 };
    size_t pack_size = 0, asset_bytes = 0;
    unsigned char *pack;
    const char *dir;
    FILE *f;
    int i, rc, digest_only, with_assets = 1, n_assets = 0;

    if (argc < 3)
        return usage();
    dir = argv[2];
    digest_only = strcmp(dir, "--digest") == 0;
    for (i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--hz") && i + 1 < argc)
            cfg.hz = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--start-tick") && i + 1 < argc)
            cfg.start_tick = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--short"))
            cfg.flags |= XB_START_SHORT;
        else if (!strcmp(argv[i], "--wide"))
            cfg.flags |= XB_START_WIDE;
        else if (!strcmp(argv[i], "--no-sound"))
            cfg.sound = 0;
        else if (!strcmp(argv[i], "--no-assets"))
            with_assets = 0;
        else
            return usage();
    }
    if (cfg.hz < 1 || cfg.hz > 1000)
        return usage();

    pack = read_file(argv[1], &pack_size);
    m.state = aligned_alloc16(xb_state_size());
    m.sound = cfg.sound ? aligned_alloc16(xb_audio_size()) : NULL;
    m.draws = malloc(sizeof(xb_draw) * XB_FRAME_DRAWS_MAX);
    m.params = malloc(sizeof(xb_real) * XB_FRAME_PARAMS_MAX);
    m.deformed = malloc(sizeof(xb_real) * 4 * XB_BLOB_VERTICES);
    m.samples = malloc(sizeof(int16_t) * 2 * RUN_SAMPLES_MAX);
    if (!pack || !m.state || (cfg.sound && !m.sound) || !m.draws || !m.params || !m.deformed || !m.samples) {
        fprintf(stderr, "cannot read %s or out of memory\n", argv[1]);
        return 2;
    }

    if (digest_only) {
        rc = run_whole(&m, pack, pack_size, &cfg, NULL, &r);
        if (rc != XB_OK) {
            fprintf(stderr, "the run failed: %d\n", rc);
            return 1;
        }
        printf("digest %016llX %u %u\n", (unsigned long long)r.digest, r.frames, r.samples);
        return 0;
    }

    w.states = open_in(dir, "states.csv", "w");
    w.frames = open_in(dir, "frames.bin", "wb");
    w.wav = cfg.sound ? open_in(dir, "sound.wav", "wb") : NULL;
    if (!w.states || !w.frames || (cfg.sound && !w.wav)) {
        fprintf(stderr, "cannot write into %s (the directory and its assets/ must exist)\n", dir);
        return 2;
    }
    fprintf(w.states, "frame,tick,rc,t_bits,t,x18,x1c,x20,x24,path_s,path_key,chamber_on,logo_on,"
                      "eye_x,eye_y,eye_z,target_x,target_y,target_z,track_dist,track_azimuth,"
                      "track_elevation,track_blob_dist,fx_draws,blob_bulges,blob_drops,blob_rng_draws,"
                      "blob_flat,logo_tm,logo_texture,logo_phase,logo_rtt,logo_x,logo_y,logo_z,logo_alpha,"
                      "draws,values,clear_depth,samples\n");
    put_u32(w.frames, FRAMES_MAGIC);
    put_u32(w.frames, FRAMES_VERSION);
    put_u32(w.frames, (uint32_t)sizeof(xb_draw));
    put_u32(w.frames, XB_API_VERSION);
    if (w.wav)
        wav_head(w.wav, 0);
    sink.user = &w;
    sink.setup = on_setup;
    sink.frame = on_frame;
    rc = run_whole(&m, pack, pack_size, &cfg, &sink, &r);
    if (rc != XB_OK) {
        fprintf(stderr, "the run failed: %d\n", rc);
        return 1;
    }
    if (w.wav) {
        /* the length is known only now */
        fseek(w.wav, 0, SEEK_SET);
        wav_head(w.wav, w.wav_frames);
    }
    if (fclose(w.states) != 0 || fclose(w.frames) != 0 || (w.wav && fclose(w.wav) != 0)) {
        fprintf(stderr, "writing into %s failed\n", dir);
        return 1;
    }
    /* the state after the run is as good as any for what does not depend
     * on the run: meshes and textures hang on the pack and the set-up */
    if (with_assets && (rc = write_assets(m.state, dir, &asset_bytes, &n_assets)) != 0) {
        fprintf(stderr, "writing the assets failed: %d\n", rc);
        return 1;
    }

    f = open_in(dir, "run.json", "w");
    if (!f)
        return 1;
    fprintf(f, "{\n  \"api_version\": %u,\n  \"pack_bytes\": %u,\n  \"hz\": %u,\n  \"start_tick\": %u,\n"
               "  \"short\": %s,\n  \"wide\": %s,\n  \"sound\": %s,\n",
            XB_API_VERSION, (unsigned)pack_size, cfg.hz, cfg.start_tick,
            cfg.flags & XB_START_SHORT ? "true" : "false", cfg.flags & XB_START_WIDE ? "true" : "false",
            cfg.sound ? "true" : "false");
    fprintf(f, "  \"state_bytes\": %u,\n  \"sound_bytes\": %u,\n  \"draw_bytes\": %u,\n",
            (unsigned)xb_state_size(), (unsigned)xb_audio_size(), (unsigned)sizeof(xb_draw));
    fprintf(f, "  \"frames\": %u,\n  \"t_end\": %.9g,\n  \"samples\": %u,\n  \"setup_draws\": %u,\n"
               "  \"setup_values\": %u,\n  \"draws\": %u,\n  \"values\": %u,\n  \"max_draws\": %u,\n"
               "  \"max_values\": %u,\n  \"max_frame\": %u,\n  \"blob_frames\": %u,\n",
            r.frames, (double)r.t_end, r.samples, r.setup_draws, r.setup_params, r.draws, r.params,
            r.max_draws, r.max_params, r.max_frame, r.blob_frames);
    fprintf(f, "  \"assets\": %d,\n  \"asset_bytes\": %u,\n  \"digest\": \"%016llX\"\n}\n", n_assets,
            (unsigned)asset_bytes, (unsigned long long)r.digest);
    if (fclose(f) != 0)
        return 1;
    printf("%u frames at %u Hz to t = %.6f, %u draws and %u values (largest frame %u: %u draws), set-up %u "
           "draws; %u samples; %d assets, %u bytes\n",
           r.frames, cfg.hz, (double)r.t_end, r.draws, r.params, r.max_frame, r.max_draws, r.setup_draws,
           r.samples, n_assets, (unsigned)asset_bytes);
    printf("digest %016llX %u %u\n", (unsigned long long)r.digest, r.frames, r.samples);
    return 0;
}
