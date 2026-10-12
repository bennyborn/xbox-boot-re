/* Host test of milestone K7: the sound against docs/vectors/c4_audio.json
 * and docs/vectors/c4_gp.json.
 *
 *     test_k7 <pack> <c4_audio.json>          (c4_gp.json is read next to it)
 *     test_k7 <pack> --tables <dir>
 *     test_k7 <pack> --run <dir> <blocks>
 *     test_k7 <pack> --gp <in> <out> <frames>
 *     test_k7 <pack> --wav <file> <blocks>
 *     test_k7 <pack> --digest
 *
 * First form:
 *   - the eight sample tables: number of values and SHA-256 against the
 *     guest's memory (section "tables");
 *   - the sequencer: every command of the run c4_seq in tick, channel,
 *     address and number ("commands/list"), every call into the voice layer
 *     with its arguments ("calls_model");
 *   - the voice layer: after the first sample call of each channel the
 *     values of 14 methods equal those the guest sent ("first_sample_config");
 *   - for every note and release, whether the core starts or releases the
 *     voice where the guest did ("voice_on", "voice_release"). This is
 *     printed, not checked: the run stops at 2243 breakpoints while xemu's
 *     voices run on, so its voices end earlier in sequencer time than in an
 *     undisturbed run (docs/core.md 4);
 *   - the GP stage: the impulse responses of the bins 0, 1 and 11 as
 *     float32, SHA-256 against c4_gp.json ("ir_sha256"); no other output;
 *   - rules of the interface: silence without sound memory and in the short
 *     version, xb_audio in pieces equals xb_audio_block, muting;
 *   - a run over 8 s, twice: the same digest (--digest).
 *
 * --tables writes table i as <dir>/table<i>.bin (int16).
 * --run renders blocks and writes into <dir>: voices.f32 (16 x 32 float32
 *   per block), gains.f32 (16 x 3), bins.i32 (3 x 32), out.i32 (2 x 32) and
 *   ticks.txt with a line per command (C), call (F) and voice (V) of every
 *   tick. For check_k7.py.
 * --gp runs the GP stage alone: <in> holds 3 x 32 int32 per frame (bins 0,
 *   1, 11), <out> gets 2 x 32 int32.
 * --wav writes what xb_audio returns as a WAV file.
 * --digest folds bins and output of 12000 blocks and what every tick did
 *   into an FNV-1a digest and prints "digest <16 hex digits> 12000". The
 *   wasm build prints the same line (core/test/wasm_k7.mjs).
 */
#include "xboot.h"
#include "json.h"
#include "sha256.h"

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

static uint32_t hex(const json_doc *d, int node)
{
    char buf[24];
    size_t n = d->nodes[node].len < 23 ? d->nodes[node].len : 23;

    memcpy(buf, d->nodes[node].text, n);
    buf[n] = 0;
    return (uint32_t)strtoul(buf, NULL, 16);
}

static int32_t num(const json_doc *d, int arr, int i)
{
    return (int32_t)json_num(d, json_at(d, arr, i));
}

#define DIGEST_BLOCKS 12000u
#define IR_FRAMES 6000u             /* 4 s, as tools/dsp/gp_ir.py */

static const void *g_pack;
static size_t g_pack_size;
static xb_state *g_state;
static void *g_sound;

static int start(uint32_t flags, int with_sound)
{
    xb_config cfg = { 0, 0 };

    cfg.flags = flags;
    if (xb_init(g_state, g_pack, g_pack_size, &cfg) != XB_OK)
        return -1;
    return with_sound ? xb_audio_init(g_state, g_sound, xb_audio_size()) : XB_OK;
}

/* ---- tables ---------------------------------------------------------------- */

static const char *const table_key[XB_TABLES] = {
    "0x800a1b78", "0x80093b78", "0x80083b70", "0x80083328", "0x80078898", "0x800970f8", "0x800a5b80",
    "0x80093c78"
};
static const char *const table_name[XB_TABLES] = {
    "noise", "sine", "fm", "saw", "pcm signed", "reversed", "pcm a", "pcm b"
};

