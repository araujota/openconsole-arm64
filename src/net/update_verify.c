#include "openconsole/update_verify.h"

static int string_equal(const char *a, const char *b) {
    if (a == 0 || b == 0) return 0;
    while (*a != '\0' && *a == *b) { ++a; ++b; }
    return *a == '\0' && *b == '\0';
}
static int bytes_equal(const uint8_t *a, const uint8_t *b, size_t count) {
    uint8_t difference = 0u;
    while (count-- != 0u) difference |= (uint8_t)(*a++ ^ *b++);
    return difference == 0u;
}

oc_update_verify_result oc_update_verify(const oc_update_crypto *crypto,
                                         const oc_https_identity *identity,
                                         const oc_update_policy *policy,
                                         uint64_t now,
                                         const uint8_t *metadata_bytes,
                                         size_t metadata_size,
                                         const uint8_t *artifact,
                                         size_t artifact_size,
                                         oc_update_policy *accepted,
                                         oc_metadata_result *policy_result) {
    oc_update_metadata metadata;
    uint8_t signed_bytes[OC_UPDATE_MAX_BYTES];
    uint8_t public_key[OC_UPDATE_PUBLIC_KEY_BYTES];
    uint8_t digest[OC_UPDATE_DIGEST_BYTES];
    oc_update_policy candidate;
    size_t signed_size = 0u;
    oc_metadata_result result;
    if (policy_result != 0) *policy_result = OC_METADATA_INVALID;
    if (crypto == 0 || identity == 0 || policy == 0 || accepted == 0 ||
        crypto->ed448_verify == 0 || crypto->sha256 == 0 ||
        crypto->lookup_key == 0 || metadata_bytes == 0 || artifact == 0)
        return OC_UPDATE_VERIFY_INVALID;
    if (!identity->peer_chain_verified || !identity->hostname_verified)
        return OC_UPDATE_VERIFY_TLS_REJECTED;
    if (!string_equal(identity->requested_host, identity->verified_host))
        return OC_UPDATE_VERIFY_WRONG_HOST;
    result = oc_update_metadata_parse(metadata_bytes, metadata_size, &metadata);
    if (result != OC_METADATA_OK) {
        if (policy_result != 0) *policy_result = result;
        return OC_UPDATE_VERIFY_INVALID;
    }
    if (!crypto->lookup_key(crypto->context, metadata.signing_key_id, public_key))
        return OC_UPDATE_VERIFY_UNKNOWN_KEY;
    if (oc_update_write_signed(&metadata, signed_bytes, sizeof(signed_bytes),
                               &signed_size) != OC_METADATA_OK)
        return OC_UPDATE_VERIFY_INVALID;
    if (!crypto->ed448_verify(crypto->context, public_key, signed_bytes,
                              signed_size, metadata.signature))
        return OC_UPDATE_VERIFY_BAD_SIGNATURE;
    result = oc_update_policy_check(policy, &metadata, now, &candidate);
    if (policy_result != 0) *policy_result = result;
    if (result != OC_METADATA_OK) return OC_UPDATE_VERIFY_POLICY_REJECTED;
    if (metadata.artifact_size != artifact_size)
        return OC_UPDATE_VERIFY_ARTIFACT_SIZE;
    if (!crypto->sha256(crypto->context, artifact, artifact_size, digest) ||
        !bytes_equal(digest, metadata.artifact_sha256, sizeof(digest)))
        return OC_UPDATE_VERIFY_ARTIFACT_DIGEST;
    *accepted = candidate;
    return OC_UPDATE_VERIFY_OK;
}
