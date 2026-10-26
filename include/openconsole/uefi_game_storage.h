/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OPENCONSOLE_UEFI_GAME_STORAGE_H
#define OPENCONSOLE_UEFI_GAME_STORAGE_H
#include "openconsole/authenticated_load.h"
#include "openconsole/crypto_protocol.h"
#include "openconsole/game_storage.h"
typedef struct {EFI_BOOT_SERVICES*boot_services;EFI_FILE_PROTOCOL*root;OC_CRYPTO_PROTOCOL*crypto;const oc_publisher_key_store*publisher_keys;EFI_FILE_PROTOCOL*pending_file;char pending_path[OC_GAME_STORAGE_PATH_MAX];oc_game_storage_ops operations;} oc_uefi_game_storage;
EFI_STATUS oc_uefi_game_storage_initialize(oc_uefi_game_storage*,EFI_BOOT_SERVICES*,EFI_FILE_PROTOCOL*,OC_CRYPTO_PROTOCOL*,const oc_publisher_key_store*);
EFI_STATUS oc_uefi_game_storage_read_authenticated_image(oc_uefi_game_storage*,const oc_game_selection*,void**,UINTN*);
void oc_uefi_game_storage_shutdown(oc_uefi_game_storage*);
#endif
