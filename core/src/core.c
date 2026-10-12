/* Entry points of xboot.h. Each one checks its arguments and hands over to
 * the subsystem files, which are named as in docs/decomp/. */
#include "core_internal.h"

static int aligned(const void *p)
{
    return ((uintptr_t)p % XB_ALIGN) == 0;
}

static int ready(const xb_state *s)
{
    return s && s->magic == STATE_MAGIC;
}

size_t xb_state_size(void)
{
    return sizeof(xb_state);
}

int xb_init(xb_state *s, const void *pack, size_t pack_size, const xb_config *cfg)
{
    int rc;

    if (!s || !pack || !cfg)
        return XB_ERR_ARG;
    if (!aligned(s) || !aligned(pack))
        return XB_ERR_ALIGN;
    s->magic = 0;
    rc = pack_open(s->pack, (const uint8_t *)pack, pack_size);
    if (rc != XB_OK)
        return rc;
    /* Order of the constructor 0x80056ACA and the set-up 0x8005768C: seed,
     * camera path (one draw), pulse table and set-up (37 draws). The
     * chamber draws nothing. */
    s->rng = RNG_SEED;
    rc = camera_start(&s->camera, s, &s->rng, cfg->flags);
    if (rc == XB_OK)
        rc = chamber_start(&s->chamber, s);
    if (rc != XB_OK)
        return rc;
    timing_start(&s->timing, &s->rng, cfg->flags, cfg->start_tick);
    /* The last draw of timing_start is the one fx_noise_textures makes at
     * 0x8005A178 for the seed of the noise (Timing.md 4.1, Textures.md 4.8). */
    s->noise_seed = s->rng;
    /* The blob has its own generator (Blob.md 4): constructor and set-up
     * draw from it only. */
    blob_start(&s->blob);
    logo_start(&s->logo);
    /* anim_construct swaps +0x690, +0x698, +0x6A0 with their neighbours,
     * which are still 0 (0x80056B92-0x80056BDC, Camera.md 6). */
    s->track.dist = s->track.azimuth = s->track.elevation = s->track.blob_dist = 0.0f;
    s->track.fx_draws = s->track.fx_flip = 0;
    s->flags = cfg->flags;
    s->sound = 0;
    s->magic = STATE_MAGIC;
    return XB_OK;
}

/* The rest of anim_update after the time step (FrameOrder.md 3): the
 * camera sets this+0xD and this+0xE at the new t, the track values follow
 * the eye (Camera.md 6), then chamber_update (a function of t in the core,
 * docs/core.md 2), blob_update(t, arg) and logo_update(t)
 * (0x800575FB-0x80057617). anim_draw follows every anim_update
 * (0x80057949); its call of fx_draw swaps the veil's targets, so the
 * count is kept here. */
static int objects(xb_state *s, uint32_t steps, int rc)
{
    xb_camera cam;

    if (rc < 0 || s->timing.steps == steps)
        return rc;
    camera_at(s, s->timing.t, &cam);
    camera_track(s, &s->track, cam.eye, s->blob.center);
    if (cam.chamber_on)
        blob_update(&s->blob, s->timing.t, s->timing.arg);
    if (cam.logo_on)
        logo_update(s, &s->logo, s->timing.t);
    s->track.fx_flip = s->track.fx_draws & 1u;
    if (fx_veil_on(cam.chamber_on, s->timing.t, s->timing.x20))
        s->track.fx_draws++;
    return rc;
}

int xb_advance(xb_state *s, uint32_t tick)
{
    uint32_t n;

    if (!ready(s))
        return XB_ERR_ARG;
    n = s->timing.steps;
    return objects(s, n, timing_frame(&s->timing, tick));
}

int xb_advance_free(xb_state *s, double dt)
{
    uint32_t n;

    if (!ready(s) || !(dt >= 0.0))     /* also NaN */
        return XB_ERR_ARG;
    n = s->timing.steps;
    return objects(s, n, timing_free(&s->timing, dt));
}

xb_real xb_time(const xb_state *s)
{
    return ready(s) ? s->timing.t : 0.0f;
}

