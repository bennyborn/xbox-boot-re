/*
 * Measures what the core costs, for docs/core.md 6 (milestone K8).
 *
 *     cost_k8 <pack> [--runs N] [--no-steps] [--json <file>]
 *
 * Three measures, all on a run at 60 Hz (17, 17, 16 ms) as hosts/native
 * makes it:
 *
 *   time          the time stamp counter around every call, the smallest of
 *                 N runs per frame (default 20), in microseconds. The
 *                 counter's rate is measured against the system's clock.
 *   instructions  for a few frames and calls: the number of x86-64
 *                 instructions the call executes, counted by single
 *                 stepping (trap flag, one exception per instruction), with
 *                 the floating-point arithmetic among them by kind, and the
 *                 deepest stack. Exact for this build and compiler; another
 *                 CPU needs other instructions, but the arithmetic the core
 *                 asks for is the same.
 *   memory        sizes the core reports and the buffers a host provides.
 *
 * Instruction classes, read from the opcode at each step: scalar float
 * (F3 0F xx), scalar double (F2 0F xx), packed (no prefix or 66), each
 * split into add/sub (58, 5C), mul (59), div (5E), sqrt (51); conversions
 * between float, double and integer (5A, 5B, 2A, 2C, 2D, E6); integer
 * multiplications (0F AF, 69, 6B, F7 /4 /5) and divisions (F7 /6 /7);
 * calls (E8, FF /2). The build has no AVX, so there are no VEX forms.
 *
 * Windows on x86-64 only: the stepping needs a vectored exception handler.
 */
#include "xboot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32) || !defined(__x86_64__)
int main(void)
{
    fprintf(stderr, "cost_k8 needs Windows on x86-64\n");
    return 2;
}
#else

#include <windows.h>
#include <intrin.h>

/* ---- memory of the run ------------------------------------------------------ */

static void *aligned_alloc16(size_t size)
{
    void *base = malloc(size + XB_ALIGN);

    if (!base)
        return NULL;
    return (void *)(((uintptr_t)base + XB_ALIGN - 1) & ~(uintptr_t)(XB_ALIGN - 1));
}

static unsigned char *g_pack;
static size_t g_pack_size;
static xb_state *g_state;
static void *g_sound;
static xb_draw g_draws[XB_FRAME_DRAWS_MAX];
static xb_real g_params[XB_FRAME_PARAMS_MAX];
static xb_real g_mesh[XB_BLOB_VERTICES][4];
static int16_t g_samples[2 * 48 * 17];
static xb_frame_out g_out;
static xb_audio_out g_block;

static void buffers(void)
{
    memset(&g_out, 0, sizeof g_out);
    g_out.draws = g_draws;
    g_out.draw_cap = XB_FRAME_DRAWS_MAX;
    g_out.params = g_params;
    g_out.param_cap = XB_FRAME_PARAMS_MAX;
}

static int start(int sound)
{
    xb_config cfg = { 0, 0 };

    if (xb_init(g_state, g_pack, g_pack_size, &cfg) != XB_OK)
        return -1;
    return sound ? xb_audio_init(g_state, g_sound, xb_audio_size()) : XB_OK;
}

static uint32_t step_ms(uint32_t n)
{
    static const uint32_t steps[3] = { 17, 17, 16 };

    return steps[n % 3];
}

/* ---- time ------------------------------------------------------------------- */

static double g_tsc_us;                 /* counts of the time stamp counter per microsecond */

static void tsc_rate(void)
{
    LARGE_INTEGER f, a, b;
    uint64_t t0, t1;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    t0 = __rdtsc();
    do
        QueryPerformanceCounter(&b);
    while (b.QuadPart - a.QuadPart < f.QuadPart / 5);
    t1 = __rdtsc();
    g_tsc_us = (double)(t1 - t0) / ((double)(b.QuadPart - a.QuadPart) * 1e6 / (double)f.QuadPart);
}

