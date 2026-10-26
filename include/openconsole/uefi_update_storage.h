#ifndef OPENCONSOLE_UEFI_UPDATE_STORAGE_H
#define OPENCONSOLE_UEFI_UPDATE_STORAGE_H

#include "openconsole/crypto_protocol.h"
#include "openconsole/update_storage.h"

typedef struct {
    EFI_BOOT_SERVICES *boot_services;
    EFI_FILE_PROTOCOL *root;
    OC_CRYPTO_PROTOCOL *crypto;
    uint64_t maximum_artifact_size;
    EFI_FILE_PROTOCOL *pending_file;
    char pending_path[OC_UPDATE_PATH_MAX];
    oc_update_storage_ops operations;
} oc_uefi_update_storage;

EFI_STATUS oc_uefi_update_storage_initialize(oc_uefi_update_storage *,
                                             EFI_BOOT_SERVICES *,
                                             EFI_FILE_PROTOCOL *,
                                             OC_CRYPTO_PROTOCOL *, uint64_t);
void oc_uefi_update_storage_shutdown(oc_uefi_update_storage *);

#endif