uint32_t xb_last_tick(const xb_state *s)
{
    return ready(s) ? s->timing.last_tick : 0u;
}

int xb_get_timing(const xb_state *s, xb_timing *out)
{
    int i;

    if (!ready(s) || !out)
        return XB_ERR_ARG;
    out->t = s->timing.t;
    out->x18 = s->timing.x18;
    out->x1c = s->timing.x1c;
    out->x20 = s->timing.x20;
    out->x24 = s->timing.x24;
    out->last_tick = s->timing.last_tick;
    out->setup_tick = s->timing.setup_tick;
    out->rng = s->rng;
    for (i = 0; i < 3 * XB_PULSE_COUNT; i++)
        out->pulse[i / 3][i % 3] = s->timing.pulse[i / 3][i % 3];
    return XB_OK;
}

int xb_camera_at(const xb_state *s, xb_real t, xb_camera *out)
{
    if (!ready(s) || !out)
        return XB_ERR_ARG;
    camera_at(s, t, out);
    return XB_OK;
}

int xb_select_path(xb_state *s, uint32_t path)
{
    if (!ready(s))
        return XB_ERR_ARG;
    return camera_select(&s->camera, s, path);
}

int xb_get_campath(const xb_state *s, xb_campath *out)
{
    const camera_state *cs;
    uint32_t i;

    if (!ready(s) || !out)
        return XB_ERR_ARG;
    cs = &s->camera;
    out->path = cs->path;
    out->first = cs->first;
    out->n_table = cs->n_table;
    out->n_keys = cs->n_keys;
    for (i = 0; i < XB_PATH_KEYS_MAX; i++) {
        const campath_key *k = &cs->keys[i];
        xb_real *o = out->key[i];
        int a;

        if (i >= cs->n_keys) {
            for (a = 0; a < 15; a++)
                o[a] = 0.0f;
            continue;
        }
        o[0] = k->t;
        for (a = 0; a < 3; a++) {
            o[1 + a] = k->eye[a];
            o[4 + a] = k->eye_tan[a];
            o[7 + a] = k->target[a];
            o[10 + a] = k->target_tan[a];
        }
        o[13] = k->cut;
        o[14] = k->split;
    }
    for (i = 0; i < 16; i++)
        out->frame[i] = cs->frame[i];
    for (i = 0; i < 3; i++) {
        out->origin[i] = cs->origin[i];
        out->look[i] = cs->look[i];
    }
    out->fade_t0 = cs->fade_t0;
    out->fade_k = cs->fade_k;
    return XB_OK;
}

int xb_light_view(const xb_real center[3], uint32_t side, xb_real out[16])
{
    if (!center || !out || side > 1)
        return XB_ERR_ARG;
    chamber_light_view(center, side, out);
    return XB_OK;
}

int xb_chamber_part(const xb_state *s, uint32_t part, xb_part *out)
{
    const chamber_part *pp;
    int a;

    if (!ready(s) || !out || part >= XB_CHAMBER_PARTS)
        return XB_ERR_ARG;
    pp = &s->chamber.parts[part];
    out->group = pp->group;
    out->type = pp->type;
    out->side = pp->side;
    out->pad = 0;
    out->index = pp->index;
    out->form = pp->form;
    out->pos_track = pp->pos_track;
    out->rot_track = pp->rot_track;
    for (a = 0; a < 4; a++)
        out->scale[a] = pp->scale[a];
    return XB_OK;
}

int xb_chamber_at(const xb_state *s, xb_real t, xb_chamber_pose *out)
{
    if (!ready(s) || !out)
        return XB_ERR_ARG;
    chamber_at(s, t, out);
    return XB_OK;
}

int xb_chamber_world(const xb_state *s, const xb_chamber_pose *pose, uint32_t first,
                     uint32_t count, xb_real (*world)[16])
{
    uint32_t i;

    if (!ready(s) || !pose || (!world && count) || first > XB_CHAMBER_PARTS ||
        count > XB_CHAMBER_PARTS - first)
        return XB_ERR_ARG;
    for (i = 0; i < count; i++)
        chamber_world(s, pose, first + i, world[i]);
    return XB_OK;
}

