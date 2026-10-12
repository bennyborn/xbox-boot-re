/* The draws of a frame, docs/decomp/FrameOrder.md.
 *
 * xb_frame lists what anim_update (steps U1 to U4) and anim_draw (D1 to
 * D5) send to the GPU for the frame of the last update, xb_frame_setup what
 * the set-up sends (A1, A2). Every entry names mesh, primitive, program,
 * combiners, textures, target, the render states in effect and the
 * constants; the values come from the subsystem files. Programs and
 * combiner blocks are named by their address in the ROM (GPU.md 3, 4);
 * the host owns their translation.
 *
 * The render states are those FrameOrder.md 5 lists per kind of draw; they
 * are what the animation's calls leave in effect, measured in the stream
 * (vectors/c8_gpu.json, `kinds`).
 */
#include "core_internal.h"

/* float32 constants of the kernel, address of each in the comment.
 * core/test/check_k6.py compares them with out/rom/kernel.img. */
#define K_ZERO      0.0f          /* 0x80012880 */
#define K_T_BLOB    0.6f          /* 0x80065E60 */
#define K_PI        3.1415927f    /* 0x80066AA4 */
#define K_HALF      0.5f          /* 0x80010A18 */
#define K_CUBE_NEAR 0.1f          /* 0x80012888 */
#define K_CUBE_FAR  400.0f        /* 0x800129D0 */
#define K_CUBE_T0   0.6f          /* 0x80066AAC */
#define K_CUBE_T1   0.25f         /* 0x8001289C */
#define K_CUBE_T2   4.5f          /* 0x80066AB0 */

/* GPU objects in the ROM (GPU.md 3, 4). */
#define VSH_CHAMBER      0x80065B60u
#define VSH_CHAMBER_BUMP 0x80065998u
#define VSH_FX_DEPTH     0x80065D28u
#define VSH_BLOB         0x800662E8u
#define VSH_DROP         0x80066470u
#define VSH_VEIL         0x80066690u
#define VSH_LOGO_D       0x80066868u
#define PSB_CHAMBER      0x800657A8u
#define PSB_CHAMBER_BUMP 0x800656B0u
#define PSB_FX_DEPTH     0x800658A0u
#define PSB_BLOB         0x800660F8u
#define PSB_DROP         0x800661F0u
#define PSB_VEIL         0x80066598u
#define PSB_LOGO_D       0x80066770u

#define TEX(t) ((uint8_t)(XB_SURF_TEX + (t)))

/* ---- emitter --------------------------------------------------------------- */

typedef struct emit {
    xb_frame_out *o;
    uint32_t      nd, np;
    xb_draw       spare;          /* written when the host's buffer is full */
} emit;

static uint32_t put(emit *e, const xb_real *v, uint32_t n)
{
    uint32_t at = e->np, i;

    if (e->o->params && at + n <= e->o->param_cap)
        for (i = 0; i < n; i++)
            e->o->params[at + i] = v[i];
    e->np += n;
    return at;
}

/* render states per kind of draw, FrameOrder.md 5 */
enum { ST_PRESENT, ST_CUBE, ST_LIGHT, ST_FX_TARGET, ST_CHAMBER, ST_BLOB_GLOW, ST_BLOB,
       ST_LOGO, ST_LOGO_QUAD, ST_LOGO_TEXT, ST_LOGO_IMAGE, ST_VEIL };

