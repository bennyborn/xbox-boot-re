/*
 * xboot.h - public interface of the portable core.
 *
 * Draft from milestone K0, time keeping fixed in K1, chamber and camera
 * in K2, the blob in K3, the logo in K4, the textures in K5, effects and
 * the frame description in K6, the sound in K7; complete since K8, which
 * added no call. W3 added xb_frame_from for the workbench, W3b xb_select_path. Behaviour, numerics, the order a host calls in and what
 * the calls cost are in docs/core.md; the animation itself is specified in
 * docs/decomp/.
 *
 * Rules: C99, freestanding (only <stddef.h> and <stdint.h>), no file
 * access, no allocation. The host provides all memory; the core reports
 * how much it needs.
 */
#ifndef XBOOT_H
#define XBOOT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XB_API_VERSION 2u

/* The number type of the interface. A port without FPU may replace it by a
 * fixed-point type in its own P0; see docs/core.md, "Numerik". */
typedef float xb_real;

typedef struct xb_state xb_state;   /* opaque, size from xb_state_size() */

/* Result codes. Negative values are errors. */
enum {
    XB_OK = 0,
    XB_DONE = 1,                     /* t >= 8.0, the loop has ended (Timing.md 3) */
    XB_ERR_ARG = -1,
    XB_ERR_ALIGN = -2,               /* state or pack not aligned to XB_ALIGN */
    XB_ERR_PACK_MAGIC = -3,
    XB_ERR_PACK_VERSION = -4,
    XB_ERR_PACK_LAYOUT = -5,         /* entry outside the pack, unsorted or overlapping */
    XB_ERR_PACK_MISSING = -6,        /* a range the core needs is not in the pack */
    XB_ERR_SPACE = -7                /* a host buffer is too small (xb_frame) */
};

#define XB_ALIGN 16u

/* Start configuration. */
enum {
    /* Short version: t starts at 7.5 instead of 0 (Timing.md 3; the byte at
     * 0x8003AEB8, filled from bit 4 of the dword at 0x8003B138). */
    XB_START_SHORT = 1u << 0,
    /* Aspect 0.5625 instead of 0.75 (Camera.md 2; the byte anim_object +
     * 0x181E, set from bit 16 of 0x8002FFDE(6)). */
    XB_START_WIDE = 1u << 1
};

typedef struct xb_config {
    uint32_t flags;        /* XB_START_* */
    uint32_t start_tick;   /* ms counter at the end of setup (Timing.md 2) */
} xb_config;

/* Bytes of state memory the core needs. Constant for a build. */
size_t xb_state_size(void);

/* Sets up the state. `pack` is the data pack from tools/pack/ and must
 * stay valid and unchanged as long as the state is used; the core reads
 * it in place. Both pointers must be aligned to XB_ALIGN. */
int xb_init(xb_state *s, const void *pack, size_t pack_size, const xb_config *cfg);

/* One step of the time model of Timing.md 2: `tick` is the host's
 * millisecond counter (the BIOS reads it at 0x8003A89C). The step is
 * |tick - last tick| ms, a step above 1 s counts 1 ms. Returns XB_OK, or
 * XB_DONE once t >= 8.0; after that the call changes nothing. */
int xb_advance(xb_state *s, uint32_t tick);

/* Free time for next-gen and high frame rates: advance by `dt` >= 0
 * seconds, rounded to float32 as the original's argument, without the
 * millisecond quantisation. The 1 s rule above applies. Leaves the last
 * tick alone. XB_OK, XB_DONE, or XB_ERR_ARG for negative or NaN `dt`. */
int xb_advance_free(xb_state *s, double dt);

/* Animation time t (this+0x10 in Timing.md) as stored, i.e. float32. */
xb_real xb_time(const xb_state *s);

/* Last tick seen (this+0x28 in Timing.md 2). */
uint32_t xb_last_tick(const xb_state *s);

/* ---- Time values (Timing.md 4), for tests and the workbench ----------- */
#define XB_PULSE_COUNT 12

typedef struct xb_timing {
    xb_real  t;                       /* this+0x10 */
    xb_real  x18, x1c, x20, x24;      /* this+0x18 integrated, +0x1C ramp,
                                         +0x20 ramp + pulses, +0x24 smoothed */
    uint32_t last_tick;               /* this+0x28 */
    uint32_t setup_tick;              /* this+0x2C */
    uint32_t rng;                     /* generator state, this+0x68C */
    xb_real  pulse[XB_PULSE_COUNT][3];/* this+0x30: t0, w, a */
} xb_timing;

int xb_get_timing(const xb_state *s, xb_timing *out);

/* ---- Camera (Camera.md), K2 -------------------------------------------
 * The camera is a function of t alone: path time s (Camera.md 4), the
 * path (5.5), view and projection (2, 3). xb_camera_at evaluates it at any
 * t without changing the state, so a host can scrub the time line. */
typedef struct xb_camera {
    xb_real  t, s;               /* animation time, path time */
    int32_t  key;                /* section i of Camera.md 5.5, -1 past the path */
    uint32_t path;               /* path chosen at set-up (5.2) */
    uint8_t  chamber_on;         /* this+0xD: chamber and blob are updated and drawn */
    uint8_t  logo_on;            /* this+0xE: the logo is updated and drawn */
    uint8_t  pad[2];
    xb_real  eye[3], target[3], up[3];   /* cam+0x58, +0x64, +0x70 */
    xb_real  view[16];           /* cam+0xFC, row vectors */
    xb_real  view_inv[16];       /* cam+0xBC; row 3 is the eye */
    xb_real  proj[16];           /* cam+0x13C */
} xb_camera;