#define FRAMES_MAX 600
enum { C_ADVANCE, C_FRAME, C_MESH, C_SOUND, C_KINDS };
static const char *const kind_name[C_KINDS] = { "xb_advance", "xb_frame", "xb_blob_deformed", "xb_audio" };

typedef struct frame_cost {
    uint64_t tsc[C_KINDS];              /* smallest of the runs */
    xb_real  t;
    uint32_t draws, params, samples;
    int      mesh;
} frame_cost;

static frame_cost g_cost[FRAMES_MAX];
static int g_frames;

static int timed_run(int first)
{
    uint64_t a, b, c[C_KINDS];
    uint32_t tick = 0, i, done = 0;
    int n = 0, rc, k, mesh;

    if (start(1) != XB_OK)
        return -1;
    do {
        tick += step_ms((uint32_t)n);
        a = __rdtsc();
        rc = xb_advance(g_state, tick);
        b = __rdtsc();
        c[C_ADVANCE] = b - a;
        buffers();
        a = __rdtsc();
        if (xb_frame(g_state, &g_out) != XB_OK)
            return -1;
        b = __rdtsc();
        c[C_FRAME] = b - a;
        mesh = 0;
        for (i = 0; i < g_out.draw_count && !mesh; i++)
            mesh = g_out.draws[i].mesh == XB_MESH_BLOB;
        c[C_MESH] = 0;
        if (mesh) {
            a = __rdtsc();
            xb_blob_deformed(g_state, g_mesh);
            b = __rdtsc();
            c[C_MESH] = b - a;
        }
        i = tick * 48 - done;
        a = __rdtsc();
        xb_audio(g_state, g_samples, i);
        b = __rdtsc();
        c[C_SOUND] = b - a;
        done += i;
        if (n < FRAMES_MAX) {
            frame_cost *f = &g_cost[n];

            for (k = 0; k < C_KINDS; k++)
                if (first || c[k] < f->tsc[k])
                    f->tsc[k] = c[k];
            f->t = xb_time(g_state);
            f->draws = g_out.draw_count;
            f->params = g_out.param_count;
            f->samples = i;
            f->mesh = mesh;
        }
        n++;
    } while (rc == XB_OK && n < FRAMES_MAX);
    g_frames = n;
    return 0;
}

/* Smallest time of `reps` calls of fn, in microseconds. */
static double timed(void (*fn)(void *), void *arg, int reps)
{
    uint64_t a, b, best = ~0ull;
    int i;

    for (i = 0; i < reps; i++) {
        a = __rdtsc();
        fn(arg);
        b = __rdtsc();
        if (b - a < best)
            best = b - a;
    }
    return (double)best / g_tsc_us;
}

/* ---- instructions ----------------------------------------------------------- */

enum { F_ADD, F_MUL, F_DIV, F_SQRT, F_OPS };
enum { W_FLOAT, W_DOUBLE, W_PACKED, W_KINDS };

typedef struct steps {
    uint64_t n;
    uint64_t fp[W_KINDS][F_OPS];
    uint64_t conv, imul, idiv, calls;
    uint64_t rsp_min;
} steps;

static volatile int g_stepping;
static steps g_steps;

static void classify(const unsigned char *p)
{
    int f2 = 0, f3 = 0, p66 = 0, w, op;

    for (;; p++) {
        if (*p == 0xF2)
            f2 = 1;
        else if (*p == 0xF3)
            f3 = 1;
        else if (*p == 0x66)
            p66 = 1;
        else if (*p != 0x2E && *p != 0x36 && *p != 0x3E && *p != 0x26 && *p != 0x64 && *p != 0x65 && *p != 0x67)
            break;
    }
    if ((*p & 0xF0) == 0x40)
        p++;                            /* REX */
    if (*p == 0xE8 || (*p == 0xFF && ((p[1] >> 3) & 7) == 2)) {
        g_steps.calls++;
        return;
    }
    if (*p == 0x69 || *p == 0x6B) {
        g_steps.imul++;
        return;
    }
    if (*p == 0xF7 || *p == 0xF6) {
        int r = (p[1] >> 3) & 7;

        if (r == 4 || r == 5)
            g_steps.imul++;
        else if (r == 6 || r == 7)
            g_steps.idiv++;
        return;
    }
    if (*p != 0x0F)
        return;
    op = p[1];
    if (op == 0xAF) {
        g_steps.imul++;
        return;
    }
    if (op == 0x5A || op == 0x5B || op == 0x2A || op == 0x2C || op == 0x2D || op == 0xE6) {
        g_steps.conv++;
        return;
    }
    w = f3 ? W_FLOAT : f2 ? W_DOUBLE : W_PACKED;
    (void)p66;
    if (op == 0x58 || op == 0x5C)
        g_steps.fp[w][F_ADD]++;
    else if (op == 0x59)
        g_steps.fp[w][F_MUL]++;
    else if (op == 0x5E)
        g_steps.fp[w][F_DIV]++;
    else if (op == 0x51)
        g_steps.fp[w][F_SQRT]++;
}