static void check_tables(const json_doc *d)
{
    static int16_t buf[0x8000];
    static uint8_t bytes[0x10000];
    char what[300], got[65];
    uint32_t t;
    int i;

    for (t = 0; t < XB_TABLES; t++) {
        int c = json_get(d, json_path(d, 0, "tables/by_address"), table_key[t]);
        int sh = json_get(d, c, "sha256"), n = xb_audio_table(g_state, t, NULL, 0), ok;

        ok = c >= 0 && n == (int)json_num(d, json_get(d, c, "values")) &&
             (int)json_num(d, json_get(d, c, "mismatches")) == 0 &&
             xb_audio_table(g_state, t, buf, (uint32_t)n) == n;
        for (i = 0; i < n; i++) {
            bytes[2 * i] = (uint8_t)((uint16_t)buf[i] & 0xFF);
            bytes[2 * i + 1] = (uint8_t)((uint16_t)buf[i] >> 8);
        }
        sha256_hex(bytes, (size_t)n * 2, got);
        ok = ok && sh >= 0 && d->nodes[sh].len == 64 && memcmp(d->nodes[sh].text, got, 64) == 0;
        snprintf(what, sizeof what, "table %-10s %s: %d values, SHA-256 %.16s... %s the guest's memory",
                 table_name[t], table_key[t], n, got, ok ? "equals" : "differs from");
        check(ok, what);
    }
    check(xb_audio_table(g_state, XB_TABLE_SINE, buf, 127) == XB_ERR_ARG &&
          xb_audio_table(g_state, XB_TABLES, buf, 0x8000) == XB_ERR_ARG,
          "xb_audio_table rejects a buffer one value short and an unknown table");
}

/* ---- sequencer and voice layer --------------------------------------------- */

static const char *const fn_name[] = { "filter", "stop", "sample", "level", "pitch_play", "pitch", "nop_b2" };

static const struct { const char *key; size_t at; } reg_key[] = {
    { "304", offsetof(xb_voice, fmt) }, { "318", offsetof(xb_voice, misc) },
    { "308", offsetof(xb_voice, env0) }, { "30c", offsetof(xb_voice, enva) },
    { "310", offsetof(xb_voice, env1) }, { "314", offsetof(xb_voice, envf) },
    { "36c", offsetof(xb_voice, lfo_env) }, { "374", offsetof(xb_voice, fca) },
    { "378", offsetof(xb_voice, fca) }, { "300", offsetof(xb_voice, vbin) },
    { "360", offsetof(xb_voice, vola) }, { "364", offsetof(xb_voice, volb) },
    { "368", offsetof(xb_voice, volc) }, { "37c", offsetof(xb_voice, pitch) }
};
#define REG_KEYS (sizeof reg_key / sizeof reg_key[0])