int xb_chamber_mesh_info(const xb_state *s, uint32_t group, uint32_t detail, xb_mesh_info *out)
{
    if (!ready(s) || !out)
        return XB_ERR_ARG;
    return chamber_mesh(s, group, detail, out, 0, 0, 0, 0);
}

int xb_chamber_mesh(const xb_state *s, uint32_t group, uint32_t detail,
                    xb_real *pos, xb_real *attr, uint16_t *idx, xb_form *forms)
{
    xb_mesh_info info;

    if (!ready(s) || !pos || !attr || !idx || !forms)
        return XB_ERR_ARG;
    return chamber_mesh(s, group, detail, &info, pos, attr, idx, forms);
}

int xb_get_blob(const xb_state *s, xb_blob *out)
{
    const blob_state *b;
    int i;

    if (!ready(s) || !out)
        return XB_ERR_ARG;
    b = &s->blob;
    out->rng = b->rng;
    out->draws = b->draws;
    out->n_bulges = b->n_bulges;
    out->n_drops = b->n_drops;
    out->flat = b->flat;
    out->updates = b->updates;
    for (i = 0; i < 3; i++)
        out->center[i] = b->center[i];
    out->radius = b->radius;
    for (i = 0; i < 4; i++)
        out->colour[i] = b->colour[i];
    for (i = 0; i < XB_BLOB_BULGES; i++)
        out->bulge[i] = b->bulge[i];
    for (i = 0; i < XB_BLOB_DROPS; i++)
        out->drop[i] = b->drop[i];
    return XB_OK;
}

int xb_blob_mesh(const xb_state *s, uint32_t small, xb_real *unit, uint16_t *idx)
{
    if (!ready(s) || !unit || !idx || small > 1)
        return XB_ERR_ARG;
    blob_mesh(small, unit, idx);
    return XB_OK;
}

int xb_blob_deformed(const xb_state *s, xb_real (*out)[4])
{
    if (!ready(s) || !out)
        return XB_ERR_ARG;
    blob_deformed(&s->blob, out);
    return XB_OK;
}

int xb_blob_draw(const xb_state *s, const xb_camera *cam, xb_real t, xb_real x1c, xb_real x20,
                 xb_blob_params *out)
{
    if (!ready(s) || !cam || !out)
        return XB_ERR_ARG;
    blob_draw(&s->blob, cam, t, x1c, x20, out);
    return XB_OK;
}

int xb_blob_center(const xb_state *s, const xb_real ref[3], xb_real x20, xb_real center[3],
                   xb_real *second)
{
    if (!ready(s) || !ref || !center)
        return XB_ERR_ARG;
    blob_weighted_center(&s->blob, ref, x20, center, second);
    return XB_OK;
}

int xb_blob_light_view(const xb_state *s, uint32_t side, xb_real out[16])
{
    /* Reference points of chamber_prepass: (0, 0, -30) for side 0 and
     * (0, 0, 40) for side 1, 0x800669D0 and 0x800669CC (Chamber.md 8). The
     * second output needs x20; the light view does not read it. */
    float ref[3] = { 0.0f, 0.0f, 0.0f }, c[3];

    if (!ready(s) || !out || side > 1)
        return XB_ERR_ARG;
    ref[2] = side ? 40.0f : -30.0f;
    blob_weighted_center(&s->blob, ref, 0.0f, c, 0);
    chamber_light_view(c, side, out);
    return XB_OK;
}

int xb_blob_update(xb_state *s, xb_real t, xb_real dt)
{
    if (!ready(s))
        return XB_ERR_ARG;
    blob_update(&s->blob, t, dt);
    return XB_OK;
}

int xb_blob_restart(xb_state *s)
{
    if (!ready(s))
        return XB_ERR_ARG;
    blob_restart(&s->blob);
    return XB_OK;
}

int xb_blob_set_rng(xb_state *s, uint32_t rng)
{
    if (!ready(s))
        return XB_ERR_ARG;
    s->blob.rng = rng;
    return XB_OK;
}

