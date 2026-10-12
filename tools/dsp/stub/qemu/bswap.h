/* Stand-in for QEMU's bswap.h: little-endian loads and stores (x86 host). Own file. */
#pragma once
#include "osdep.h"
static inline uint32_t ldl_le_p(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline void stl_le_p(void *p, uint32_t v) { memcpy(p, &v, 4); }