static void check_sequencer(const json_doc *d)
{
    int list = json_path(d, 0, "commands/list"), calls = json_get(d, 0, "calls_model");
    int first = json_get(d, 0, "first_sample_config"), von = json_get(d, 0, "voice_on");
    int vrel = json_get(d, 0, "voice_release");
    int n_list = d->nodes[list].count, n_calls = d->nodes[calls].count;
    uint32_t last = (uint32_t)json_num(d, json_path(d, 0, "ticks/last/0"));
    static xb_audio_out o;
    xb_audio_state st;
    int ci = 0, fi = 0, cmd_bad = 0, call_bad = 0, reg_bad = 0, reg_n = 0, reg_ch = 0, seen[XB_AUDIO_CHANNELS] = { 0 };
    int oi = 0, ri = 0, on_same = 0, rel_same = 0, on_n = 0, rel_n = 0, on_core = 0, on_guest = 0;
    int ticks = 0, spacing_bad = 0, lc = json_at(d, list, 0), fc = json_at(d, calls, 0);
    int oc = json_at(d, von, 0), rc = json_at(d, vrel, 0);
    char what[400];
    uint32_t i, k;

    check(start(0, 1) == XB_OK, "xb_init and xb_audio_init");
    for (;;) {
        if (xb_audio_block(g_state, &o) != XB_OK)
            break;
        if ((o.tick != 0) != (o.block % XB_AUDIO_TICK_BLOCKS == 0))
            spacing_bad++;
        if (!o.tick)
            continue;
        /* the run ends inside the tick after the last one it counted: the
         * vector has that tick's first command, and the calls up to `last` */
        if (o.tick > last + 1)
            break;
        ticks++;
        for (i = 0; i < o.n_cmds; i++, ci++) {
            if (ci >= n_list)
                continue;
            if ((int32_t)o.cmd[i].tick != num(d, lc, 0) || (int32_t)o.cmd[i].channel != num(d, lc, 1) ||
                o.cmd[i].addr != (uint32_t)json_num(d, json_at(d, lc, 2)) || (int32_t)o.cmd[i].op != num(d, lc, 3))
                cmd_bad++;
            lc = d->nodes[lc].next;
        }
        xb_get_audio(g_state, &st);
        for (i = 0; i < o.n_calls; i++, fi++) {
            const xb_audio_call *c = &o.call[i];
            int want_args, ok;

            if (c->tick > last) {
                fi--;
                continue;
            }
            if (fi >= n_calls)
                continue;
            want_args = d->nodes[fc].count - 3;
            ok = (int32_t)c->tick == num(d, fc, 0) && (int32_t)c->channel == num(d, fc, 1) &&
                 json_str_eq(d, json_at(d, fc, 2), fn_name[c->fn]);
            if (ok && want_args >= 1)
                ok = c->a0 == num(d, fc, 3);
            if (ok && want_args >= 2) {
                int a = json_at(d, fc, 4);

                ok = d->nodes[a].type == JSON_BOOL ? (c->a1 != 0) == (d->nodes[a].text[0] == 't') :
                     c->a1 == (int32_t)json_num(d, a);
            }
            if (!ok)
                call_bad++;
            fc = d->nodes[fc].next;
            if (c->fn == XB_CALL_SAMPLE && !seen[c->channel]) {
                char name[8];
                int cfg;

                seen[c->channel] = 1;
                snprintf(name, sizeof name, "%u", (unsigned)c->channel);
                cfg = json_get(d, first, name);
                if (cfg < 0)
                    continue;
                reg_ch++;
                for (k = 0; k < REG_KEYS; k++, reg_n++) {
                    uint32_t got;

                    memcpy(&got, (const char *)&st.voice[c->channel] + reg_key[k].at, 4);
                    if (got != hex(d, json_get(d, cfg, reg_key[k].key)))
                        reg_bad++;
                }
            } else if (c->fn == XB_CALL_NOTE && oi < d->nodes[von].count) {
                on_n++;
                on_core += c->effect;
                on_guest += num(d, oc, 2);
                on_same += (int32_t)c->tick == num(d, oc, 0) && (int32_t)c->effect == num(d, oc, 2);
                oc = d->nodes[oc].next;
                oi++;
            } else if (c->fn == XB_CALL_STOP && ri < d->nodes[vrel].count) {
                rel_n++;
                rel_same += (int32_t)c->tick == num(d, rc, 0) && (int32_t)c->effect == num(d, rc, 2);
                rc = d->nodes[rc].next;
                ri++;
            }
        }
    }
    snprintf(what, sizeof what, "c4_seq: %d ticks, one every %u blocks (%d wrong); %d commands, vector %d; "
             "tick, channel, address or number differ in %d", ticks, XB_AUDIO_TICK_BLOCKS, spacing_bad, ci, n_list, cmd_bad);
    check(spacing_bad == 0 && ci >= n_list && n_list == 900 && cmd_bad == 0, what);
    snprintf(what, sizeof what, "c4_seq: %d calls into the voice layer up to tick %u, vector %d; differ in %d", fi,
             (unsigned)last, n_calls, call_bad);
    check(fi == n_calls && call_bad == 0, what);
    snprintf(what, sizeof what, "c4_seq: after the first sample call of %d channels, %d method values (0x300 to "
             "0x37C), %d differ from the guest's", reg_ch, reg_n, reg_bad);
    check(reg_ch == d->nodes[first].count && reg_bad == 0, what);
    printf("      c4_seq: %d notes, the core starts the voice in %d, the guest in %d, the same in %d; %d releases, "
           "the same in %d (not checked: the run stops at breakpoints, its voices end earlier)\n", on_n, on_core,
           on_guest, on_same, rel_n, rel_same);
}

