/* Sound (docs/decomp/Audio.md): the sample tables, the sequencer, the voice
 * layer, the voices as xemu's VP plays them and the frame routine of the GP
 * program.
 *
 * Three kinds of code stand side by side here, and they have different
 * evidence (docs/core.md 4):
 * - tables, sequencer and voice layer follow the disassembly and are
 *   bit-exact against the guest (Audio.md 2, 3, 5);
 * - the frame routine of the GP program is its arithmetic in 24-bit fixed
 *   point, bit-exact against the program run on xemu's DSP core (6.2, 6.6);
 * - how a voice turns its registers into samples is not in the BIOS. That
 *   part follows xemu's reading of the hardware (Audio.md 7, 8), in the
 *   same steps as tools/xemu/audio_render.py, and is an approximation. */
#include "core_internal.h"

#define BLOCK   XB_AUDIO_BLOCK
#define NCH     XB_AUDIO_CHANNELS

/* ---- own arithmetic helpers, no model in the binary --------------------- */

#define M_PI_D   3.14159265358979323846
#define LN2      0.69314718055994530942
#define LN2_HI   0.693147180369123816490     /* upper bits of ln 2 */
#define LN2_LO   1.90821492927058770002e-10  /* ln 2 - LN2_HI */
#define LN10     2.30258509299404568402

/* Round to the nearest integer, ties to even, for |v| < 2^51. */
static double rint_even(double v)
{
    const double big = 6755399441055744.0;   /* 1.5 * 2^52 */

    return (v + big) - big;
}

static double floor_pos(double v)            /* floor for 0 <= v < 2^31 */
{
    return (double)(uint32_t)v;
}

/* e^x for |x| < 700: x = k ln 2 + r with |r| <= ln 2 / 2, Taylor series to
 * r^13, then times 2^k. */
static double a_exp(double x)
{
    union { double d; uint64_t u; } scale;
    double kf = rint_even(x * (1.0 / LN2));
    double r = (x - kf * LN2_HI) - kf * LN2_LO;
    double p = 1.0;
    int n;

    for (n = 13; n >= 1; n--)
        p = 1.0 + p * r / (double)n;
    scale.u = (uint64_t)(1023 + (int)kf) << 52;
    return p * scale.d;
}

/* ---- data of the ROM ---------------------------------------------------- */

#define SEQ_BASE     0x8006E908u
#define SEQ_END      0x8006F000u
#define SEQ_EVENTS   0x8006EFE0u    /* 8-byte records: handler, mask, list */
#define PTR_DESC     0x800A7BB0u    /* 11 pointers to sample descriptors */
#define PTR_EVMAX    0x800A7C10u    /* highest event number */
#define PTR_END      0x800A7C14u
#define ENV_BASE     0x800A7980u
#define ENV_END      0x800A7BB0u
#define N_DESC       11u

/* Tables by the address the descriptors name (Audio.md 2, 5.2). */
#define T_NOISE      0x800A1B78u
#define T_SINE       0x80093B78u
#define T_FM         0x80083B70u
#define T_SAW        0x80083328u
#define T_PCM_SIGNED 0x80078898u
#define T_REVERSED   0x800970F8u
#define T_PCM_A      0x800A5B80u
#define T_PCM_B      0x80093C78u

/* Doubles of 0x80061AC1 (Audio.md 2), as bit patterns. */
#define K_SCALE_BITS 0x40DFFFC000000000ull   /* 0x80012A08: 32767.0 */
#define K_SINE_BITS  0x3FA921F9F01B866Eull   /* 0x80012A00: 0.04908734375 */
#define K_MOD_BITS   0x3FB921F9F01B866Eull   /* 0x800129F8: 0.0981746875 */
#define K_CAR_BITS   0x3FC921F9F01B866Eull   /* 0x800129E8: 0.196349375 */
#define K_IDX_BITS   0x3F10000000000000ull   /* 0x800129F0: 2^-14 */
#define NOISE_SEED   0x3EBu                  /* 0x80061AC7 */
#define NOISE_MUL    0x343FDu                /* 0x80061A9E */
#define NOISE_ADD    0x269EC3u

static double dbits(uint64_t u)
{
    union { double d; uint64_t u; } v;

    v.u = u;
    return v.d;
}

static uint32_t rd_u16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

/* 0x80055F8C truncates; the caller stores the low 16 bits. */
static int16_t trunc16(double x)
{
    return (int16_t)(uint16_t)(uint32_t)(int32_t)x;
}

static double sine_of(double x)
{
    double sn, cs;

    x87_fsincos(x, &sn, &cs);
    return sn;
}

static const uint32_t table_addr[XB_TABLES] = {
    T_NOISE, T_SINE, T_FM, T_SAW, T_PCM_SIGNED, T_REVERSED, T_PCM_A, T_PCM_B
};
static const uint32_t table_len[XB_TABLES] = {
    0x2000u, 0x80u, 0x8000u, 0x80u, 0x5540u, 0x5540u, 0xEB8u, 0x1A3Fu
};

/* Value i of a table that needs no state. The noise is a sequence and is
 * not served here. */
static int16_t table_value(const xb_state *s, uint32_t addr, uint32_t i)
{
    switch (addr) {
    case T_SINE:
        return trunc16(sine_of((double)i * dbits(K_SINE_BITS)) * dbits(K_SCALE_BITS));
    case T_FM: {
        /* 0x80061B37-0x80061B59: m counts up to 0x4000 and down again,
         * before the value is computed */
        int32_t m = i < 0x4000u ? (int32_t)i + 1 : 0x7FFF - (int32_t)i;
        double mod = sine_of(dbits(K_MOD_BITS) * (double)i) * (double)m * dbits(K_IDX_BITS);

        return trunc16(sine_of((double)i * dbits(K_CAR_BITS) + mod) * dbits(K_SCALE_BITS));
    }
    case T_SAW:
        return (int16_t)(((int32_t)i - 64) * 512);
    case T_PCM_SIGNED:
        return (int16_t)((int32_t)(int8_t)pack_at(s, PACK_PCM_SIGNED, 0x80066AC0u)[i] * 256);
    case T_REVERSED:
        /* read from 0x80083318 downwards; the first word lies behind the
         * end of 0x80078898 and is 0 in the image and in every run */
        return i == 0 ? 0 : table_value(s, T_PCM_SIGNED, 0x5540u - i);
    case T_PCM_A:
        return (int16_t)(((int32_t)pack_at(s, PACK_PCM_A, 0x8006C008u)[i] - 0x80) * 256);
    case T_PCM_B:
        return (int16_t)(((int32_t)pack_at(s, PACK_PCM_B, 0x8006CEC8u)[i] - 0x80) * 256);
    default:
        return 0;
    }
}

static void noise_table(int16_t *out)
{
    uint32_t seed = NOISE_SEED, i;

    for (i = 0; i < 0x2000u; i++) {
        seed = seed * NOISE_MUL + NOISE_ADD;
        out[i] = (int16_t)((seed >> 16) & 0x7FFFu);
    }
}

