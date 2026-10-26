#include "openconsole/net_protocol.h"
#include "openconsole/uefi_update_crypto.h"
#include "openconsole/uefi_update_storage.h"

#define ARTIFACT_CAPACITY (256u * 1024u)
static uint8_t catalog_bytes[OC_CATALOG_MAX_BYTES];
static uint8_t metadata_bytes[OC_UPDATE_MAX_BYTES];
static uint8_t artifact_bytes[ARTIFACT_CAPACITY];

static const EFI_GUID simple_fs_guid =
    {0x964e5b22u,0x6459u,0x11d2u,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

static void debug_write(const char *text) {
    while (*text != '\0') {
        __asm__ volatile("outb %0, %1" : : "a"((uint8_t)*text), "Nd"((uint16_t)0x402));
        ++text;
    }
}
static void debug_hex32(uint32_t value) {
    static const char digits[]="0123456789ABCDEF";
    int shift;
    for (shift=28;shift>=0;shift-=4) {
        char c=digits[(value>>(uint32_t)shift)&15u];
        __asm__ volatile("outb %0, %1" : : "a"((uint8_t)c), "Nd"((uint16_t)0x402));
    }
    debug_write("\n");
}
static void debug_bytes(const uint8_t *bytes, UINTN size) {
    static const char digits[]="0123456789ABCDEF";
    UINTN i;
    for (i=0u;i<size;++i) {
        char high=digits[bytes[i]>>4u], low=digits[bytes[i]&15u];
        __asm__ volatile("outb %0, %1" : : "a"((uint8_t)high), "Nd"((uint16_t)0x402));
        __asm__ volatile("outb %0, %1" : : "a"((uint8_t)low), "Nd"((uint16_t)0x402));
    }
    debug_write("\n");
}
static void zero(void *memory, UINTN size) {
    uint8_t *bytes = memory; while (size-- != 0u) *bytes++ = 0u;
}
static int equal(const uint8_t *a, const uint8_t *b, UINTN size) {
    uint8_t difference = 0u;
    while (size-- != 0u) difference |= (uint8_t)(*a++ ^ *b++);
    return difference == 0u;
}

static int text_equal(const uint8_t *encoded, UINTN encoded_size,
                      const char *expected, UINTN expected_size) {
    return encoded_size == expected_size &&
           equal(encoded, (const uint8_t *)expected, encoded_size);
}

static EFI_STATUS await(OC_NET_PROTOCOL *net, OC_NET_SESSION *session,
                        OC_NET_OPERATION *operation, EFI_BOOT_SERVICES *bs,
                        uint64_t *now, OC_NET_OPERATION_STATUS *result) {
    for (;;) {
        EFI_STATUS status;
        result->structure_size = sizeof(*result);
        status = net->query_operation(operation, result);
        if (EFI_ERROR(status) || result->result != OC_NET_RESULT_PENDING) return status;
        *now += 1000u;
        status = net->pump(session, *now);
        if (EFI_ERROR(status)) return status;
        (void)bs->stall(1000u);
    }
}

static EFI_STATUS download(OC_NET_PROTOCOL *net, OC_NET_SESSION *session,
                           EFI_BOOT_SERVICES *bs, uint64_t *now,
                           const char *url, uint32_t url_size, void *buffer,
                           uint64_t capacity, uint32_t expected_result,
                           uint64_t *written) {
    OC_NET_HTTPS_DESC request;
    OC_NET_OPERATION *operation = 0;
    OC_NET_OPERATION_STATUS result;
    EFI_STATUS status;
    UINTN attempt;
    zero(&request, sizeof(request));
    request.structure_size = sizeof(request); request.url = url;
    request.url_size = url_size; request.response_body = buffer;
    request.response_capacity = capacity;
    request.deadline.structure_size = sizeof(request.deadline);
    request.deadline.deadline_usec = *now + 180000000u;
    for (attempt = 0u; attempt < 100u; ++attempt) {
        status = net->start_https(session, &request, &operation);
        if (status != EFI_NO_MAPPING) break;
        *now += 100000u; (void)net->pump(session, *now); (void)bs->stall(100000u);
    }
    if (EFI_ERROR(status)) return status;
    status = await(net, session, operation, bs, now, &result);
    if (!EFI_ERROR(status) && result.result != expected_result) {
        debug_write("OPENCONSOLE_UPDATE_E2E_UNEXPECTED_NET_RESULT_");
        debug_hex32(result.result);
        debug_write("OPENCONSOLE_UPDATE_E2E_PROTOCOL_STATUS_");
        debug_hex32(result.protocol_status);
        debug_write("OPENCONSOLE_UPDATE_E2E_BYTES_TRANSFERRED_");
        debug_hex32((uint32_t)result.bytes_transferred);
        debug_write("OPENCONSOLE_UPDATE_E2E_BYTES_REQUIRED_");
        debug_hex32((uint32_t)result.bytes_required);
        status = EFI_SECURITY_VIOLATION;
    }
    if (!EFI_ERROR(status) && written != 0) *written = result.bytes_transferred;
    if (EFI_ERROR(net->release_operation(operation)) && !EFI_ERROR(status))
        status = EFI_DEVICE_ERROR;
    return status;
}

static int selection_is(const oc_update_storage_ops *ops, uint64_t generation,
                        uint64_t sequence) {
    oc_update_boot_selection selected;
    return oc_update_recover(ops, &selected) == OC_UPDATE_STORAGE_OK &&
           selected.generation == generation &&
           selected.policy.highest_sequence == sequence;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    static const char catalog_url[] = "https://10.0.2.2:9443/catalog";
    static const char metadata_url[] = "https://10.0.2.2:9443/metadata";
    static const char artifact_url[] = "https://10.0.2.2:9443/artifact";
    static const char expired_url[] = "https://10.0.2.2:9444/update";
    static const char wrong_host_url[] = "https://10.0.2.2:9445/update";
    static const uint8_t public_key[OC_UPDATE_PUBLIC_KEY_BYTES] = {
        0xfb,0x95,0x3e,0x64,0x35,0xfb,0xea,0xc2,0x04,0x41,0x9e,0xa7,0x44,0x6b,0x9d,0x52,
        0x69,0x2c,0x4a,0x9a,0xa8,0x24,0x18,0x30,0x93,0x7e,0x7f,0xea,0x5b,0x58,0x3d,0x7a,
        0x8c,0x0a,0x86,0x24,0x4e,0xcc,0x90,0xcc,0x50,0x76,0xe0,0x6a,0x97,0x98,0x6b,0x89,
        0x39,0x29,0xb7,0x65,0xb2,0x28,0x27,0xa1,0x00
    };
    OC_NET_PROTOCOL *net = 0; OC_NET_SESSION *session = 0;
    OC_CRYPTO_PROTOCOL *crypto = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0; EFI_FILE_PROTOCOL *root = 0;
    oc_uefi_update_storage storage; oc_uefi_update_crypto crypto_adapter;
    oc_update_trusted_key trusted; oc_update_policy policy, accepted;
    oc_update_boot_selection selected; oc_catalog catalog; oc_update_metadata metadata;
    oc_https_identity identity = {"10.0.2.2", "10.0.2.2", 1u, 1u};
    oc_metadata_result policy_result; oc_update_verify_result verify_result;
    oc_update_storage_result storage_result;
    OC_NET_SESSION_DESC session_desc = {sizeof(session_desc),OC_NET_ABI_VERSION,
                                        1024u*1024u,8u,0u};
    OC_NET_DEADLINE deadline = {sizeof(deadline),0u,15000000u};
    OC_NET_OPERATION *operation = 0; OC_NET_OPERATION_STATUS operation_status;
    uint64_t now = 0u, catalog_size = 0u, metadata_size = 0u, artifact_size = 0u;
    uint8_t original;
    EFI_STATUS status;
    (void)image_handle;
    zero(&storage,sizeof(storage)); zero(&trusted,sizeof(trusted)); zero(&policy,sizeof(policy));
    status = system_table->boot_services->locate_protocol(&OC_NET_PROTOCOL_GUID,0,(void **)&net);
    if (EFI_ERROR(status)) return status;
    status = system_table->boot_services->locate_protocol(&OC_CRYPTO_PROTOCOL_GUID,0,(void **)&crypto);
    if (EFI_ERROR(status)) return status;
    status = system_table->boot_services->locate_protocol(&simple_fs_guid,0,(void **)&fs);
    if (EFI_ERROR(status) || EFI_ERROR(fs->open_volume(fs,&root))) return EFI_NOT_FOUND;
    status = oc_uefi_update_storage_initialize(&storage,system_table->boot_services,
                                                root,crypto,ARTIFACT_CAPACITY);
    if (EFI_ERROR(status)) goto done;
    if (EFI_ERROR(net->create_session(&session_desc,&session))) { status=EFI_DEVICE_ERROR; goto shutdown; }
    if (EFI_ERROR(net->configure_dhcp4(session,&deadline,&operation)) ||
        EFI_ERROR(await(net,session,operation,system_table->boot_services,&now,&operation_status)) ||
        operation_status.result != OC_NET_RESULT_OK ||
        EFI_ERROR(net->release_operation(operation))) { status=EFI_DEVICE_ERROR; goto network_done; }
    operation=0;
    debug_write("OPENCONSOLE_UPDATE_E2E_DHCP_OK\n");

    if (EFI_ERROR(download(net,session,system_table->boot_services,&now,catalog_url,
            sizeof(catalog_url)-1u,catalog_bytes,sizeof(catalog_bytes),OC_NET_RESULT_OK,&catalog_size)) ||
        oc_catalog_parse(catalog_bytes,(size_t)catalog_size,&catalog)!=OC_METADATA_OK ||
        catalog.entry_count!=1u ||
        !text_equal(catalog.entries[0].metadata_url,
                    catalog.entries[0].metadata_url_length,
                    metadata_url,sizeof(metadata_url)-1u) ||
        catalog.entries[0].artifact_size>sizeof(artifact_bytes))
        { status=EFI_SECURITY_VIOLATION; goto network_done; }
    debug_write("OPENCONSOLE_UPDATE_E2E_CATALOG_OK\n");
    if (EFI_ERROR(download(net,session,system_table->boot_services,&now,metadata_url,
            sizeof(metadata_url)-1u,metadata_bytes,sizeof(metadata_bytes),OC_NET_RESULT_OK,&metadata_size)))
        { debug_write("OPENCONSOLE_UPDATE_E2E_METADATA_DOWNLOAD_FAILED\n"); status=EFI_SECURITY_VIOLATION; goto network_done; }
    debug_write("OPENCONSOLE_UPDATE_E2E_METADATA_DOWNLOADED\n");
    if (oc_update_metadata_parse(metadata_bytes,(size_t)metadata_size,&metadata)!=OC_METADATA_OK)
        { debug_write("OPENCONSOLE_UPDATE_E2E_METADATA_PARSE_FAILED\n"); status=EFI_SECURITY_VIOLATION; goto network_done; }
    if (!text_equal(metadata.artifact_url,metadata.artifact_url_length,
                    artifact_url,sizeof(artifact_url)-1u))
        { status=EFI_SECURITY_VIOLATION; goto network_done; }
    debug_write("OPENCONSOLE_UPDATE_E2E_METADATA_OK\n");
    if (EFI_ERROR(download(net,session,system_table->boot_services,&now,artifact_url,
            sizeof(artifact_url)-1u,artifact_bytes,catalog.entries[0].artifact_size,
            OC_NET_RESULT_OK,&artifact_size)))
        { debug_write("OPENCONSOLE_UPDATE_E2E_ARTIFACT_DOWNLOAD_FAILED\n"); status=EFI_SECURITY_VIOLATION; goto network_done; }
    if (catalog.entries[0].artifact_size!=artifact_size ||
        metadata.artifact_size!=artifact_size ||
        !equal(catalog.entries[0].artifact_sha256,metadata.artifact_sha256,32u))
        { status=EFI_SECURITY_VIOLATION; goto network_done; }
    debug_write("OPENCONSOLE_UPDATE_E2E_DOWNLOADS_OK\n");

    for (UINTN i=0u;i<16u;++i) trusted.id[i]=0x11u;
    for (UINTN i=0u;i<sizeof(public_key);++i) trusted.public_key[i]=public_key[i];
    policy.installed_version=10u; policy.highest_sequence=7u;
    policy.maximum_artifact_size=ARTIFACT_CAPACITY;
    for (UINTN i=0u;i<16u;++i) policy.trusted_key_id[i]=0x11u;
    if (EFI_ERROR(oc_uefi_update_crypto_initialize(&crypto_adapter,crypto,&trusted,1u)))
        { status=EFI_SECURITY_VIOLATION; goto network_done; }
    verify_result=oc_update_verify(&crypto_adapter.callbacks,&identity,&policy,1000u,
        metadata_bytes,(size_t)metadata_size,artifact_bytes,(size_t)artifact_size,
        &accepted,&policy_result);
    if (verify_result!=OC_UPDATE_VERIFY_OK) {
        uint8_t received_digest[32];
        debug_write("OPENCONSOLE_UPDATE_E2E_VERIFY_FAILED_");
        debug_hex32((uint32_t)verify_result);
        debug_write("OPENCONSOLE_UPDATE_E2E_POLICY_RESULT_");
        debug_hex32((uint32_t)policy_result);
        if (!EFI_ERROR(crypto->sha256(crypto,artifact_bytes,(UINTN)artifact_size,
                                      received_digest))) {
            debug_write("OPENCONSOLE_UPDATE_E2E_RECEIVED_DIGEST_");
            debug_bytes(received_digest,sizeof(received_digest));
            debug_write("OPENCONSOLE_UPDATE_E2E_EXPECTED_DIGEST_");
            debug_bytes(metadata.artifact_sha256,sizeof(metadata.artifact_sha256));
        }
        debug_write("OPENCONSOLE_UPDATE_E2E_ARTIFACT_PREFIX_");
        debug_bytes(artifact_bytes,16u);
        status=EFI_SECURITY_VIOLATION; goto network_done;
    }
    storage_result=oc_update_stage_and_commit(&storage.operations,artifact_bytes,
        (size_t)artifact_size,metadata.artifact_sha256,&accepted,&selected);
    if (storage_result!=OC_UPDATE_STORAGE_OK) {
        debug_write("OPENCONSOLE_UPDATE_E2E_STORAGE_FAILED_");
        debug_hex32((uint32_t)storage_result);
        status=EFI_SECURITY_VIOLATION; goto network_done;
    }
    if (!selection_is(&storage.operations,1u,8u)) {
        debug_write("OPENCONSOLE_UPDATE_E2E_SELECTION_FAILED\n");
        status=EFI_SECURITY_VIOLATION; goto network_done;
    }
    debug_write("OPENCONSOLE_UPDATE_E2E_COMMITTED\n");

#define REJECT_AND_PRESERVE(expected, now_value, meta_size_value, art_size_value) do { \
    verify_result=oc_update_verify(&crypto_adapter.callbacks,&identity,&policy,(now_value), \
        metadata_bytes,(meta_size_value),artifact_bytes,(art_size_value),&accepted,&policy_result); \
    if (verify_result!=(expected) || !selection_is(&storage.operations,1u,8u)) \
        { status=EFI_SECURITY_VIOLATION; goto network_done; } \
} while (0)
    identity.peer_chain_verified=0u;
    REJECT_AND_PRESERVE(OC_UPDATE_VERIFY_TLS_REJECTED,1000u,(size_t)metadata_size,(size_t)artifact_size);
    identity.peer_chain_verified=1u; identity.verified_host="wrong.openconsole.test";
    REJECT_AND_PRESERVE(OC_UPDATE_VERIFY_WRONG_HOST,1000u,(size_t)metadata_size,(size_t)artifact_size);
    identity.verified_host=identity.requested_host;
    original=metadata_bytes[48]; metadata_bytes[48]^=1u;
    REJECT_AND_PRESERVE(OC_UPDATE_VERIFY_UNKNOWN_KEY,1000u,(size_t)metadata_size,(size_t)artifact_size);
    metadata_bytes[48]=original;
    REJECT_AND_PRESERVE(OC_UPDATE_VERIFY_POLICY_REJECTED,2000000001u,(size_t)metadata_size,(size_t)artifact_size);
    accepted=policy; accepted.highest_sequence=8u;
    verify_result=oc_update_verify(&crypto_adapter.callbacks,&identity,&accepted,1000u,
        metadata_bytes,(size_t)metadata_size,artifact_bytes,(size_t)artifact_size,&selected.policy,&policy_result);
    if (verify_result!=OC_UPDATE_VERIFY_POLICY_REJECTED || !selection_is(&storage.operations,1u,8u))
        { status=EFI_SECURITY_VIOLATION; goto network_done; }
    REJECT_AND_PRESERVE(OC_UPDATE_VERIFY_INVALID,1000u,(size_t)metadata_size-1u,(size_t)artifact_size);
    original=metadata_bytes[169]; metadata_bytes[169]^=1u;
    REJECT_AND_PRESERVE(OC_UPDATE_VERIFY_BAD_SIGNATURE,1000u,(size_t)metadata_size,(size_t)artifact_size);
    metadata_bytes[169]=original;
    artifact_bytes[artifact_size-1u]^=1u;
    REJECT_AND_PRESERVE(OC_UPDATE_VERIFY_ARTIFACT_DIGEST,1000u,(size_t)metadata_size,(size_t)artifact_size);
    artifact_bytes[artifact_size-1u]^=1u;
#undef REJECT_AND_PRESERVE
    if (EFI_ERROR(download(net,session,system_table->boot_services,&now,expired_url,
            sizeof(expired_url)-1u,metadata_bytes,sizeof(metadata_bytes),
            OC_NET_RESULT_TLS_IDENTITY_REJECTED,0)) ||
        EFI_ERROR(download(net,session,system_table->boot_services,&now,wrong_host_url,
            sizeof(wrong_host_url)-1u,metadata_bytes,sizeof(metadata_bytes),
            OC_NET_RESULT_TLS_IDENTITY_REJECTED,0)) || !selection_is(&storage.operations,1u,8u))
        { status=EFI_SECURITY_VIOLATION; goto network_done; }
    debug_write("OPENCONSOLE_UPDATE_E2E_REJECTIONS_PRESERVED\n");
    status=EFI_SUCCESS;
network_done:
    if (operation!=0) (void)net->release_operation(operation);
    if (session!=0) (void)net->destroy_session(session);
shutdown:
    oc_uefi_update_storage_shutdown(&storage);
done:
    if (root!=0) (void)root->close(root);
    return status;
}