/* ---- GP stage -------------------------------------------------------------- */

static void ir_sha(const int32_t *x, uint32_t n, char *hexout)
{
    static uint8_t bytes[IR_FRAMES * XB_AUDIO_BLOCK * 4];
    uint32_t i;

    for (i = 0; i < n; i++) {
        /* as gp_ir.py: 24-bit value / 2^23 as float32, divided by the impulse 0.5 */
        union { float f; uint32_t u; } v;

        v.f = (float)x[i] / 8388608.0f / 0.5f;
        bytes[4 * i] = (uint8_t)v.u;
        bytes[4 * i + 1] = (uint8_t)(v.u >> 8);
        bytes[4 * i + 2] = (uint8_t)(v.u >> 16);
        bytes[4 * i + 3] = (uint8_t)(v.u >> 24);
    }
    sha256_hex(bytes, (size_t)n * 4, hexout);
}

static int impulse(uint32_t bin, int32_t *left, int32_t *right)
{
    int32_t in[3][XB_AUDIO_BLOCK];
    uint32_t f;

    if (start(0, 1) != XB_OK)
        return -1;
    for (f = 0; f < IR_FRAMES; f++) {
        memset(in, 0, sizeof in);
        if (f == 0)
            in[bin][0] = 0x400000;
        if (xb_audio_gp(g_state, in[0], in[1], in[2], left + f * XB_AUDIO_BLOCK, right + f * XB_AUDIO_BLOCK) != XB_OK)
            return -1;
    }
    return 0;
}

static void check_gp(const json_doc *g)
{
    static int32_t left[IR_FRAMES * XB_AUDIO_BLOCK], right[IR_FRAMES * XB_AUDIO_BLOCK];
    static const char *const key[3][2] = { { "b0_L", NULL }, { NULL, "b1_R" }, { "b11_L", "b11_R" } };
    static const char *const name[3] = { "0", "1", "11" };
    const uint32_t n = IR_FRAMES * XB_AUDIO_BLOCK;
    int sums = json_get(g, 0, "ir_sha256");
    char what[400], got[2][65];
    uint32_t b, side, i;

    check((uint32_t)json_num(g, json_get(g, 0, "frames")) == IR_FRAMES + 100 &&
          json_num(g, json_get(g, 0, "impulse")) == 0.5, "c4_gp.json: impulse 0.5, 6000 frames after the impulse");
    for (b = 0; b < 3; b++) {
        int ok = impulse(b, left, right) == 0;

        ir_sha(left, n, got[0]);
        ir_sha(right, n, got[1]);
        for (side = 0; side < 2; side++) {
            const int32_t *x = side ? right : left;
            int sh = key[b][side] ? json_get(g, sums, key[b][side]) : -1, same;
            uint32_t nonzero = 0, last = 0;

            for (i = 0; i < n; i++) {
                if (x[i]) {
                    nonzero++;
                    last = i;
                }
            }
            if (key[b][side]) {
                same = ok && sh >= 0 && memcmp(g->nodes[sh].text, got[side], 64) == 0;
                snprintf(what, sizeof what, "impulse in bin %s, %s: %u samples, %u not 0, the last at %.4f s, SHA-256 "
                         "%.16s... %s %s", name[b], side ? "right" : "left", (unsigned)n, (unsigned)nonzero,
                         last / 48000.0, got[side], same ? "equals" : "differs from", key[b][side]);
                check(same, what);
            } else {
                snprintf(what, sizeof what, "impulse in bin %s, %s: silent, as in the vector (%u samples not 0)",
                         name[b], side ? "right" : "left", (unsigned)nonzero);
                check(ok && nonzero == 0, what);
            }
        }
    }
}

