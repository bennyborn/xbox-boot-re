/* Host test of milestone K0.
 *
 *     test_k0 <pack> <c3_timing.json>
 *
 * Loads the data pack through the core and compares the start value of t of
 * both runs in docs/vectors/c3_timing.json (checks.start.t) bit for bit.
 * The tick in this+0x2C (checks.start.tick_this_0x2c) is an input: the host
 * hands it over and the core must keep it. Also checks that the core
 * rejects broken packs.
 */
#include "xboot.h"
#include "json.h"

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

static uint32_t f32_bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

/* Memory aligned to XB_ALIGN, without C11 or platform calls. */
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

static int is_short_run(const json_doc *d, int checks_node)
{
    /* c3_short records the byte at 0x8003AEB8 as poked (Timing.md 3); the
     * long run has no such entry and the byte is 0 there. */
    int flag = json_get(d, checks_node, "flag_0x8003aeb8");
    return flag >= 0 && d->nodes[flag].len >= 2 && memcmp(d->nodes[flag].text, "00", 2) != 0;
}

int main(int argc, char **argv)
{
    void *pack_base, *state_base, *copy_base;
    size_t pack_size;
    unsigned char *pack, *copy;
    xb_state *s;
    json_doc doc;
    int runs, i;
    char what[256];

    if (argc != 3) {
        fprintf(stderr, "usage: test_k0 <pack> <c3_timing.json>\n");
        return 2;
    }
    pack = read_file(argv[1], &pack_size, &pack_base);
    if (!pack) {
        fprintf(stderr, "%s: cannot read\n", argv[1]);
        return 2;
    }
    if (json_load(&doc, argv[2]) != 0)
        return 2;
    s = aligned_alloc16(xb_state_size(), &state_base);
    printf("state %u bytes, pack %u bytes\n", (unsigned)xb_state_size(), (unsigned)pack_size);

    runs = json_get(&doc, 0, "runs");
    check(runs >= 0 && doc.nodes[runs].count == 2, "c3_timing.json has two runs");
    for (i = 0; i < doc.nodes[runs].count; i++) {
        int run = json_at(&doc, runs, i);
        int name = json_get(&doc, run, "run");
        int chk = json_get(&doc, run, "checks");
        float t_want = (float)json_num(&doc, json_path(&doc, chk, "start/t"));
        uint32_t tick = (uint32_t)json_num(&doc, json_path(&doc, chk, "start/tick_this_0x2c"));
        xb_config cfg;
        int rc;

        cfg.flags = is_short_run(&doc, chk) ? XB_START_SHORT : 0u;
        cfg.start_tick = tick;
        rc = xb_init(s, pack, pack_size, &cfg);
        snprintf(what, sizeof what, "%.*s: xb_init = %d", (int)doc.nodes[name].len,
                 doc.nodes[name].text, rc);
        check(rc == XB_OK, what);
        snprintf(what, sizeof what, "%.*s: t = %08X, vector %08X (%g)", (int)doc.nodes[name].len,
                 doc.nodes[name].text, (unsigned)f32_bits(xb_time(s)), (unsigned)f32_bits(t_want),
                 (double)t_want);
        check(f32_bits(xb_time(s)) == f32_bits(t_want), what);
        snprintf(what, sizeof what, "%.*s: setup tick kept: %u, given %u (this+0x2C)",
                 (int)doc.nodes[name].len, doc.nodes[name].text, (unsigned)xb_last_tick(s),
                 (unsigned)tick);
        check(xb_last_tick(s) == tick, what);
    }

    /* xb_frame without buffers (K6) and the empty sound of K0 answer as
     * documented. */
    {
        xb_frame_out out;
        int16_t pcm[64];
        int k, silent = 1;

        memset(&out, 0, sizeof out);
        memset(pcm, 0x55, sizeof pcm);
        check(xb_frame(s, &out) == XB_ERR_SPACE && out.draw_count > 0 && out.param_count > 0,
              "xb_frame without buffers: XB_ERR_SPACE and the sizes needed");
        check(xb_audio(s, pcm, 32) == XB_OK, "xb_audio: 32 frames");
        for (k = 0; k < 64; k++)
            silent &= pcm[k] == 0;
        check(silent, "xb_audio: silence");
    }

    /* Broken packs are rejected. */
    copy = aligned_alloc16(pack_size, &copy_base);
    {
        xb_config cfg = { 0, 0 };

        memcpy(copy, pack, pack_size);
        copy[0] ^= 1;
        check(xb_init(s, copy, pack_size, &cfg) == XB_ERR_PACK_MAGIC, "wrong magic");
        memcpy(copy, pack, pack_size);
        copy[4] = 2;
        check(xb_init(s, copy, pack_size, &cfg) == XB_ERR_PACK_VERSION, "wrong version");
        check(xb_init(s, pack, pack_size - 16, &cfg) == XB_ERR_PACK_LAYOUT, "truncated pack");
        memcpy(copy, pack, pack_size);
        copy[16 + 4] ^= 1;   /* length of the first entry */
        check(xb_init(s, copy, pack_size, &cfg) == XB_ERR_PACK_MISSING, "first range one byte off");
        memcpy(copy, pack, pack_size);
        copy[16 + 8] = 4;    /* offset of the first entry, misaligned */
        check(xb_init(s, copy, pack_size, &cfg) == XB_ERR_PACK_LAYOUT, "misaligned offset");
        check(xb_init((xb_state *)((char *)s + 4), pack, pack_size, &cfg) == XB_ERR_ALIGN,
              "misaligned state");
        check(xb_time(s) == 0.0f && xb_advance(s, 0) == XB_ERR_ARG, "state unusable after failed init");
    }

    printf("%d checks, %d failed\n", checks, failed);
    json_free(&doc);
    free(copy_base);
    free(state_base);
    free(pack_base);
    return failed ? 1 : 0;
}