int xb_camera_at(const xb_state *s, xb_real t, xb_camera *out);

/* The keys of the chosen path as the evaluation uses them: the path's keys
 * from the table, then the eight own keys, each with tangents (5.3, 5.4). */
#define XB_PATH_KEYS_MAX (21 + 8)

typedef struct xb_campath {
    uint32_t path, first, n_table, n_keys;
    xb_real  key[XB_PATH_KEYS_MAX][15];  /* t, eye, eye_tan, target, target_tan, cut, split */
    xb_real  frame[16];                  /* rows x, m, o and d * rho (5.3); the logo's W */
    xb_real  origin[3], look[3];         /* U and B of 5.3 */
    xb_real  fade_t0, fade_k;            /* path+0x250, +0x254 */
} xb_campath;

int xb_get_campath(const xb_state *s, xb_campath *out);

/* For the workbench: the camera follows path `path` of the table instead
 * of the one xb_init drew, as campath_select 0x8005A6CB does with an
 * argument >= 0 (Camera.md 5.2). The generator stays as xb_init left it, so
 * pulses, noise and blob are those of the drawn path. Call it before the
 * first xb_advance. XB_ERR_ARG when the table has no such path; there are
 * four (Camera.md 5.1). Not in the original's start. */
int xb_select_path(xb_state *s, uint32_t path);

/* View T from a point straight down (side 0) or up (side 1), times the
 * texel matrix (Chamber.md 8). The point is the weighted centre of the
 * blob, which K3 provides; until then the caller gives it. */
int xb_light_view(const xb_real center[3], uint32_t side, xb_real out[16]);

/* ---- Chamber (Chamber.md), K2 ------------------------------------------
 * 271 parts in six groups. Group g holds the parts of one primitive type
 * (0 sphere, 1 cylinder, 2 box, 3 torus, 4 cone, 5 lathe). Parts are
 * numbered group by group, in the order of the ROM. */
#define XB_CHAMBER_GROUPS 6
#define XB_CHAMBER_PARTS 271
#define XB_ROT_TRACKS 29
#define XB_POS_TRACKS 32

typedef struct xb_part {
    uint8_t  group, type, side, pad;     /* side: +0x118, Chamber.md 5 */
    uint16_t index;                      /* within the group */
    int16_t  form, pos_track, rot_track; /* -1: none */
    xb_real  scale[4];                   /* +0xC8 */
} xb_part;

int xb_chamber_part(const xb_state *s, uint32_t part, xb_part *out);

/* State of the tracks at time t (chamber_update 0x8005F9F1, Chamber.md 6).
 * Like the camera a function of t; NaN where the ROM's quaternion 238
 * makes it so (Chamber.md 3, 6). */
typedef struct xb_chamber_pose {
    xb_real  t, u, k;
    uint32_t detail;                     /* ch+0x5C: 1 from t >= 5.2 */
    xb_real  rot[XB_ROT_TRACKS][16];     /* ch+0x1E4 */
    xb_real  pos[XB_POS_TRACKS][3];      /* ch+0x1EC */
} xb_chamber_pose;

int xb_chamber_at(const xb_state *s, xb_real t, xb_chamber_pose *out);

/* World matrix W of each part for a pose (prim_group_draw 0x8005EE08,
 * Chamber.md 7), `count` matrices from part `first` on. */
int xb_chamber_world(const xb_state *s, const xb_chamber_pose *pose, uint32_t first,
                     uint32_t count, xb_real (*world)[16]);

/* Geometry of a group in a detail level (0 fine, 1 coarse), generated
 * into host buffers (prim_group_build 0x8005F3DB, Chamber.md 4). The
 * attributes per vertex are (u, v,) a, b, n: 11 floats in vertex format
 * 1, 9 in format 0. Indices are 16 bit. The table of forms says where
 * each form lies. Cylinders and cones count two vertices per form that
 * the original never writes; the core writes 0 there. */
typedef struct xb_form {
    uint32_t first_vertex, vertex_count, first_index, prim_count, index_count, fan;
} xb_form;

typedef struct xb_mesh_info {
    uint32_t type;           /* primitive type of the group */
    uint32_t format;         /* vertex format, 0 or 1 */
    uint32_t attr_floats;    /* 9 or 11 */
    uint32_t prim;           /* +0x1C: 6 strip, 5 triangles */
    uint32_t forms, vertices, indices;
} xb_mesh_info;

int xb_chamber_mesh_info(const xb_state *s, uint32_t group, uint32_t detail, xb_mesh_info *out);
int xb_chamber_mesh(const xb_state *s, uint32_t group, uint32_t detail,
                    xb_real *pos, xb_real *attr, uint16_t *idx, xb_form *forms);