int audio_table(const xb_state *s, uint32_t table, int16_t *out, uint32_t count)
{
    uint32_t i, n = table_len[table];

    if (!out)
        return (int)n;
    if (count < n)
        return XB_ERR_ARG;
    if (table == XB_TABLE_NOISE) {
        noise_table(out);
    } else {
        for (i = 0; i < n; i++)
            out[i] = table_value(s, table_addr[table], i);
    }
    return (int)n;
}

/* ---- state ---------------------------------------------------------------- */

#define SND_MAGIC   0x78624B37u
#define TRACKS      30              /* 0x8006E908 */
#define RING        16
#define GP_LINES    12              /* A, B, C, D, s1 to s8; L1 and L2 are dead */
#define GP_RING_MAX 68512u

typedef struct seq_track {          /* track record, Audio.md 3.2 */
    uint32_t ptr;                   /* +0x8 */
    int16_t  wait;                  /* +0xC */
    uint16_t last;                  /* +0xE */
    uint8_t  channel;               /* +0x13 */
    int8_t   next;                  /* +0x0, as an index, -1: end */
    uint8_t  pad[2];
} seq_track;

typedef struct seq_channel {        /* channel record of layer 0, Audio.md 3.2 */
    uint16_t sample;                /* +0x0 */
    uint8_t  b2, vol;               /* +0x2, +0x3 */
    uint16_t pitch;                 /* +0x6 */
    uint16_t loop_count[4];         /* +0x8 */
    uint32_t loop_start[4];         /* +0x10 */
    uint32_t depth;                 /* +0x20 */
    int16_t  transpose;             /* +0x32 */
    uint16_t f34, f36;              /* +0x34, +0x36 */
} seq_channel;

typedef struct env_state {          /* xemu: EACUR/EFCUR, count and level */
    uint8_t  cur, lvl;
    uint16_t count;
} env_state;

/* What the library keeps for the voice of a channel (the object the
 * pointers at 0x80083B28 lead to, `this` of 0x8004EE84, 0x8004EF2D and
 * 0x8004EFC6), and behind it what the hardware keeps while it plays. Of
 * the library's object: +0x8 bit 0 is r.alloc (no method is written
 * without it, 0x8005082A, 0x800509DA, 0x8004F0B8), bit 1 is r.on
 * (0x80052064); +0x88 is r.misc with mrel in its bits 0 to 11; +0x8C to
 * +0x98 are r.env0, r.enva, r.env1, r.envf; +0xA0 is r.lfo_env; +0xA8 and
 * +0xAC are r.fca; +0xB0 is start. */
typedef struct lib_voice {
    xb_voice  r;                    /* what the library has sent */
    uint32_t  data;                 /* table the sample comes from */
    uint32_t  mrel;                 /* +0x88 bits 0-11: release rate of the second envelope */
    uint32_t  start;                /* +0xB0: start states, bits 24 to 31 of VOICE_ON */
    uint8_t   loop, fmode, pad[2];
    env_state ea, ef;
    double    pos;
    double    svf_b, svf_h, svf_l;
} lib_voice;

typedef struct gp_state {
    int32_t  lp[10];                /* states of the one-pole filters */
    uint32_t len[GP_LINES];         /* ring lengths in samples */
    uint32_t first[GP_LINES];       /* start of each ring in `ring` */
    uint32_t w[GP_LINES];           /* write position */
} gp_state;

struct xb_sound {
    uint32_t      magic;
    uint32_t      playing;          /* 0 in the short version */
    uint32_t      blocks;
    uint32_t      mute;
    /* sequencer */
    uint16_t      now;              /* 0x800A7958 */
    uint8_t       ring[RING];       /* 0x800A7912 */
    uint8_t       ring_rd, ring_wr; /* 0x800A7910, 0x800A7911 */
    int8_t        active, free;     /* heads of the two lists */
    seq_track     track[TRACKS];
    seq_channel   ch[NCH];
    /* what the last tick did */
    uint32_t      n_cmds, n_calls;
    xb_audio_cmd  cmd[NCH];
    xb_audio_call call[NCH];
    /* voices */
    lib_voice   v[NCH];
    double        win_cos[16], win_sin[16];   /* window of the resampler */
    /* output held for xb_audio */
    int16_t       held[BLOCK][2];
    uint32_t      held_pos;
    gp_state      gp;
    int16_t       noise[0x2000];
    int32_t       gp_ring[GP_RING_MAX];
};

/* ---- sequencer, Audio.md 3 ------------------------------------------------- */

static int seq_inside(uint32_t a, uint32_t n)
{
    return a >= SEQ_BASE && a <= SEQ_END - n;
}

/* A word of the sequencer data; 15 (the command that halts) outside. */
static uint32_t seq_u16(const xb_state *s, uint32_t a)
{
    return seq_inside(a, 2) ? rd_u16(pack_at(s, PACK_SEQUENCE, a)) : 15u;
}

static uint32_t seq_u32(const xb_state *s, uint32_t a)
{
    return seq_inside(a, 4) ? rd_u32(pack_at(s, PACK_SEQUENCE, a)) : 0u;
}

static uint32_t ptr_u32(const xb_state *s, uint32_t a)
{
    return a >= PTR_DESC && a <= PTR_END - 4 ? rd_u32(pack_at(s, PACK_SEQ_PTRS, a)) : 0u;
}

static void channel_reset(seq_channel *c)       /* 0x800616C7 */
{
    int i;

    c->sample = 0;
    c->b2 = 0;
    c->vol = 0x7F;
    c->pitch = 0;
    for (i = 0; i < 4; i++) {
        c->loop_count[i] = 0;
        c->loop_start[i] = 0;
    }
    c->depth = 0;
    c->transpose = 0;
    c->f34 = c->f36 = 0;
}

static void track_release_all(xb_sound *a)      /* 0x8006170B for layer 0 */
{
    while (a->active >= 0) {
        int8_t t = a->active;

        a->active = a->track[t].next;
        a->track[t].next = a->free;
        a->free = t;
    }
}

static void track_release_channel(xb_sound *a, uint32_t channel)   /* 0x80061732 */
{
    int8_t *link = &a->active;

    while (*link >= 0) {
        int8_t t = *link;

        if (a->track[t].channel == channel) {
            *link = a->track[t].next;
            a->track[t].next = a->free;
            a->free = t;
        } else {
            link = &a->track[t].next;
        }
    }
}

/* Event handlers 0 (0x80061762) and 1 (0x800617D0), Audio.md 3.1. Handler 1
 * starts a track on every channel of the mask; a new track goes to the
 * front of the list (0x80062638). */
static void seq_event(const xb_state *s, xb_sound *a, uint32_t e)
{
    uint32_t rec = SEQ_EVENTS + 8u * e;
    uint32_t handler = seq_u16(s, rec), mask = seq_u16(s, rec + 2), list = seq_u32(s, rec + 4);
    uint32_t c, k = 0;

    if (handler == 0) {
        track_release_all(a);
        for (c = 0; c < NCH; c++)
            channel_reset(&a->ch[c]);
        return;
    }
    if (handler != 1)
        return;                 /* handler 2 is layer 1, which no event uses */
    for (c = 0; c < NCH; c++) {
        seq_track *t;
        int8_t i;

        if (!((mask >> c) & 1u))
            continue;
        track_release_channel(a, c);
        channel_reset(&a->ch[c]);
        i = a->free;
        if (i < 0)
            break;
        t = &a->track[i];
        a->free = t->next;
        t->ptr = ptr_u32(s, list + 4u * k++);
        t->wait = 0;
        t->last = a->now;
        t->channel = (uint8_t)c;
        t->next = a->active;
        a->active = i;
    }
}

