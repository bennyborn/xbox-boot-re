/*
 * run.h - one whole run of the core, shared by the native host and the
 * host test of K8.
 *
 * A run is: xb_init, the seven passes of the set-up, then one frame per
 * step until the core says XB_DONE, and with sound the samples that fall
 * into each frame. Everything the core hands out on the way goes through
 * a sink; the digest folds the same values, so a host that writes a run
 * and a test that only counts can be held to one number. docs/core.md 5
 * describes the files the native host makes from it.
 */
#ifndef XBOOT_RUN_H
#define XBOOT_RUN_H

#include "xboot.h"

/* Steps of a 60 Hz host on a millisecond counter: 17, 17, 16 ms. */
#define RUN_HZ_DEFAULT 60u
/* The longest step is 1000 ms at 1 Hz: 48 stereo frames per ms. */
#define RUN_SAMPLES_MAX (XB_AUDIO_RATE)

typedef struct run_config {
    uint32_t flags;          /* XB_START_* */
    uint32_t start_tick;
    uint32_t hz;             /* frames per second, 1 to 1000 */
    int      sound;          /* 1: xb_audio_init and the samples of every frame */
} run_config;

/* What a frame leaves behind, apart from the draws. */
typedef struct run_frame {
    uint32_t    index;       /* 0 for the first frame after the set-up */
    uint32_t    tick;        /* the tick given to xb_advance */
    int         rc;          /* XB_OK or XB_DONE */
    xb_timing   timing;
    xb_camera   camera;      /* at the new t */
    xb_track    track;
    xb_logo     logo;
    uint32_t    blob_bulges, blob_drops, blob_draws, blob_flat;
    int         blob_drawn;  /* the frame draws the blob's mesh; `deformed` is valid */
    const xb_real (*deformed)[4];    /* XB_BLOB_VERTICES rows */
    const xb_frame_out *frame;
    const int16_t *samples;  /* interleaved left, right; NULL without sound */
    uint32_t    stereo_frames;   /* frames of sound that fall into this picture */
} run_frame;

typedef struct run_sink {
    void *user;
    /* pass 0 to XB_SETUP_PASSES - 1; may be NULL */
    int (*setup)(void *user, uint32_t pass, const xb_frame_out *frame);
    int (*frame)(void *user, const run_frame *f);
} run_sink;

typedef struct run_result {
    uint64_t digest;         /* FNV-1a over set-up, frames, states, meshes, samples */
    uint32_t frames;
    uint32_t samples;        /* stereo frames of sound */
    uint32_t draws, params;  /* summed over the frames, without the set-up */
    uint32_t max_draws, max_params;
    uint32_t max_frame;      /* index of the frame with the most draws */
    uint32_t setup_draws, setup_params;
    uint32_t blob_frames;    /* frames that draw the deformed mesh */
    xb_real  t_end;
} run_result;

/* Memory of a run, all from the host: state and sound aligned to XB_ALIGN. */
typedef struct run_memory {
    xb_state *state;
    void     *sound;         /* xb_audio_size() bytes, or NULL without sound */
    xb_draw  *draws;         /* XB_FRAME_DRAWS_MAX */
    xb_real  *params;        /* XB_FRAME_PARAMS_MAX */
    xb_real (*deformed)[4];  /* XB_BLOB_VERTICES */
    int16_t  *samples;       /* RUN_SAMPLES_MAX stereo frames */
} run_memory;

/* Runs from xb_init to XB_DONE. Returns XB_OK, the core's error, or what a
 * sink returned when it was not 0. */
int run_whole(const run_memory *m, const void *pack, size_t pack_size, const run_config *cfg,
              const run_sink *sink, run_result *res);

/* Tick of frame n + 1 minus tick of frame n for a host at `hz` on a
 * millisecond counter: ceil((n + 1) * 1000 / hz) - ceil(n * 1000 / hz);
 * 60 Hz gives 17, 17, 16, the steps of the runs in core/test/. */
uint32_t run_step_ms(uint32_t hz, uint32_t n);

#endif