/* ---- Blob (Blob.md), K3 -------------------------------------------------
 * Unlike camera and chamber, the blob has a state of its own: 32 bulges
 * and 8 drops driven by a second generator. xb_advance and
 * xb_advance_free update it as anim_update does: after the time step,
 * blob_update(t, dt) while the camera's switch this+0xD is set (Blob.md 1,
 * FrameOrder.md 3). Values as stored by the original, float32. */
#define XB_BLOB_BULGES 32
#define XB_BLOB_DROPS 8
#define XB_BLOB_VERTICES 1734          /* large mesh, n = 32 (Blob.md 3) */
#define XB_BLOB_INDICES 4054
#define XB_BLOB_SMALL_VERTICES 150     /* small mesh of the drops, n = 8 */
#define XB_BLOB_SMALL_INDICES 346

typedef struct xb_bulge {              /* 0x40 bytes, Blob.md 2.1 */
    xb_real  size, size2, inv2, h;     /* +0x00 ... +0x0C */
    xb_real  c[3];                     /* +0x10, direction the deformation uses */
    uint32_t mask;                     /* +0x1C, cube faces reached */
    xb_real  dir[3];                   /* +0x20 */
    xb_real  t0, freq, amp;            /* +0x2C ... +0x34 */
    int32_t  drop;                     /* +0x38 as an index, -1: none */
    uint32_t flag;                     /* +0x3C */
} xb_bulge;

typedef struct xb_drop {               /* 0x38 bytes, Blob.md 2.2 */
    xb_real  size, pos[3];             /* +0x00, +0x04 */
    uint32_t neg;                      /* +0x10 */
    xb_real  dir[3];                   /* +0x14 */
    xb_real  t0, freq, amp, disp;      /* +0x20 ... +0x2C */
    xb_real  scale, rate;              /* +0x30, +0x34 */
} xb_drop;

typedef struct xb_blob {
    uint32_t rng;                      /* generator 0x800708BC */
    uint32_t draws;                    /* its steps since xb_init */
    uint32_t n_bulges, n_drops;        /* +0x870, +0xA34 */
    uint32_t flat;                     /* 1: the mesh is undeformed (before t = 0.6) */
    uint32_t updates;                  /* calls of blob_update since xb_init */
    xb_real  center[3], radius;        /* +0x54, +0x6C */
    xb_real  colour[4];                /* +0x44 */
    xb_bulge bulge[XB_BLOB_BULGES];
    xb_drop  drop[XB_BLOB_DROPS];
} xb_blob;

int xb_get_blob(const xb_state *s, xb_blob *out);

/* The two meshes of cube_sphere_build (Blob.md 3) into host buffers:
 * `small` 0 for the large mesh (stream 0 of the blob), 1 for the drops.
 * Three float32 per vertex, 16-bit strip indices. */
int xb_blob_mesh(const xb_state *s, uint32_t small, xb_real *unit, uint16_t *idx);

/* The deformed mesh as the last blob_update left it (Blob.md 6.4), four
 * float32 per vertex: summed direction and height. XB_BLOB_VERTICES rows. */
int xb_blob_deformed(const xb_state *s, xb_real (*out)[4]);

/* What blob_draw hands the GPU (Blob.md 7) for a camera and the time
 * values t, x1c, x20 (normally xb_camera_at and xb_get_timing). */
typedef struct xb_blob_params {
    xb_real  k;                        /* 7: radius with the pulses */
    uint32_t glow_colour;              /* 7.1, ARGB */
    xb_real  glow[4][5];               /* 7.1: x, y, z, u, v */
    xb_real  mesh_vs[13][4];           /* 7.2: vertex constants 0..12; the original
                                          leaves 9.w and 11.w unset, the core writes 0 */
    xb_real  mesh_ps[2][4];            /* 7.2: c0, c1 */
    xb_real  drop_vs[6][4];            /* 7.3: vertex constants 4..9 */
    xb_real  drop_ps[3][4];            /* 7.3: c0 ... c2 */
    uint32_t drops;
    xb_real  drop[XB_BLOB_DROPS][4][4];/* 7.3: c10 ... c13 of each drop */
} xb_blob_params;

int xb_blob_draw(const xb_state *s, const xb_camera *cam, xb_real t, xb_real x1c, xb_real x20,
                 xb_blob_params *out);

/* Weighted centre 0x80058D1F (Blob.md 8, Chamber.md 8) for a point `ref`:
 * the centre and the second output, which no caller of the original reads.
 * `second` may be NULL. */
int xb_blob_center(const xb_state *s, const xb_real ref[3], xb_real x20, xb_real center[3],
                   xb_real *second);

/* Light view T of side 0 (down) or 1 (up) from the weighted centre for the
 * reference point of chamber_prepass (Chamber.md 8). */
int xb_blob_light_view(const xb_state *s, uint32_t side, xb_real out[16]);

/* Interventions, as the probes make them in the guest (Blob.md 10); for
 * tests and the workbench. xb_blob_update calls blob_update with the
 * host's arguments, whatever the camera says; xb_blob_restart is
 * blob_restart (the original calls it when it loops at t >= 8, which the
 * core does not rebuild, docs/core.md 2); xb_blob_set_rng writes the
 * generator. */
int xb_blob_update(xb_state *s, xb_real t, xb_real dt);
int xb_blob_restart(xb_state *s);
int xb_blob_set_rng(xb_state *s, uint32_t rng);