static void seq_call(xb_sound *a, uint32_t channel, uint32_t fn, int32_t a0, int32_t a1)
{
    xb_audio_call *c;

    if (a->n_calls >= NCH)
        return;
    c = &a->call[a->n_calls++];
    c->a0 = a0;
    c->a1 = a1;
    c->tick = a->now;
    c->channel = (uint8_t)channel;
    c->fn = (uint8_t)fn;
    c->effect = 0;
    c->pad[0] = c->pad[1] = c->pad[2] = 0;
}

/* Sample descriptor n (0x14 bytes, Audio.md 5.2), found through the
 * pointers at 0x800A7BB0. 0 when n is not a sample. */
static uint32_t descriptor(const xb_state *s, uint32_t n)
{
    uint32_t d = n < N_DESC ? ptr_u32(s, PTR_DESC + 4u * n) : 0u;

    return seq_inside(d, 0x14) ? d : 0u;
}

/* One command of a track (table 0x800669D8, Audio.md 3.4). Returns 0 for
 * command 15, which also ends the tick. */
static int seq_step(const xb_state *s, xb_sound *a, seq_track *t)
{
    seq_channel *c = &a->ch[t->channel];
    uint32_t op = seq_u16(s, t->ptr), next = t->ptr;
    uint32_t w1 = seq_u16(s, t->ptr + 2), w2 = seq_u16(s, t->ptr + 4);

    if (a->n_cmds < NCH) {
        xb_audio_cmd *m = &a->cmd[a->n_cmds++];

        m->addr = t->ptr;
        m->tick = a->now;
        m->channel = t->channel;
        m->op = (uint8_t)op;
    }
    switch (op) {
    case 0:                                         /* 0x80061E54 */
    case 13:                                        /* 0x80062469 */
        t->wait = (int16_t)(uint16_t)((uint32_t)(uint16_t)t->wait + w1);
        if (op == 0)
            seq_call(a, t->channel, XB_CALL_STOP, 0, 0);
        next += 4;
        break;
    case 1:                                         /* 0x80061D75 */
    case 12: {                                      /* 0x8006238A */
        uint32_t p, d = descriptor(s, c->sample);
        int32_t lib;

        t->wait = (int16_t)(uint16_t)((uint32_t)(uint16_t)t->wait + w2);
        p = (((w1 & 0xFFu) << 8) + (uint32_t)(uint16_t)c->transpose) & 0xFFFFu;
        c->pitch = (uint16_t)p;
        /* 0x800618DF, 0x80061952: 0x155 per semitone, Audio.md 5.3 */
        lib = (int32_t)((p & 0xFFu) * 0x155u / 0xFFu) + ((int32_t)(p >> 8) - 60) * 0x155;
        if (op == 1)
            seq_call(a, t->channel, XB_CALL_NOTE, lib, d && seq_u16(s, d + 10) != 0);
        else
            seq_call(a, t->channel, XB_CALL_PITCH, lib, 0);
        next += 6;
        break;
    }
    case 2:                                         /* 0x80061EC1 */
        next = seq_u32(s, t->ptr + 2);
        break;
    case 3:                                         /* 0x80061F02 */
        next += 4;
        c->loop_count[c->depth & 3u] = (uint16_t)w1;
        c->loop_start[c->depth & 3u] = next;
        c->depth++;
        break;
    case 4: {                                       /* 0x80061F5E */
        uint32_t top = (c->depth - 1u) & 3u;

        next += 2;
        c->loop_count[top] = (uint16_t)(c->loop_count[top] - 1u);
        if (c->loop_count[top] == 0)
            c->depth--;
        else
            next = c->loop_start[top];
        break;
    }
    case 5:                                         /* 0x80061FB2 */
        c->sample = (uint16_t)w1;
        c->vol = 0;
        c->b2 = 0;
        seq_call(a, t->channel, XB_CALL_SAMPLE, (int32_t)w1, 0);
        next += 4;
        break;
    case 6:                                         /* 0x80062026 */
        c->b2 = (uint8_t)w1;
        seq_call(a, t->channel, XB_CALL_B2, 0, 0);
        next += 4;
        break;
    case 9:                                         /* 0x8006219E */
        c->vol = (uint8_t)(c->vol + w1);
        seq_call(a, t->channel, XB_CALL_LEVEL, 200 - 30 * (int32_t)c->vol, 0);
        next += 4;
        break;
    case 10:                                        /* 0x8006224A */
        c->transpose = (int16_t)(uint16_t)((uint32_t)(uint16_t)c->transpose + w1);
        next += 4;
        break;
    case 11:                                        /* 0x80062284 */
        c->transpose = (int16_t)(uint16_t)w1;
        next += 4;
        break;
    case 15:                                        /* 0x800624ED */
        return 0;
    case 16:                                        /* 0x800622BE */
    case 17:                                        /* 0x80062324 */
        c->f34 = (uint16_t)(op == 16 ? c->f34 + w1 : w1);
        c->f36 = (uint16_t)w2;
        seq_call(a, t->channel, XB_CALL_FILTER, (int32_t)c->f34 + 0x8000, (int32_t)c->f36);
        next += 6;
        break;
    default:
        /* 0x80061D71 returns 1 and leaves the pointer; the commands the
         * tracks do not use (Audio.md 3.4) are not rebuilt and do the same */
        break;
    }
    t->ptr = next;
    return 1;
}

static void seq_tick(const xb_state *s, xb_sound *a)       /* 0x8006165E, 0x80060252 */
{
    int8_t i;

    a->now++;
    a->n_cmds = a->n_calls = 0;
    if (a->ring_rd != a->ring_wr) {
        uint32_t e = a->ring[a->ring_rd];

        a->ring_rd = (uint8_t)((a->ring_rd + 1u) % RING);       /* 0x80060166 */
        if (e > ptr_u32(s, PTR_EVMAX))
            return;                                             /* 0x80060270 */
        seq_event(s, a, e);
    }
    for (i = a->active; i >= 0; i = a->track[i].next) {
        seq_track *t = &a->track[i];

        t->wait = (int16_t)(uint16_t)((uint32_t)(uint16_t)t->wait + (uint32_t)(uint16_t)(t->last - a->now));
        t->last = a->now;
        if (t->wait >= 0)
            continue;
        if (!seq_step(s, a, t))
            return;
    }
}

/* ---- voice layer, Audio.md 5 ----------------------------------------------- */

#define FMT_PLAIN   0x5000E0E6u     /* Audio.md 5.2 */
#define FMT_REVERB  0x5000E0C5u     /* channels 3 and 5 */
#define VBIN_PLAIN  0x14830820u
#define VBIN_REVERB 0x10622C20u
#define LEVEL_SHIFT 600             /* 0x80051085, Audio.md 5.4 */

