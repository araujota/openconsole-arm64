#include "openconsole/gpu_probe.h"

#define EFI_LOCATE_BY_PROTOCOL 2u
#define EFI_PCI_IO_WIDTH_UINT32 2u
#define AMD_PCI_VENDOR_ID 0x1002u

static const EFI_GUID pci_io_guid =
    {0x4cf5b200u, 0x68b8u, 0x4ca5u, {0x9e,0xec,0xb2,0x3e,0x3f,0x50,0x02,0x9a}};

EFI_STATUS oc_probe_amd_pci(EFI_BOOT_SERVICES *boot_services,
                            oc_pci_device *devices, UINTN capacity,
                            UINTN *device_count) {
    EFI_HANDLE *handles = 0;
    UINTN handle_count = 0u;
    UINTN i;
    EFI_STATUS status;
    *device_count = 0u;
    status = boot_services->locate_handle_buffer(
        EFI_LOCATE_BY_PROTOCOL, &pci_io_guid, 0, &handle_count, &handles);
    if (EFI_ERROR(status)) return status;
    for (i = 0u; i < handle_count; ++i) {
        EFI_PCI_IO_PROTOCOL *pci = 0;
        uint32_t config[4] = {0u, 0u, 0u, 0u};
        status = boot_services->handle_protocol(
            handles[i], &pci_io_guid, (void **)&pci);
        if (EFI_ERROR(status)) continue;
        status = pci->pci.read(pci, EFI_PCI_IO_WIDTH_UINT32, 0u, 4u, config);
        if (EFI_ERROR(status) || (uint16_t)config[0] != AMD_PCI_VENDOR_ID) continue;
        if (*device_count < capacity) {
            oc_pci_device *device = &devices[*device_count];
            device->vendor_id = (uint16_t)config[0];
            device->device_id = (uint16_t)(config[0] >> 16);
            device->programming_interface = (uint8_t)(config[2] >> 8);
            device->sub_class = (uint8_t)(config[2] >> 16);
            device->base_class = (uint8_t)(config[2] >> 24);
            ++*device_count;
        }
    }
    boot_services->free_pool(handles);
    return EFI_SUCCESS;
}
