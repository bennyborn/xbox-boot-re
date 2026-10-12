/* Internal state and helpers of the core. Not part of the interface. */
#ifndef CORE_INTERNAL_H
#define CORE_INTERNAL_H

#include "xboot.h"

/* Little-endian reads, byte by byte, so that the core runs unchanged on
 * big-endian ports. The pack and the ROM ranges in it are little-endian. */
static inline uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---- pack.c ---- */

typedef struct pack_range {
    const uint8_t *bytes;
    uint32_t va;
    uint32_t size;
} pack_range;

/* Ranges of the unpacked kernel the core reads; index = PACK_* below. */
enum {
    PACK_SEQUENCE, PACK_ENVBLOCKS, PACK_SEQ_PTRS, PACK_GP_PROGRAM,
    PACK_PCM_SIGNED, PACK_PCM_A, PACK_PCM_B,
    PACK_CAMPATH, PACK_LOGO, PACK_SCENE, PACK_ASIN,
    PACK_COUNT
};

int pack_open(pack_range out[PACK_COUNT], const uint8_t *pack, size_t size);

/* Pointer to the byte at kernel address `addr` inside range `k`. The caller
 * stays inside the range; the ranges are checked once in pack_open. */
#define pack_at(s, k, addr) ((s)->pack[k].bytes + ((uint32_t)(addr) - (s)->pack[k].va))

#define ASIN_FACTORS 0x80066030u   /* 0x31 float32, Chamber.md 11 */