/* ---- rules of the interface ------------------------------------------------ */

static void check_rules(void)
{
    static int16_t a[4000 * 2];
    static xb_audio_out o;
    static const uint32_t piece[] = { 1, 31, 32, 33, 700, 5, 64, 1234 };
    void *small_base, *small = aligned_alloc16(64, &small_base);
    uint32_t i, k, n, sounding = 0, bad = 0, loud = 0;
    char what[300];
    int ok;

    ok = start(0, 0) == XB_OK;
    memset(a, 0x55, sizeof a);
    ok = ok && xb_audio(g_state, a, 4000) == XB_OK && xb_audio_block(g_state, &o) == XB_ERR_ARG;
    for (i = 0; i < 8000; i++)
        sounding += a[i] != 0;
    check(ok && sounding == 0, "without xb_audio_init: xb_audio writes silence, xb_audio_block is refused");
    check(xb_audio_init(g_state, small, 64) == XB_ERR_ARG &&
          xb_audio_init(g_state, (char *)g_sound + 4, xb_audio_size()) == XB_ERR_ALIGN &&
          xb_audio_init(g_state, NULL, xb_audio_size()) == XB_ERR_ARG,
          "xb_audio_init rejects memory that is too small, unaligned or missing");
    free(small_base);

    ok = start(XB_START_SHORT, 1) == XB_OK && xb_audio(g_state, a, 4000) == XB_OK;
    for (sounding = 0, i = 0; i < 8000; i++)
        sounding += a[i] != 0;
    check(ok && sounding == 0, "short version: no sound is started, 4000 frames of silence");

    /* xb_audio in pieces against the blocks of a second run */
    ok = start(0, 1) == XB_OK;
    for (n = 0, k = 0; n + piece[k % 8] <= 4000; n += piece[k % 8], k++)
        ok = ok && xb_audio(g_state, a + 2 * n, piece[k % 8]) == XB_OK;
    ok = ok && start(0, 1) == XB_OK;
    for (i = 0; i * XB_AUDIO_BLOCK < n; i++) {
        uint32_t j;

        ok = ok && xb_audio_block(g_state, &o) == XB_OK;
        for (j = 0; j < XB_AUDIO_BLOCK && i * XB_AUDIO_BLOCK + j < n; j++) {
            int32_t l = o.out[0][j], r = o.out[1][j];
            int16_t wl = (int16_t)(l >= 0 ? l / 256 : -((-l + 255) / 256));
            int16_t wr = (int16_t)(r >= 0 ? r / 256 : -((-r + 255) / 256));

            bad += a[2 * (i * XB_AUDIO_BLOCK + j)] != wl || a[2 * (i * XB_AUDIO_BLOCK + j) + 1] != wr;
            loud += wl != 0 || wr != 0;
        }
    }
    snprintf(what, sizeof what, "xb_audio in %u pieces: %u frames equal bits 8 to 23 of the blocks' output (%u differ), "
             "%u of them not silent", (unsigned)k, (unsigned)n, (unsigned)bad, (unsigned)loud);
    check(ok && bad == 0 && loud > 0, what);

    /* muting: all channels muted is silence, the sequencer runs on */
    ok = start(0, 1) == XB_OK && xb_audio_mute(g_state, 0xFFFF) == XB_OK;
    for (sounding = 0, k = 0, i = 0; i < 2000 && ok; i++) {
        uint32_t j;

        ok = xb_audio_block(g_state, &o) == XB_OK;
        k += o.n_cmds;
        for (j = 0; j < XB_AUDIO_BLOCK; j++)
            sounding += o.out[0][j] != 0 || o.out[1][j] != 0;
    }
    snprintf(what, sizeof what, "all channels muted: 2000 blocks silent (%u samples not 0), %u commands run", (unsigned)sounding,
             (unsigned)k);
    check(ok && sounding == 0 && k > 100, what);
}