/* ---- Logo (Logo.md), K4 -------------------------------------------------
 * Seven meshes: A, B, C, D form the X, Q1 and Q2 carry "TM", E is the
 * lettering. xb_advance and xb_advance_free call logo_update(t) after the
 * blob while the camera's switch this+0xE is set (FrameOrder.md 3). */
#define XB_LOGO_MESHES 7
enum { XB_LOGO_A, XB_LOGO_B, XB_LOGO_C, XB_LOGO_D, XB_LOGO_Q1, XB_LOGO_Q2, XB_LOGO_E };

typedef struct xb_logo_mesh_desc {
    uint32_t vertices, indices;  /* triangle lists, 16-bit indices */
    uint32_t floats;             /* per vertex: 5 (x, y, z, u, v) or 3 for E */
} xb_logo_mesh_desc;

int xb_logo_mesh_info(const xb_state *s, uint32_t mesh, xb_logo_mesh_desc *out);
/* The vertex buffer and index list as logo_setup leaves them (Logo.md 3.2):
 * decoded from the ROM, for mesh C every v near 1 set to -1. */
int xb_logo_mesh(const xb_state *s, uint32_t mesh, xb_real *vertices, uint16_t *idx);

typedef struct xb_logo {
    uint32_t tm;                 /* +0x4C: Q1, Q2 and E are drawn */
    uint32_t texture;            /* +0xD0 set: the X has been drawn into its texture */
    uint32_t phase;              /* +0xD4: 1 once drawn with TM and lettering (t >= 6.25) */
    uint32_t rtt;                /* the last logo_update drew into the texture (U4) */
    uint32_t updates;            /* calls of logo_update since xb_init */
    uint32_t rtt_count;          /* draws into the texture since xb_init */
    xb_real  pos[3];             /* +0x90: position of the lettering, row 3 of M */
    xb_real  alpha;              /* +0xC4: opacity of TM, 0 ... 255 */
} xb_logo;

int xb_get_logo(const xb_state *s, xb_logo *out);

/* What logo_draw hands the GPU (Logo.md 5) for a camera and t. `to_texture`
 * 0 for the draw on the screen, 1 for the draw into the texture that the
 * last logo_update made (xb_logo.rtt). Fields of parts not drawn are 0. */
typedef struct xb_logo_params {
    uint32_t solid;              /* 5.1: A-D as meshes; 0: the X as a picture (5.2) */
    uint32_t text;               /* 5.3: Q1, Q2 and E are drawn */
    xb_real  world[16];          /* W of A-D: the path's frame (Camera.md 5.3) */
    xb_real  d_vs[9][4];         /* 5.1: vertex constants 0..8 of mesh D */
    uint32_t quad_colour;        /* 5.3: factor colour of Q1 and Q2, alpha << 24 */
    uint32_t text_colour;        /* 5.3: factor colour of E */
    xb_real  text_world[16];     /* 5.3: rot_x(pi / 2) M W, world matrix of E */
} xb_logo_params;

int xb_logo_draw(const xb_state *s, const xb_camera *cam, xb_real t, uint32_t to_texture,
                 xb_logo_params *out);

/* Intervention for tests and the workbench: logo_update(t) whatever the
 * camera says. */
int xb_logo_update(xb_state *s, xb_real t);

/* ---- Textures (Textures.md), K5 -----------------------------------------
 * The textures the set-up computes on the CPU and TM from the ROM, in the
 * order the set-up makes them (Textures.md 1). xb_texture generates one
 * into a host buffer in the layout the GPU reads (Textures.md 2, 3): mip
 * levels one after another, each half as wide and high; for a cube six
 * faces 0..5 with their levels, each aligned to 128 bytes; texels swizzled,
 * little-endian. The textures the animation draws on the GPU (Textures.md
 * 5, 6) are not data of the core. */
enum {
    XB_TEX_CUBE128,              /* anim+0x938: cube of directions, 128 */
    XB_TEX_CUBE64,               /* anim+0x934: cube of directions, 64 */
    XB_TEX_BUMP,                 /* anim+0x940: normal map of a height field, 128 */
    XB_TEX_FALLOFF,              /* anim+0x944: fall-off, 256, 8 levels */
    XB_TEX_GRADIENT_A,           /* logo+0x10: gradient of mesh A */
    XB_TEX_RADIAL,               /* logo+0x24: radial brightness of mesh B */
    XB_TEX_GRADIENT_C,           /* logo+0x38: gradient of mesh C */
    XB_TEX_TM,                   /* logo+0xC0: the texels at 0x80073FD0 */
    XB_TEX_NOISE0,               /* fx+0x08, +0x0C, +0x10: noise of the veil; */
    XB_TEX_NOISE1,               /* all three hold layer 0 (Textures.md 4.8) */
    XB_TEX_NOISE2,
    XB_TEXTURES
};

typedef struct xb_texture_desc {
    uint32_t width, height;      /* of level 0 */
    uint32_t levels, faces;      /* faces: 6 for a cube, else 1 */
    uint32_t format;             /* colour format: 6 A8R8G8B8, 7 X8R8G8B8, 0x19 A8 */
    uint32_t texel_bytes;
    uint32_t format_word;        /* SET_TEXTURE_FORMAT as the original sends it */
    uint32_t bytes;              /* size of the data xb_texture writes */
} xb_texture_desc;

