/* Host test of milestone K1: time keeping against docs/vectors/c3_timing.json.
 *
 *     test_k1 <pack> <c3_timing.json>
 *     test_k1 <pack> --dump <ticks.txt>
 *
 * First form, for both runs of the vector file:
 *   - the pulse table, bit for bit, against the guest's table and the model's,
 *   - the generator state after set-up against the guest (where read),
 *   - every frame step: the core gets the tick the guest stored in this+0x28
 *     and must meet t, x18, x1c, x20 and x24 bit for bit,
 *   - XB_OK before the last step, XB_DONE at it, nothing after it,
 *   - t after the loop and the number of ticks in the run.
 * Then a few rules of Timing.md 2 the runs never hit: a step above 1 s,
 * a tick going backwards, a wrapped counter, and the free-time call.
 *
 * Second form, for core/test/check_k1.py: the first line of the file is
 * "<flags> <start tick>", then one tick per line. Prints one line per step
 * with the return code and the bits of t, x18, x1c, x20, x24.
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

static uint32_t bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
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

static int dump(xb_state *s, const void *pack, size_t pack_size, const char *path)
{
    FILE *f = fopen(path, "r");
    unsigned long flags, tick;
    xb_config cfg;
    xb_timing tm;
    int rc;

    if (!f || fscanf(f, "%lu %lu", &flags, &tick) != 2) {
        fprintf(stderr, "%s: cannot read\n", path);
        return 2;
    }
    cfg.flags = (uint32_t)flags;
    cfg.start_tick = (uint32_t)tick;
    if (xb_init(s, pack, pack_size, &cfg) != XB_OK)
        return 2;
    while (fscanf(f, "%lu", &tick) == 1) {
        rc = xb_advance(s, (uint32_t)tick);
        xb_get_timing(s, &tm);
        printf("%d %08X %08X %08X %08X %08X\n", rc, (unsigned)bits(tm.t), (unsigned)bits(tm.x18),
               (unsigned)bits(tm.x1c), (unsigned)bits(tm.x20), (unsigned)bits(tm.x24));
    }
    fclose(f);
    return 0;
}

static int table_equal(const json_doc *d, int arr, const xb_timing *tm)
{
    int i, k;

    if (arr < 0 || d->nodes[arr].count != XB_PULSE_COUNT)
        return 0;
    for (i = 0; i < XB_PULSE_COUNT; i++)
        for (k = 0; k < 3; k++)
            if (bits((float)json_num(d, json_at(d, json_at(d, arr, i), k))) != bits(tm->pulse[i][k]))
                return 0;
    return 1;
}

static void check_run(xb_state *s, const void *pack, size_t pack_size, const json_doc *d, int run)
{
    int name = json_get(d, run, "run");
    int chk = json_get(d, run, "checks");
    int rows = json_get(d, run, "steps");
    int flag = json_get(d, chk, "flag_0x8003aeb8");
    int gst = json_path(d, chk, "pulse_table/guest_state_this_0x68c");
    const char *nm = d->nodes[name].text;
    int nl = (int)d->nodes[name].len;
    int n = d->nodes[rows].count, i, k, bad = 0, rc_bad = 0, shown = 0, rc = XB_OK;
    float t_end = (float)json_num(d, json_path(d, chk, "end/t_after_loop"));
    uint32_t ticks_total = (uint32_t)json_num(d, json_path(d, chk, "end/ticks_total"));
    static const char *field[5] = { "t", "x18", "x1c", "x20", "x24" };
    char what[256];
    xb_config cfg;
    xb_timing tm, tm2;

    /* c3_short records the byte at 0x8003AEB8 as poked (Timing.md 3). */
    cfg.flags = (flag >= 0 && memcmp(d->nodes[flag].text, "00", 2) != 0) ? XB_START_SHORT : 0u;
    cfg.start_tick = (uint32_t)json_num(d, json_path(d, chk, "start/tick_this_0x2c"));
    rc = xb_init(s, pack, pack_size, &cfg);
    snprintf(what, sizeof what, "%.*s: xb_init = %d, start t = %g", nl, nm, rc, (double)xb_time(s));
    check(rc == XB_OK, what);

    xb_get_timing(s, &tm);
    snprintf(what, sizeof what, "%.*s: pulse table equals the guest's (36 values)", nl, nm);
    check(table_equal(d, json_path(d, chk, "pulse_table/table_guest"), &tm), what);
    snprintf(what, sizeof what, "%.*s: pulse table equals the model's", nl, nm);
    check(table_equal(d, json_get(d, 0, "pulse_table_model"), &tm), what);
    if (gst >= 0) {
        snprintf(what, sizeof what, "%.*s: generator after set-up %08X, guest %.*s", nl, nm,
                 (unsigned)tm.rng, (int)d->nodes[gst].len, d->nodes[gst].text);
        check(strtoul(d->nodes[gst].text, NULL, 16) == tm.rng, what);
    }

    for (i = 0; i < n; i++) {
        int row = json_at(d, rows, i);
        uint32_t tick = (uint32_t)json_num(d, json_at(d, row, 1));
        float got[5], want[5];

        rc = xb_advance(s, tick);
        xb_get_timing(s, &tm);
        got[0] = tm.t; got[1] = tm.x18; got[2] = tm.x1c; got[3] = tm.x20; got[4] = tm.x24;
        for (k = 0; k < 5; k++)
            want[k] = (float)json_num(d, json_at(d, row, 3 + k));
        for (k = 0; k < 5; k++) {
            if (bits(got[k]) != bits(want[k])) {
                bad++;
                if (shown++ < 5)
                    printf("      step %d %s: core %08X, guest %08X\n", i, field[k],
                           (unsigned)bits(got[k]), (unsigned)bits(want[k]));
            }
        }
        if (rc != (i == n - 1 ? XB_DONE : XB_OK))
            rc_bad++;
    }
    snprintf(what, sizeof what, "%.*s: %d steps, %d values, %d differ from the guest", nl, nm,
             n, 5 * n, bad);
    check(bad == 0 && n > 0, what);
    snprintf(what, sizeof what, "%.*s: XB_OK before the last step, XB_DONE at it (%d wrong)", nl,
             nm, rc_bad);
    check(rc_bad == 0, what);
    snprintf(what, sizeof what, "%.*s: t after the loop %08X, guest %08X (%.6f)", nl, nm,
             (unsigned)bits(tm.t), (unsigned)bits(t_end), (double)t_end);
    check(bits(tm.t) == bits(t_end), what);
    snprintf(what, sizeof what, "%.*s: ticks in the run %u, guest %u", nl, nm,
             (unsigned)(tm.last_tick - tm.setup_tick), (unsigned)ticks_total);
    check(tm.last_tick - tm.setup_tick == ticks_total, what);
    rc = xb_advance(s, tm.last_tick + 20);
    xb_get_timing(s, &tm2);
    snprintf(what, sizeof what, "%.*s: after the end XB_DONE and nothing changes", nl, nm);
    check(rc == XB_DONE && memcmp(&tm, &tm2, sizeof tm) == 0, what);
}

