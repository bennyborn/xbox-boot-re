/* SHA-256 for the host tests. Test code only, not part of the core. */
#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

/* Lowercase hex digest of `len` bytes, 64 characters and a terminating 0. */
void sha256_hex(const void *data, size_t len, char out[65]);

#endif
