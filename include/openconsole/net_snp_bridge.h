#ifndef OPENCONSOLE_NET_SNP_BRIDGE_H
#define OPENCONSOLE_NET_SNP_BRIDGE_H

#include "openconsole/uefi.h"

#define OC_NET_SNP_MAX_NII_HANDLES 16u

typedef struct {
    void *context;
    EFI_STATUS (*load_wrapper)(void *, EFI_HANDLE *);
    EFI_STATUS (*connect)(void *, EFI_HANDLE, EFI_HANDLE);
    int (*snp_present)(void *, EFI_HANDLE);
    EFI_STATUS (*disconnect)(void *, EFI_HANDLE, EFI_HANDLE);
    EFI_STATUS (*unload)(void *, EFI_HANDLE);
} oc_net_snp_bridge_ops;

typedef struct {
    UINTN nii_handles;
    UINTN connect_attempts;
    UINTN connect_failures;
    UINTN rollback_failures;
    EFI_HANDLE wrapper_image;
    EFI_HANDLE selected_controller;
} oc_net_snp_bridge_report;

EFI_STATUS oc_net_bridge_nii_to_snp(int, const EFI_HANDLE *, UINTN,
                                    const oc_net_snp_bridge_ops *,
                                    oc_net_snp_bridge_report *);

#endif