static int with_reverb(uint32_t ch)             /* 0x80061C7B-0x80061CD5 */
{
    return ch == 3 || ch == 5;
}

/* 0x8004EB1D: eight attenuations in 1/64 dB from the level and the mix
 * levels 0x80061AC1 gave the voice, packed into VOLA, VOLB, VOLC. */
static void volumes(uint32_t ch, int32_t level, xb_voice *r)
{
    static const int32_t even[3] = { 0, -600, -10000 }, odd[3] = { -600, 0, -10000 },
                         rev[3] = { 0, -100, 0 };
    const int32_t *mix = with_reverb(ch) ? rev : (ch & 1u) ? odd : even;
    uint32_t v[8], b;

    for (b = 0; b < 8; b++) {
        int32_t lv = b < 3 && mix[b] != -10000 ? level - LEVEL_SHIFT + mix[b] : -10000;
        int32_t att = lv < 0 ? -lv * 64 / 100 : 0;

        v[b] = att > 0xFFF ? 0xFFFu : (uint32_t)att;
    }
    r->vola = (v[6] & 0xFu) | v[0] << 4 | (v[7] & 0xFu) << 16 | v[1] << 20;
    r->volb = ((v[6] >> 4) & 0xFu) | v[2] << 4 | ((v[7] >> 4) & 0xFu) << 16 | v[3] << 20;
    r->volc = ((v[6] >> 8) & 0xFu) | v[4] << 4 | ((v[7] >> 8) & 0xFu) << 16 | v[5] << 20;
}

/* Bits of `word` under `mask` replaced by `value`, as the two functions
 * below do it with xor, and, xor. */
static uint32_t field(uint32_t word, uint32_t mask, uint32_t value)
{
    return (word & ~mask) | (value & mask);
}

/* 0x8004EF2D (kind 1) and 0x8004EE84 (kind 0) on one block of ten words.
 * Each writes its own fields and leaves the rest of the words standing:
 * rates in bits 0 to 11, times in bits 12 to 23, a byte on top. */
static void envelope_block(const xb_state *s, uint32_t at, lib_voice *v)
{
    const uint8_t *p;
    uint32_t w[10], i;

    if (at < ENV_BASE || at > ENV_END - 0x28u)
        return;
    p = pack_at(s, PACK_ENVBLOCKS, at);
    for (i = 0; i < 10; i++)
        w[i] = rd_u32(p + 4u * i);
    if (w[0] == 1) {
        v->start = field(v->start, 0xF0000000u, w[1] << 28);            /* 0x8004EF33 */
        v->r.lfo_env = field(v->r.lfo_env, 0xFFFu, w[6]);               /* 0x8004EF4C */
        v->r.env0 = field(v->r.env0, 0xFFFu, w[3]);                     /* 0x8004EF60 */
        v->r.env0 = field(v->r.env0, 0xFFF000u, w[2] << 12);
        v->r.enva = field(v->r.enva, 0xFFFu, w[5]);                     /* 0x8004EF8E */
        v->r.enva = field(v->r.enva, 0xFFF000u, w[4] << 12);
        v->r.enva = field(v->r.enva, 0xFF000000u, w[7] << 24);          /* 0x8004EFB7 */
    } else {
        v->start = field(v->start, 0x0F000000u, w[1] << 24);            /* 0x8004EE8A */
        v->mrel = w[6] & 0xFFFu;                                        /* 0x8004EEA1 */
        v->r.env0 = field(v->r.env0, 0xFF000000u, w[8] << 24);          /* 0x8004EEB5 */
        v->r.env1 = field(v->r.env1, 0xFFFu, w[3]);                     /* 0x8004EEBE */
        v->r.env1 = field(v->r.env1, 0xFFF000u, w[2] << 12);
        v->r.env1 = field(v->r.env1, 0xFF000000u, w[9] << 24);          /* 0x8004EEEC */
        v->r.envf = field(v->r.envf, 0xFFFu, w[5]);                     /* 0x8004EEF5 */
        v->r.envf = field(v->r.envf, 0xFFF000u, w[4] << 12);
        v->r.envf = field(v->r.envf, 0xFF000000u, w[7] << 24);          /* 0x8004EF1E */
    }
}

static void env_start(env_state *e, uint32_t state, uint32_t delay, uint32_t hold)
{
    e->cur = (uint8_t)state;
    if (state == 1)
        e->count = (uint16_t)(delay * 16u);
    else if (state == 2)
        e->count = 0;
    else if (state == 3)
        e->count = (uint16_t)(hold * 16u);
}

static void voice_call(const xb_state *s, xb_sound *a, xb_audio_call *c)
{
    lib_voice *v = &a->v[c->channel];
    uint32_t ch = c->channel;

    switch (c->fn) {
    case XB_CALL_SAMPLE: {                          /* 0x80061A08 */
        uint32_t d = descriptor(s, (uint32_t)c->a0);

        if (!d)
            break;
        v->r.sample = (uint8_t)c->a0;
        v->data = seq_u32(s, d);
        /* 0x80061A41: the length in bytes is read as a 16-bit word */
        v->r.ebo = seq_u16(s, d + 4) / 2u - 1u;
        v->loop = seq_u16(s, d + 10) != 0;
        envelope_block(s, seq_u32(s, d + 12), v);
        envelope_block(s, seq_u32(s, d + 16), v);
        v->r.fmt = with_reverb(ch) ? FMT_REVERB : FMT_PLAIN;
        v->r.vbin = with_reverb(ch) ? VBIN_REVERB : VBIN_PLAIN;
        v->r.misc = 0x800000u | (uint32_t)v->fmode << 16 | v->mrel;
        v->r.pitch = 0;
        volumes(ch, v->r.level, &v->r);
        v->r.alloc = 1;
        break;
    }
    case XB_CALL_LEVEL:                             /* 0x80061D48 */
        v->r.level = c->a0;
        if (v->r.alloc)
            volumes(ch, c->a0, &v->r);
        break;
    case XB_CALL_FILTER:                            /* 0x800619B1, 0x8004EFC6 */
        v->r.fca = ((uint32_t)c->a0 & 0xFFFFu) | (uint32_t)c->a1 << 16;
        v->fmode = 1;
        if (v->r.alloc)
            v->r.misc = 0x800000u | 1u << 16 | v->mrel;
        break;
    case XB_CALL_NOTE:                              /* 0x800618DF, 0x80051F2E */
    case XB_CALL_PITCH:                             /* 0x80061952 */
        if (!v->r.alloc)
            break;
        v->r.pitch = ((uint32_t)c->a0 & 0xFFFFu) << 16;
        if (c->fn != XB_CALL_NOTE)
            break;
        v->r.fmt = (with_reverb(ch) ? FMT_REVERB : FMT_PLAIN) | (uint32_t)(c->a1 != 0) << 25;
        if (!v->r.on) {
            /* only a voice that is not running for the library is started */
            v->pos = 0.0;
            env_start(&v->ea, v->start >> 28, (v->r.env0 >> 12) & 0xFFFu, (v->r.enva >> 12) & 0xFFFu);
            env_start(&v->ef, (v->start >> 24) & 0xFu, (v->r.env1 >> 12) & 0xFFFu,
                      (v->r.envf >> 12) & 0xFFFu);
            v->svf_b = v->svf_h = v->svf_l = 0.0;
            v->r.on = v->r.active = 1;
            c->effect = 1;
        }
        break;
    case XB_CALL_STOP:                              /* 0x80061994 */
        if (v->r.on) {
            v->ea.count = (uint16_t)(v->r.lfo_env * 16u);
            v->ea.cur = 6;
            v->ef.count = (uint16_t)(v->mrel * 16u);
            v->ef.cur = 6;
            c->effect = 1;
        }
        break;
    default:
        break;
    }
}

