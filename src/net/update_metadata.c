#include "openconsole/update_metadata.h"

#define CATALOG_HEADER 32u
#define UPDATE_FIXED 287u
#define SIGNED_FIXED 170u

static uint16_t read16(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}
static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t read64(const uint8_t *p) {
    uint64_t value = 0u;
    unsigned i;
    for (i = 0u; i < 8u; ++i) value |= (uint64_t)p[i] << (8u * i);
    return value;
}
static void write16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}
static void write64(uint8_t *p, uint64_t value) {
    unsigned i;
    for (i = 0u; i < 8u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}
static void copy_bytes(uint8_t *to, const uint8_t *from, size_t count) {
    while (count-- != 0u) *to++ = *from++;
}
static int equal(const uint8_t *a, const uint8_t *b, size_t count) {
    uint8_t difference = 0u;
    while (count-- != 0u) difference |= (uint8_t)(*a++ ^ *b++);
    return difference == 0u;
}
static int zero(const uint8_t *p, size_t count) {
    uint8_t value = 0u;
    while (count-- != 0u) value |= *p++;
    return value == 0u;
}
static int text_valid(const uint8_t *p, size_t count) {
    size_t i;
    if (count == 0u) return 0;
    if (p[0] == 0x20u || p[count - 1u] == 0x20u) return 0;
    for (i = 0u; i < count; ++i)
        if (p[i] < 0x20u || p[i] > 0x7eu) return 0;
    return 1;
}
static int token_valid(const uint8_t *p, size_t count) {
    size_t i;
    if (!text_valid(p, count)) return 0;
    for (i = 0u; i < count; ++i) if (p[i] == 0x20u) return 0;
    return 1;
}

oc_metadata_result oc_catalog_parse(const uint8_t *data, size_t size,
                                    oc_catalog *out) {
    size_t offset = CATALOG_HEADER;
    uint16_t i;
    if (data == 0 || out == 0 || size < CATALOG_HEADER) return OC_METADATA_INVALID;
    if (size > OC_CATALOG_MAX_BYTES) return OC_METADATA_TOO_LARGE;
    if (!equal(data, (const uint8_t *)"OCC1", 4u) || read16(data + 4u) != 1u ||
        read16(data + 6u) != CATALOG_HEADER || read32(data + 8u) != size ||
        read32(data + 12u) != 0u) return OC_METADATA_NON_CANONICAL;
    out->sequence = read64(data + 16u);
    out->expires_unix = read64(data + 24u);
    if (size < offset + 2u) return OC_METADATA_INVALID;
    out->entry_count = read16(data + offset); offset += 2u;
    if (out->entry_count > OC_CATALOG_MAX_ENTRIES) return OC_METADATA_TOO_LARGE;
    for (i = 0u; i < out->entry_count; ++i) {
        oc_catalog_entry *entry = &out->entries[i];
        uint16_t id_length, name_length, url_length;
        if (size - offset < 56u) return OC_METADATA_INVALID;
        id_length = read16(data + offset); name_length = read16(data + offset + 2u);
        url_length = read16(data + offset + 4u);
        if (read16(data + offset + 6u) != 0u || id_length > OC_CATALOG_MAX_ID ||
            name_length > OC_CATALOG_MAX_NAME || url_length > OC_UPDATE_MAX_URL)
            return OC_METADATA_NON_CANONICAL;
        entry->version = read64(data + offset + 8u);
        entry->artifact_size = read64(data + offset + 16u);
        copy_bytes(entry->artifact_sha256, data + offset + 24u, 32u);
        offset += 56u;
        if (size - offset < (size_t)id_length + name_length + url_length)
            return OC_METADATA_INVALID;
        entry->id = data + offset; entry->id_length = id_length; offset += id_length;
        entry->name = data + offset; entry->name_length = name_length; offset += name_length;
        entry->metadata_url = data + offset; entry->metadata_url_length = url_length;
        offset += url_length;
        if (!token_valid(entry->id, id_length) || !text_valid(entry->name, name_length) ||
            !token_valid(entry->metadata_url, url_length) || entry->artifact_size == 0u)
            return OC_METADATA_NON_CANONICAL;
    }
    return offset == size ? OC_METADATA_OK : OC_METADATA_NON_CANONICAL;
}

oc_metadata_result oc_update_metadata_parse(const uint8_t *data, size_t size,
                                            oc_update_metadata *out) {
    uint16_t url_length;
    if (data == 0 || out == 0 || size < UPDATE_FIXED) return OC_METADATA_INVALID;
    if (size > OC_UPDATE_MAX_BYTES) return OC_METADATA_TOO_LARGE;
    if (!equal(data, (const uint8_t *)"OCU1", 4u) || read16(data + 4u) != 1u ||
        read16(data + 6u) != UPDATE_FIXED || read32(data + 8u) != size)
        return OC_METADATA_NON_CANONICAL;
    out->flags = read16(data + 12u);
    if ((out->flags & (uint16_t)~OC_UPDATE_FLAG_ROTATE_KEY) != 0u ||
        read16(data + 14u) != 0u) return OC_METADATA_NON_CANONICAL;
    out->sequence = read64(data + 16u); out->expires_unix = read64(data + 24u);
    out->artifact_version = read64(data + 32u); out->artifact_size = read64(data + 40u);
    copy_bytes(out->signing_key_id, data + 48u, 16u);
    copy_bytes(out->next_key_id, data + 64u, 16u);
    copy_bytes(out->next_public_key, data + 80u, OC_UPDATE_PUBLIC_KEY_BYTES);
    copy_bytes(out->artifact_sha256, data + 137u, 32u);
    copy_bytes(out->signature, data + 169u, OC_UPDATE_SIGNATURE_BYTES);
    url_length = read16(data + 283u);
    if (read16(data + 285u) != 0u || size != UPDATE_FIXED + (size_t)url_length ||
        url_length > OC_UPDATE_MAX_URL) return OC_METADATA_NON_CANONICAL;
    out->artifact_url = data + UPDATE_FIXED; out->artifact_url_length = url_length;
    if (!token_valid(out->artifact_url, url_length) || out->sequence == 0u ||
        out->artifact_version == 0u || out->artifact_size == 0u ||
        zero(out->signing_key_id, 16u) || zero(out->artifact_sha256, 32u))
        return OC_METADATA_NON_CANONICAL;
    if (zero(out->signature, OC_UPDATE_SIGNATURE_BYTES))
        return OC_METADATA_NON_CANONICAL;
    if ((out->flags & OC_UPDATE_FLAG_ROTATE_KEY) == 0u) {
        if (!zero(out->next_key_id, 16u) ||
            !zero(out->next_public_key, OC_UPDATE_PUBLIC_KEY_BYTES))
            return OC_METADATA_NON_CANONICAL;
    } else if (zero(out->next_key_id, 16u) ||
               zero(out->next_public_key, OC_UPDATE_PUBLIC_KEY_BYTES) ||
               equal(out->signing_key_id, out->next_key_id, 16u))
        return OC_METADATA_NON_CANONICAL;
    return OC_METADATA_OK;
}

size_t oc_update_signed_size(const oc_update_metadata *metadata) {
    if (metadata == 0 || metadata->artifact_url_length > OC_UPDATE_MAX_URL) return 0u;
    return SIGNED_FIXED + metadata->artifact_url_length;
}

oc_metadata_result oc_update_write_signed(const oc_update_metadata *m, uint8_t *out,
                                          size_t capacity, size_t *written) {
    static const uint8_t domain[11] = {'O','C','U','1','-','S','I','G','N','E','D'};
    size_t needed = oc_update_signed_size(m);
    size_t offset = 0u;
    if (written != 0) *written = needed;
    if (m == 0 || out == 0 || needed == 0u || capacity < needed)
        return OC_METADATA_TOO_LARGE;
    copy_bytes(out + offset, domain, sizeof(domain)); offset += sizeof(domain);
    write16(out + offset, 1u); offset += 2u; write16(out + offset, m->flags); offset += 2u;
    write64(out + offset, m->sequence); offset += 8u;
    write64(out + offset, m->expires_unix); offset += 8u;
    write64(out + offset, m->artifact_version); offset += 8u;
    write64(out + offset, m->artifact_size); offset += 8u;
    copy_bytes(out + offset, m->signing_key_id, 16u); offset += 16u;
    copy_bytes(out + offset, m->next_key_id, 16u); offset += 16u;
    copy_bytes(out + offset, m->next_public_key, OC_UPDATE_PUBLIC_KEY_BYTES);
    offset += OC_UPDATE_PUBLIC_KEY_BYTES;
    copy_bytes(out + offset, m->artifact_sha256, 32u); offset += 32u;
    write16(out + offset, m->artifact_url_length); offset += 2u;
    copy_bytes(out + offset, m->artifact_url, m->artifact_url_length);
    return OC_METADATA_OK;
}

oc_metadata_result oc_update_policy_check(const oc_update_policy *policy,
                                          const oc_update_metadata *m,
                                          uint64_t now, oc_update_policy *accepted) {
    int current, pending;
    if (policy == 0 || m == 0 || accepted == 0) return OC_METADATA_INVALID;
    current = equal(m->signing_key_id, policy->trusted_key_id, 16u);
    pending = policy->has_pending_key &&
              equal(m->signing_key_id, policy->pending_key_id, 16u);
    if (!current && !pending) return OC_METADATA_WRONG_KEY;
    if (m->expires_unix < now) return OC_METADATA_EXPIRED;
    if (m->sequence <= policy->highest_sequence ||
        m->artifact_version <= policy->installed_version) return OC_METADATA_ROLLBACK;
    if (m->artifact_size > policy->maximum_artifact_size)
        return OC_METADATA_ARTIFACT_TOO_LARGE;
    *accepted = *policy;
    accepted->highest_sequence = m->sequence;
    if (pending) {
        copy_bytes(accepted->trusted_key_id, policy->pending_key_id, 16u);
        accepted->has_pending_key = 0u;
    }
    if ((m->flags & OC_UPDATE_FLAG_ROTATE_KEY) != 0u) {
        if (!current) return OC_METADATA_WRONG_KEY;
        copy_bytes(accepted->pending_key_id, m->next_key_id, 16u);
        copy_bytes(accepted->pending_public_key, m->next_public_key,
                   OC_UPDATE_PUBLIC_KEY_BYTES);
        accepted->has_pending_key = 1u;
    }
    return OC_METADATA_OK;
}