static LONG CALLBACK on_step(EXCEPTION_POINTERS *e)
{
    CONTEXT *c = e->ContextRecord;

    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;
    if (!g_stepping) {
        c->EFlags &= ~0x100u;                   /* the trap flag stays off */
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    /* the trap comes after an instruction; Rip is the next one, which will
     * run: count and classify that one */
    g_steps.n++;
    if (c->Rsp < g_steps.rsp_min)
        g_steps.rsp_min = c->Rsp;
    classify((const unsigned char *)c->Rip);
    c->EFlags |= 0x100;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void nothing(void *arg)
{
    (void)arg;
}

static __declspec(noinline) void stepped_call(void (*fn)(void *), void *arg, steps *out, uint64_t *rsp0)
{
    volatile uint64_t mark = 0;

    memset(&g_steps, 0, sizeof g_steps);
    g_steps.rsp_min = ~0ull;
    *rsp0 = (uint64_t)(uintptr_t)&mark;
    g_stepping = 1;
    __writeeflags(__readeflags() | 0x100);
    fn(arg);
    g_stepping = 0;
    *out = g_steps;
}

/* Steps of fn(arg) alone: the harness's own instructions, measured with an
 * empty function, are taken off. Stack: bytes below the harness's frame,
 * less what the empty call needs. */
static void stepped(void (*fn)(void *), void *arg, steps *out, uint64_t *stack)
{
    steps base, s;
    uint64_t rsp0, rsp1;
    int w, o;

    stepped_call(nothing, NULL, &base, &rsp0);
    stepped_call(fn, arg, &s, &rsp1);
    *stack = (rsp1 - s.rsp_min) - (rsp0 - base.rsp_min);
    s.n -= base.n;
    s.calls -= base.calls;
    s.conv -= base.conv;
    s.imul -= base.imul;
    s.idiv -= base.idiv;
    for (w = 0; w < W_KINDS; w++)
        for (o = 0; o < F_OPS; o++)
            s.fp[w][o] -= base.fp[w][o];
    *out = s;
}

/* A loop whose instructions are known: n times mulss, dec, jnz. Two
 * lengths, so that entry and return cancel. */
static void known_loop(void *arg)
{
    uint32_t n = *(uint32_t *)arg;
    float x = 1.0f;

    __asm__ volatile("1: mulss %%xmm0, %%xmm0\n\tdecl %%ecx\n\tjnz 1b" : "+x"(x), "+c"(n) : : "cc");
    (void)x;
}

static int counter_ok(void)
{
    steps a, b;
    uint64_t sa, sb;
    uint32_t n1 = 1000, n2 = 3000;
    int ok;

    stepped(known_loop, &n1, &a, &sa);
    stepped(known_loop, &n2, &b, &sb);
    ok = b.n - a.n == 6000 && b.fp[W_FLOAT][F_MUL] - a.fp[W_FLOAT][F_MUL] == 2000 && sa == sb;
    printf("%s  counter: a loop of mulss, dec, jnz 3000 times against 1000 times: %llu instructions more "
           "(6000), %llu float multiplications more (2000), stack %llu and %llu\n", ok ? "ok  " : "FAIL",
           (unsigned long long)(b.n - a.n), (unsigned long long)(b.fp[W_FLOAT][F_MUL] - a.fp[W_FLOAT][F_MUL]),
           (unsigned long long)sa, (unsigned long long)sb);
    return ok;
}

/* ---- the calls that are measured -------------------------------------------- */

static int g_rc;
static uint32_t g_tick, g_pass, g_tex, g_n;
static unsigned char *g_texels;

static void do_advance(void *a) { (void)a; g_rc = xb_advance(g_state, g_tick); }
static void do_frame(void *a) { (void)a; buffers(); g_rc = xb_frame(g_state, &g_out); }
static void do_mesh(void *a) { (void)a; g_rc = xb_blob_deformed(g_state, g_mesh); }
static void do_sound(void *a) { (void)a; g_rc = xb_audio(g_state, g_samples, g_n); }
static void do_block(void *a) { (void)a; g_rc = xb_audio_block(g_state, &g_block); }
static void do_setup(void *a) { (void)a; buffers(); g_rc = xb_frame_setup(g_state, g_pass, &g_out); }
static void do_init(void *a) { (void)a; g_rc = start(0); }
static void do_sound_init(void *a) { (void)a; g_rc = xb_audio_init(g_state, g_sound, xb_audio_size()); }
static void do_texture(void *a) { (void)a; g_rc = xb_texture(g_state, g_tex, g_texels, 393216); }

static FILE *g_json;
static int g_json_n;

static void report(const char *what, double t, uint32_t draws, const steps *s, uint64_t stack, double us)
{
    uint64_t fl = 0, db = 0, pk = 0;
    int o;

    for (o = 0; o < F_OPS; o++) {
        fl += s->fp[W_FLOAT][o];
        db += s->fp[W_DOUBLE][o];
        pk += s->fp[W_PACKED][o];
    }
    printf("%-22s t=%6.3f %5u draws %10llu instr  float %7llu (mul %llu div %llu sqrt %llu)  double %7llu "
           "(mul %llu div %llu sqrt %llu)  packed %6llu  conv %6llu  imul %6llu idiv %4llu  calls %6llu  "
           "stack %5llu  %8.1f us\n",
           what, t, draws, (unsigned long long)s->n, (unsigned long long)fl,
           (unsigned long long)s->fp[W_FLOAT][F_MUL], (unsigned long long)s->fp[W_FLOAT][F_DIV],
           (unsigned long long)s->fp[W_FLOAT][F_SQRT], (unsigned long long)db,
           (unsigned long long)s->fp[W_DOUBLE][F_MUL], (unsigned long long)s->fp[W_DOUBLE][F_DIV],
           (unsigned long long)s->fp[W_DOUBLE][F_SQRT], (unsigned long long)pk, (unsigned long long)s->conv,
           (unsigned long long)s->imul, (unsigned long long)s->idiv, (unsigned long long)s->calls,
           (unsigned long long)stack, us);
    fflush(stdout);
    if (g_json)
        fprintf(g_json,
                "%s\n    {\"call\": \"%s\", \"t\": %.6f, \"draws\": %u, \"instructions\": %llu, \"float\": %llu, "
                "\"float_mul\": %llu, \"float_div\": %llu, \"float_sqrt\": %llu, \"double\": %llu, "
                "\"double_mul\": %llu, \"double_div\": %llu, \"double_sqrt\": %llu, \"packed\": %llu, "
                "\"conversions\": %llu, \"imul\": %llu, \"idiv\": %llu, \"calls\": %llu, \"stack\": %llu, "
                "\"us\": %.2f}",
                g_json_n++ ? "," : "", what, t, draws, (unsigned long long)s->n, (unsigned long long)fl,
                (unsigned long long)s->fp[W_FLOAT][F_MUL], (unsigned long long)s->fp[W_FLOAT][F_DIV],
                (unsigned long long)s->fp[W_FLOAT][F_SQRT], (unsigned long long)db,
                (unsigned long long)s->fp[W_DOUBLE][F_MUL], (unsigned long long)s->fp[W_DOUBLE][F_DIV],
                (unsigned long long)s->fp[W_DOUBLE][F_SQRT], (unsigned long long)pk, (unsigned long long)s->conv,
                (unsigned long long)s->imul, (unsigned long long)s->idiv, (unsigned long long)s->calls,
                (unsigned long long)stack, us);
}

/* Steps through the frames in `at`: the run goes at full speed up to each,
 * then the calls of that frame are stepped. */
static int step_frames(const int *at, int n_at)
{
    steps s;
    uint64_t stack;
    uint32_t tick = 0, done = 0, i;
    int n = 0, k = 0, rc, mesh;

    if (start(1) != XB_OK)
        return -1;
    do {
        tick += step_ms((uint32_t)n);
        g_tick = tick;
        g_n = tick * 48 - done;
        done += g_n;
        if (k < n_at && at[k] == n) {
            double us[C_KINDS];
            int j;

            for (j = 0; j < C_KINDS; j++)
                us[j] = (double)g_cost[n].tsc[j] / g_tsc_us;
            stepped(do_advance, NULL, &s, &stack);
            rc = g_rc;
            report(kind_name[C_ADVANCE], (double)xb_time(g_state), 0, &s, stack, us[C_ADVANCE]);
            stepped(do_frame, NULL, &s, &stack);
            report(kind_name[C_FRAME], (double)xb_time(g_state), g_out.draw_count, &s, stack, us[C_FRAME]);
            mesh = 0;
            for (i = 0; i < g_out.draw_count && !mesh; i++)
                mesh = g_out.draws[i].mesh == XB_MESH_BLOB;
            if (mesh) {
                stepped(do_mesh, NULL, &s, &stack);
                report(kind_name[C_MESH], (double)xb_time(g_state), 0, &s, stack, us[C_MESH]);
            }
            stepped(do_sound, NULL, &s, &stack);
            report(kind_name[C_SOUND], (double)xb_time(g_state), 0, &s, stack, us[C_SOUND]);
            k++;
        } else {
            rc = xb_advance(g_state, tick);
            xb_audio(g_state, g_samples, g_n);
        }
        if (rc < 0 || g_rc < 0)
            return -1;
        n++;
    } while (rc == XB_OK && n < FRAMES_MAX);
    return 0;
}

/* ---- main ------------------------------------------------------------------- */

typedef struct span {
    const char *name;
    xb_real from, to;
} span;

int main(int argc, char **argv)
{
    /* what a frame holds changes at these times: the blob deforms from
     * t = 0.6 (Blob.md 6.1), the coarse detail level from 5.2 (Chamber.md
     * 6), chamber and blob go at about 5.94 (Camera.md 5.5), the logo's
     * texture is drawn at 6 and 6.25 (Logo.md 6) */
    static const span spans[] = {
        { "t < 0.6: chamber fine, blob round", 0.0f, 0.6f },
        { "0.6 <= t < 5.2: chamber fine, blob deformed", 0.6f, 5.2f },
        { "5.2 <= t < 5.94: chamber coarse, blob, logo", 5.2f, 5.94f },
        { "t >= 5.94: logo and veil only", 5.94f, 99.0f },
    };
    static const int at[] = { 20, 120, 240, 311, 313, 350, 357, 360, 375, 420, 479 };
    const char *json = NULL;
    FILE *f;
    steps s;
    uint64_t stack;
    double us;
    int runs = 20, with_steps = 1, i, k, n;
    uint32_t t;

    if (argc < 2) {
        fprintf(stderr, "usage: cost_k8 <pack> [--runs N] [--no-steps] [--json <file>]\n");
        return 2;
    }
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--runs") && i + 1 < argc)
            runs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--json") && i + 1 < argc)
            json = argv[++i];
        else if (!strcmp(argv[i], "--no-steps"))
            with_steps = 0;
    }
    f = fopen(argv[1], "rb");
    if (!f)
        return 2;
    fseek(f, 0, SEEK_END);
    g_pack_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    g_pack = aligned_alloc16(g_pack_size);
    g_state = aligned_alloc16(xb_state_size());
    g_sound = aligned_alloc16(xb_audio_size());
    g_texels = malloc(393216);
    if (!g_pack || !g_state || !g_sound || !g_texels || fread(g_pack, 1, g_pack_size, f) != g_pack_size)
        return 2;
    fclose(f);
    if (json) {
        g_json = fopen(json, "w");
        if (!g_json)
            return 2;
    }

    /* keep the thread on one processor and ahead of others, for the counter */
    SetThreadAffinityMask(GetCurrentThread(), 1);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    tsc_rate();

    printf("memory: state %u, sound %u, pack %u, xb_draw %u, xb_frame_out %u, xb_audio_out %u bytes; buffers for any "
           "frame %u draws = %u bytes and %u values = %u bytes; deformed mesh %u bytes\n",
           (unsigned)xb_state_size(), (unsigned)xb_audio_size(), (unsigned)g_pack_size, (unsigned)sizeof(xb_draw),
           (unsigned)sizeof(xb_frame_out), (unsigned)sizeof(xb_audio_out), XB_FRAME_DRAWS_MAX,
           (unsigned)sizeof g_draws, XB_FRAME_PARAMS_MAX, (unsigned)sizeof g_params, (unsigned)sizeof g_mesh);
    printf("time stamp counter: %.1f counts per microsecond\n", g_tsc_us);
    if (g_json)
        fprintf(g_json, "{\n  \"tsc_per_us\": %.2f,\n  \"runs\": %d,\n  \"state_bytes\": %u,\n  \"sound_bytes\": %u,\n",
                g_tsc_us, runs, (unsigned)xb_state_size(), (unsigned)xb_audio_size());

    /* time of every frame */
    for (i = 0; i < runs; i++)
        if (timed_run(i == 0) != 0)
            return 1;
    printf("\ntime per frame at 60 Hz, smallest of %d runs, microseconds (mean and largest over the frames):\n", runs);
    if (g_json)
        fprintf(g_json, "  \"spans\": [");
    for (k = 0; k <= (int)(sizeof spans / sizeof spans[0]); k++) {
        double sum[C_KINDS] = { 0 }, max[C_KINDS] = { 0 }, all = 0, all_max = 0;
        uint64_t draws = 0, params = 0;
        uint32_t max_draws = 0;
        int whole = k == (int)(sizeof spans / sizeof spans[0]), j;

        n = 0;
        for (i = 0; i < g_frames; i++) {
            double fr = 0;

            if (!whole && !(g_cost[i].t >= spans[k].from && g_cost[i].t < spans[k].to))
                continue;
            for (j = 0; j < C_KINDS; j++) {
                us = (double)g_cost[i].tsc[j] / g_tsc_us;
                sum[j] += us;
                if (us > max[j])
                    max[j] = us;
                fr += us;
            }
            all += fr;
            if (fr > all_max)
                all_max = fr;
            draws += g_cost[i].draws;
            params += g_cost[i].params;
            if (g_cost[i].draws > max_draws)
                max_draws = g_cost[i].draws;
            n++;
        }
        if (!n)
            continue;
        printf("%-46s %3d frames, %6.1f draws (max %4u), %7.0f values | advance %6.1f (%6.1f)  frame %7.1f "
               "(%7.1f)  mesh %6.1f (%6.1f)  sound %6.1f (%6.1f) | together %7.1f (%7.1f)\n",
               whole ? "the whole run" : spans[k].name, n, (double)draws / n, max_draws, (double)params / n,
               sum[C_ADVANCE] / n, max[C_ADVANCE], sum[C_FRAME] / n, max[C_FRAME], sum[C_MESH] / n, max[C_MESH],
               sum[C_SOUND] / n, max[C_SOUND], all / n, all_max);
        if (g_json)
            fprintf(g_json,
                    "%s\n    {\"span\": \"%s\", \"frames\": %d, \"draws_mean\": %.1f, \"draws_max\": %u, "
                    "\"values_mean\": %.0f, \"advance_us\": [%.2f, %.2f], \"frame_us\": [%.2f, %.2f], "
                    "\"mesh_us\": [%.2f, %.2f], \"sound_us\": [%.2f, %.2f], \"together_us\": [%.2f, %.2f]}",
                    k ? "," : "", whole ? "the whole run" : spans[k].name, n, (double)draws / n, max_draws,
                    (double)params / n, sum[C_ADVANCE] / n, max[C_ADVANCE], sum[C_FRAME] / n, max[C_FRAME],
                    sum[C_MESH] / n, max[C_MESH], sum[C_SOUND] / n, max[C_SOUND], all / n, all_max);
        if (whole)
            printf("the whole run: %.1f ms of the core for %d frames and %.1f s of sound\n", all / 1000.0, n,
                   (double)g_cost[g_frames - 1].t);
    }
    if (g_json)
        fprintf(g_json, "\n  ],\n  \"calls\": [");

    /* calls outside the frames: time, and with steps the instructions */
    AddVectoredExceptionHandler(1, on_step);
    printf("\ncalls a host makes once%s:\n", with_steps ? ", and frames stepped" : "");
    if (with_steps && !counter_ok())
        return 1;
    memset(&s, 0, sizeof s);
    stack = 0;
    us = timed(do_init, NULL, 50);
    if (with_steps)
        stepped(do_init, NULL, &s, &stack);
    report("xb_init", 0, 0, &s, stack, us);
    us = timed(do_sound_init, NULL, 50);
    if (with_steps)
        stepped(do_sound_init, NULL, &s, &stack);
    report("xb_audio_init", 0, 0, &s, stack, us);
    for (g_pass = 0; g_pass < 2; g_pass++) {
        us = timed(do_setup, NULL, 50);
        if (with_steps)
            stepped(do_setup, NULL, &s, &stack);
        report(g_pass ? "xb_frame_setup 1 (of 6)" : "xb_frame_setup 0", 5.35, g_out.draw_count, &s, stack, us);
    }
    for (t = 0; t < XB_TEXTURES; t++) {
        static const char *const names[XB_TEXTURES] = {
            "xb_texture cube128", "xb_texture cube64", "xb_texture bump", "xb_texture falloff",
            "xb_texture gradient_a", "xb_texture radial", "xb_texture gradient_c", "xb_texture tm",
            "xb_texture noise0", "xb_texture noise1", "xb_texture noise2"
        };

        g_tex = t;
        us = timed(do_texture, NULL, 5);
        memset(&s, 0, sizeof s);
        stack = 0;
        /* textures are stepped only where they are small; the stack of the
         * others is that of a smaller one of the same generator */
        if (with_steps && (t == XB_TEX_CUBE64 || t == XB_TEX_GRADIENT_A || t == XB_TEX_TM || t == XB_TEX_BUMP ||
                           t == XB_TEX_NOISE0))
            stepped(do_texture, NULL, &s, &stack);
        report(names[t], 0, 0, &s, stack, us);
    }
    /* one block of sound with a tick of the sequencer and one without, in
     * the middle of the sound (block 6003 begins with a tick: 6003 = 9 * 667) */
    if (start(1) != XB_OK)
        return 1;
    for (i = 0; i < 6003; i++)
        xb_audio_block(g_state, &g_block);
    for (i = 0; i < 2; i++) {
        memset(&s, 0, sizeof s);
        stack = 0;
        if (with_steps)
            stepped(do_block, NULL, &s, &stack);
        else
            do_block(NULL);
        report(g_block.tick ? "xb_audio_block, tick" : "xb_audio_block", (double)g_block.block * 32 / 48000.0, 0, &s,
               stack, 0);
    }
    if (with_steps && step_frames(at, (int)(sizeof at / sizeof at[0])) != 0)
        return 1;
    if (g_json) {
        fprintf(g_json, "\n  ]\n}\n");
        fclose(g_json);
    }
    return 0;
}

#endif
