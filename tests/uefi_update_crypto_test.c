#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "openconsole/uefi_update_crypto.h"

static unsigned hash_calls, verify_calls;
static EFI_STATUS EFIAPI hash(OC_CRYPTO_PROTOCOL *self, const uint8_t *data,
                              UINTN size, uint8_t out[32]) {
    (void)self; (void)data; (void)size; memset(out, 0x5a, 32); ++hash_calls;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI verify(OC_CRYPTO_PROTOCOL *self, const uint8_t key[57],
                                const uint8_t *message, UINTN size,
                                const uint8_t signature[114]) {
    (void)self; (void)message; (void)size; ++verify_calls;
    return key[0] == 0x42 && signature[0] == 0x24 ? EFI_SUCCESS : EFI_SECURITY_VIOLATION;
}
int main(void) {
    OC_CRYPTO_PROTOCOL protocol = {OC_CRYPTO_PROTOCOL_REVISION,
        sizeof(OC_CRYPTO_PROTOCOL), hash, verify};
    oc_update_trusted_key keys[2];
    oc_uefi_update_crypto adapter;
    uint8_t out[57], digest[32], signature[114] = {0x24};
    memset(keys, 0, sizeof(keys)); keys[0].id[0] = 1; keys[0].public_key[0] = 0x42;
    keys[1].id[0] = 2; keys[1].public_key[0] = 0x43;
    assert(oc_uefi_update_crypto_initialize(&adapter, &protocol, keys, 2) == EFI_SUCCESS);
    assert(adapter.callbacks.lookup_key(adapter.callbacks.context, keys[0].id, out));
    assert(out[0] == 0x42);
    keys[0].id[0] = 9;
    { uint8_t missing[16] = {7}; assert(!adapter.callbacks.lookup_key(adapter.callbacks.context, missing, out)); }
    assert(adapter.callbacks.sha256(adapter.callbacks.context, (const uint8_t *)"x", 1, digest));
    assert(digest[0] == 0x5a && hash_calls == 1);
    assert(adapter.callbacks.ed448_verify(adapter.callbacks.context, out,
        (const uint8_t *)"x", 1, signature) && verify_calls == 1);
    protocol.revision = 2;
    assert(oc_uefi_update_crypto_initialize(&adapter, &protocol, keys, 2) == EFI_INVALID_PARAMETER);
    assert(oc_uefi_update_crypto_initialize(&adapter, &protocol, keys,
        OC_UPDATE_TRUSTED_KEY_MAX + 1u) == EFI_INVALID_PARAMETER);
    puts("PASS: bounded UEFI crypto protocol adapter and trusted-key lookup");
    return 0;
}
