#include "openconsole/crypto_protocol.h"
#include "openconsole/uefi_update_crypto.h"

static void debug_write(const char *text) {
    while (*text != '\0') {
        __asm__ volatile("outb %0, %1" : : "a"((uint8_t)*text), "Nd"((uint16_t)0x402));
        ++text;
    }
}

static int bytes_equal(const uint8_t *left, const uint8_t *right, UINTN size) {
    while (size-- != 0u) if (*left++ != *right++) return 0;
    return 1;
}

static void fill(uint8_t *out, uint8_t value, UINTN size) {
    while (size-- != 0u) *out++ = value;
}
static void copy(uint8_t *out, const uint8_t *source, UINTN size) {
    while (size-- != 0u) *out++ = *source++;
}
static void w16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)value; out[1] = (uint8_t)(value >> 8);
}
static void w32(uint8_t *out, uint32_t value) {
    unsigned i; for (i = 0u; i < 4u; ++i) out[i] = (uint8_t)(value >> (8u * i));
}
static void w64(uint8_t *out, uint64_t value) {
    unsigned i; for (i = 0u; i < 8u; ++i) out[i] = (uint8_t)(value >> (8u * i));
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    static const uint8_t message[] = "OpenConsole UEFI Ed448 probe v1";
    static const uint8_t public_key[OC_CRYPTO_ED448_PUBLIC_KEY_BYTES] = {
        0xda,0x91,0x8b,0xa3,0xe5,0x7f,0xdc,0xa0,0x32,0x6f,0x46,0xc7,0xec,0x84,0x3b,0xa8,
        0xfc,0xb0,0xd5,0x7f,0xa1,0x5f,0x25,0x88,0xa5,0x7b,0xae,0x9d,0xf5,0x58,0x21,0x03,
        0x51,0xe7,0xe1,0x55,0x81,0xb2,0x44,0x59,0xc0,0xa7,0xcd,0xe1,0xe8,0x35,0x58,0x2d,
        0x71,0x7c,0x06,0x99,0xea,0x72,0xe8,0xc9,0x00
    };
    static const uint8_t signature[OC_CRYPTO_ED448_SIGNATURE_BYTES] = {
        0x84,0x74,0x03,0x0f,0xce,0xc6,0x38,0x4f,0xf4,0x68,0xa0,0x2d,0x0d,0xa1,0xbf,0xb6,
        0xbc,0x82,0x8b,0x68,0x6e,0xfe,0xbd,0x07,0x12,0xe3,0x11,0x47,0xcf,0x54,0x18,0x82,
        0x11,0xee,0x47,0x3c,0xd6,0x3c,0xdc,0xdb,0xee,0xac,0x1c,0xb0,0xce,0x08,0x0e,0x4a,
        0x84,0x35,0x3f,0xef,0x09,0x00,0xc5,0xd9,0x80,0x5b,0x9a,0x7b,0xeb,0x63,0x70,0x96,
        0x24,0xd7,0xec,0xcb,0x68,0x77,0x24,0x47,0x64,0xad,0x6a,0x1c,0x47,0x7d,0x44,0xbf,
        0x78,0xc4,0xf5,0xa3,0x4a,0x08,0x8d,0x83,0xb4,0x04,0x44,0xf7,0xcf,0xf2,0x6f,0xf2,
        0xf2,0x0d,0xa7,0x08,0xc6,0x17,0x82,0xac,0xfb,0x86,0x9f,0xba,0x5d,0x8b,0xb6,0xb9,
        0x08,0x00
    };
    static const uint8_t expected_sha256[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };
    static const uint8_t abc[] = {'a','b','c'};
    OC_CRYPTO_PROTOCOL *crypto = 0;
    uint8_t digest[32];
    uint8_t corrupted[OC_CRYPTO_ED448_SIGNATURE_BYTES];
    UINTN index;
    EFI_STATUS status;
    (void)image_handle;
    status = system_table->boot_services->locate_protocol(
        &OC_CRYPTO_PROTOCOL_GUID, 0, (void **)&crypto);
    if (EFI_ERROR(status) || crypto == 0 ||
        crypto->revision != OC_CRYPTO_PROTOCOL_REVISION ||
        crypto->structure_size < sizeof(*crypto)) return EFI_NOT_FOUND;
    debug_write("OPENCONSOLE_CRYPTO_CHILD_PROTOCOL_OK\n");
    status = crypto->sha256(crypto, abc, sizeof(abc), digest);
    if (EFI_ERROR(status) || !bytes_equal(digest, expected_sha256, sizeof(digest)))
        return EFI_SECURITY_VIOLATION;
    debug_write("OPENCONSOLE_CRYPTO_CHILD_SHA256_OK\n");
    status = crypto->ed448_verify(crypto, public_key, message,
                                  sizeof(message) - 1u, signature);
    if (status == EFI_INVALID_PARAMETER) {
        debug_write("OPENCONSOLE_CRYPTO_CHILD_ED448_KEY_REJECTED\n");
        return status;
    }
    if (EFI_ERROR(status)) {
        debug_write("OPENCONSOLE_CRYPTO_CHILD_ED448_SIGNATURE_REJECTED\n");
        return status;
    }
    debug_write("OPENCONSOLE_CRYPTO_CHILD_ED448_OK\n");
    for (index = 0u; index < sizeof(corrupted); ++index) corrupted[index] = signature[index];
    corrupted[0] ^= 1u;
    status = crypto->ed448_verify(crypto, public_key, message,
                                  sizeof(message) - 1u, corrupted);
    if (status != EFI_SECURITY_VIOLATION) return EFI_SECURITY_VIOLATION;
    debug_write("OPENCONSOLE_CRYPTO_CHILD_CORRUPTION_REJECTED\n");
    {
        static const uint8_t artifact[4] = {0x4d,0x5a,0x90,0x00};
        static const uint8_t artifact_digest[32] = {
            0x9f,0x29,0x81,0xa7,0xcc,0x4d,0x40,0xa2,0xa4,0x09,0xdc,0x89,0x5d,0xe6,0x42,0x53,
            0xac,0xd8,0x19,0xd7,0xc0,0x01,0x1c,0x8e,0x80,0xb8,0x6f,0xe8,0x99,0x46,0x4e,0x31
        };
        static const uint8_t metadata_signature[OC_UPDATE_SIGNATURE_BYTES] = {
            0x35,0x17,0x91,0x6b,0x28,0xae,0x0b,0x5a,0x31,0x0f,0x76,0x95,0x90,0xe7,0x84,0x81,
            0x0d,0x82,0x9b,0x32,0x84,0x07,0x4c,0xf6,0x3d,0x54,0x31,0xa6,0xe7,0xd1,0x94,0xb3,
            0x57,0x1c,0xf9,0xd3,0x9f,0xbd,0x47,0x75,0xff,0xbf,0xb0,0x52,0x01,0x37,0xa6,0xa5,
            0x95,0x9e,0x32,0x2c,0x00,0x86,0x1a,0x40,0x80,0x0b,0x2a,0x10,0xc5,0xae,0x22,0x40,
            0x9d,0xc6,0xc9,0x41,0x76,0x35,0x8f,0xc8,0x1a,0xc9,0x3c,0xb1,0x19,0x0a,0x14,0x5e,
            0x2d,0xb4,0xd2,0x19,0xff,0x74,0xb1,0x1d,0x11,0x45,0x1d,0xff,0xf8,0xa4,0x0b,0x40,
            0x61,0x07,0x43,0xa2,0xad,0xd1,0x8e,0xee,0x17,0xe7,0xbe,0x3e,0x89,0xa9,0x39,0x90,
            0x35,0x00
        };
        static const uint8_t url[] = "https://updates.openconsole.test/bootx64.efi";
        oc_update_trusted_key trusted;
        oc_uefi_update_crypto adapter;
        oc_https_identity identity = {"updates.openconsole.test",
            "updates.openconsole.test", 1u, 1u};
        oc_update_policy policy, accepted;
        oc_metadata_result policy_result;
        uint8_t metadata[287u + sizeof(url) - 1u];
        uint8_t bad_artifact[4] = {0x4d,0x5a,0x90,0x01};
        fill((uint8_t *)&trusted, 0u, sizeof(trusted));
        fill((uint8_t *)&policy, 0u, sizeof(policy));
        fill(metadata, 0u, sizeof(metadata));
        fill(trusted.id, 0x11u, sizeof(trusted.id));
        copy(trusted.public_key, public_key, sizeof(public_key));
        copy(metadata, (const uint8_t *)"OCU1", 4u); w16(metadata + 4u, 1u);
        w16(metadata + 6u, 287u); w32(metadata + 8u, sizeof(metadata));
        w64(metadata + 16u, 8u); w64(metadata + 24u, 2000u);
        w64(metadata + 32u, 12u); w64(metadata + 40u, sizeof(artifact));
        fill(metadata + 48u, 0x11u, 16u); copy(metadata + 137u, artifact_digest, 32u);
        copy(metadata + 169u, metadata_signature, sizeof(metadata_signature));
        w16(metadata + 283u, sizeof(url) - 1u); copy(metadata + 287u, url, sizeof(url) - 1u);
        policy.installed_version = 10u; policy.highest_sequence = 7u;
        policy.maximum_artifact_size = 8192u; fill(policy.trusted_key_id, 0x11u, 16u);
        if (EFI_ERROR(oc_uefi_update_crypto_initialize(&adapter, crypto, &trusted, 1u)))
            return EFI_SECURITY_VIOLATION;
#define VERIFY(now, bytes, bytes_size, art, art_size) \
        oc_update_verify(&adapter.callbacks, &identity, &policy, (now), \
            (bytes), (bytes_size), (art), (art_size), &accepted, &policy_result)
        if (VERIFY(1000u, metadata, sizeof(metadata), artifact, sizeof(artifact)) != OC_UPDATE_VERIFY_OK)
            return EFI_SECURITY_VIOLATION;
        debug_write("OPENCONSOLE_UPDATE_CHILD_VALID_OK\n");
        if (VERIFY(2001u, metadata, sizeof(metadata), artifact, sizeof(artifact)) != OC_UPDATE_VERIFY_POLICY_REJECTED || policy_result != OC_METADATA_EXPIRED)
            return EFI_SECURITY_VIOLATION;
        debug_write("OPENCONSOLE_UPDATE_CHILD_EXPIRED_REJECTED\n");
        identity.verified_host = "wrong.openconsole.test";
        if (VERIFY(1000u, metadata, sizeof(metadata), artifact, sizeof(artifact)) != OC_UPDATE_VERIFY_WRONG_HOST)
            return EFI_SECURITY_VIOLATION;
        identity.verified_host = identity.requested_host; metadata[48] ^= 1u;
        if (VERIFY(1000u, metadata, sizeof(metadata), artifact, sizeof(artifact)) != OC_UPDATE_VERIFY_UNKNOWN_KEY)
            return EFI_SECURITY_VIOLATION;
        metadata[48] ^= 1u; debug_write("OPENCONSOLE_UPDATE_CHILD_HOST_KEY_REJECTED\n");
        policy.highest_sequence = 8u;
        if (VERIFY(1000u, metadata, sizeof(metadata), artifact, sizeof(artifact)) != OC_UPDATE_VERIFY_POLICY_REJECTED || policy_result != OC_METADATA_ROLLBACK)
            return EFI_SECURITY_VIOLATION;
        policy.highest_sequence = 7u;
        if (VERIFY(1000u, metadata, sizeof(metadata) - 1u, artifact, sizeof(artifact)) != OC_UPDATE_VERIFY_INVALID)
            return EFI_SECURITY_VIOLATION;
        metadata[169] ^= 1u;
        if (VERIFY(1000u, metadata, sizeof(metadata), artifact, sizeof(artifact)) != OC_UPDATE_VERIFY_BAD_SIGNATURE)
            return EFI_SECURITY_VIOLATION;
        metadata[169] ^= 1u;
        if (VERIFY(1000u, metadata, sizeof(metadata), bad_artifact, sizeof(bad_artifact)) != OC_UPDATE_VERIFY_ARTIFACT_DIGEST)
            return EFI_SECURITY_VIOLATION;
        debug_write("OPENCONSOLE_UPDATE_CHILD_ROLLBACK_TRUNCATION_CORRUPTION_REJECTED\n");
#undef VERIFY
    }
    return EFI_SUCCESS;
}
