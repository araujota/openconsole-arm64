#ifndef OPENCONSOLE_UEFI_UPDATE_CRYPTO_H
#define OPENCONSOLE_UEFI_UPDATE_CRYPTO_H

#include "openconsole/crypto_protocol.h"
#include "openconsole/update_verify.h"

#define OC_UPDATE_TRUSTED_KEY_MAX 4u

typedef struct {
    uint8_t id[16];
    uint8_t public_key[OC_UPDATE_PUBLIC_KEY_BYTES];
} oc_update_trusted_key;

typedef struct {
    OC_CRYPTO_PROTOCOL *protocol;
    const oc_update_trusted_key *keys;
    size_t key_count;
    oc_update_crypto callbacks;
} oc_uefi_update_crypto;

EFI_STATUS oc_uefi_update_crypto_initialize(oc_uefi_update_crypto *,
                                            OC_CRYPTO_PROTOCOL *,
                                            const oc_update_trusted_key *,
                                            size_t);

#endif