static void state(xb_render_state *r, int kind)
{
    r->alpha_test = 0;
    r->alpha_func = 0x206;
    r->alpha_ref = 1;
    r->blend = 0;
    r->blend_src = 1;
    r->blend_dst = 0;
    r->depth_test = 1;
    r->depth_func = 0x203;
    r->depth_write = 1;
    r->cull = 1;
    r->cull_face = 0x405;
    r->shadow_func = 0;
    r->colour_mask = 0x01010101u;
    r->pad[0] = r->pad[1] = r->pad[2] = 0;
    switch (kind) {
    case ST_PRESENT:
        r->depth_test = 0;
        r->cull = 0;
        break;
    case ST_LIGHT:
        r->colour_mask = 0;
        break;
    case ST_FX_TARGET:
        r->alpha_test = 1;
        r->blend = 1;
        break;
    case ST_CHAMBER:
        r->shadow_func = 0x204;
        break;
    case ST_BLOB_GLOW:
        r->blend = 1;
        r->blend_src = 0x302;
        r->blend_dst = 1;
        r->depth_write = 0;
        break;
    case ST_BLOB:
        r->blend = 1;
        r->blend_src = 0x302;
        r->blend_dst = 0x303;
        break;
    case ST_LOGO:
        r->cull_face = 0x404;
        break;
    case ST_LOGO_QUAD:
        r->blend = 1;
        r->blend_src = 0x302;
        r->blend_dst = 0x303;
        r->depth_test = 0;
        r->cull_face = 0x404;
        break;
    case ST_LOGO_TEXT:
        r->blend_src = 0x302;
        r->blend_dst = 0x303;
        r->depth_test = 0;
        r->cull_face = 0x404;
        break;
    case ST_LOGO_IMAGE:
        r->blend = 1;
        r->depth_write = 0;
        break;
    case ST_VEIL:
        r->alpha_test = 1;
        r->blend = 1;
        r->blend_src = 0x302;
        r->blend_dst = 1;
        r->depth_write = 0;
        break;
    default:
        break;
    }
}

static xb_draw *draw(emit *e, uint32_t step, uint32_t what, uint32_t mesh, uint32_t prim,
                     uint32_t first, uint32_t count, int kind)
{
    xb_draw *d = (e->o->draws && e->nd < e->o->draw_cap) ? &e->o->draws[e->nd] : &e->spare;
    int i;

    e->nd++;
    d->step = (uint16_t)step;
    d->what = (uint16_t)what;
    d->mesh = (uint16_t)mesh;
    d->part = 0;
    d->prim = (uint8_t)prim;
    d->detail = 0;
    d->color = XB_SURF_SCREEN;
    d->zeta = XB_SURF_SCREEN_DEPTH;
    for (i = 0; i < 4; i++)
        d->tex[i] = XB_SURF_NONE;
    d->flags = 0;
    d->first = first;
    d->count = count;
    d->program = 0;
    d->combiners = 0;
    state(&d->state, kind);
    d->factor = 0;
    d->world = d->view = d->proj = 0;
    d->vs[0].base = d->vs[0].count = d->vs[1].base = d->vs[1].count = 0;
    d->vs[0].first = d->vs[1].first = 0;
    d->ps_first = d->ps_count = 0;
    d->vtx_first = d->vtx_count = 0;
    return d;
}

static void run(xb_draw *d, int i, uint32_t base, uint32_t count, uint32_t first)
{
    d->vs[i].base = (uint16_t)base;
    d->vs[i].count = (uint16_t)count;
    d->vs[i].first = first;
}

static void matrices(xb_draw *d, uint32_t world, uint32_t view, uint32_t proj)
{
    d->flags |= XB_DRAW_MATRICES;
    d->world = world;
    d->view = view;
    d->proj = proj;
}

/* Bild abschliessen 0x8003E550 (GPU.md 7): the library's own triangle. */
static void present(emit *e, uint32_t step)
{
    xb_draw *d = draw(e, step, XB_DRAW_PRESENT, XB_MESH_PRESENT, 5, 0, 3, ST_PRESENT);

    d->color = XB_SURF_DISPLAY;
    d->zeta = XB_SURF_NONE;
    d->tex[0] = XB_SURF_SCREEN;
}

/* ---- the chamber: A2, U1, U3, D1 ------------------------------------------- */

enum { CH_LIT, CH_CUBE, CH_DEPTH, CH_SIDE };

typedef struct chamber_pass {
    int       mode;               /* CH_* */
    uint32_t  step;
    uint32_t  side;               /* CH_SIDE */
    uint8_t   color, zeta;
    float     x20;
    const float (*light)[16];     /* CH_LIT, CH_CUBE: T of both sides */
    uint32_t  view, proj;         /* CH_SIDE: indices into params */
    uint32_t  tail;               /* CH_DEPTH: index of constants 16 to 19 */
} chamber_pass;

