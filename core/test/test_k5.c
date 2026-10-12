/* Host test of milestone K5: the textures against docs/vectors/c8_gpu.json.
 *
 *     test_k5 <pack> <c8_gpu.json>
 *     test_k5 <pack> --dump <dir>
 *     test_k5 <pack> --digest
 *
 * First form, for each of the eleven textures the set-up makes on the CPU
 * (section "textures" of the vector, in the order of the set-up and of
 * XB_TEX_*):
 *   - the model met the guest's dump (differ 0 in the vector);
 *   - the format word and the length of the data equal the guest's
 *     resource;
 *   - the SHA-256 of the core's texels equals the SHA-256 of the guest's
 *     dump (run c8_main2, read from the guest's memory at the end of the
 *     set-up).
 * Also: the seed of the noise equals the vector's fx_seed, and xb_texture
 * rejects a buffer one byte short and an unknown texture. Then all textures,
 * made twice from two fresh states, must give the same digest (--digest).
 *
 * --dump writes texture i as <dir>/<i>.bin for check_k5.py.
 * --digest folds every texture, in the order of XB_TEX_*, with its
 * description into an FNV-1a digest and prints "digest <16 hex digits> 11".
 * The wasm build prints the same line (core/test/wasm_k5.mjs).
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

#define TEX_MAX (6u * 128u * 128u * 4u)

static const char *const tex_name[XB_TEXTURES] = {
    "cube128", "cube64", "bump", "falloff", "gradient_a", "radial", "gradient_c", "tm", "noise0", "noise1",
    "noise2"
};

/* ---- against the vector -------------------------------------------------- */

static void check_textures(const xb_state *s, const json_doc *d, uint8_t *buf)
{
    int cases = json_path(d, 0, "textures/cases"), i;
    char what[400];

    check(d->nodes[cases].count == XB_TEXTURES, "c8_gpu.json: eleven textures made on the CPU");
    for (i = 0; i < XB_TEXTURES && i < d->nodes[cases].count; i++) {
        int c = json_at(d, cases, i), sh = json_get(d, c, "sha256");
        xb_texture_desc in;
        char got[65];
        int differ = (int)json_num(d, json_get(d, c, "differ")), ok;

        xb_texture_info(s, (uint32_t)i, &in);
        ok = xb_texture(s, (uint32_t)i, buf, TEX_MAX) == XB_OK;
        sha256_hex(buf, in.bytes, got);
        ok = ok && differ == 0 && in.format_word == hex(d, json_get(d, c, "format")) &&
             in.bytes == (uint32_t)json_num(d, json_get(d, c, "bytes")) && sh >= 0 &&
             d->nodes[sh].len == 64 && memcmp(d->nodes[sh].text, got, 64) == 0;
        snprintf(what, sizeof what, "%-10s %.*s: %ux%u, %u level(s), %u face(s), format %08X, %u bytes, "
                 "SHA-256 %.16s... %s the guest's dump", tex_name[i], (int)d->nodes[json_get(d, c, "field")].len,
                 d->nodes[json_get(d, c, "field")].text, in.width, in.height, in.levels, in.faces,
                 (unsigned)in.format_word, in.bytes, got, ok ? "equals" : "differs from");
        check(ok, what);
    }
}

/* ---- digest -------------------------------------------------------------- */

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

static void fold_bytes(digest *g, const uint8_t *p, size_t n)
{
    size_t i;

    for (i = 0; i + 4 <= n; i += 4)
        fold(g, (uint32_t)p[i] | (uint32_t)p[i + 1] << 8 | (uint32_t)p[i + 2] << 16 | (uint32_t)p[i + 3] << 24);
}

static int run_digest(xb_state *s, const void *pack, size_t pack_size, uint8_t *buf, uint64_t *out)
{
    digest g = { 0xCBF29CE484222325ull };
    xb_config cfg = { 0, 0 };
    uint32_t i;

    if (xb_init(s, pack, pack_size, &cfg) != XB_OK)
        return -1;
    for (i = 0; i < XB_TEXTURES; i++) {
        xb_texture_desc in;

        if (xb_texture_info(s, i, &in) != XB_OK || xb_texture(s, i, buf, TEX_MAX) != XB_OK)
            return -1;
        fold_bytes(&g, (const uint8_t *)&in, sizeof in);
        fold_bytes(&g, buf, in.bytes);
    }
    *out = g.h;
    return 0;
}

