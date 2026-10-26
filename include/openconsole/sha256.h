/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OPENCONSOLE_SHA256_H
#define OPENCONSOLE_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} oc_sha256_context;

void oc_sha256_init(oc_sha256_context *);
int oc_sha256_update(oc_sha256_context *, const void *, size_t);
void oc_sha256_final(oc_sha256_context *, uint8_t out[32]);
int oc_sha256(const void *, size_t, uint8_t out[32]);

#endif