int xb_logo_mesh_info(const xb_state *s, uint32_t mesh, xb_logo_mesh_desc *out)
{
    if (!ready(s) || !out || mesh >= XB_LOGO_MESHES)
        return XB_ERR_ARG;
    logo_mesh_info(mesh, out);
    return XB_OK;
}

int xb_logo_mesh(const xb_state *s, uint32_t mesh, xb_real *vertices, uint16_t *idx)
{
    if (!ready(s) || !vertices || !idx || mesh >= XB_LOGO_MESHES)
        return XB_ERR_ARG;
    return logo_mesh(s, mesh, vertices, idx);
}

int xb_get_logo(const xb_state *s, xb_logo *out)
{
    const logo_state *lg;
    int i;

    if (!ready(s) || !out)
        return XB_ERR_ARG;
    lg = &s->logo;
    out->tm = lg->tm;
    out->texture = lg->texture;
    out->phase = lg->phase;
    out->rtt = lg->rtt;
    out->updates = lg->updates;
    out->rtt_count = lg->rtt_count;
    for (i = 0; i < 3; i++)
        out->pos[i] = lg->pos[i];
    out->alpha = lg->alpha;
    return XB_OK;
}

int xb_logo_draw(const xb_state *s, const xb_camera *cam, xb_real t, uint32_t to_texture,
                 xb_logo_params *out)
{
    if (!ready(s) || !cam || !out || to_texture > 1)
        return XB_ERR_ARG;
    logo_draw(s, &s->logo, cam, t, (int)to_texture, out);
    return XB_OK;
}

int xb_logo_update(xb_state *s, xb_real t)
{
    if (!ready(s))
        return XB_ERR_ARG;
    logo_update(s, &s->logo, t);
    return XB_OK;
}

int xb_texture_info(const xb_state *s, uint32_t tex, xb_texture_desc *out)
{
    if (!ready(s) || !out || tex >= XB_TEXTURES)
        return XB_ERR_ARG;
    texture_info(tex, out);
    return XB_OK;
}

int xb_texture(const xb_state *s, uint32_t tex, void *texels, size_t size)
{
    xb_texture_desc d;

    if (!ready(s) || !texels || tex >= XB_TEXTURES)
        return XB_ERR_ARG;
    texture_info(tex, &d);
    if (size < d.bytes)
        return XB_ERR_ARG;
    texture_make(s, tex, (uint8_t *)texels);
    return XB_OK;
}

int xb_get_track(const xb_state *s, xb_track *out)
{
    if (!ready(s) || !out)
        return XB_ERR_ARG;
    out->dist = s->track.dist;
    out->azimuth = s->track.azimuth;
    out->elevation = s->track.elevation;
    out->blob_dist = s->track.blob_dist;
    out->fx_draws = s->track.fx_draws;
    return XB_OK;
}

int xb_chamber_draw(const xb_state *s, const xb_camera *cam, const xb_chamber_pose *pose,
                    xb_real x20, const xb_real (*light)[16], uint32_t lit, uint32_t part,
                    xb_part_params *out)
{
    if (!ready(s) || !cam || !pose || !light || !out || lit > 1 || part >= XB_CHAMBER_PARTS)
        return XB_ERR_ARG;
    chamber_part_draw(s, cam, pose, x20, light, (int)lit, part, out);
    return XB_OK;
}

int xb_blob_light_camera(const xb_state *s, uint32_t side, xb_real view[16], xb_real proj[16])
{
    float ref[3] = { 0.0f, 0.0f, 0.0f }, c[3];

    if (!ready(s) || !view || !proj || side > 1)
        return XB_ERR_ARG;
    ref[2] = side ? 40.0f : -30.0f;     /* 0x800669CC, 0x800669D0 */
    blob_weighted_center(&s->blob, ref, 0.0f, c, 0);
    chamber_light_camera(c, side, view, proj);
    return XB_OK;
}

int xb_fx_draw(const xb_state *s, const xb_camera *cam, xb_real t, xb_real x20,
               const xb_track *track, xb_fx_params *out)
{
    if (!ready(s) || !cam || !track || !out)
        return XB_ERR_ARG;
    fx_draw(cam, s->camera.aspect, t, x20, track, out);
    return XB_OK;
}