int xb_texture_info(const xb_state *s, uint32_t tex, xb_texture_desc *out);
/* Writes texture `tex`; `size` must be at least xb_texture_desc.bytes. The
 * noise uses the draw the set-up made for it (Textures.md 4.8). */
int xb_texture(const xb_state *s, uint32_t tex, void *texels, size_t size);

/* ---- Track values (Camera.md 6), K6 --------------------------------------
 * anim_update keeps them after every evaluation of the path: the length of
 * the eye, its angle about z (unwrapped against the previous value), its
 * elevation and its distance from the blob's centre. The veil reads them
 * (Effects.md 3, 4). */
typedef struct xb_track {
    xb_real  dist;               /* anim+0x690: |eye| */
    xb_real  azimuth;            /* anim+0x698 */
    xb_real  elevation;          /* anim+0x6A0 */
    xb_real  blob_dist;          /* anim+0x6A8: D of Effects.md */
    uint32_t fx_draws;           /* calls of fx_draw since xb_init; each swaps the
                                    veil's two targets (Effects.md 4) */
} xb_track;

int xb_get_track(const xb_state *s, xb_track *out);

/* ---- Chamber, values of a draw (Chamber.md 7), K6 -------------------------
 * What prim_group_draw hands the GPU for one part: W, the vertex constants
 * 0 to 11 (c[96] to c[107] of GPU.md 3.1) and the three colours for
 * 0x800410B0. `lit` 1 is the draw on the screen (colours and constant 7
 * with m = 2 * x20, the light at the weighted centre of the blob), 0 the
 * draw of the set-up into the cube texture (colours without m, the light
 * at light_params + 0x34). `light` are the two matrices T of Chamber.md 8,
 * side 0 and side 1. */
typedef struct xb_part_params {
    xb_real world[16];           /* W */
    xb_real vs[12][4];           /* constants 0 to 11 */
    xb_real ps[3][4];            /* colours 0 to 2 as handed to 0x800410B0 */
} xb_part_params;

int xb_chamber_draw(const xb_state *s, const xb_camera *cam, const xb_chamber_pose *pose,
                    xb_real x20, const xb_real (*light)[16], uint32_t lit, uint32_t part,
                    xb_part_params *out);

/* View and projection chamber_light_view sets for side 0 or 1 from the
 * weighted centre (Chamber.md 8); the prepass draws with them. */
int xb_blob_light_camera(const xb_state *s, uint32_t side, xb_real view[16], xb_real proj[16]);

/* ---- Veil and glow (Effects.md), K6 ----------------------------------------
 * What fx_render_offscreen and fx_draw hand the GPU for a camera, the time
 * values t and x20 and the track values. */
typedef struct xb_fx_params {
    xb_real  quad_vs[4][4];      /* 3: c[112] to c[115] for the quad into the target */
    xb_real  quad_m[4][4];       /* 3: c[96] to c[99] for the quad */
    xb_real  depth_vs[2][4];     /* 3: c[112], c[113] for the chamber in depth */
    uint32_t veil;               /* 2: fx_draw is called (FrameOrder.md 3, v) */
    xb_real  veil_ps[2][4];      /* 4: the two colours for 0x800410B0 */
    xb_real  veil_vs[6][4];      /* 4: c[96] to c[101], texture coordinates of the noise */
    uint32_t depth_func;         /* 4: 0x207 always, 0x202 equal while the logo is on */
    uint32_t glow;               /* 5: the glow is drawn (its alpha is not 0) */
    uint32_t glow_alpha;         /* 5: a */
    uint32_t glow_colour;        /* 5: factor colour, a << 24 | 0xA0FF60 */
    xb_real  glow_s;             /* 5: s */
    xb_real  glow_world[16];     /* 5: slot 6, diag(1/s, 1/s, 0) with [3][2] = 1 */
} xb_fx_params;

int xb_fx_draw(const xb_state *s, const xb_camera *cam, xb_real t, xb_real x20,
               const xb_track *track, xb_fx_params *out);

/* The two vertex buffers of fx_setup (Effects.md 1): the veil's four
 * vertices (x, y, z, u, v, p, q) and the quad for the target (x, y, z). */
int xb_fx_quads(xb_real (*veil)[7], xb_real (*quad)[3]);

/* ---- Frame description (FrameOrder.md), K6 ---------------------------------
 * One entry per draw call, in the order of FrameOrder.md. xb_frame
 * describes the frame the last xb_advance made: the draws of anim_update
 * (U1 to U4) and of anim_draw (D1 to D5). xb_frame_setup describes the
 * draws of the set-up (A1, A2). The core names what is drawn, with which
 * program, combiners, textures, target, render states and constants; how a
 * GPU does that is the host's business (docs/core.md 2). */
enum {
    XB_STEP_A1 = 0x0A01, XB_STEP_A2 = 0x0A02,
    XB_STEP_U1 = 0x0B01, XB_STEP_U2 = 0x0B02, XB_STEP_U3 = 0x0B03, XB_STEP_U4 = 0x0B04,
    XB_STEP_D1 = 0x0D01, XB_STEP_D2 = 0x0D02, XB_STEP_D3 = 0x0D03, XB_STEP_D4 = 0x0D04,
    XB_STEP_D5 = 0x0D05
};

