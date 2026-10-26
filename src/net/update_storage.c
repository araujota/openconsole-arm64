#include "openconsole/update_storage.h"

static const char *const artifact_paths[2] = {
    "\\EFI\\OpenConsole\\Updates\\slot-a.efi",
    "\\EFI\\OpenConsole\\Updates\\slot-b.efi"
};
static const char *const selector_paths[2] = {
    "\\EFI\\OpenConsole\\Updates\\select-a.ocu",
    "\\EFI\\OpenConsole\\Updates\\select-b.ocu"
};

static uint32_t crc32(const uint8_t *bytes, size_t size) {
    uint32_t crc = 0xffffffffu;
    size_t i;
    for (i = 0u; i < size; ++i) {
        uint32_t bit;
        crc ^= bytes[i];
        for (bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void put32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)value; out[1] = (uint8_t)(value >> 8u);
    out[2] = (uint8_t)(value >> 16u); out[3] = (uint8_t)(value >> 24u);
}
static uint32_t get32(const uint8_t *in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8u) |
           ((uint32_t)in[2] << 16u) | ((uint32_t)in[3] << 24u);
}
static void put64(uint8_t *out, uint64_t value) {
    uint32_t i;
    for (i = 0u; i < 8u; ++i) out[i] = (uint8_t)(value >> (i * 8u));
}
static uint64_t get64(const uint8_t *in) {
    uint64_t value = 0u;
    uint32_t i;
    for (i = 0u; i < 8u; ++i) value |= (uint64_t)in[i] << (i * 8u);
    return value;
}
static int bytes_equal(const uint8_t *a, const uint8_t *b, size_t size) {
    uint8_t difference = 0u;
    while (size-- != 0u) difference |= (uint8_t)(*a++ ^ *b++);
    return difference == 0u;
}
static void bytes_copy(void *destination, const void *source, size_t size) {
    uint8_t *out = destination;
    const uint8_t *in = source;
    while (size-- != 0u) *out++ = *in++;
}

const char *oc_update_artifact_path(uint8_t slot) {
    return slot < 2u ? artifact_paths[slot] : 0;
}
const char *oc_update_selector_path(uint8_t selector) {
    return selector < 2u ? selector_paths[selector] : 0;
}

static void encode(const oc_update_boot_selection *selection,
                   uint8_t out[OC_UPDATE_SELECTOR_BYTES]) {
    size_t i;
    for (i = 0u; i < OC_UPDATE_SELECTOR_BYTES; ++i) out[i] = 0u;
    out[0] = 'O'; out[1] = 'C'; out[2] = 'S'; out[3] = '1';
    out[4] = 1u;
    out[5] = selection->slot;
    put64(out + 8u, selection->generation);
    put64(out + 16u, selection->artifact_size);
    for (i = 0u; i < 32u; ++i) out[24u + i] = selection->artifact_sha256[i];
    put64(out + 56u, selection->policy.installed_version);
    put64(out + 64u, selection->policy.highest_sequence);
    put64(out + 72u, selection->policy.maximum_artifact_size);
    for (i = 0u; i < 16u; ++i) {
        out[80u + i] = selection->policy.trusted_key_id[i];
        out[96u + i] = selection->policy.pending_key_id[i];
    }
    for (i = 0u; i < OC_UPDATE_PUBLIC_KEY_BYTES; ++i)
        out[112u + i] = selection->policy.pending_public_key[i];
    out[169u] = selection->policy.has_pending_key;
    put32(out + 172u, crc32(out, 172u));
}

static int decode(const uint8_t in[OC_UPDATE_SELECTOR_BYTES], uint8_t selector,
                  oc_update_boot_selection *selection) {
    size_t i;
    if (in[0] != 'O' || in[1] != 'C' || in[2] != 'S' || in[3] != '1' ||
        in[4] != 1u || in[5] > 1u || in[6] != 0u || in[7] != 0u ||
        in[169u] > 1u || in[170u] != 0u || in[171u] != 0u ||
        get32(in + 172u) != crc32(in, 172u)) return 0;
    selection->slot = in[5];
    selection->selector = selector;
    selection->generation = get64(in + 8u);
    selection->artifact_size = get64(in + 16u);
    if (selection->generation == 0u || selection->artifact_size == 0u) return 0;
    for (i = 0u; i < 32u; ++i) selection->artifact_sha256[i] = in[24u + i];
    selection->policy.installed_version = get64(in + 56u);
    selection->policy.highest_sequence = get64(in + 64u);
    selection->policy.maximum_artifact_size = get64(in + 72u);
    for (i = 0u; i < 16u; ++i) {
        selection->policy.trusted_key_id[i] = in[80u + i];
        selection->policy.pending_key_id[i] = in[96u + i];
    }
    for (i = 0u; i < OC_UPDATE_PUBLIC_KEY_BYTES; ++i)
        selection->policy.pending_public_key[i] = in[112u + i];
    selection->policy.has_pending_key = in[169u];
    return 1;
}