/* Rules of Timing.md 2 outside the runs. */
static void check_rules(xb_state *s, const void *pack, size_t pack_size)
{
    xb_config cfg = { 0, 1000 };
    xb_timing a, b;
    int rc;

    xb_init(s, pack, pack_size, &cfg);
    xb_advance(s, 2500);
    check(bits(xb_time(s)) == bits(0.001f), "step of 1500 ms counts 1 ms (0x800571BC)");
    /* 1000 * f32(0.001) rounds to 1.0f, which is not > 1.0: no cut. */
    xb_advance(s, 2500 + 1000);
    check(bits(xb_time(s)) == bits((float)((double)1.0f + (double)0.001f)),
          "step of exactly 1000 ms is not cut");

    /* Backwards and forwards by the same amount give the same step. */
    xb_init(s, pack, pack_size, &cfg);
    xb_advance(s, 1000 + 1500 / 2);
    xb_get_timing(s, &a);
    xb_init(s, pack, pack_size, &cfg);
    xb_advance(s, 1000 - 1500 / 2);
    xb_get_timing(s, &b);
    check(bits(a.t) == bits(b.t) && bits(a.x18) == bits(b.x18), "tick going back: |d| (cdq, xor, sub)");

    /* A counter that wraps from 0xFFFFFFFE to 3 is a step of 5 ms. */
    cfg.start_tick = 0xFFFFFFFEu;
    xb_init(s, pack, pack_size, &cfg);
    xb_advance(s, 3);
    xb_get_timing(s, &a);
    cfg.start_tick = 100;
    xb_init(s, pack, pack_size, &cfg);
    xb_advance(s, 105);
    xb_get_timing(s, &b);
    check(bits(a.t) == bits(b.t) && bits(a.t) == bits((float)(5.0 * (double)0.001f)),
          "wrapped counter: step of 5 ms");

    /* Free time: same update, no millisecond grid. */
    cfg.start_tick = 0;
    xb_init(s, pack, pack_size, &cfg);
    check(xb_advance_free(s, -0.001) == XB_ERR_ARG, "free time: negative step rejected");
    {
        double zero = 0.0;
        check(xb_advance_free(s, zero / zero) == XB_ERR_ARG, "free time: NaN rejected");
    }
    check(xb_advance_free(s, 0.0004) == XB_OK && bits(xb_time(s)) == bits(0.0004f),
          "free time: 0.4 ms step gives t = 0.0004");
    {
        int steps = 0;
        do {
            rc = xb_advance_free(s, 1.0 / 240.0);
            steps++;
        } while (rc == XB_OK && steps < 10000);
        xb_get_timing(s, &a);
        printf("      free time at 240 Hz: %d steps, t = %.6f, x18 = %.6f\n", steps, (double)a.t,
               (double)a.x18);
        check(rc == XB_DONE && a.t >= 8.0f && a.t < 8.0f + 1.0f / 240.0f,
              "free time at 240 Hz ends within one step after t = 8");
    }
}