/* What a draw is. */
enum {
    XB_DRAW_PRESENT,             /* the library's triangle that shows the frame (GPU.md 7) */
    XB_DRAW_CHAMBER,             /* one draw of a part of the chamber */
    XB_DRAW_BLOB_GLOW, XB_DRAW_BLOB_MESH, XB_DRAW_BLOB_DROP,
    XB_DRAW_LOGO_A, XB_DRAW_LOGO_B, XB_DRAW_LOGO_C, XB_DRAW_LOGO_D,
    XB_DRAW_LOGO_Q1, XB_DRAW_LOGO_Q2, XB_DRAW_LOGO_E,
    XB_DRAW_LOGO_IMAGE,          /* the X as a picture (Logo.md 5.2) */
    XB_DRAW_FX_QUAD,             /* the quad into the veil's target (Effects.md 3) */
    XB_DRAW_VEIL, XB_DRAW_GLOW   /* Effects.md 4, 5 */
};

/* Where the vertices come from. */
enum {
    XB_MESH_PRESENT,             /* three vertices the library writes into the stream */
    XB_MESH_CHAMBER,             /* + group 0 to 5: xb_chamber_mesh(group, xb_draw.detail) */
    XB_MESH_BLOB = XB_MESH_CHAMBER + XB_CHAMBER_GROUPS,  /* xb_blob_mesh(0) and xb_blob_deformed */
    XB_MESH_DROP,                /* xb_blob_mesh(1) */
    XB_MESH_LOGO,                /* + XB_LOGO_*: xb_logo_mesh */
    XB_MESH_VEIL = XB_MESH_LOGO + XB_LOGO_MESHES,        /* xb_fx_quads, veil */
    XB_MESH_FX_QUAD,             /* xb_fx_quads, quad */
    XB_MESH_INLINE               /* vertices in xb_frame_out.params (vtx_first) */
};

/* Textures and render targets. 0 is none; 1 + XB_TEX_* are the textures of
 * xb_texture; the rest is drawn on the GPU (Textures.md 5, 6, GPU.md 7). */
enum {
    XB_SURF_NONE = 0,
    XB_SURF_TEX = 1,                         /* + XB_TEX_* */
    XB_SURF_LIGHT0 = XB_SURF_TEX + XB_TEXTURES,  /* chamber+0x160: light view of side 0, depth */
    XB_SURF_LIGHT1,                          /* chamber+0x15C: side 1 */
    XB_SURF_FX_A,                            /* veil target made at 0x8005A445 (first in fx+0x18) */
    XB_SURF_FX_B,                            /* veil target made at 0x8005A45B (first in fx+0x14) */
    XB_SURF_FX_DEPTH,                        /* fx+0x1C */
    XB_SURF_LOGO,                            /* logo+0xD0, the picture of the X */
    XB_SURF_LOGO_TARGET,                     /* the 1024 x 1024 target logo+0xD0 is copied from */
    XB_SURF_LOGO_DEPTH,
    XB_SURF_CUBE,                            /* + face 0 to 5: anim+0x93C, cube of the chamber */
    XB_SURF_CUBE_DEPTH = XB_SURF_CUBE + 6,
    XB_SURF_SCREEN,                          /* 1280 x 480 the frame is drawn into */
    XB_SURF_SCREEN_DEPTH,
    XB_SURF_DISPLAY,                         /* 640 x 480 that is shown */
    XB_SURFACES
};

/* Flags of a draw. */
enum {
    XB_DRAW_INDEXED = 1u << 0,   /* `first`, `count` are indices of the mesh, else vertices */
    XB_DRAW_FACTOR = 1u << 1,    /* `factor` was set for this draw (0x8003CED0) */
    XB_DRAW_MATRICES = 1u << 2   /* fixed function: world, view, proj are valid */
};

/* Render states in effect at a draw, as values of the NV2A methods
 * (GPU.md 6). A function or factor is only meaningful while its switch is
 * set. `shadow_func` is 0 where the draw has no light view. */
typedef struct xb_render_state {
    uint8_t  alpha_test, blend, depth_test, depth_write, cull, pad[3];
    uint16_t alpha_func, blend_src, blend_dst, depth_func, cull_face, shadow_func;
    uint32_t alpha_ref;
    uint32_t colour_mask;        /* SET_COLOR_MASK, 0x01010101 all channels */
} xb_render_state;

typedef struct xb_const_run {    /* vertex constants base to base + count - 1 */
    uint16_t base, count;        /* constant k is c[96 + k] of GPU.md */
    uint32_t first;              /* index into params, four values each */
} xb_const_run;

