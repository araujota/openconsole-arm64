#ifndef OPENCONSOLE_IO_INVENTORY_H
#define OPENCONSOLE_IO_INVENTORY_H

#include "openconsole/uefi.h"

typedef void (*oc_io_inventory_emit)(void *context, const char *text);

typedef struct {
    UINTN pci_handles;
    UINTN pci_read_failures;
    UINTN pci_device_paths;
    UINTN network_controllers;
    UINTN hda_controllers;
    UINTN audio_controllers;
    UINTN snp_handles;
    UINTN undi_handles;
    UINTN undi31_handles;
    UINTN mnp_service_bindings;
    UINTN dhcp4_service_bindings;
    UINTN ip4_service_bindings;
    UINTN udp4_service_bindings;
    UINTN tcp4_service_bindings;
    UINTN dns4_service_bindings;
    UINTN http_service_bindings;
    UINTN tls_service_bindings;
    UINTN audio_io_handles;
    UINTN hda_controller_info_handles;
    UINTN hda_codec_info_handles;
    UINTN usb_io_handles;
    UINTN usb_audio_interfaces;
} oc_io_inventory_result;

typedef enum {
    OC_AUDIO_DRIVER_NONE = 0,
    OC_AUDIO_DRIVER_EXISTING_AUDIOIO = 1,
    OC_AUDIO_DRIVER_OPENCORE_AUDIODXE = 2,
    OC_AUDIO_DRIVER_USB_UAC_REQUIRED = 3
} oc_audio_driver_recommendation;

oc_audio_driver_recommendation oc_io_inventory_recommend_audio_driver(
    const oc_io_inventory_result *result);

EFI_STATUS oc_io_inventory_collect(EFI_BOOT_SERVICES *boot_services,
                                   oc_io_inventory_emit emit,
                                   void *emit_context,
                                   oc_io_inventory_result *result);

#endif
