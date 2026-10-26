#ifndef OPENCONSOLE_ETHERNET_DRIVER_SELECTOR_H
#define OPENCONSOLE_ETHERNET_DRIVER_SELECTOR_H

#include "openconsole/uefi.h"

#define OC_ETH_MAX_CONTROLLERS 16u
#define OC_ETH_MAX_CANDIDATES 32u

typedef enum {
    OC_ETH_ARCH_X64 = 1,
    OC_ETH_ARCH_ARM64 = 2
} oc_eth_architecture;

typedef enum {
    OC_ETH_OWNER_NONE = 0,
    OC_ETH_OWNER_FIRMWARE = 1,
    OC_ETH_OWNER_SUPPLEMENTAL = 2
} oc_eth_owner;

typedef enum {
    OC_ETH_DIAG_FIRMWARE_SELECTED = 1,
    OC_ETH_DIAG_NO_EXACT_CANDIDATE = 2,
    OC_ETH_DIAG_VERIFY_REJECTED = 3,
    OC_ETH_DIAG_LOAD_REJECTED = 4,
    OC_ETH_DIAG_CONNECT_REJECTED = 5,
    OC_ETH_DIAG_PROTOCOL_REJECTED = 6,
    OC_ETH_DIAG_CLEANUP_FAILED = 7,
    OC_ETH_DIAG_SUPPLEMENTAL_SELECTED = 8
} oc_eth_diagnostic;

typedef struct {
    EFI_HANDLE handle;
    uint16_t vendor;
    uint16_t device;
    uint16_t subsystem_vendor;
    uint16_t subsystem_device;
    uint8_t revision;
    uint8_t firmware_snp_present;
    uint8_t firmware_snp_usable;
} oc_eth_controller;

typedef struct {
    const char *manifest_id;
    const void *artifact;
    UINTN artifact_size;
    oc_eth_architecture architecture;
    uint16_t vendor;
    uint16_t device;
    uint16_t subsystem_vendor;
    uint16_t subsystem_device;
    uint8_t revision_min;
    uint8_t revision_max;
    uint32_t expected_protocols;
    uint32_t priority;
} oc_eth_candidate;

typedef struct {
    oc_eth_owner owner;
    UINTN controller_index;
    UINTN candidate_index;
    EFI_HANDLE image;
} oc_eth_selection;

typedef EFI_STATUS (*oc_eth_verify_candidate)(void *, const oc_eth_candidate *);
typedef EFI_STATUS (*oc_eth_load_candidate)(void *, const oc_eth_candidate *,
                                            EFI_HANDLE *);
typedef EFI_STATUS (*oc_eth_connect_candidate)(void *, EFI_HANDLE, EFI_HANDLE);
typedef EFI_STATUS (*oc_eth_probe_candidate)(void *, EFI_HANDLE, uint32_t);
typedef EFI_STATUS (*oc_eth_disconnect_candidate)(void *, EFI_HANDLE, EFI_HANDLE);
typedef EFI_STATUS (*oc_eth_unload_candidate)(void *, EFI_HANDLE);
typedef void (*oc_eth_emit_diagnostic)(void *, oc_eth_diagnostic, UINTN, UINTN,
                                       EFI_STATUS);

typedef struct {
    void *context;
    oc_eth_verify_candidate verify;
    oc_eth_load_candidate load;
    oc_eth_connect_candidate connect;
    oc_eth_probe_candidate probe;
    oc_eth_disconnect_candidate disconnect;
    oc_eth_unload_candidate unload;
    oc_eth_emit_diagnostic emit;
} oc_eth_selector_ops;

typedef struct {
    UINTN controller_count;
    UINTN firmware_selected;
    UINTN supplemental_selected;
    UINTN unserved;
    UINTN attempted;
    UINTN rejected;
    UINTN cleanup_failures;
    oc_eth_selection selections[OC_ETH_MAX_CONTROLLERS];
} oc_eth_selection_report;

EFI_STATUS oc_eth_select_drivers(const oc_eth_controller *, UINTN,
                                 const oc_eth_candidate *, UINTN,
                                 oc_eth_architecture,
                                 const oc_eth_selector_ops *,
                                 oc_eth_selection_report *);

#endif