/* prim_group_draw 0x8005EE08 for one group in the pose's detail level. */
static int group(emit *e, const xb_state *s, const xb_camera *cam, const xb_chamber_pose *pose,
                 const chamber_pass *cp, uint32_t gi, uint32_t first_part)
{
    xb_form forms[8];
    xb_mesh_info info;
    uint32_t j, k, n, prims[3][3];
    int rc = chamber_mesh(s, gi, pose->detail, &info, 0, 0, 0, forms);

    if (rc != XB_OK)
        return rc;
    for (j = 0; s->chamber.parts[first_part + j].group == gi; j++) {
        const chamber_part *pp = &s->chamber.parts[first_part + j];
        uint32_t part = first_part + j, vs0 = 0, ps0 = 0, w0 = 0;
        xb_part_params pv;

        if (cp->mode == CH_SIDE && pp->side != cp->side) {
            if (part + 1 == CH_PARTS)
                break;
            continue;
        }
        if (cp->mode == CH_LIT || cp->mode == CH_CUBE) {
            chamber_part_draw(s, cam, pose, cp->x20, cp->light, cp->mode == CH_LIT, part, &pv);
            vs0 = put(e, &pv.vs[0][0], 48);
            ps0 = put(e, &pv.ps[0][0], 12);
        } else {
            float w[16], vp[16], m[16];
            xb_real t[4][4];
            int r, c;

            chamber_world(s, pose, part, w);
            if (cp->mode == CH_SIDE) {
                w0 = put(e, w, 16);
            } else {
                m4_mul(cam->view, cam->proj, vp);
                m4_mul(w, vp, m);
                for (r = 0; r < 4; r++)
                    for (c = 0; c < 4; c++)
                        t[r][c] = m[c * 4 + r];
                vs0 = put(e, &t[0][0], 16);
            }
        }
        n = chamber_part_prims(info.type, &forms[pp->form < 0 ? 0 : pp->form], prims);
        for (k = 0; k < n; k++) {
            int st = cp->mode == CH_LIT ? ST_CHAMBER : cp->mode == CH_CUBE ? ST_CUBE :
                     cp->mode == CH_DEPTH ? ST_FX_TARGET : ST_LIGHT;
            xb_draw *d = draw(e, cp->step, XB_DRAW_CHAMBER, XB_MESH_CHAMBER + gi, prims[k][0],
                              prims[k][1], prims[k][2], st);

            d->part = (uint16_t)part;
            d->detail = (uint8_t)pose->detail;
            d->flags |= XB_DRAW_INDEXED;
            d->color = cp->color;
            d->zeta = cp->zeta;
            switch (cp->mode) {
            case CH_LIT:
            case CH_CUBE:
                if (chamber_group_format(gi)) {
                    d->program = VSH_CHAMBER_BUMP;
                    d->combiners = PSB_CHAMBER_BUMP;
                    d->tex[0] = TEX(XB_TEX_BUMP);
                } else {
                    d->program = VSH_CHAMBER;
                    d->combiners = PSB_CHAMBER;
                }
                d->tex[1] = d->tex[2] = TEX(chamber_group_cube128(gi) ? XB_TEX_CUBE128 : XB_TEX_CUBE64);
                d->tex[3] = pp->side ? XB_SURF_LIGHT1 : XB_SURF_LIGHT0;
                run(d, 0, 0, 12, vs0);
                d->ps_first = ps0;
                d->ps_count = 3;
                break;
            case CH_DEPTH:
                d->program = VSH_FX_DEPTH;
                d->combiners = PSB_FX_DEPTH;
                run(d, 0, 0, 4, vs0);
                run(d, 1, 16, 4, cp->tail);
                break;
            default:
                d->program = 0x2;
                matrices(d, w0, cp->view, cp->proj);
                break;
            }
        }
        if (part + 1 == CH_PARTS)
            break;
    }
    return XB_OK;
}

static int chamber(emit *e, const xb_state *s, const xb_camera *cam, const xb_chamber_pose *pose,
                   const chamber_pass *cp)
{
    /* chamber_draw and chamber_draw_depth take the slots 0 to 5,
     * chamber_light_view the slots 3, 4, 2, 1 (Chamber.md 7, 8). */
    static const uint8_t all[6] = { 0, 1, 2, 3, 4, 5 }, sides[4] = { 3, 4, 2, 1 };
    const uint8_t *order = cp->mode == CH_SIDE ? sides : all;
    uint32_t n = cp->mode == CH_SIDE ? 4 : 6, i, p, first[XB_CHAMBER_GROUPS];
    int rc;

    for (i = 0, p = 0; i < XB_CHAMBER_GROUPS; i++) {
        first[i] = p;
        while (p < CH_PARTS && s->chamber.parts[p].group == i)
            p++;
    }
    for (i = 0; i < n; i++) {
        rc = group(e, s, cam, pose, cp, order[i], first[order[i]]);
        if (rc != XB_OK)
            return rc;
    }
    return XB_OK;
}

