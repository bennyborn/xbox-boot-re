/* Reads the data pack written by tools/pack/pack.py. Format: docs/core.md,
 * "Datenpaket". The pack holds ranges of the unpacked kernel unchanged;
 * the core finds them by start address and length. */
#include "core_internal.h"

#define PACK_MAGIC   0x4B504258u   /* "XBPK" */
#define PACK_VERSION 1u
#define PACK_HEADER  16u
#define PACK_ENTRY   32u

/* Start address and length of every range the core needs. The same list
 * is in tools/pack/pack.py; the evidence is in the extractor named there. */
static const struct { uint32_t va, size; } wanted[PACK_COUNT] = {
    [PACK_SEQUENCE]   = { 0x8006E908u, 0x6F8u },    /* Audio.md 3 */
    [PACK_ENVBLOCKS]  = { 0x800A7980u, 0x230u },    /* Audio.md 5 */
    [PACK_SEQ_PTRS]   = { 0x800A7BB0u, 0x64u },     /* Audio.md 3.1 */
    [PACK_GP_PROGRAM] = { 0x80053568u, 0x1894u },   /* Audio.md 6 */
    [PACK_PCM_SIGNED] = { 0x80066AC0u, 0x5540u },   /* Audio.md 5 */
    [PACK_PCM_A]      = { 0x8006C008u, 0xEB8u },    /* Audio.md 5 */
    [PACK_PCM_B]      = { 0x8006CEC8u, 0x1A3Fu },   /* Audio.md 5 */
    [PACK_ASIN]       = { 0x80066030u, 0xC4u },     /* Chamber.md 11, asin_approx */
    [PACK_CAMPATH]    = { 0x80070DB8u, 0x24Cu },    /* Camera.md */
    [PACK_LOGO]       = { 0x80071008u, 0x33C8u },   /* Logo.md */
    [PACK_SCENE]      = { 0x800743D0u, 0x4134u },   /* Chamber.md */
};

int pack_open(pack_range out[PACK_COUNT], const uint8_t *pack, size_t size)
{
    uint32_t count, total, i, k, prev_end = 0;

    if (size < PACK_HEADER)
        return XB_ERR_PACK_LAYOUT;
    if (rd_u32(pack) != PACK_MAGIC)
        return XB_ERR_PACK_MAGIC;
    if (rd_u32(pack + 4) != PACK_VERSION)
        return XB_ERR_PACK_VERSION;
    count = rd_u32(pack + 8);
    total = rd_u32(pack + 12);
    if (total != size || count > (size - PACK_HEADER) / PACK_ENTRY)
        return XB_ERR_PACK_LAYOUT;

    for (k = 0; k < PACK_COUNT; k++)
        out[k].bytes = 0;

    for (i = 0; i < count; i++) {
        const uint8_t *e = pack + PACK_HEADER + i * PACK_ENTRY;
        uint32_t va = rd_u32(e), len = rd_u32(e + 4), off = rd_u32(e + 8);

        /* sorted by address, no overlap, inside the pack, aligned */
        if ((i > 0 && va < prev_end) || len == 0 || va + len < va)
            return XB_ERR_PACK_LAYOUT;
        if (off % XB_ALIGN != 0 || off < PACK_HEADER + count * PACK_ENTRY ||
            off > total || len > total - off)
            return XB_ERR_PACK_LAYOUT;
        prev_end = va + len;

        for (k = 0; k < PACK_COUNT; k++) {
            if (wanted[k].va == va && wanted[k].size == len) {
                out[k].bytes = pack + off;
                out[k].va = va;
                out[k].size = len;
            }
        }
    }

    for (k = 0; k < PACK_COUNT; k++)
        if (!out[k].bytes)
            return XB_ERR_PACK_MISSING;
    return XB_OK;
}