int xb_fx_quads(xb_real (*veil)[7], xb_real (*quad)[3])
{
    if (!veil && !quad)
        return XB_ERR_ARG;
    fx_quads(veil, quad);
    return XB_OK;
}

int xb_frame(const xb_state *s, xb_frame_out *out)
{
    if (!ready(s) || !out)
        return XB_ERR_ARG;
    return frame_build(s, 0, out);
}

int xb_frame_from(const xb_state *s, const xb_real eye[3], const xb_real target[3],
                  const xb_real up[3], xb_frame_out *out)
{
    xb_camera cam;
    int i;

    if (!ready(s) || !eye || !target || !up || !out)
        return XB_ERR_ARG;
    camera_at(s, s->timing.t, &cam);
    for (i = 0; i < 3; i++) {
        cam.eye[i] = eye[i];
        cam.target[i] = target[i];
        cam.up[i] = up[i];
    }
    /* camera_set_view 0x8005B2C0 (Camera.md 3) */
    camera_view_toward(cam.eye, cam.target, cam.up, cam.view);
    m4_inverse_rigid(cam.view, cam.view_inv);
    for (i = 0; i < 16; i++)
        if (cam.view[i] != cam.view[i])      /* no direction, or along `up` */
            return XB_ERR_ARG;
    return frame_build(s, &cam, out);
}

int xb_frame_setup(const xb_state *s, uint32_t pass, xb_frame_out *out)
{
    if (!ready(s) || !out || pass >= XB_SETUP_PASSES)
        return XB_ERR_ARG;
    return frame_setup(s, pass, out);
}

size_t xb_audio_size(void)
{
    return audio_size();
}

int xb_audio_init(xb_state *s, void *memory, size_t size)
{
    if (!ready(s) || !memory || size < audio_size())
        return XB_ERR_ARG;
    if (!aligned(memory))
        return XB_ERR_ALIGN;
    s->sound = (xb_sound *)memory;
    /* 0x800579B8 starts the sound only in the long version (Audio.md 1) */
    audio_start(s, s->sound, !(s->flags & XB_START_SHORT));
    return XB_OK;
}

int xb_audio(xb_state *s, int16_t *buffer, uint32_t frames)
{
    uint32_t i;

    if (!ready(s) || (!buffer && frames))
        return XB_ERR_ARG;
    if (audio_ready(s->sound)) {
        audio_read(s, s->sound, buffer, frames);
    } else {
        for (i = 0; i < 2 * frames; i++)
            buffer[i] = 0;
    }
    return XB_OK;
}

int xb_audio_block(xb_state *s, xb_audio_out *out)
{
    if (!ready(s) || !audio_ready(s->sound) || !out)
        return XB_ERR_ARG;
    audio_block(s, s->sound, out);
    return XB_OK;
}

int xb_audio_mute(xb_state *s, uint32_t mask)
{
    if (!ready(s) || !audio_ready(s->sound))
        return XB_ERR_ARG;
    audio_mute(s->sound, mask);
    return XB_OK;
}

int xb_get_audio(const xb_state *s, xb_audio_state *out)
{
    if (!ready(s) || !audio_ready(s->sound) || !out)
        return XB_ERR_ARG;
    audio_get(s->sound, out);
    return XB_OK;
}

int xb_audio_gp(xb_state *s, const int32_t *bin0, const int32_t *bin1, const int32_t *bin11,
                int32_t *left, int32_t *right)
{
    if (!ready(s) || !audio_ready(s->sound) || !bin0 || !bin1 || !bin11 || !left || !right)
        return XB_ERR_ARG;
    audio_gp(s, s->sound, bin0, bin1, bin11, left, right);
    return XB_OK;
}

int xb_audio_table(const xb_state *s, uint32_t table, int16_t *out, uint32_t count)
{
    if (!ready(s) || table >= XB_TABLES)
        return XB_ERR_ARG;
    return audio_table(s, table, out, count);
}
