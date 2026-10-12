/* One whole run of the core; see run.h. Only calls of xboot.h, no files. */
#include "run.h"

#include <string.h>

static void fold(uint64_t *h, uint32_t u)
{
    int i;

    for (i = 0; i < 4; i++) {
        *h ^= (u >> (8 * i)) & 0xFFu;
        *h *= 0x100000001B3ull;
    }
}

/* NaN as one pattern: which NaN an operation gives differs between builds
 * (the parts at quaternion 238, Chamber.md 6; docs/core.md 4). */
static void fold_real(uint64_t *h, xb_real f)
{
    uint32_t u;

    memcpy(&u, &f, 4);
    fold(h, f != f ? 0x7FC00000u : u);
}

static void fold_frame(uint64_t *h, const xb_frame_out *o)
{
    uint32_t i, k, w;

    fold(h, o->draw_count);
    fold(h, o->param_count);
    fold(h, o->clear_depth);
    for (i = 0; i < 16; i++) {
        fold_real(h, o->view[i]);
        fold_real(h, o->proj[i]);
    }
    /* xb_draw has no padding the core leaves unset: 112 bytes, 28 words */
    for (i = 0; i < o->draw_count; i++)
        for (k = 0; k < sizeof(xb_draw) / 4; k++) {
            memcpy(&w, (const unsigned char *)&o->draws[i] + 4 * k, 4);
            fold(h, w);
        }
    for (i = 0; i < o->param_count; i++)
        fold_real(h, o->params[i]);
}

uint32_t run_step_ms(uint32_t hz, uint32_t n)
{
    uint64_t a = ((uint64_t)n * 1000u + hz - 1) / hz;
    uint64_t b = ((uint64_t)(n + 1) * 1000u + hz - 1) / hz;

    return (uint32_t)(b - a);
}

static void buffers(const run_memory *m, xb_frame_out *o)
{
    memset(o, 0, sizeof *o);
    o->draws = m->draws;
    o->draw_cap = XB_FRAME_DRAWS_MAX;
    o->params = m->params;
    o->param_cap = XB_FRAME_PARAMS_MAX;
}

int run_whole(const run_memory *m, const void *pack, size_t pack_size, const run_config *cfg,
              const run_sink *sink, run_result *res)
{
    /* static: xb_blob has 2.5 KB, and a host's stack may be small */
    static xb_blob blob;
    static run_frame f;
    xb_config xc;
    xb_frame_out o;
    uint64_t h = 0xCBF29CE484222325ull;
    uint32_t tick, p, i, k, n = 0;
    int rc, err;

    if (!m || !m->state || !m->draws || !m->params || !m->deformed || !cfg || !res || cfg->hz < 1 ||
        cfg->hz > 1000 || (cfg->sound && (!m->sound || !m->samples)))
        return XB_ERR_ARG;
    memset(res, 0, sizeof *res);
    xc.flags = cfg->flags;
    xc.start_tick = cfg->start_tick;
    rc = xb_init(m->state, pack, pack_size, &xc);
    if (rc == XB_OK && cfg->sound)
        rc = xb_audio_init(m->state, m->sound, xb_audio_size());
    if (rc != XB_OK)
        return rc;

    for (p = 0; p < XB_SETUP_PASSES; p++) {
        buffers(m, &o);
        rc = xb_frame_setup(m->state, p, &o);
        if (rc != XB_OK)
            return rc;
        fold_frame(&h, &o);
        res->setup_draws += o.draw_count;
        res->setup_params += o.param_count;
        if (sink && sink->setup && (rc = sink->setup(sink->user, p, &o)) != 0)
            return rc;
    }

    tick = cfg->start_tick;
    do {
        memset(&f, 0, sizeof f);
        tick += run_step_ms(cfg->hz, n);
        rc = xb_advance(m->state, tick);
        if (rc < 0)
            return rc;
        f.index = n;
        f.tick = tick;
        f.rc = rc;
        buffers(m, &o);
        err = xb_frame(m->state, &o);
        if (err != XB_OK)
            return err;             /* XB_ERR_SPACE cannot happen with these buffers */
        if (xb_get_timing(m->state, &f.timing) != XB_OK ||
            xb_camera_at(m->state, f.timing.t, &f.camera) != XB_OK ||
            xb_get_track(m->state, &f.track) != XB_OK || xb_get_logo(m->state, &f.logo) != XB_OK ||
            xb_get_blob(m->state, &blob) != XB_OK)
            return XB_ERR_ARG;
        f.frame = &o;
        f.blob_bulges = blob.n_bulges;
        f.blob_drops = blob.n_drops;
        f.blob_draws = blob.draws;
        f.blob_flat = blob.flat;
        for (i = 0; i < o.draw_count && !f.blob_drawn; i++)
            f.blob_drawn = o.draws[i].mesh == XB_MESH_BLOB;
        if (f.blob_drawn) {
            if (xb_blob_deformed(m->state, m->deformed) != XB_OK)
                return XB_ERR_ARG;
            f.deformed = (const xb_real (*)[4])m->deformed;
            res->blob_frames++;
        }
        if (cfg->sound) {
            /* the sound runs on its own clock: 48 stereo frames per ms */
            f.stereo_frames = (tick - cfg->start_tick) * (XB_AUDIO_RATE / 1000u) - res->samples;
            if (f.stereo_frames > RUN_SAMPLES_MAX)
                return XB_ERR_SPACE;
            if (xb_audio(m->state, m->samples, f.stereo_frames) != XB_OK)
                return XB_ERR_ARG;
            f.samples = m->samples;
            res->samples += f.stereo_frames;
        }

        fold(&h, f.tick);
        fold(&h, (uint32_t)f.rc);
        fold_real(&h, f.timing.t);
        fold_real(&h, f.timing.x18);
        fold_real(&h, f.timing.x1c);
        fold_real(&h, f.timing.x20);
        fold_real(&h, f.timing.x24);
        fold_real(&h, f.track.dist);
        fold_real(&h, f.track.azimuth);
        fold_real(&h, f.track.elevation);
        fold_real(&h, f.track.blob_dist);
        fold(&h, f.track.fx_draws);
        fold(&h, f.logo.tm << 3 | f.logo.texture << 2 | f.logo.phase << 1 | f.logo.rtt);
        for (i = 0; i < 3; i++)
            fold_real(&h, f.logo.pos[i]);
        fold_real(&h, f.logo.alpha);
        fold(&h, f.blob_bulges << 8 | f.blob_drops);
        fold(&h, f.blob_draws);
        fold(&h, f.blob_flat);
        fold_frame(&h, &o);
        if (f.blob_drawn)
            for (i = 0; i < XB_BLOB_VERTICES; i++)
                for (k = 0; k < 4; k++)
                    fold_real(&h, m->deformed[i][k]);
        for (i = 0; i < 2 * f.stereo_frames; i++)
            fold(&h, (uint16_t)m->samples[i]);

        res->draws += o.draw_count;
        res->params += o.param_count;
        if (o.draw_count > res->max_draws) {
            res->max_draws = o.draw_count;
            res->max_frame = n;
        }
        if (o.param_count > res->max_params)
            res->max_params = o.param_count;
        res->t_end = f.timing.t;
        if (sink && sink->frame && (err = sink->frame(sink->user, &f)) != 0)
            return err;
        n++;
    } while (rc == XB_OK && n < 1000000u);
    res->frames = n;
    res->digest = h;
    return XB_OK;
}
