#include "openconsole/uefi_update_storage.h"

static const EFI_GUID simple_fs_guid =
    {0x964e5b22u,0x6459u,0x11d2u,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

static void debug_write(const char *text) {
    while (*text != '\0') {
        __asm__ volatile("outb %0, %1" : : "a"((uint8_t)*text), "Nd"((uint16_t)0x402));
        ++text;
    }
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    static const uint8_t first_artifact[] = {
        'M','Z','O','p','e','n','C','o','n','s','o','l','e','-','A'
    };
    static const uint8_t second_artifact[] = {
        'M','Z','O','p','e','n','C','o','n','s','o','l','e','-','B','-','2'
    };
    uint8_t corrupted[sizeof(second_artifact)];
    uint8_t digest[32];
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0;
    OC_CRYPTO_PROTOCOL *crypto = 0;
    oc_uefi_update_storage storage;
    oc_update_boot_selection selected;
    oc_update_policy policy;
    EFI_STATUS status;
    UINTN index;

    debug_write("OPENCONSOLE_UPDATE_STORAGE_ENTRY\n");
    status = system_table->boot_services->locate_protocol(
        &OC_CRYPTO_PROTOCOL_GUID, 0, (void **)&crypto);
    if (EFI_ERROR(status) || crypto == 0) return EFI_NOT_FOUND;
    debug_write("OPENCONSOLE_UPDATE_STORAGE_CRYPTO\n");
    (void)image_handle;
    status = system_table->boot_services->locate_protocol(
        &simple_fs_guid, 0, (void **)&fs);
    if (EFI_ERROR(status) || fs == 0) return EFI_NOT_FOUND;
    debug_write("OPENCONSOLE_UPDATE_STORAGE_FILESYSTEM\n");
    status = fs->open_volume(fs, &root);
    if (EFI_ERROR(status) || root == 0) return status;
    debug_write("OPENCONSOLE_UPDATE_STORAGE_VOLUME\n");
    status = oc_uefi_update_storage_initialize(&storage,
        system_table->boot_services, root, crypto, 1024u * 1024u);
    if (EFI_ERROR(status)) goto done;
    debug_write("OPENCONSOLE_UPDATE_STORAGE_READY\n");
    for (index = 0u; index < sizeof(policy); ++index)
        ((uint8_t *)&policy)[index] = 0u;
    policy.installed_version = 1u; policy.highest_sequence = 1u;
    policy.maximum_artifact_size = 1024u * 1024u;
    for (index = 0u; index < sizeof(policy.trusted_key_id); ++index)
        policy.trusted_key_id[index] = 0x11u;

    status = crypto->sha256(crypto, first_artifact, sizeof(first_artifact), digest);
    if (EFI_ERROR(status) || oc_update_stage_and_commit(&storage.operations,
        first_artifact, sizeof(first_artifact), digest, &policy, &selected) !=
        OC_UPDATE_STORAGE_OK || selected.generation != 1u || selected.slot != 0u)
        { status = EFI_SECURITY_VIOLATION; goto shutdown; }
    debug_write("OPENCONSOLE_UPDATE_STORAGE_FIRST_COMMITTED\n");

    policy.installed_version = 2u; policy.highest_sequence = 2u;
    status = crypto->sha256(crypto, second_artifact, sizeof(second_artifact), digest);
    if (EFI_ERROR(status) || oc_update_stage_and_commit(&storage.operations,
        second_artifact, sizeof(second_artifact), digest, &policy, &selected) !=
        OC_UPDATE_STORAGE_OK || selected.generation != 2u || selected.slot != 1u)
        { status = EFI_SECURITY_VIOLATION; goto shutdown; }
    if (oc_update_recover(&storage.operations, &selected) != OC_UPDATE_STORAGE_OK ||
        selected.generation != 2u || selected.slot != 1u ||
        selected.policy.highest_sequence != 2u ||
        selected.policy.installed_version != 2u)
        { status = EFI_SECURITY_VIOLATION; goto shutdown; }
    debug_write("OPENCONSOLE_UPDATE_STORAGE_SECOND_COMMITTED\n");

    for (index = 0u; index < sizeof(corrupted); ++index)
        corrupted[index] = second_artifact[index];
    corrupted[sizeof(corrupted) - 1u] ^= 1u;
    if (!storage.operations.write_file(storage.operations.context,
            oc_update_artifact_path(1u), corrupted, sizeof(corrupted)) ||
        !storage.operations.flush_file(storage.operations.context,
            oc_update_artifact_path(1u)) ||
        oc_update_recover(&storage.operations, &selected) != OC_UPDATE_STORAGE_OK ||
        selected.generation != 1u || selected.slot != 0u)
        { status = EFI_SECURITY_VIOLATION; goto shutdown; }
    debug_write("OPENCONSOLE_UPDATE_STORAGE_LKG_RECOVERED\n");
    status = EFI_SUCCESS;
shutdown:
    oc_uefi_update_storage_shutdown(&storage);
done:
    (void)root->close(root);
    return status;
}