/* ---- voices after xemu's VP, Audio.md 7 and 8 ------------------------------ */

#define K_DECAY_LN  (-1.1201627358857538e-4)    /* ln 0.99988799, xemu's decay base */
#define K_RELEASE   6.91                        /* xemu: level falls by e^-6.91 */
#define K_FC_MIN    0.003906                    /* xemu: limits of cut-off and Q */
#define K_Q_MIN     0.079407
#define TAPS        16                          /* own choice: windowed sinc */

/* One step of an envelope per block, xemu's voice_step_envelope. Returns
 * the value 0 to 1; *off is set when the voice ends. */
static double env_step(env_state *e, uint32_t attack, uint32_t hold, uint32_t decay,
                       uint32_t sustain, uint32_t rr, int *off)
{
    double value;

    switch (e->cur) {
    case 1:                                         /* delay */
        e->lvl = 0;
        if (e->count == 0)
            e->cur = 2;
        else
            e->count--;
        return 0.0;
    case 2:                                         /* attack */
        if (attack == 0 || e->count > attack * 16u)
            value = 255.0;
        else
            value = (double)((uint32_t)e->count * 0xFFu / (attack * 16u));
        e->lvl = (uint8_t)value;
        if (e->count == attack * 16u) {
            e->cur = 3;
            e->count = (uint16_t)(hold * 16u);
        } else {
            e->count++;
        }
        return value / 255.0;
    case 3:                                         /* hold */
        e->lvl = 0xFF;
        if (e->count == 0) {
            e->cur = 4;
            e->count = (uint16_t)(decay * 16u);
        } else {
            e->count--;
        }
        return 1.0;
    case 4:                                         /* decay */
        if (decay == 0)
            value = 0.0;
        else
            value = 255.0 * a_exp(K_DECAY_LN * ((double)((int32_t)(decay * 16u) - (int32_t)e->count) *
                                                 4096.0 / (double)decay));
        if (value <= (double)sustain + 0.2 || value > 255.0) {
            e->cur = 5;
        } else {
            e->count--;
            e->lvl = (uint8_t)value;
        }
        return value / 255.0;
    case 5:                                         /* sustain */
        e->count = 0;
        e->lvl = (uint8_t)sustain;
        return (double)sustain / 255.0;
    case 6: {                                       /* release */
        double pos;

        if (rr == 0)
            e->count = 0;
        if (e->count == 0) {
            e->cur = 7;
            return 0.0;
        }
        pos = 1.0 - (double)e->count / ((double)rr * 16.0);
        pos = pos < 0.0 ? 0.0 : pos > 1.0 ? 1.0 : pos;
        value = a_exp(-K_RELEASE * pos) * (double)e->lvl;
        e->count--;
        return value / 255.0;
    }
    case 7:                                         /* the voice goes off */
        *off = 1;
        return 0.0;
    default:
        return 0.0;
    }
}

static double sample_at(const xb_state *s, const xb_sound *a, const lib_voice *v, int64_t idx)
{
    int64_t length = (int64_t)v->r.ebo + 1;

    if (v->loop) {
        idx %= length;
        if (idx < 0)
            idx += length;
    } else if (idx < 0 || idx >= length) {
        return 0.0;
    }
    if ((uint32_t)idx >= 0x8000u)
        return 0.0;                 /* no table is longer */
    if (v->data == T_NOISE)
        return (double)a->noise[idx & 0x1FFF];
    return (double)table_value(s, v->data, (uint32_t)idx);
}

/* 32 output samples from position pos in steps of `step`: a sinc of 16
 * taps under a raised-cosine window, cut off at min(1, 1/step). The period
 * of a loop is EBO + 1 (Audio.md 8). Returns how many samples lie before
 * the end of a sample without loop. */
static uint32_t resample(const xb_state *s, const xb_sound *a, lib_voice *v, double step,
                         double *out)
{
    double length = (double)v->r.ebo + 1.0;
    double fc = 1.0 / step < 1.0 ? 1.0 / step : 1.0;
    double tap_sin[TAPS], tap_cos[TAPS], p = v->pos;
    uint32_t i, n = BLOCK;
    int j;

    for (j = 0; j < TAPS; j++)
        x87_fsincos(M_PI_D * fc * (double)(j - 7), &tap_sin[j], &tap_cos[j]);
    for (i = 0; i < BLOCK; i++) {
        double k, frac, sf, cf, sw, cw, sum = 0.0;

        p = v->pos + step * (double)i;
        if (!v->loop && n == BLOCK && p >= length)
            n = i;
        k = floor_pos(p);
        frac = p - k;
        x87_fsincos(M_PI_D * fc * frac, &sf, &cf);
        x87_fsincos(M_PI_D * frac / 8.0, &sw, &cw);
        for (j = 0; j < TAPS; j++) {
            double x = frac - (double)(j - 7), y = fc * x;
            /* sin(pi fc (frac - j)) and cos(pi (frac - j) / 8) from the sums of angles */
            double sinc = y == 0.0 ? 1.0 : (sf * tap_cos[j] - cf * tap_sin[j]) / (M_PI_D * y);
            double win = 0.5 + 0.5 * (cw * a->win_cos[j] + sw * a->win_sin[j]);

            sum += sample_at(s, a, v, (int64_t)k + (j - 7)) * (fc * sinc * win);
        }
        out[i] = sum / 32768.0;
    }
    for (i = n; i < BLOCK; i++)
        out[i] = 0.0;
    p += step;
    if (v->loop) {
        p -= floor_pos(p / length) * length;
        if (p < 0.0)
            p += length;
        else if (p >= length)
            p -= length;
    }
    v->pos = p;
    return n;
}

static double attenuate(uint32_t vol)           /* xemu: 1/64 dB, 0xFFF is silent */
{
    vol &= 0xFFFu;
    return vol == 0xFFFu ? 0.0 : a_exp((double)vol * (LN10 / -1280.0));
}

/* One block of a voice: out[] before any gain, gain[] into bins 0, 1, 11.
 * Returns 0 when the voice is silent. */
