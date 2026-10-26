#include "openconsole/uefi_update_crypto.h"

static int bytes_equal(const uint8_t *left, const uint8_t *right, size_t size) {
    uint8_t difference = 0u;
    while (size-- != 0u) difference |= (uint8_t)(*left++ ^ *right++);
    return difference == 0u;
}

static void bytes_copy(uint8_t *out, const uint8_t *source, size_t size) {
    while (size-- != 0u) *out++ = *source++;
}

static int verify_callback(void *context,
                           const uint8_t key[OC_UPDATE_PUBLIC_KEY_BYTES],
                           const uint8_t *message, size_t message_size,
                           const uint8_t signature[OC_UPDATE_SIGNATURE_BYTES]) {
    oc_uefi_update_crypto *adapter = context;
    if (adapter == 0 || adapter->protocol == 0 || message_size > (size_t)(~(UINTN)0))
        return 0;
    return !EFI_ERROR(adapter->protocol->ed448_verify(
        adapter->protocol, key, message, (UINTN)message_size, signature));
}

static int hash_callback(void *context, const uint8_t *data, size_t size,
                         uint8_t digest[32]) {
    oc_uefi_update_crypto *adapter = context;
    if (adapter == 0 || adapter->protocol == 0 || size > (size_t)(~(UINTN)0))
        return 0;
    return !EFI_ERROR(adapter->protocol->sha256(
        adapter->protocol, data, (UINTN)size, digest));
}

static int lookup_callback(void *context, const uint8_t id[16],
                           uint8_t key[OC_UPDATE_PUBLIC_KEY_BYTES]) {
    oc_uefi_update_crypto *adapter = context;
    size_t index;
    if (adapter == 0 || id == 0 || key == 0) return 0;
    for (index = 0u; index < adapter->key_count; ++index) {
        if (bytes_equal(id, adapter->keys[index].id, 16u)) {
            bytes_copy(key, adapter->keys[index].public_key,
                       OC_UPDATE_PUBLIC_KEY_BYTES);
            return 1;
        }
    }
    return 0;
}

EFI_STATUS oc_uefi_update_crypto_initialize(oc_uefi_update_crypto *adapter,
                                            OC_CRYPTO_PROTOCOL *protocol,
                                            const oc_update_trusted_key *keys,
                                            size_t key_count) {
    if (adapter == 0 || protocol == 0 || keys == 0 || key_count == 0u ||
        key_count > OC_UPDATE_TRUSTED_KEY_MAX ||
        protocol->revision != OC_CRYPTO_PROTOCOL_REVISION ||
        protocol->structure_size < sizeof(*protocol) || protocol->sha256 == 0 ||
        protocol->ed448_verify == 0)
        return EFI_INVALID_PARAMETER;
    adapter->protocol = protocol;
    adapter->keys = keys;
    adapter->key_count = key_count;
    adapter->callbacks.context = adapter;
    adapter->callbacks.ed448_verify = verify_callback;
    adapter->callbacks.sha256 = hash_callback;
    adapter->callbacks.lookup_key = lookup_callback;
    return EFI_SUCCESS;
}