int main(int argc, char **argv)
{
    void *pack_base, *state_base;
    size_t pack_size;
    unsigned char *pack;
    xb_state *s;
    json_doc doc;
    int runs, i;

    if (argc != 3 && !(argc == 4 && strcmp(argv[2], "--dump") == 0)) {
        fprintf(stderr, "usage: test_k1 <pack> <c3_timing.json> | test_k1 <pack> --dump <ticks.txt>\n");
        return 2;
    }
    pack = read_file(argv[1], &pack_size, &pack_base);
    if (!pack) {
        fprintf(stderr, "%s: cannot read\n", argv[1]);
        return 2;
    }
    s = aligned_alloc16(xb_state_size(), &state_base);
    if (argc == 4)
        return dump(s, pack, pack_size, argv[3]);

    if (json_load(&doc, argv[2]) != 0)
        return 2;
    printf("state %u bytes, pack %u bytes\n", (unsigned)xb_state_size(), (unsigned)pack_size);
    runs = json_get(&doc, 0, "runs");
    check(runs >= 0 && doc.nodes[runs].count == 2, "c3_timing.json has two runs");
    for (i = 0; runs >= 0 && i < doc.nodes[runs].count; i++)
        check_run(s, pack, pack_size, &doc, json_at(&doc, runs, i));
    check_rules(s, pack, pack_size);

    printf("%d checks, %d failed\n", checks, failed);
    json_free(&doc);
    free(state_base);
    free(pack_base);
    return failed ? 1 : 0;
}
