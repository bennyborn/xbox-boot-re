/* Stand-in for QEMU's osdep.h: only what xemu's DSP core needs. Own file. */
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <assert.h>
#include <stddef.h>
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define likely(x) (x)
#define unlikely(x) (x)
#define QEMU_FALLTHROUGH
#define G_GNUC_UNUSED
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
