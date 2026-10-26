#ifndef OPENCONSOLE_UPDATE_VERIFY_H
#define OPENCONSOLE_UPDATE_VERIFY_H

#include "openconsole/update_metadata.h"

typedef int (*oc_ed448_verify_fn)(void *,
                                 const uint8_t[OC_UPDATE_PUBLIC_KEY_BYTES],
                                 const uint8_t *, size_t,
                                 const uint8_t[OC_UPDATE_SIGNATURE_BYTES]);
typedef int (*oc_sha256_fn)(void *, const uint8_t *, size_t, uint8_t[32]);
typedef int (*oc_key_lookup_fn)(void *, const uint8_t[16],
                                uint8_t[OC_UPDATE_PUBLIC_KEY_BYTES]);

typedef struct {
    void *context;
    oc_ed448_verify_fn ed448_verify;
    oc_sha256_fn sha256;
    oc_key_lookup_fn lookup_key;
} oc_update_crypto;

typedef struct {
    const char *requested_host;
    const char *verified_host;
    uint8_t peer_chain_verified;
    uint8_t hostname_verified;
} oc_https_identity;

typedef enum {
    OC_UPDATE_VERIFY_OK = 0,
    OC_UPDATE_VERIFY_INVALID,
    OC_UPDATE_VERIFY_TLS_REJECTED,
    OC_UPDATE_VERIFY_WRONG_HOST,
    OC_UPDATE_VERIFY_UNKNOWN_KEY,
    OC_UPDATE_VERIFY_BAD_SIGNATURE,
    OC_UPDATE_VERIFY_POLICY_REJECTED,
    OC_UPDATE_VERIFY_ARTIFACT_SIZE,
    OC_UPDATE_VERIFY_ARTIFACT_DIGEST
} oc_update_verify_result;

oc_update_verify_result oc_update_verify(const oc_update_crypto *,
                                         const oc_https_identity *,
                                         const oc_update_policy *, uint64_t,
                                         const uint8_t *, size_t,
                                         const uint8_t *, size_t,
                                         oc_update_policy *,
                                         oc_metadata_result *);

#endif