/* ---- the logo: U4, D3 -------------------------------------------------------- */

static void logo(emit *e, const xb_state *s, const xb_camera *cam, float t, int to_texture,
                 uint32_t view, uint32_t proj, uint32_t ident)
{
    static const uint8_t tex[3] = { XB_TEX_GRADIENT_A, XB_TEX_RADIAL, XB_TEX_GRADIENT_C };
    uint32_t step = to_texture ? XB_STEP_U4 : XB_STEP_D3, world, i;
    uint8_t color = to_texture ? XB_SURF_LOGO_TARGET : XB_SURF_SCREEN;
    uint8_t zeta = to_texture ? XB_SURF_LOGO_DEPTH : XB_SURF_SCREEN_DEPTH;
    xb_logo_mesh_desc md;
    xb_logo_params lp;
    xb_draw *d;

    logo_draw(s, &s->logo, cam, t, to_texture, &lp);
    world = put(e, lp.world, 16);
    if (lp.solid) {
        /* 5.1: A, B, C with fixed function 0x102, D with its program */
        for (i = 0; i < 3; i++) {
            logo_mesh_info(XB_LOGO_A + i, &md);
            d = draw(e, step, XB_DRAW_LOGO_A + i, XB_MESH_LOGO + XB_LOGO_A + i, 5, 0, md.indices,
                     ST_LOGO);
            d->flags |= XB_DRAW_INDEXED;
            d->program = 0x102;
            d->tex[0] = TEX(tex[i]);
            matrices(d, world, view, proj);
            d->color = color;
            d->zeta = zeta;
        }
        logo_mesh_info(XB_LOGO_D, &md);
        d = draw(e, step, XB_DRAW_LOGO_D, XB_MESH_LOGO + XB_LOGO_D, 5, 0, md.indices, ST_LOGO);
        d->flags |= XB_DRAW_INDEXED;
        d->program = VSH_LOGO_D;
        d->combiners = PSB_LOGO_D;
        run(d, 0, 0, 9, put(e, &lp.d_vs[0][0], 36));
        d->color = color;
        d->zeta = zeta;
    } else {
        /* 5.2: the picture on the veil's quad, all three slots the identity */
        d = draw(e, step, XB_DRAW_LOGO_IMAGE, XB_MESH_VEIL, 7, 0, 4, ST_LOGO_IMAGE);
        d->program = 0x202;
        d->tex[0] = XB_SURF_LOGO;
        matrices(d, ident, ident, ident);
    }
    if (lp.text) {
        /* 5.3: Q1 and Q2 with the opacity, then the lettering */
        for (i = 0; i < 2; i++) {
            logo_mesh_info(XB_LOGO_Q1 + i, &md);
            d = draw(e, step, XB_DRAW_LOGO_Q1 + i, XB_MESH_LOGO + XB_LOGO_Q1 + i, 5, 0, md.indices,
                     ST_LOGO_QUAD);
            d->flags |= XB_DRAW_INDEXED | XB_DRAW_FACTOR;
            d->program = 0x102;
            d->tex[0] = TEX(XB_TEX_TM);
            d->factor = lp.quad_colour;
            matrices(d, world, view, proj);
            d->color = color;
            d->zeta = zeta;
        }
        logo_mesh_info(XB_LOGO_E, &md);
        d = draw(e, step, XB_DRAW_LOGO_E, XB_MESH_LOGO + XB_LOGO_E, 5, 0, md.indices, ST_LOGO_TEXT);
        d->flags |= XB_DRAW_INDEXED | XB_DRAW_FACTOR;
        d->program = 0x2;
        d->tex[0] = TEX(XB_TEX_TM);
        d->factor = lp.text_colour;
        matrices(d, put(e, lp.text_world, 16), view, proj);
        d->color = color;
        d->zeta = zeta;
    }
}