static int voice_block(const xb_state *s, xb_sound *a, uint32_t ch, double *out, double *gain)
{
    lib_voice *v = &a->v[ch];
    const xb_voice *r = &v->r;
    double efv, eav, step;
    uint32_t vol[3], n, i;
    int off = 0, ps;

    if (!r->active)
        return 0;
    efv = env_step(&v->ef, r->env1 & 0xFFFu, (r->envf >> 12) & 0xFFFu, r->envf & 0xFFFu,
                   r->envf >> 24, v->mrel, &off);
    ps = (int)(int8_t)(uint8_t)(r->env0 >> 24);
    step = a_exp(LN2 * (((double)(int16_t)(uint16_t)(r->pitch >> 16) + (double)(ps * 32) * efv) / 4096.0));
    off = 0;
    eav = env_step(&v->ea, r->env0 & 0xFFFu, (r->enva >> 12) & 0xFFFu, r->enva & 0xFFFu,
                   r->enva >> 24, r->lfo_env, &off);
    if (off) {
        v->r.active = 0;
        return 0;
    }
    n = resample(s, a, v, step, out);
    if (n < BLOCK)
        v->r.active = 0;
    if (v->fmode & 1u) {
        /* state variable low-pass, xemu's svf.h */
        double fcf = a_exp(LN2 * ((double)(int16_t)(uint16_t)(r->fca & 0xFFFFu) / 4096.0));
        double qf = (double)(r->fca >> 16) / 32768.0, qn, b = v->svf_b, h = v->svf_h, l = v->svf_l;

        fcf = fcf < K_FC_MIN ? K_FC_MIN : fcf > 1.0 ? 1.0 : fcf;
        qf = qf < K_Q_MIN ? K_Q_MIN : qf > 1.0 ? 1.0 : qf;
        qn = __builtin_sqrt(qf / 2.0 + 0.01);
        for (i = 0; i < BLOCK; i++) {
            double x = qn * out[i];

            b = b - b * b * b * 0.001;
            h = x - l - qf * b;
            b = b + fcf * h;
            l = l + fcf * b;
            out[i] = l < -1.0 ? -1.0 : l > 1.0 ? 1.0 : l;
        }
        v->svf_b = b;
        v->svf_h = h;
        v->svf_l = l;
    }
    vol[0] = (r->vola >> 4) & 0xFFFu;
    vol[1] = r->vola >> 20;
    vol[2] = (r->volb >> 4) & 0xFFFu;
    /* headroom 1 of the bins 0 to 30 (Audio.md 5.1): half */
    gain[0] = eav * attenuate(vol[0]) / 2.0;
    gain[1] = eav * attenuate(vol[1]) / 2.0;
    gain[2] = with_reverb(ch) ? eav * attenuate(vol[2]) / 2.0 : 0.0;
    return 1;
}

/* A mix bin as the GP gets it: xemu's float_to_24b. */
static int32_t to_24(double x)
{
    double v = x * 8388608.0;

    if (v >= 8388607.0)
        return 0x7FFFFF;
    if (v <= -8388608.0)
        return -0x800000;
    return (int32_t)rint_even(v);
}

/* ---- the frame routine of the GP program, Audio.md 6.2 and 6.6 ------------- */

#define GP_BASE     0x80053568u
#define GP_WORDS    0x625u

static int32_t sx24(uint32_t w)
{
    w &= 0xFFFFFFu;
    return (w & 0x800000u) ? (int32_t)w - 0x1000000 : (int32_t)w;
}

static uint32_t gp_word(const xb_state *s, uint32_t p)
{
    return p < GP_WORDS ? rd_u32(pack_at(s, PACK_GP_PROGRAM, GP_BASE + 4u * p)) & 0xFFFFFFu : 0u;
}

/* X:a of the parameter block as the program's set-up writes it, from the
 * immediates of its moves (Audio.md 6.6). */
static int32_t gp_x(const xb_state *s, uint32_t a)
{
    if (a >= 0x1DEu && a <= 0x1E3u)
        a = a - 0x1DEu + 0x67u;                     /* copied at P:0x0235 */
    if (a >= 0x2Eu && a <= 0x8Bu)
        return sx24(gp_word(s, 0x050Au + 3u * (a - 0x2Eu)));
    if (a >= 0x8Cu && a <= 0x92u)
        return sx24(gp_word(s, 0x04AFu + 3u * (a - 0x8Cu)));
    if (a >= 0x93u && a <= 0x96u)
        return sx24(gp_word(s, 0x04C9u + 3u * (a - 0x93u)));
    return 0;
}

/* The accumulator: 56 bits, here sign-extended in 64. */
static int64_t acc56(int64_t a)
{
    uint64_t u = (uint64_t)a & 0xFFFFFFFFFFFFFFull;

    return (u & 0x80000000000000ull) ? (int64_t)(u | 0xFF00000000000000ull) : (int64_t)u;
}

static int64_t gp_mul(int32_t x, int32_t y)         /* fractional product, 48 bits */
{
    return (int64_t)x * (int64_t)y * 2;
}

static int64_t gp_load(int32_t x)                   /* a 24-bit word into a1 */
{
    return (int64_t)x * 0x1000000;
}

static int64_t gp_rnd(int64_t a)                    /* round a0 away, ties to even */
{
    a = acc56(a + 0x800000);
    if (((uint64_t)a & 0xFFFFFFu) == 0)
        a = (int64_t)((uint64_t)a & ~0x1000000ull);
    return (int64_t)((uint64_t)a & ~0xFFFFFFull);
}

static int32_t gp_store(int64_t a)                  /* a1 with limiting */
{
    int64_t v = a >= 0 ? a / 0x1000000 : -((-a + 0xFFFFFF) / 0x1000000);

    return v > 0x7FFFFF ? 0x7FFFFF : v < -0x800000 ? -0x800000 : (int32_t)v;
}

static void blk_scale(int32_t *d, int32_t c)                        /* P:0x00FD */
{
    uint32_t i;

    for (i = 0; i < BLOCK; i++)
        d[i] = gp_store(gp_mul(d[i], c));
}

static void blk_macr(const int32_t *src, int32_t *d, int32_t c)     /* P:0x0103 */
{
    uint32_t i;

    for (i = 0; i < BLOCK; i++)
        d[i] = gp_store(gp_rnd(acc56(gp_load(d[i]) + gp_mul(src[i], c))));
}

static void blk_add(const int32_t *src, int32_t *d)                 /* P:0x010A */
{
    uint32_t i;

    for (i = 0; i < BLOCK; i++)
        d[i] = gp_store(gp_load(d[i]) + gp_load(src[i]));
}

static void blk_sum(const int32_t *x, const int32_t *y, int32_t *d) /* P:0x0111 */
{
    uint32_t i;

    for (i = 0; i < BLOCK; i++)
        d[i] = gp_store(gp_load(x[i]) + gp_load(y[i]));
}

/* P:0x03A2: y = c[blk+1] * y_old + c[blk+2] * x, state at X:blk. */
static void blk_lp(const xb_state *s, int32_t *state, uint32_t blk, int32_t *d)
{
    int32_t ca = gp_x(s, blk + 1), cb = gp_x(s, blk + 2);
    uint32_t i;

    for (i = 0; i < BLOCK; i++) {
        *state = gp_store(acc56(gp_mul(ca, *state) + gp_mul(cb, d[i])));
        d[i] = *state;
    }
}