static int valid_candidate(const oc_update_storage_ops *ops, uint8_t selector,
                           oc_update_boot_selection *candidate) {
    uint8_t record[OC_UPDATE_SELECTOR_BYTES], digest[32];
    uint64_t size = 0u;
    size_t read_size = sizeof(record);
    if (!ops->read_file(ops->context, selector_paths[selector], record,
                        sizeof(record), &read_size) || read_size != sizeof(record) ||
        !decode(record, selector, candidate) ||
        !ops->digest_file(ops->context, artifact_paths[candidate->slot],
                          &size, digest) || size != candidate->artifact_size ||
        !bytes_equal(digest, candidate->artifact_sha256, sizeof(digest))) return 0;
    return 1;
}

oc_update_storage_result oc_update_recover(const oc_update_storage_ops *ops,
                                           oc_update_boot_selection *selection) {
    oc_update_boot_selection candidate, best;
    uint8_t index;
    int found = 0;
    if (ops == 0 || selection == 0 || ops->read_file == 0 ||
        ops->digest_file == 0) return OC_UPDATE_STORAGE_INVALID;
    for (index = 0u; index < 2u; ++index) {
        if (valid_candidate(ops, index, &candidate) &&
            (!found || candidate.generation > best.generation)) {
            bytes_copy(&best, &candidate, sizeof(best));
            found = 1;
        }
    }
    if (!found) return OC_UPDATE_STORAGE_NO_BOOTABLE;
    bytes_copy(selection, &best, sizeof(*selection));
    return OC_UPDATE_STORAGE_OK;
}

oc_update_storage_result oc_update_stage_and_commit(
    const oc_update_storage_ops *ops, const uint8_t *artifact, size_t artifact_size,
    const uint8_t expected_sha256[32], const oc_update_policy *accepted_policy,
    oc_update_boot_selection *committed) {
    oc_update_boot_selection current, next, check;
    uint8_t record[OC_UPDATE_SELECTOR_BYTES], persisted_digest[32];
    uint64_t persisted_size = 0u;
    size_t read_size = sizeof(record);
    oc_update_storage_result recovered;
    if (ops == 0 || artifact == 0 || artifact_size == 0u ||
        expected_sha256 == 0 || accepted_policy == 0 || committed == 0 ||
        ops->write_file == 0 ||
        ops->flush_file == 0 || ops->read_file == 0 || ops->digest_file == 0)
        return OC_UPDATE_STORAGE_INVALID;
    recovered = oc_update_recover(ops, &current);
    next.slot = recovered == OC_UPDATE_STORAGE_OK ? (uint8_t)(1u - current.slot) : 0u;
    next.selector = recovered == OC_UPDATE_STORAGE_OK ?
                    (uint8_t)(1u - current.selector) : 0u;
    next.generation = recovered == OC_UPDATE_STORAGE_OK ? current.generation + 1u : 1u;
    if (next.generation == 0u) return OC_UPDATE_STORAGE_INVALID;
    next.artifact_size = artifact_size;
    bytes_copy(&next.policy, accepted_policy, sizeof(next.policy));
    for (read_size = 0u; read_size < 32u; ++read_size)
        next.artifact_sha256[read_size] = expected_sha256[read_size];

    if (!ops->write_file(ops->context, artifact_paths[next.slot], artifact,
                         artifact_size)) return OC_UPDATE_STORAGE_IO;
    if (!ops->flush_file(ops->context, artifact_paths[next.slot]))
        return OC_UPDATE_STORAGE_FLUSH;
    if (!ops->digest_file(ops->context, artifact_paths[next.slot], &persisted_size,
                          persisted_digest) || persisted_size != artifact_size ||
        !bytes_equal(persisted_digest, expected_sha256, 32u))
        return OC_UPDATE_STORAGE_VERIFY;

    encode(&next, record);
    if (!ops->write_file(ops->context, selector_paths[next.selector], record,
                         sizeof(record))) return OC_UPDATE_STORAGE_IO;
    if (!ops->flush_file(ops->context, selector_paths[next.selector]))
        return OC_UPDATE_STORAGE_FLUSH;
    read_size = sizeof(record);
    if (!ops->read_file(ops->context, selector_paths[next.selector], record,
                        sizeof(record), &read_size) || read_size != sizeof(record) ||
        !decode(record, next.selector, &check) ||
        check.generation != next.generation || check.slot != next.slot ||
        check.artifact_size != next.artifact_size ||
        !bytes_equal(check.artifact_sha256, next.artifact_sha256, 32u) ||
        check.policy.installed_version != next.policy.installed_version ||
        check.policy.highest_sequence != next.policy.highest_sequence ||
        check.policy.maximum_artifact_size != next.policy.maximum_artifact_size ||
        check.policy.has_pending_key != next.policy.has_pending_key ||
        !bytes_equal(check.policy.trusted_key_id, next.policy.trusted_key_id, 16u) ||
        !bytes_equal(check.policy.pending_key_id, next.policy.pending_key_id, 16u) ||
        !bytes_equal(check.policy.pending_public_key,
                     next.policy.pending_public_key, OC_UPDATE_PUBLIC_KEY_BYTES))
        return OC_UPDATE_STORAGE_VERIFY;
    bytes_copy(committed, &next, sizeof(*committed));
    return OC_UPDATE_STORAGE_OK;
}