static int dump(const xb_state *s, uint8_t *buf, const char *dir)
{
    char path[512];
    uint32_t i;

    for (i = 0; i < XB_TEXTURES; i++) {
        xb_texture_desc in;
        FILE *f;

        xb_texture_info(s, i, &in);
        if (xb_texture(s, i, buf, TEX_MAX) != XB_OK)
            return 2;
        snprintf(path, sizeof path, "%s/%u.bin", dir, (unsigned)i);
        f = fopen(path, "wb");
        if (!f || fwrite(buf, 1, in.bytes, f) != in.bytes)
            return 2;
        fclose(f);
    }
    return 0;
}

int main(int argc, char **argv)
{
    void *pack_base, *state_base, *buf_base;
    size_t pack_size;
    unsigned char *pack;
    uint8_t *buf;
    xb_state *s;
    xb_config cfg = { 0, 0 };
    xb_texture_desc in;
    xb_timing tm;
    json_doc d;
    uint64_t dg[2];
    char what[256];
    int c;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: test_k5 <pack> <c8_gpu.json> | --dump <dir> | --digest\n");
        return 2;
    }
    pack = read_file(argv[1], &pack_size, &pack_base);
    s = aligned_alloc16(xb_state_size(), &state_base);
    buf = aligned_alloc16(TEX_MAX, &buf_base);
    if (!pack || !s || !buf) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    if (xb_init(s, pack, pack_size, &cfg) != XB_OK)
        return 2;
    if (argc == 4 && strcmp(argv[2], "--dump") == 0)
        return dump(s, buf, argv[3]);
    if (argc == 3 && strcmp(argv[2], "--digest") == 0) {
        if (run_digest(s, pack, pack_size, buf, &dg[0]) != 0)
            return 2;
        printf("digest %016llX %d\n", (unsigned long long)dg[0], XB_TEXTURES);
        return 0;
    }

    printf("state %u bytes, pack %u bytes; xb_texture_desc %u bytes\n", (unsigned)xb_state_size(),
           (unsigned)pack_size, (unsigned)sizeof(xb_texture_desc));
    if (json_load(&d, argv[2]) != 0)
        return 2;
    xb_get_timing(s, &tm);
    snprintf(what, sizeof what, "seed of the noise %08X, vector fx_seed %.*s", (unsigned)tm.rng,
             (int)d.nodes[json_path(&d, 0, "textures/fx_seed")].len, d.nodes[json_path(&d, 0, "textures/fx_seed")].text);
    check(tm.rng == hex(&d, json_path(&d, 0, "textures/fx_seed")), what);
    check_textures(s, &d, buf);

    xb_texture_info(s, XB_TEX_FALLOFF, &in);
    c = xb_texture(s, XB_TEX_FALLOFF, buf, in.bytes - 1) == XB_ERR_ARG &&
        xb_texture(s, XB_TEXTURES, buf, TEX_MAX) == XB_ERR_ARG && xb_texture_info(s, XB_TEXTURES, &in) == XB_ERR_ARG;
    check(c, "xb_texture rejects a buffer one byte short and an unknown texture");

    c = run_digest(s, pack, pack_size, buf, &dg[0]);
    c |= run_digest(s, pack, pack_size, buf, &dg[1]);
    snprintf(what, sizeof what, "all textures, twice from a fresh state: digest %016llX and %016llX",
             (unsigned long long)dg[0], (unsigned long long)dg[1]);
    check(c == 0 && dg[0] == dg[1], what);
    printf("digest %016llX %d\n", (unsigned long long)dg[0], XB_TEXTURES);

    json_free(&d);
    free(pack_base);
    free(state_base);
    free(buf_base);
    printf("%d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