/* P:0x0367: one side of the output. */
static void blk_out(const xb_state *s, uint32_t c0, const int32_t *s_lo, const int32_t *s_hi,
                    const int32_t *e_lo, const int32_t *e_hi, int32_t *d)
{
    int32_t k0 = gp_x(s, c0), k1 = gp_x(s, c0 + 1), y0 = gp_x(s, 0x1E2);
    uint32_t i;

    for (i = 0; i < BLOCK; i++) {
        int64_t a = gp_mul(s_hi[i], k1);

        a = gp_rnd(acc56(a + gp_mul(s_lo[i], k0)));
        a = gp_rnd(acc56(a + gp_mul(e_hi[i], y0)));
        a = gp_rnd(acc56(a + gp_mul(e_lo[i], y0)));
        /* the fifth product is with the dead line and adds 0 */
        d[i] = gp_store(acc56((int64_t)((uint64_t)a << 7)));
    }
}

/* Taps of the delay lines: line and delay in samples for the buffers
 * X:0x204 + 0x20 k, k = 0 to 28 (Audio.md 6.2; 0 = the whole ring). */
static const struct { uint8_t line; uint16_t delay; } gp_tap[29] = {
    { 0, 336 }, { 0, 392 }, { 0, 372 }, { 0, 428 }, { 0, 460 },
    { 0, 524 }, { 0, 640 }, { 0, 792 }, { 0, 988 },
    { 0, 524 }, { 0, 640 }, { 0, 792 }, { 0, 988 },
    { 0, 1920 }, { 0, 1016 },
    { 1, 9044 }, { 1, 0 }, { 2, 10960 }, { 2, 11908 }, { 3, 10960 }, { 3, 6512 },
    { 4, 304 }, { 5, 284 }, { 6, 296 }, { 7, 328 },
    { 8, 0 }, { 9, 0 }, { 10, 0 }, { 11, 0 }
};
/* Buffer each line is written from at the end of a frame. */
static const uint8_t gp_from[GP_LINES] = { 0, 14, 16, 18, 1, 2, 3, 4, 5, 6, 7, 8 };

static void gp_start(const xb_state *s, xb_sound *a)
{
    uint32_t i, at = 0;

    for (i = 0; i < 10; i++)
        a->gp.lp[i] = 0;
    for (i = 0; i < GP_LINES; i++) {
        /* size - 1 in bytes at X:0x2F + 2 i, four bytes a sample */
        uint32_t n = ((uint32_t)gp_x(s, 0x2Fu + 2u * i) + 1u) / 4u;

        if (n < BLOCK || at + n > GP_RING_MAX)
            n = BLOCK;
        a->gp.len[i] = n;
        a->gp.first[i] = at;
        a->gp.w[i] = 0;
        at += n;
    }
    for (i = 0; i < GP_RING_MAX; i++)
        a->gp_ring[i] = 0;
}

/* One frame, P:0x0242-0x0366. b0, b1, b11 are the mix bins; b0 and b1
 * become left and right. */
static void gp_frame(const xb_state *s, xb_sound *a, int32_t *b0, int32_t *b1, const int32_t *b11)
{
    int32_t B[29][BLOCK], in[BLOCK], m[4][BLOCK], left[BLOCK], right[BLOCK];
    gp_state *g = &a->gp;
    uint32_t i, j, k;

    /* P:0x0396: every tap is read before anything is written */
    for (k = 0; k < 29; k++) {
        uint32_t line = gp_tap[k].line, len = g->len[line];
        uint32_t at = (g->w[line] + len - gp_tap[k].delay % len) % len;
        const int32_t *ring = a->gp_ring + g->first[line];

        for (i = 0; i < BLOCK; i++)
            B[k][i] = ring[(at + i) % len];
    }

    /* P:0x024B: bin 11 times the short immediate, which lands as 0x400000 */
    for (i = 0; i < BLOCK; i++)
        in[i] = b11[i];
    blk_scale(in, (int32_t)((gp_word(s, 0x024B) >> 8) & 0xFFu) << 16);
    blk_lp(s, &g->lp[0], 0x6E, in);                 /* P:0x0250 */

    /* P:0x025C: s1 to s4 = their tap of A + A[336] + g * their own tap */
    for (k = 0; k < 4; k++)
        blk_add(B[0], B[1 + k]);
    for (k = 0; k < 4; k++)
        blk_macr(B[21 + k], B[1 + k], gp_x(s, 0x93 + k));

    /* P:0x0272: the taps of s5 to s8 take two taps of A each, X:0x5D on */
    for (k = 0; k < 4; k++)
        for (j = 0; j < 2; j++)
            blk_macr(B[5 + 2 * k + j], B[25 + k], gp_x(s, 0x5D + 2 * k + j));

    /* P:0x0285: the sum of the eight taps of A, B, C and D times X:0x66.
     * The two halves times X:0x65 = 0 would feed the lines L1 and L2, which
     * therefore stay 0 and are left out here. */
    blk_sum(B[14], B[15], B[9]);
    blk_sum(B[13], B[16], B[10]);
    blk_add(B[17], B[9]);
    blk_add(B[18], B[10]);
    blk_add(B[20], B[9]);
    blk_add(B[19], B[10]);
    blk_sum(B[9], B[10], B[11]);
    blk_scale(B[11], gp_x(s, 0x66));

    /* P:0x02D7: A[1016] -> B, B[12000] -> C, C[11908] -> D, each low-passed */
    blk_lp(s, &g->lp[1], 0x71, B[14]);
    blk_lp(s, &g->lp[2], 0x74, B[16]);
    blk_lp(s, &g->lp[3], 0x77, B[18]);

    /* P:0x02E5: s5 to s8 = matrix times their taps, rounded after every
     * product; row i uses X:0x8F - i + j */
    for (k = 0; k < 4; k++) {
        for (i = 0; i < BLOCK; i++) {
            int64_t acc = 0;

            for (j = 0; j < 4; j++)
                acc = gp_rnd(acc56(acc + gp_mul(gp_x(s, 0x8F - k + j), B[25 + j][i])));
            m[k][i] = gp_store(acc);
        }
    }
    for (k = 0; k < 4; k++) {
        blk_add(B[11], m[k]);                       /* P:0x02FD */
        blk_lp(s, &g->lp[4 + k], 0x7A + 3 * k, m[k]);   /* P:0x0305 */
    }

    /* P:0x0318: left from s1, s2 and the taps of s5, s6; right from s3, s4,
     * s7, s8 */
    blk_out(s, 0x1DE, B[1], B[2], B[25], B[26], left);
    blk_out(s, 0x1E0, B[3], B[4], B[27], B[28], right);
    blk_add(left, b0);                              /* P:0x0358 */
    blk_add(right, b1);

    /* P:0x039C: the write buffers go into their rings; every ring moves on */
    for (k = 0; k < GP_LINES; k++) {
        const int32_t *src = k == 0 ? in : k >= 8 ? m[k - 8] : B[gp_from[k]];
        int32_t *ring = a->gp_ring + g->first[k];
        uint32_t len = g->len[k];

        for (i = 0; i < BLOCK; i++)
            ring[(g->w[k] + i) % len] = src[i];
        g->w[k] = (g->w[k] + BLOCK) % len;
    }
}

