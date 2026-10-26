#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "openconsole/update_metadata.h"

static void w16(unsigned char *p, unsigned value) {
    p[0] = (unsigned char)value; p[1] = (unsigned char)(value >> 8);
}
static void w32(unsigned char *p, unsigned value) {
    unsigned i; for (i = 0; i < 4; ++i) p[i] = (unsigned char)(value >> (8 * i));
}
static void w64(unsigned char *p, unsigned long long value) {
    unsigned i; for (i = 0; i < 8; ++i) p[i] = (unsigned char)(value >> (8 * i));
}

static size_t update_blob(unsigned char *b, unsigned flags) {
    static const char url[] = "https://updates.openconsole.test/bootx64.efi";
    size_t size = 287u + sizeof(url) - 1u;
    memset(b, 0, size); memcpy(b, "OCU1", 4); w16(b + 4, 1); w16(b + 6, 287);
    w32(b + 8, (unsigned)size); w16(b + 12, flags);
    w64(b + 16, 8); w64(b + 24, 2000); w64(b + 32, 12); w64(b + 40, 4096);
    memset(b + 48, 0x11, 16); memset(b + 137, 0x33, 32);
    memset(b + 169, 0x44, OC_UPDATE_SIGNATURE_BYTES);
    if (flags) { memset(b + 64, 0x22, 16); memset(b + 80, 0x55, OC_UPDATE_PUBLIC_KEY_BYTES); }
    w16(b + 283, sizeof(url) - 1u); memcpy(b + 287, url, sizeof(url) - 1u);
    return size;
}

static void test_update(void) {
    unsigned char blob[1024], signed_a[1024], signed_b[1024];
    oc_update_metadata metadata;
    oc_update_policy policy, accepted;
    size_t size = update_blob(blob, OC_UPDATE_FLAG_ROTATE_KEY), a, b;
    assert(oc_update_metadata_parse(blob, size, &metadata) == OC_METADATA_OK);
    assert(metadata.sequence == 8 && metadata.artifact_size == 4096);
    assert(oc_update_write_signed(&metadata, signed_a, sizeof(signed_a), &a) == OC_METADATA_OK);
    memset(metadata.signature, 0xaa, sizeof(metadata.signature));
    assert(oc_update_write_signed(&metadata, signed_b, sizeof(signed_b), &b) == OC_METADATA_OK);
    assert(a == b && a == oc_update_signed_size(&metadata));
    assert(memcmp(signed_a, signed_b, a) == 0); /* Signature is outside its own domain. */

    memset(&policy, 0, sizeof(policy)); memset(policy.trusted_key_id, 0x11, 16);
    policy.installed_version = 10; policy.highest_sequence = 7;
    policy.maximum_artifact_size = 8192;
    assert(oc_update_policy_check(&policy, &metadata, 1000, &accepted) == OC_METADATA_OK);
    assert(accepted.highest_sequence == 8 && accepted.has_pending_key == 1);
    assert(memcmp(accepted.pending_key_id, metadata.next_key_id, 16) == 0);
    assert(oc_update_policy_check(&accepted, &metadata, 1000, &policy) == OC_METADATA_ROLLBACK);
    metadata.sequence = 9; metadata.artifact_version = 13;
    memcpy(metadata.signing_key_id, accepted.pending_key_id, 16); metadata.flags = 0;
    memset(metadata.next_key_id, 0, 16);
    memset(metadata.next_public_key, 0, OC_UPDATE_PUBLIC_KEY_BYTES);
    assert(oc_update_policy_check(&accepted, &metadata, 1000, &policy) == OC_METADATA_OK);
    assert(policy.has_pending_key == 0 && memcmp(policy.trusted_key_id,
                                                accepted.pending_key_id, 16) == 0);
    metadata.expires_unix = 999;
    assert(oc_update_policy_check(&accepted, &metadata, 1000, &policy) == OC_METADATA_EXPIRED);

    size = update_blob(blob, 0); blob[size] = 0;
    assert(oc_update_metadata_parse(blob, size + 1, &metadata) == OC_METADATA_NON_CANONICAL);
    blob[12] = 0x80;
    assert(oc_update_metadata_parse(blob, size, &metadata) == OC_METADATA_NON_CANONICAL);
}

static void test_catalog(void) {
    unsigned char blob[512];
    static const char id[] = "openconsole";
    static const char name[] = "OpenConsole Stable";
    static const char url[] = "https://updates.openconsole.test/stable.ocum";
    oc_catalog catalog;
    size_t offset = 34, size;
    memset(blob, 0, sizeof(blob)); memcpy(blob, "OCC1", 4); w16(blob + 4, 1);
    w16(blob + 6, 32); w64(blob + 16, 4); w64(blob + 24, 2000); w16(blob + 32, 1);
    w16(blob + offset, sizeof(id) - 1); w16(blob + offset + 2, sizeof(name) - 1);
    w16(blob + offset + 4, sizeof(url) - 1); w64(blob + offset + 8, 12);
    w64(blob + offset + 16, 4096); memset(blob + offset + 24, 0x66, 32); offset += 56;
    memcpy(blob + offset, id, sizeof(id) - 1); offset += sizeof(id) - 1;
    memcpy(blob + offset, name, sizeof(name) - 1); offset += sizeof(name) - 1;
    memcpy(blob + offset, url, sizeof(url) - 1); offset += sizeof(url) - 1; size = offset;
    w32(blob + 8, (unsigned)size);
    assert(oc_catalog_parse(blob, size, &catalog) == OC_METADATA_OK);
    assert(catalog.entry_count == 1 && catalog.entries[0].version == 12);
    blob[size] = 0;
    assert(oc_catalog_parse(blob, size + 1, &catalog) == OC_METADATA_NON_CANONICAL);
    w16(blob + 32, OC_CATALOG_MAX_ENTRIES + 1);
    assert(oc_catalog_parse(blob, size, &catalog) == OC_METADATA_TOO_LARGE);
}

int main(void) {
    test_update(); test_catalog();
    puts("PASS: canonical bounded catalog/update metadata and rotation policy");
    return 0;
}