/* ---- digest ---------------------------------------------------------------- */

typedef struct digest {
    uint64_t h;
} digest;

static void fold(digest *g, uint32_t u)
{
    int i;

    for (i = 0; i < 4; i++) {
        g->h ^= (u >> (8 * i)) & 0xFF;
        g->h *= 0x100000001B3ull;
    }
}

static int run_digest(uint64_t *out, uint32_t *peak)
{
    digest g = { 0xCBF29CE484222325ull };
    static xb_audio_out o;
    uint32_t n, i, b;

    if (start(0, 1) != XB_OK)
        return -1;
    *peak = 0;
    for (n = 0; n < DIGEST_BLOCKS; n++) {
        if (xb_audio_block(g_state, &o) != XB_OK)
            return -1;
        fold(&g, o.tick);
        fold(&g, o.n_cmds);
        fold(&g, o.n_calls);
        for (i = 0; i < o.n_cmds; i++) {
            fold(&g, o.cmd[i].addr);
            fold(&g, (uint32_t)o.cmd[i].channel << 8 | o.cmd[i].op);
        }
        for (i = 0; i < o.n_calls; i++) {
            fold(&g, (uint32_t)o.call[i].a0);
            fold(&g, (uint32_t)o.call[i].a1);
            fold(&g, (uint32_t)o.call[i].channel << 16 | (uint32_t)o.call[i].fn << 8 | o.call[i].effect);
        }
        for (b = 0; b < XB_AUDIO_BINS; b++)
            for (i = 0; i < XB_AUDIO_BLOCK; i++)
                fold(&g, (uint32_t)o.bin[b][i]);
        for (b = 0; b < 2; b++) {
            for (i = 0; i < XB_AUDIO_BLOCK; i++) {
                uint32_t mag = (uint32_t)(o.out[b][i] < 0 ? -o.out[b][i] : o.out[b][i]);

                fold(&g, (uint32_t)o.out[b][i]);
                if (mag > *peak)
                    *peak = mag;
            }
        }
    }
    *out = g.h;
    return 0;
}

/* ---- dumps for check_k7.py -------------------------------------------------- */

static FILE *open_in(const char *dir, const char *name, const char *mode)
{
    char path[600];

    snprintf(path, sizeof path, "%s/%s", dir, name);
    return fopen(path, mode);
}

static int dump_tables(const char *dir)
{
    static int16_t buf[0x8000];
    uint32_t t;

    for (t = 0; t < XB_TABLES; t++) {
        char name[32];
        FILE *f;
        int n = xb_audio_table(g_state, t, buf, 0x8000);

        snprintf(name, sizeof name, "table%u.bin", (unsigned)t);
        f = open_in(dir, name, "wb");
        if (n < 0 || !f || fwrite(buf, 2, (size_t)n, f) != (size_t)n)
            return 2;
        fclose(f);
    }
    return 0;
}

