#ifndef OPENCONSOLE_GPU_PROBE_H
#define OPENCONSOLE_GPU_PROBE_H

#include "openconsole/uefi.h"

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t base_class;
    uint8_t sub_class;
    uint8_t programming_interface;
} oc_pci_device;

EFI_STATUS oc_probe_amd_pci(EFI_BOOT_SERVICES *boot_services,
                            oc_pci_device *devices, UINTN capacity,
                            UINTN *device_count);

#endif