/* ---- blocks and entry points ----------------------------------------------- */

size_t audio_size(void)
{
    return sizeof(xb_sound);
}

void audio_start(const xb_state *s, xb_sound *a, int playing)
{
    uint32_t c;
    int i;

    a->playing = (uint32_t)playing;
    a->blocks = 0;
    a->mute = 0;
    /* 0x8006033C: ring and time, tables, records chained, event 1 posted */
    a->now = 1;
    for (i = 0; i < RING; i++)
        a->ring[i] = 0;
    a->ring_rd = 0;
    a->ring[0] = 1;
    a->ring_wr = 1;
    a->active = -1;
    a->free = 0;
    for (i = 0; i < TRACKS; i++) {
        a->track[i].ptr = 0;
        a->track[i].wait = 0;
        a->track[i].last = 0;
        a->track[i].channel = 0;
        a->track[i].next = (int8_t)(i + 1 < TRACKS ? i + 1 : -1);
        a->track[i].pad[0] = a->track[i].pad[1] = 0;
    }
    a->n_cmds = a->n_calls = 0;
    for (c = 0; c < NCH; c++) {
        lib_voice *v = &a->v[c];
        xb_voice zero = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

        channel_reset(&a->ch[c]);
        v->r = zero;
        /* nothing is audible before the first level call: the start level
         * of the library is -600 and every bin is silent */
        v->r.vola = v->r.volb = v->r.volc = 0xFFFFFFFFu;
        v->data = 0;
        v->mrel = v->start = 0;
        v->loop = v->fmode = 0;
        v->pad[0] = v->pad[1] = 0;
        v->ea.cur = v->ea.lvl = v->ef.cur = v->ef.lvl = 0;
        v->ea.count = v->ef.count = 0;
        v->pos = v->svf_b = v->svf_h = v->svf_l = 0.0;
    }
    for (i = 0; i < TAPS; i++)
        x87_fsincos(M_PI_D * (double)(i - 7) / 8.0, &a->win_sin[i], &a->win_cos[i]);
    for (i = 0; i < (int)BLOCK; i++)
        a->held[i][0] = a->held[i][1] = 0;
    a->held_pos = BLOCK;
    noise_table(a->noise);
    gp_start(s, a);
    a->magic = SND_MAGIC;
}

static int32_t asr8(int32_t v)
{
    return v >= 0 ? v / 256 : -((-v + 255) / 256);
}

void audio_block(const xb_state *s, xb_sound *a, xb_audio_out *out)
{
    double mix[XB_AUDIO_BINS][BLOCK], one[BLOCK], gain[XB_AUDIO_BINS];
    int32_t bin[XB_AUDIO_BINS][BLOCK];
    uint32_t c, b, i, tick = 0;

    for (b = 0; b < XB_AUDIO_BINS; b++)
        for (i = 0; i < BLOCK; i++)
            mix[b][i] = 0.0;
    if (a->playing && a->blocks % XB_AUDIO_TICK_BLOCKS == 0) {
        /* The library learns of a voice's end on a way of its own (Audio.md
         * 5.5); here: by the next tick. */
        for (c = 0; c < NCH; c++)
            if (a->v[c].r.on && !a->v[c].r.active)
                a->v[c].r.on = 0;
        seq_tick(s, a);
        for (i = 0; i < a->n_calls; i++)
            voice_call(s, a, &a->call[i]);
        tick = a->now;
    }
    if (out) {
        out->block = a->blocks;
        out->tick = tick;
        out->n_cmds = tick ? a->n_cmds : 0;
        out->n_calls = tick ? a->n_calls : 0;
        for (i = 0; i < NCH; i++) {
            out->cmd[i] = a->cmd[i];
            out->call[i] = a->call[i];
        }
    }
    for (c = 0; c < NCH; c++) {
        int sounding = a->playing && voice_block(s, a, c, one, gain);

        if (sounding && ((a->mute >> c) & 1u))
            gain[0] = gain[1] = gain[2] = 0.0;
        for (b = 0; sounding && b < XB_AUDIO_BINS; b++)
            for (i = 0; i < BLOCK; i++)
                mix[b][i] += one[i] * gain[b];
        if (out) {
            for (i = 0; i < BLOCK; i++)
                out->voice[c][i] = sounding ? (xb_real)one[i] : 0.0f;
            for (b = 0; b < XB_AUDIO_BINS; b++)
                out->gain[c][b] = sounding ? (xb_real)gain[b] : 0.0f;
        }
    }
    for (b = 0; b < XB_AUDIO_BINS; b++)
        for (i = 0; i < BLOCK; i++)
            bin[b][i] = to_24(mix[b][i]);
    if (out)
        for (b = 0; b < XB_AUDIO_BINS; b++)
            for (i = 0; i < BLOCK; i++)
                out->bin[b][i] = bin[b][i];
    gp_frame(s, a, bin[0], bin[1], bin[2]);
    for (i = 0; i < BLOCK; i++) {
        /* xemu plays bits 8 to 23 of X:0x1400 and X:0x1420 (gp_ep.c) */
        a->held[i][0] = (int16_t)asr8(bin[0][i]);
        a->held[i][1] = (int16_t)asr8(bin[1][i]);
        if (out) {
            out->out[0][i] = bin[0][i];
            out->out[1][i] = bin[1][i];
        }
    }
    a->held_pos = out ? BLOCK : 0;
    a->blocks++;
}

void audio_read(const xb_state *s, xb_sound *a, int16_t *buffer, uint32_t frames)
{
    uint32_t i;

    for (i = 0; i < frames; i++) {
        if (a->held_pos >= BLOCK)
            audio_block(s, a, 0);
        buffer[2 * i] = a->held[a->held_pos][0];
        buffer[2 * i + 1] = a->held[a->held_pos][1];
        a->held_pos++;
    }
}

void audio_gp(const xb_state *s, xb_sound *a, const int32_t *bin0, const int32_t *bin1,
              const int32_t *bin11, int32_t *left, int32_t *right)
{
    int32_t b11[BLOCK];
    uint32_t i;

    for (i = 0; i < BLOCK; i++) {
        left[i] = sx24((uint32_t)bin0[i]);
        right[i] = sx24((uint32_t)bin1[i]);
        b11[i] = sx24((uint32_t)bin11[i]);
    }
    gp_frame(s, a, left, right, b11);
}

void audio_get(const xb_sound *a, xb_audio_state *out)
{
    uint32_t c;
    int8_t i;

    out->blocks = a->blocks;
    out->tick = a->now;
    out->tracks = 0;
    for (i = a->active; i >= 0; i = a->track[i].next)
        out->tracks++;
    out->mute = a->mute;
    for (c = 0; c < NCH; c++)
        out->voice[c] = a->v[c].r;
}

int audio_ready(const xb_sound *a)
{
    return a && a->magic == SND_MAGIC;
}

void audio_mute(xb_sound *a, uint32_t mask)
{
    a->mute = mask;
}