static int dump_run(const char *dir, uint32_t blocks)
{
    FILE *fv = open_in(dir, "voices.f32", "wb"), *fg = open_in(dir, "gains.f32", "wb");
    FILE *fb = open_in(dir, "bins.i32", "wb"), *fo = open_in(dir, "out.i32", "wb");
    FILE *ft = open_in(dir, "ticks.txt", "w");
    static xb_audio_out o;
    xb_audio_state st;
    uint32_t n, i;

    if (!fv || !fg || !fb || !fo || !ft || start(0, 1) != XB_OK)
        return 2;
    for (n = 0; n < blocks; n++) {
        if (xb_audio_block(g_state, &o) != XB_OK)
            return 2;
        fwrite(o.voice, sizeof o.voice, 1, fv);
        fwrite(o.gain, sizeof o.gain, 1, fg);
        fwrite(o.bin, sizeof o.bin, 1, fb);
        fwrite(o.out, sizeof o.out, 1, fo);
        if (!o.tick)
            continue;
        xb_get_audio(g_state, &st);
        for (i = 0; i < o.n_cmds; i++)
            fprintf(ft, "C %u %u %u %u\n", (unsigned)o.cmd[i].tick, (unsigned)o.cmd[i].channel,
                    (unsigned)o.cmd[i].addr, (unsigned)o.cmd[i].op);
        for (i = 0; i < o.n_calls; i++)
            fprintf(ft, "F %u %u %s %d %d %u\n", (unsigned)o.call[i].tick, (unsigned)o.call[i].channel,
                    fn_name[o.call[i].fn], (int)o.call[i].a0, (int)o.call[i].a1, (unsigned)o.call[i].effect);
        for (i = 0; i < XB_AUDIO_CHANNELS; i++) {
            const xb_voice *v = &st.voice[i];

            fprintf(ft, "V %u %u %u %u %u %u %d %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n", (unsigned)o.tick,
                    (unsigned)i, v->alloc, v->on, v->active, v->sample, (int)v->level, (unsigned)v->fmt,
                    (unsigned)v->vbin, (unsigned)v->misc, (unsigned)v->env0, (unsigned)v->enva, (unsigned)v->env1,
                    (unsigned)v->envf, (unsigned)v->lfo_env, (unsigned)v->fca, (unsigned)v->vola, (unsigned)v->volb,
                    (unsigned)v->volc, (unsigned)v->pitch, (unsigned)v->ebo);
        }
    }
    fclose(fv);
    fclose(fg);
    fclose(fb);
    fclose(fo);
    fclose(ft);
    return 0;
}

static int run_gp(const char *in_path, const char *out_path, uint32_t frames)
{
    FILE *in = fopen(in_path, "rb"), *out = fopen(out_path, "wb");
    int32_t x[3][XB_AUDIO_BLOCK], y[2][XB_AUDIO_BLOCK];
    uint32_t f;

    if (!in || !out || start(0, 1) != XB_OK)
        return 2;
    for (f = 0; f < frames; f++) {
        if (fread(x, sizeof x, 1, in) != 1)
            memset(x, 0, sizeof x);
        if (xb_audio_gp(g_state, x[0], x[1], x[2], y[0], y[1]) != XB_OK)
            return 2;
        fwrite(y, sizeof y, 1, out);
    }
    fclose(in);
    fclose(out);
    return 0;
}