static inline int16_t rd_i16(const uint8_t *p)
{
    return (int16_t)(uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

/* ---- generator, Timing.md 4.1 ---- */

/* Step 0x8005A645: s' = ror(s, 13) - (s - 11), 32 bit. Shared by the main
 * object (this+0x68C) and the blob (0x800708BC), both seeded 0x76543210. */
static inline uint32_t rng_step(uint32_t s)
{
    return ((s >> 13) | (s << 19)) - (s - 11u);
}

#define RNG_SEED 0x76543210u

/* ---- x87.c ---- */

/* fsincos for |x| < 1.6e6: sin and cos to within about an ulp of double.
 * Returns -1 and NaN outside that range. */
int x87_fsincos(double x, double *s, double *c);

/* A store of an x87 register to a float32 in memory (Timing.md 4.2). */
static inline double r32(double x)
{
    return (double)(float)x;
}

static inline float r32f(double x)
{
    return (float)x;
}

static inline uint32_t fbits(float f)
{
    union { float f; uint32_t u; } v;
    v.f = f;
    return v.u;
}

static inline float from_bits(uint32_t u)
{
    union { float f; uint32_t u; } v;
    v.u = u;
    return v.f;
}

static inline float rd_f32(const uint8_t *p)
{
    return from_bits(rd_u32(p));
}

/* ---- Overview.c: small helpers, Overview.md and Chamber.md 11 ---- */

float  sse_sqrt(float x);                         /* 0x80057F42 */
float  sse_sin(float x);                          /* 0x80057F9D */
float  sse_atan(float x);                         /* 0x80057BA9 */
float  sse_asin(const xb_state *s, float x);      /* 0x80057E2D */
double sse_acos(const xb_state *s, float x);      /* 0x80058039, unrounded */
float  v3_len(const float v[3]);                  /* 0x8005690A */
void   v3_norm(float v[3]);                       /* 0x8005816D */
void   v3_cross(const float a[3], const float b[3], float out[3]);  /* 0x8005A662 */
void   v3_sub(const float a[3], const float b[3], float out[3]);    /* 0x800581B9 */
void   v3_add(const float a[3], const float b[3], float out[3]);    /* 0x800582C0 */
void   v3_scale(float a[3], double k);            /* 0x80058196 */
void   v3_madd(float a[3], const float b[3], double k);             /* 0x800581E0 */
void   v3_mul_m3(const float v[3], const float m[16], float out[3]); /* 0x8005667A */
void   m4_ident(float m[16]);                     /* 0x800580F8 */
void   m4_mul(const float a[16], const float b[16], float out[16]); /* 0x800566CF */
void   m4_inverse_rigid(const float m[16], float out[16]);          /* 0x8005B22E */
void   quat_to_m4(const float q[4], float m[16]);                   /* 0x8005C790 */
void   quat_slerp(const xb_state *s, const float a[4], const float b[4], float u,
                  float out[4]);                  /* 0x8005E0B1 */

/* ---- Timing.c ---- */

#define PULSE_COUNT XB_PULSE_COUNT

typedef struct timing_state {
    float    t;                      /* this+0x10 */
    float    x18, x1c, x20, x24;     /* this+0x18 to this+0x24, Timing.md 4 */
    uint32_t last_tick;              /* this+0x28 */
    uint32_t setup_tick;             /* this+0x2C */
    float    pulse[PULSE_COUNT][3];  /* this+0x30: t0, w, a per entry */
    float    arg;                    /* argument of the last update after the 1 s rule */
    uint32_t steps;                  /* updates since xb_init */
} timing_state;

/* Constructor and set-up: start values and pulse table. Draws 37 times
 * from the generator *rng, after the draw of the camera path (Timing.md
 * 4.1). */
void timing_start(timing_state *ts, uint32_t *rng, uint32_t flags, uint32_t tick);
/* One frame step 0x80057949 with the host's tick. XB_OK or XB_DONE. */
int timing_frame(timing_state *ts, uint32_t tick);
/* The update 0x800571AD with a step chosen by the host (free time). */
int timing_free(timing_state *ts, double dt);

/* ---- Camera.c ---- */

#define CAM_TABLE_KEYS 21          /* 0x15 keys at 0x80070DB8 */
#define CAM_OWN_KEYS   8

typedef struct campath_key {       /* 0x3C bytes at 0x800708C8, Camera.md 5.1 */
    float t, eye[3], eye_tan[3], target[3], target_tan[3], cut, split;
} campath_key;

typedef struct camera_state {
    float       proj[16];          /* cam+0x13C */
    float       aspect;            /* cam+0x4C */
    uint32_t    n_paths;           /* path+0x23C */
    uint32_t    path;              /* path+0x240 */
    uint32_t    first;             /* path+0x244 */
    uint32_t    n_table;           /* path+0x24C: keys of the path in the table */
    uint32_t    n_keys;            /* path+0x248 = n_table + 8 */
    campath_key keys[CAM_TABLE_KEYS + CAM_OWN_KEYS];
    float       frame[16];         /* path+0x1E0: rows x, m, o and d * rho, Camera.md 5.3 */
    float       origin[3];         /* path+0x220 */
    float       look[3];           /* path+0x22C */
    float       fade_t0, fade_k;   /* path+0x250, path+0x254 */
} camera_state;

/* What anim_update keeps of the path (Camera.md 6) and the count of
 * fx_draw calls, which decides the veil's target (Effects.md 4). */
typedef struct track_state {
    float    dist;                 /* anim+0x690 */
    float    azimuth;              /* anim+0x698 */
    float    elevation;            /* anim+0x6A0 */
    float    blob_dist;            /* anim+0x6A8 */
    uint32_t fx_draws;             /* calls of fx_draw */
    uint32_t fx_flip;              /* their parity before the current frame's call */
} track_state;

void camera_track(const xb_state *s, track_state *tr, const float eye[3], const float centre[3]);

int  camera_start(camera_state *cs, const xb_state *s, uint32_t *rng, uint32_t flags);
int  camera_select(camera_state *cs, const xb_state *s, uint32_t path);
void camera_at(const xb_state *s, float t, xb_camera *out);
void camera_perspective(float fov, float aspect, float near, float far, float m[16]); /* 0x8005B18C */
void camera_view_toward(const float eye[3], const float target[3], const float up[3],
                        float m[16]);             /* 0x8005B093 */

/* ---- Chamber.c ---- */

#define CH_QUATS      413
#define CH_POSITIONS  320
#define CH_ROT_TRACKS XB_ROT_TRACKS
#define CH_POS_TRACKS XB_POS_TRACKS
#define CH_TRACK_LEN  30
#define CH_PARTS      XB_CHAMBER_PARTS

typedef struct chamber_part {      /* the record of 0x11C bytes, Chamber.md 5 */
    uint8_t type, group, side, pad;
    int16_t form, pos_track, rot_track;
    uint16_t index;
    float   L[16];                 /* +0x08 */
    float   SL[16];                /* +0xD8 */
    float   scale[4];              /* +0xC8 */
} chamber_part;

typedef struct chamber_state {
    float        quats[CH_QUATS][4];            /* ch+0x60 */
    float        positions[CH_POSITIONS][3];    /* ch+0xD8 */
    int16_t      rot_tracks[CH_ROT_TRACKS][CH_TRACK_LEN];
    int16_t      pos_tracks[CH_POS_TRACKS][CH_TRACK_LEN];
    chamber_part parts[CH_PARTS];
} chamber_state;

int  chamber_start(chamber_state *ch, const xb_state *s);
void chamber_at(const xb_state *s, float t, xb_chamber_pose *out);
void chamber_world(const xb_state *s, const xb_chamber_pose *pose, uint32_t part, float w[16]);
void chamber_light_view(const float center[3], uint32_t side, float out[16]);
int  chamber_mesh(const xb_state *s, uint32_t group, uint32_t detail, xb_mesh_info *info,
                  float *pos, float *attr, uint16_t *idx, xb_form *forms);

void chamber_part_draw(const xb_state *s, const xb_camera *cam, const xb_chamber_pose *pose,
                       float x20, const float (*light)[16], int lit, uint32_t part,
                       xb_part_params *out);
void chamber_light_camera(const float center[3], uint32_t side, float view[16], float proj[16]);
uint32_t chamber_part_prims(uint32_t type, const xb_form *f, uint32_t out[3][3]);
int  chamber_group_cube128(uint32_t group);
int  chamber_group_format(uint32_t group);
void v4_mul_m4(const float v[4], const float m[16], float out[4]);   /* 0x80059A2E */

/* ---- Blob.c ---- */

#define BLOB_BULGES XB_BLOB_BULGES
#define BLOB_DROPS  XB_BLOB_DROPS
#define BLOB_VERTS  XB_BLOB_VERTICES

typedef struct blob_state {        /* the object at 0x8006FBAC, Blob.md 2 */
    uint32_t rng;                  /* 0x800708BC */
    uint32_t draws;                /* steps of the generator since xb_init */
    uint32_t n_bulges, n_drops;    /* +0x870, +0xA34 */
    uint32_t flat;                 /* the deformed buffer holds the unit mesh, w = 1 */
    uint32_t updates;              /* calls of blob_update since xb_init */
    float    colour[4];            /* +0x44 */
    float    center[3];            /* +0x54 */
    float    radius;               /* +0x6C */
    xb_bulge bulge[BLOB_BULGES];   /* +0x70 */
    xb_drop  drop[BLOB_DROPS];     /* +0x874 */
    float    unit[BLOB_VERTS][3];  /* +0x1C, unit vectors of the large mesh */
} blob_state;

void blob_start(blob_state *b);
void blob_restart(blob_state *b);
void blob_update(blob_state *b, float t, float dt);
void blob_deformed(const blob_state *b, float (*out)[4]);
uint32_t blob_mesh(uint32_t small, float *unit, uint16_t *idx);
void blob_draw(const blob_state *b, const xb_camera *cam, float t, float x1c, float x20,
               xb_blob_params *out);
void blob_weighted_center(const blob_state *b, const float ref[3], float x20, float out[3],
                          float *second);

/* ---- Logo.c ---- */

#define LOGO_MESHES XB_LOGO_MESHES

typedef struct logo_state {        /* what logo_update writes in the object at 0x8006F5CC */
    uint8_t  tm;                   /* +0x4C */
    uint8_t  texture;              /* +0xD0 is set */
    uint8_t  phase;                /* +0xD4 */
    uint8_t  rtt;                  /* the last logo_update drew into the texture */
    uint8_t  rtt_d4;               /* +0xD4 during that draw */
    uint8_t  pad[3];
    float    pos[3];               /* +0x90, row 3 of M */
    float    alpha;                /* +0xC4 */
    uint32_t updates, rtt_count;   /* calls of logo_update, draws into the texture */
} logo_state;

void logo_start(logo_state *lg);
void logo_update(const xb_state *s, logo_state *lg, float t);
void logo_mesh_info(uint32_t mesh, xb_logo_mesh_desc *out);
int  logo_mesh(const xb_state *s, uint32_t mesh, float *vtx, uint16_t *idx);
void logo_draw(const xb_state *s, const logo_state *lg, const xb_camera *cam, float t, int r,
               xb_logo_params *out);

/* ---- Effects.c ---- */

int  fx_veil_on(int chamber_on, float t, float x20);
void fx_quads(xb_real (*veil)[7], xb_real (*quad)[3]);
void fx_draw(const xb_camera *cam, float aspect, float t, float x20, const xb_track *trk,
             xb_fx_params *out);

/* ---- FrameOrder.c ---- */

int  frame_build(const xb_state *s, const xb_camera *from, xb_frame_out *out);
int  frame_setup(const xb_state *s, uint32_t pass, xb_frame_out *out);

/* ---- Textures.c ---- */

void texture_info(uint32_t tex, xb_texture_desc *out);
void texture_make(const xb_state *s, uint32_t tex, uint8_t *out);

/* Strip indices of Chamber.md 4.3 (0x800612D3) into idx; returns the count. */
uint32_t strip_indices(uint16_t *idx, uint32_t cols, uint32_t rows, int lead, int trail,
                       uint32_t start);

/* ---- Audio.c ---- */

typedef struct xb_sound xb_sound;  /* the sound memory of xb_audio_init */

size_t audio_size(void);
int  audio_ready(const xb_sound *a);
void audio_start(const xb_state *s, xb_sound *a, int playing);
void audio_block(const xb_state *s, xb_sound *a, xb_audio_out *out);
void audio_read(const xb_state *s, xb_sound *a, int16_t *buffer, uint32_t frames);
void audio_gp(const xb_state *s, xb_sound *a, const int32_t *bin0, const int32_t *bin1,
              const int32_t *bin11, int32_t *left, int32_t *right);
void audio_get(const xb_sound *a, xb_audio_state *out);
void audio_mute(xb_sound *a, uint32_t mask);
int  audio_table(const xb_state *s, uint32_t table, int16_t *out, uint32_t count);

/* ---- the state ---- */

struct xb_state {
    uint32_t      magic;           /* STATE_MAGIC after a successful xb_init */
    pack_range    pack[PACK_COUNT];
    uint32_t      rng;             /* generator at this+0x68C (Timing.md 4.1) */
    uint32_t      noise_seed;      /* its draw at 0x8005A178, seed of the noise (Textures.md 4.8) */
    timing_state  timing;
    camera_state  camera;
    chamber_state chamber;
    blob_state    blob;
    logo_state    logo;
    track_state   track;
    uint32_t      flags;           /* XB_START_* of xb_init */
    xb_sound     *sound;           /* host memory of xb_audio_init, or 0 */
};

#define STATE_MAGIC 0x78624B30u    /* "0Kbx" */

#endif