/* ---- a frame ----------------------------------------------------------------- */

static int finish(emit *e)
{
    e->o->draw_count = e->nd;
    e->o->param_count = e->np;
    if (!e->o->draws || e->nd > e->o->draw_cap || !e->o->params || e->np > e->o->param_cap)
        return XB_ERR_SPACE;
    return XB_OK;
}

/* `from` is NULL for the animation's own camera (xb_frame). Otherwise it is
 * the camera of the host (xb_frame_from): the draws take its eye, view and
 * projection, the veil the track values of its eye; the switches this+0xD
 * and this+0xE stay those `from` carries, which are the animation's. */
int frame_build(const xb_state *s, const xb_camera *from, xb_frame_out *out)
{
    const timing_state *tm = &s->timing;
    float t = tm->t, x20 = tm->x20, ident[16], light[2][16], lview[16], lproj[16], c[3];
    static const float refs[2][3] = { { 0.0f, 0.0f, -30.0f }, { 0.0f, 0.0f, 40.0f } };
    uint32_t i, id, view, proj, updated = tm->steps != 0;
    xb_chamber_pose pose;
    xb_fx_params fx;
    xb_camera cam;
    chamber_pass cp;
    track_state track;
    xb_track trk;
    xb_draw *d;
    emit e;
    int rc, lit, veil, alpha;

    e.o = out;
    e.nd = e.np = 0;
    track = s->track;
    if (from) {
        cam = *from;
        camera_track(s, &track, cam.eye, s->blob.center);
    } else {
        camera_at(s, t, &cam);
    }
    for (i = 0; i < 16; i++) {
        out->view[i] = cam.view[i];
        out->proj[i] = cam.proj[i];
    }
    m4_ident(ident);
    id = put(&e, ident, 16);
    view = put(&e, cam.view, 16);
    proj = put(&e, cam.proj, 16);
    trk.dist = track.dist;
    trk.azimuth = track.azimuth;
    trk.elevation = track.elevation;
    trk.blob_dist = track.blob_dist;
    trk.fx_draws = track.fx_draws;
    fx_draw(&cam, s->camera.aspect, t, x20, &trk, &fx);
    for (i = 0; i < 32; i++)
        light[i / 16][i % 16] = 0.0f;
    if (cam.chamber_on)
        chamber_at(s, t, &pose);

    /* anim_update, FrameOrder.md 3 */
    if (updated && cam.chamber_on) {
        uint8_t target = s->track.fx_flip ? XB_SURF_FX_B : XB_SURF_FX_A;
        uint32_t qv, qm, dv;

        /* U1: chamber_prepass 0x800600F5, side 0 then side 1 */
        for (i = 0; i < 2; i++) {
            blob_weighted_center(&s->blob, refs[i], x20, c, 0);
            chamber_light_view(c, i, light[i]);
            chamber_light_camera(c, i, lview, lproj);
            cp.mode = CH_SIDE;
            cp.step = XB_STEP_U1;
            cp.side = i;
            cp.color = XB_SURF_NONE;
            cp.zeta = i ? XB_SURF_LIGHT1 : XB_SURF_LIGHT0;
            cp.x20 = x20;
            cp.light = 0;
            cp.view = put(&e, lview, 16);
            cp.proj = put(&e, lproj, 16);
            cp.tail = 0;
            rc = chamber(&e, s, &cam, &pose, &cp);
            if (rc != XB_OK)
                return rc;
        }
        /* U2: fx_render_offscreen 0x8005A194, the quad */
        qm = put(&e, &fx.quad_m[0][0], 16);
        qv = put(&e, &fx.quad_vs[0][0], 16);
        d = draw(&e, XB_STEP_U2, XB_DRAW_FX_QUAD, XB_MESH_FX_QUAD, 7, 0, 4, ST_FX_TARGET);
        d->program = VSH_FX_DEPTH;
        d->combiners = PSB_FX_DEPTH;
        d->color = target;
        d->zeta = XB_SURF_FX_DEPTH;
        run(d, 0, 0, 4, qm);
        run(d, 1, 16, 4, qv);
        /* U3: the chamber's depth; constants 18 and 19 stay */
        dv = put(&e, &fx.depth_vs[0][0], 8);
        put(&e, &fx.quad_vs[2][0], 8);
        cp.mode = CH_DEPTH;
        cp.step = XB_STEP_U3;
        cp.side = 0;
        cp.color = target;
        cp.zeta = XB_SURF_FX_DEPTH;
        cp.tail = dv;
        rc = chamber(&e, s, &cam, &pose, &cp);
        if (rc != XB_OK)
            return rc;
    }
    /* U4: logo_render_to_texture 0x8005C366 */
    if (updated && cam.logo_on && s->logo.rtt)
        logo(&e, s, &cam, t, 1, view, proj, id);

    /* anim_draw 0x80056D0B */
    lit = cam.chamber_on && x20 > K_ZERO;
    veil = fx_veil_on(cam.chamber_on, t, x20);
    /* The alpha test fx_render_offscreen switched on stays until
     * chamber_draw switches it off (FrameOrder.md 5). */
    alpha = updated && cam.chamber_on && !lit;
    if (lit) {
        cp.mode = CH_LIT;
        cp.step = XB_STEP_D1;
        cp.side = 0;
        cp.color = XB_SURF_SCREEN;
        cp.zeta = XB_SURF_SCREEN_DEPTH;
        cp.x20 = x20;
        cp.light = (const float (*)[16])light;
        cp.view = cp.proj = cp.tail = 0;
        rc = chamber(&e, s, &cam, &pose, &cp);
        if (rc != XB_OK)
            return rc;
    }
    if (cam.chamber_on && t >= K_T_BLOB) {
        xb_blob_params bp;
        uint32_t mv, mp, dp;

        /* D2: blob_draw 0x800583BF */
        blob_draw(&s->blob, &cam, t, tm->x1c, x20, &bp);
        d = draw(&e, XB_STEP_D2, XB_DRAW_BLOB_GLOW, XB_MESH_INLINE, 7, 0, 4, ST_BLOB_GLOW);
        d->program = 0x102;
        d->tex[0] = TEX(XB_TEX_FALLOFF);
        d->flags |= XB_DRAW_FACTOR;
        d->factor = bp.glow_colour;
        d->state.alpha_test = (uint8_t)alpha;
        d->vtx_first = put(&e, &bp.glow[0][0], 20);
        d->vtx_count = 20;
        matrices(d, id, view, proj);
        mv = put(&e, &bp.mesh_vs[0][0], 52);
        mp = put(&e, &bp.mesh_ps[0][0], 8);
        d = draw(&e, XB_STEP_D2, XB_DRAW_BLOB_MESH, XB_MESH_BLOB, 6, 0, XB_BLOB_INDICES, ST_BLOB);
        d->flags |= XB_DRAW_INDEXED;
        d->program = VSH_BLOB;
        d->combiners = PSB_BLOB;
        d->tex[0] = d->tex[1] = TEX(XB_TEX_CUBE64);
        d->state.alpha_test = (uint8_t)alpha;
        run(d, 0, 0, 13, mv);
        d->ps_first = mp;
        d->ps_count = 2;
        dp = put(&e, &bp.drop_ps[0][0], 12);
        for (i = 0; i < bp.drops; i++) {
            /* constants 4 to 9 once, 10 to 13 per drop */
            uint32_t dv = put(&e, &bp.drop_vs[0][0], 24);

            put(&e, &bp.drop[i][0][0], 16);
            d = draw(&e, XB_STEP_D2, XB_DRAW_BLOB_DROP, XB_MESH_DROP, 6, 0, XB_BLOB_SMALL_INDICES,
                     ST_BLOB);
            d->part = (uint16_t)i;
            d->flags |= XB_DRAW_INDEXED;
            d->program = VSH_DROP;
            d->combiners = PSB_DROP;
            d->tex[0] = d->tex[1] = TEX(XB_TEX_CUBE64);
            d->state.alpha_test = (uint8_t)alpha;
            run(d, 0, 4, 10, dv);
            d->ps_first = dp;
            d->ps_count = 3;
        }
    }
    out->clear_depth = cam.logo_on && (veil || cam.chamber_on);
    if (cam.logo_on)
        logo(&e, s, &cam, t, 0, view, proj, id);
    if (veil) {
        /* D4: fx_draw 0x80059BCD; it swaps the targets and reads the one
         * fx_render_offscreen drew in this frame */
        d = draw(&e, XB_STEP_D4, XB_DRAW_VEIL, XB_MESH_VEIL, 7, 0, 4, ST_VEIL);
        d->program = VSH_VEIL;
        d->combiners = PSB_VEIL;
        d->tex[0] = s->track.fx_flip ? XB_SURF_FX_B : XB_SURF_FX_A;
        d->tex[1] = TEX(XB_TEX_NOISE0);
        d->tex[2] = TEX(XB_TEX_NOISE1);
        d->tex[3] = TEX(XB_TEX_NOISE2);
        d->state.depth_func = (uint16_t)fx.depth_func;
        run(d, 0, 0, 6, put(&e, &fx.veil_vs[0][0], 24));
        d->ps_first = put(&e, &fx.veil_ps[0][0], 8);
        d->ps_count = 2;
        if (fx.glow) {
            d = draw(&e, XB_STEP_D4, XB_DRAW_GLOW, XB_MESH_VEIL, 7, 0, 4, ST_VEIL);
            d->program = 0x202;
            d->tex[0] = TEX(XB_TEX_FALLOFF);
            d->flags |= XB_DRAW_FACTOR;
            d->factor = fx.glow_colour;
            d->state.depth_func = (uint16_t)fx.depth_func;
            matrices(d, put(&e, fx.glow_world, 16), id, id);
        }
    }
    present(&e, XB_STEP_D5);
    return finish(&e);
}