static void le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int write_wav(const char *path, uint32_t blocks)
{
    static int16_t buf[XB_AUDIO_BLOCK * 2];
    uint8_t head[44] = "RIFF", two[2];
    uint32_t bytes = blocks * XB_AUDIO_BLOCK * 4, n, i;
    FILE *f = fopen(path, "wb");

    if (!f || start(0, 1) != XB_OK)
        return 2;
    le32(head + 4, 36 + bytes);
    memcpy(head + 8, "WAVEfmt ", 8);
    le32(head + 16, 16);
    head[20] = 1;                       /* PCM */
    head[21] = 0;
    head[22] = 2;                       /* stereo */
    head[23] = 0;
    le32(head + 24, XB_AUDIO_RATE);
    le32(head + 28, XB_AUDIO_RATE * 4);
    head[32] = 4;
    head[33] = 0;
    head[34] = 16;
    head[35] = 0;
    memcpy(head + 36, "data", 4);
    le32(head + 40, bytes);
    fwrite(head, 44, 1, f);
    for (n = 0; n < blocks; n++) {
        if (xb_audio(g_state, buf, XB_AUDIO_BLOCK) != XB_OK)
            return 2;
        for (i = 0; i < XB_AUDIO_BLOCK * 2; i++) {
            two[0] = (uint8_t)((uint16_t)buf[i] & 0xFF);
            two[1] = (uint8_t)((uint16_t)buf[i] >> 8);
            fwrite(two, 2, 1, f);
        }
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    void *pack_base, *state_base, *sound_base;
    size_t pack_size;
    unsigned char *pack;
    json_doc d, g;
    uint64_t dg[2];
    uint32_t peak[2];
    char what[300], gp_path[600], *tail;
    int c;

    if (argc < 3) {
        fprintf(stderr, "usage: test_k7 <pack> <c4_audio.json> | --tables <dir> | --run <dir> <blocks> | "
                "--gp <in> <out> <frames> | --wav <file> <blocks> | --digest\n");
        return 2;
    }
    pack = read_file(argv[1], &pack_size, &pack_base);
    g_state = aligned_alloc16(xb_state_size(), &state_base);
    g_sound = aligned_alloc16(xb_audio_size(), &sound_base);
    if (!pack || !g_state || !g_sound) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    g_pack = pack;
    g_pack_size = pack_size;
    if (start(0, 1) != XB_OK)
        return 2;
    if (argc == 4 && strcmp(argv[2], "--tables") == 0)
        return dump_tables(argv[3]);
    if (argc == 5 && strcmp(argv[2], "--run") == 0)
        return dump_run(argv[3], (uint32_t)strtoul(argv[4], NULL, 10));
    if (argc == 6 && strcmp(argv[2], "--gp") == 0)
        return run_gp(argv[3], argv[4], (uint32_t)strtoul(argv[5], NULL, 10));
    if (argc == 5 && strcmp(argv[2], "--wav") == 0)
        return write_wav(argv[3], (uint32_t)strtoul(argv[4], NULL, 10));
    if (argc == 3 && strcmp(argv[2], "--digest") == 0) {
        if (run_digest(&dg[0], &peak[0]) != 0)
            return 2;
        printf("digest %016llX %u\n", (unsigned long long)dg[0], (unsigned)DIGEST_BLOCKS);
        return 0;
    }

    printf("state %u bytes, sound %u bytes, pack %u bytes; xb_audio_out %u bytes, xb_voice %u bytes\n",
           (unsigned)xb_state_size(), (unsigned)xb_audio_size(), (unsigned)pack_size, (unsigned)sizeof(xb_audio_out),
           (unsigned)sizeof(xb_voice));
    tail = strstr(argv[2], "c4_audio.json");
    if (!tail || strlen(argv[2]) + 1 > sizeof gp_path) {
        fprintf(stderr, "the vector must be c4_audio.json; c4_gp.json is read from the same folder\n");
        return 2;
    }
    snprintf(gp_path, sizeof gp_path, "%.*sc4_gp.json", (int)(tail - argv[2]), argv[2]);
    if (json_load(&d, argv[2]) != 0 || json_load(&g, gp_path) != 0)
        return 2;

    check_tables(&d);
    check_sequencer(&d);
    check_gp(&g);
    check_rules();

    c = run_digest(&dg[0], &peak[0]);
    c |= run_digest(&dg[1], &peak[1]);
    snprintf(what, sizeof what, "run over 8 s, twice: %u blocks, digest %016llX and %016llX; largest output %.4f of "
             "full scale", (unsigned)DIGEST_BLOCKS, (unsigned long long)dg[0], (unsigned long long)dg[1],
             peak[0] / 8388608.0);
    check(c == 0 && dg[0] == dg[1], what);
    printf("digest %016llX %u\n", (unsigned long long)dg[0], (unsigned)DIGEST_BLOCKS);

    json_free(&d);
    json_free(&g);
    free(pack_base);
    free(state_base);
    free(sound_base);
    printf("%d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