typedef struct xb_draw {
    uint16_t step;               /* XB_STEP_* */
    uint16_t what;               /* XB_DRAW_* */
    uint16_t mesh;               /* XB_MESH_* */
    uint16_t part;               /* chamber: part 0 to 270; drop: its index */
    uint8_t  prim;               /* 5 triangles, 6 strip, 7 fan */
    uint8_t  detail;             /* chamber: detail level of the mesh */
    uint8_t  color, zeta;        /* XB_SURF_*: colour and depth target */
    uint8_t  tex[4];             /* XB_SURF_* per stage the draw reads, 0: none */
    uint32_t flags;              /* XB_DRAW_INDEXED and so on */
    uint32_t first, count;       /* first index or vertex, number of them */
    uint32_t program;            /* address of the vertex program in the ROM (GPU.md 3), a
                                    fixed-function format 0x2, 0x102, 0x202 (GPU.md 2.1),
                                    or 0 for the library's own draw */
    uint32_t combiners;          /* address of the combiner block (GPU.md 4), or 0:
                                    built from the texture stages (GPU.md 5) */
    xb_render_state state;
    uint32_t factor;             /* XB_DRAW_FACTOR: colour for 0x8003CED0 */
    uint32_t world, view, proj;  /* XB_DRAW_MATRICES: slots 6, 0, 1 of 0x8003E6C0, each an
                                    index into params, 16 values, row vectors */
    xb_const_run vs[2];          /* vertex constants in effect, count 0: none */
    uint32_t ps_first, ps_count; /* colours for 0x800410B0, four values each */
    uint32_t vtx_first, vtx_count; /* XB_MESH_INLINE: values in params */
} xb_draw;

/* Enough for any frame and for one pass of the set-up. */
#define XB_FRAME_DRAWS_MAX 1320u
#define XB_FRAME_PARAMS_MAX 40960u

typedef struct xb_frame_out {
    xb_real   view[16];
    xb_real   proj[16];
    xb_draw  *draws;        /* host buffer */
    uint32_t  draw_cap;
    uint32_t  draw_count;   /* set by the core, also when the buffer is too small */
    xb_real  *params;       /* host buffer for constants of all draws */
    uint32_t  param_cap;
    uint32_t  param_count;  /* set by the core, also when the buffer is too small */
    uint32_t  clear_depth;  /* anim_draw clears depth and stencil before D3
                               (FrameOrder.md 3) */
} xb_frame_out;

/* Describes the frame of the last xb_advance. XB_ERR_SPACE when a buffer
 * is too small; draw_count and param_count then say what is needed. */
int xb_frame(const xb_state *s, xb_frame_out *out);

/* The same frame as a camera of the host would see it, for the workbench:
 * eye, target and up go through camera_set_view (Camera.md 3) and replace
 * those of the path; the projection stays. Every draw that takes the
 * camera takes this one: the chamber on the screen and in the veil's
 * target, blob, logo, veil and glow, the veil with the track values of
 * this eye (Camera.md 6, the angle unwrapped against the animation's).
 * What does not depend on the camera stays: time, the switches this+0xD
 * and this+0xE, the light views, the state. With the eye, target and up of
 * xb_camera_at(xb_time()) the result is that of xb_frame. Nothing of this
 * is in the original; XB_ERR_ARG when the three give no view (no
 * direction, or one along `up`). */
int xb_frame_from(const xb_state *s, const xb_real eye[3], const xb_real target[3],
                  const xb_real up[3], xb_frame_out *out);

/* The draws of the set-up: pass 0 is A1, the presentation of a black
 * frame; pass 1 to 6 is A2, the chamber at t = 5.35 into face pass - 1 of
 * the cube texture (Chamber.md 9, Textures.md 5). view and proj are those
 * of the face. */
#define XB_SETUP_PASSES 7u
int xb_frame_setup(const xb_state *s, uint32_t pass, xb_frame_out *out);

/* ---- Sound (Audio.md), K7 --------------------------------------------------
 * The sound has a clock of its own: blocks of 32 samples at 48 kHz, the
 * frame of the sound hardware. Every ninth block begins with a tick of the
 * sequencer (6 ms, Audio.md 1, 4). A block runs the sequencer (Audio.md 3),
 * the voice layer (5), the voices as xemu's VP plays them (7, 8) and the
 * frame routine of the GP program (6). None of it depends on xb_advance.
 *
 * The sound needs memory of its own, so that a host without sound does
 * not pay for it: xb_audio_size() bytes, aligned to XB_ALIGN, handed over
 * with xb_audio_init, which also starts the sound from the beginning. In
 * the short version the original starts no sound (Audio.md 1); the core
 * then plays silence. */
#define XB_AUDIO_RATE 48000u
#define XB_AUDIO_BLOCK 32u
#define XB_AUDIO_TICK_BLOCKS 9u
#define XB_AUDIO_CHANNELS 16u          /* channels of the sequencer = voices */
#define XB_AUDIO_BINS 3u               /* the mix bins in use: 0, 1 and 11 */

size_t xb_audio_size(void);
int xb_audio_init(xb_state *s, void *memory, size_t size);

/* Writes `frames` stereo frames of 16-bit samples, interleaved left, right
 * (the GP's output X:0x1400 and X:0x1420, bits 8 to 23, Audio.md 6.5).
 * Whole blocks are rendered as needed; what is left of the last block is
 * kept for the next call. Silence without xb_audio_init. */
int xb_audio(xb_state *s, int16_t *buffer, uint32_t frames);

/* Calls the sequencer makes into the voice layer (Audio.md 5). */
enum {
    XB_CALL_FILTER,      /* 0x800619B1: a0 = F, a1 = Q */
    XB_CALL_STOP,        /* 0x80061994: release */
    XB_CALL_SAMPLE,      /* 0x80061A08: a0 = sample number */
    XB_CALL_LEVEL,       /* 0x80061D48: a0 = level 200 - 30 v */
    XB_CALL_NOTE,        /* 0x800618DF: a0 = pitch for the library, a1 = loop */
    XB_CALL_PITCH,       /* 0x80061952: a0 = pitch for the library */
    XB_CALL_B2           /* 0x80061A91 */
};