/* cube_face_view 0x80061418 (Textures.md 5): from the origin along +x, -x,
 * +y, -y, +z, -z. */
static void face_view(uint32_t f, xb_camera *cam)
{
    static const float dir[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 },
                                     { 0, 0, 1 }, { 0, 0, -1 } };
    static const float up[6][3] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 },
                                    { 0, 1, 0 }, { 0, 1, 0 } };
    int a;

    for (a = 0; a < 3; a++) {
        cam->eye[a] = 0.0f;
        cam->target[a] = dir[f][a];
        cam->up[a] = up[f][a];
    }
    camera_view_toward(cam->eye, cam->target, cam->up, cam->view);
    m4_inverse_rigid(cam->view, cam->view_inv);
    camera_perspective(r32f((double)K_PI * K_HALF), 1.0f, K_CUBE_NEAR, K_CUBE_FAR, cam->proj);
}

int frame_setup(const xb_state *s, uint32_t pass, xb_frame_out *out)
{
    float light[2][16], t;
    xb_chamber_pose pose;
    chamber_pass cp;
    xb_camera cam;
    uint32_t i;
    emit e;
    int rc;

    e.o = out;
    e.nd = e.np = 0;
    out->clear_depth = 0;
    if (pass == 0) {
        /* A1: device_setup 0x80057B7E after clearing to black */
        camera_at(s, s->timing.t, &cam);
        for (i = 0; i < 16; i++) {
            out->view[i] = cam.view[i];
            out->proj[i] = cam.proj[i];
        }
        present(&e, XB_STEP_A1);
        return finish(&e);
    }
    /* A2: texgen_chamber_cube 0x800614E9, chamber_update(5.35) and
     * chamber_draw with both switches 0; the light views are not drawn yet
     * and their matrices are still 0 */
    camera_at(s, s->timing.t, &cam);
    face_view(pass - 1, &cam);
    for (i = 0; i < 16; i++) {
        out->view[i] = cam.view[i];
        out->proj[i] = cam.proj[i];
    }
    for (i = 0; i < 32; i++)
        light[i / 16][i % 16] = 0.0f;
    t = r32f(((double)K_CUBE_T0 + K_CUBE_T1) + K_CUBE_T2);
    chamber_at(s, t, &pose);
    cp.mode = CH_CUBE;
    cp.step = XB_STEP_A2;
    cp.side = 0;
    cp.color = (uint8_t)(XB_SURF_CUBE + pass - 1);
    cp.zeta = XB_SURF_CUBE_DEPTH;
    cp.x20 = 0.0f;
    cp.light = (const float (*)[16])light;
    cp.view = cp.proj = cp.tail = 0;
    rc = chamber(&e, s, &cam, &pose, &cp);
    if (rc != XB_OK)
        return rc;
    return finish(&e);
}