typedef struct xb_audio_cmd {    /* a command the sequencer ran (Audio.md 3.4) */
    uint32_t addr;               /* its address in the ROM */
    uint16_t tick;               /* time 0x800A7958 of the tick */
    uint8_t  channel, op;
} xb_audio_cmd;

typedef struct xb_audio_call {
    int32_t  a0, a1;
    uint16_t tick;
    uint8_t  channel, fn;        /* XB_CALL_* */
    uint8_t  effect;             /* note: 1 the voice was started; stop: 1 it was released */
    uint8_t  pad[3];
} xb_audio_call;

/* One block in detail, for tests, the workbench and hosts that mix the
 * voices themselves. voice[c] is voice c after envelope-controlled pitch
 * and low-pass, before any gain; gain[c][k] takes it into mix bin 0, 1 or
 * 11 (envelope, attenuation and headroom together; 0 while the voice is
 * silent or muted). bin and out are 24-bit values, sign-extended: the mix
 * bins as the GP gets them, and left and right as it leaves them. */
typedef struct xb_audio_out {
    uint32_t      block;                 /* number of this block since xb_audio_init */
    uint32_t      tick;                  /* time of the tick this block began with, 0: none */
    uint32_t      n_cmds, n_calls;
    xb_audio_cmd  cmd[XB_AUDIO_CHANNELS];
    xb_audio_call call[XB_AUDIO_CHANNELS];
    xb_real       voice[XB_AUDIO_CHANNELS][XB_AUDIO_BLOCK];
    xb_real       gain[XB_AUDIO_CHANNELS][XB_AUDIO_BINS];
    int32_t       bin[XB_AUDIO_BINS][XB_AUDIO_BLOCK];
    int32_t       out[2][XB_AUDIO_BLOCK];
} xb_audio_out;

/* Renders the next block. Samples xb_audio still held are dropped. */
int xb_audio_block(xb_state *s, xb_audio_out *out);

/* Channels whose voices are left out of the mix, bit c for channel c. The
 * voices run on as before; only their gain is 0. */
int xb_audio_mute(xb_state *s, uint32_t mask);

/* A voice as the library and the hardware hold it (Audio.md 5): the values
 * of the methods the last calls sent, and whether it runs. */
typedef struct xb_voice {
    uint8_t  alloc;              /* a sample has been set (Audio.md 5.2) */
    uint8_t  on;                 /* running for the library (5.5) */
    uint8_t  active;             /* running in the hardware */
    uint8_t  sample;             /* sample number */
    int32_t  level;              /* 200 - 30 v of the last level call */
    uint32_t fmt, vbin;          /* methods 0x304, 0x300 */
    uint32_t misc;               /* 0x318 */
    uint32_t env0, enva, env1, envf, lfo_env;   /* 0x308, 0x30C, 0x310, 0x314, 0x36C */
    uint32_t fca;                /* 0x374 and 0x378 */
    uint32_t vola, volb, volc;   /* 0x360, 0x364, 0x368 */
    uint32_t pitch;              /* 0x37C */
    uint32_t ebo;                /* 0x3DC */
} xb_voice;

typedef struct xb_audio_state {
    uint32_t blocks;             /* blocks rendered since xb_audio_init */
    uint32_t tick;               /* time 0x800A7958 */
    uint32_t tracks;             /* active tracks */
    uint32_t mute;
    xb_voice voice[XB_AUDIO_CHANNELS];
} xb_audio_state;

int xb_get_audio(const xb_state *s, xb_audio_state *out);

/* The frame routine of the GP program alone (Audio.md 6.2): takes one
 * block of the bins 0, 1 and 11 as 24-bit values and returns left and
 * right. It runs on the delay lines of the sound memory, so a test or a
 * host that calls it does not mix it with xb_audio. */
int xb_audio_gp(xb_state *s, const int32_t *bin0, const int32_t *bin1, const int32_t *bin11,
                int32_t *left, int32_t *right);

/* The sample tables (Audio.md 2): the four the kernel computes and the
 * four it converts from the ROM, as 16-bit values. xb_audio_table returns
 * the number of values, or writes them when `out` is not NULL; `count` is
 * the room in `out`. No sound memory needed. */
enum {
    XB_TABLE_NOISE,              /* 0x800A1B78, 0x2000 values */
    XB_TABLE_SINE,               /* 0x80093B78, 128 */
    XB_TABLE_FM,                 /* 0x80083B70, 0x8000 */
    XB_TABLE_SAW,                /* 0x80083328, 128 */
    XB_TABLE_PCM_SIGNED,         /* 0x80078898 from 0x80066AC0, 0x5540 */
    XB_TABLE_REVERSED,           /* 0x800970F8, 0x5540 */
    XB_TABLE_PCM_A,              /* 0x800A5B80 from 0x8006C008, 0xEB8 */
    XB_TABLE_PCM_B,              /* 0x80093C78 from 0x8006CEC8, 0x1A3F */
    XB_TABLES
};

int xb_audio_table(const xb_state *s, uint32_t table, int16_t *out, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
