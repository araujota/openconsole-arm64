#include "openconsole/io_inventory.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static EFI_HANDLE pci_handles[] = {(EFI_HANDLE)(UINTN)1u, (EFI_HANDLE)(UINTN)2u};
static EFI_HANDLE protocol_handles[] = {(EFI_HANDLE)(UINTN)3u, (EFI_HANDLE)(UINTN)4u};
static EFI_HANDLE usb_handles[] = {(EFI_HANDLE)(UINTN)5u};
static EFI_PCI_IO_PROTOCOL pci_devices[2];
static uint8_t net_path[] = {1u,1u,4u,0u, 0x7fu,0xffu,4u,0u};
static uint8_t hda_path[] = {1u,2u,4u,0u, 0x7fu,0xffu,4u,0u};
static uint8_t net_child_path[] = {1u,1u,4u,0u, 3u,1u,4u,0u,
                                   0x7fu,0xffu,4u,0u};
static uint8_t hda_child_path[] = {1u,2u,4u,0u, 3u,2u,4u,0u,
                                   0x7fu,0xffu,4u,0u};
static unsigned writes;
static char evidence[8192];
static size_t evidence_size;

#pragma pack(push, 1)
typedef struct { uint8_t bytes[8]; uint16_t vendor, product; uint8_t tail[6]; } test_usb_device;
typedef struct { uint8_t length,type,number,alternate,endpoints,klass,subclass,protocol,string; } test_usb_interface;
#pragma pack(pop)
typedef struct test_usb_io test_usb_io;
struct test_usb_io {
    void *unused[6];
    EFI_STATUS(EFIAPI *get_device)(test_usb_io *, test_usb_device *);
    void *get_config;
    EFI_STATUS(EFIAPI *get_interface)(test_usb_io *, test_usb_interface *);
};
static EFI_STATUS EFIAPI usb_get_device(test_usb_io *self, test_usb_device *descriptor) {
    (void)self; memset(descriptor, 0, sizeof(*descriptor));
    descriptor->vendor = 0x17e9u; descriptor->product = 0x6006u; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI usb_get_interface(test_usb_io *self, test_usb_interface *descriptor) {
    (void)self; memset(descriptor, 0, sizeof(*descriptor));
    descriptor->number = 2u; descriptor->klass = 1u; descriptor->subclass = 1u;
    descriptor->protocol = 0x20u; return EFI_SUCCESS;
}
static test_usb_io usb_io = {{0}, usb_get_device, 0, usb_get_interface};

static EFI_STATUS EFIAPI pool_free(void *memory) {
    (void)memory;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI pci_read(EFI_PCI_IO_PROTOCOL *self, uint32_t width,
                                  uint32_t offset, UINTN count, void *buffer) {
    uint32_t *words = buffer;
    assert(width == 2u);
    assert(offset == 0u);
    assert(count == 4u);
    memset(buffer, 0, 4u * sizeof(*words));
    if (self == &pci_devices[0]) {
        words[0] = 0x812510ecu;
        words[2] = 0x02000000u;
    } else {
        words[0] = 0x15e31022u;
        words[2] = 0x04030000u;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI pci_write(EFI_PCI_IO_PROTOCOL *self, uint32_t width,
                                   uint32_t offset, UINTN count, void *buffer) {
    (void)self;
    (void)width;
    (void)offset;
    (void)count;
    (void)buffer;
    ++writes;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI locate_handles(uint32_t search_type, const EFI_GUID *guid,
                                        void *search_key, UINTN *count,
                                        EFI_HANDLE **handles) {
    (void)search_key;
    assert(search_type == 2u);
    if (guid->data1 == 0x4cf5b200u) {
        *count = 2u;
        *handles = pci_handles;
        return EFI_SUCCESS;
    }
    if (guid->data1 == 0xa19832b9u) {
        *count = 1u;
        *handles = protocol_handles;
        return EFI_SUCCESS;
    }
    if (guid->data1 == 0x22266891u) {
        *count = 2u;
        *handles = protocol_handles;
        return EFI_SUCCESS;
    }
    if (guid->data1 == 0x2b2f68d6u) {
        *count = 1u; *handles = usb_handles; return EFI_SUCCESS;
    }
    *count = 0u;
    *handles = 0;
    return EFI_NOT_FOUND;
}

static EFI_STATUS EFIAPI handle_protocol(EFI_HANDLE handle, const EFI_GUID *guid,
                                         void **interface) {
    if (guid->data1 == 0x4cf5b200u && (UINTN)handle >= 1u && (UINTN)handle <= 2u) {
        *interface = &pci_devices[(UINTN)handle - 1u];
        return EFI_SUCCESS;
    }
    if (guid->data1 == 0x09576e91u && handle == pci_handles[0]) {
        *interface = net_path;
        return EFI_SUCCESS;
    }
    if (guid->data1 == 0x09576e91u && handle == pci_handles[1]) {
        *interface = hda_path;
        return EFI_SUCCESS;
    }
    if (guid->data1 == 0x09576e91u && handle == protocol_handles[0]) {
        *interface = net_child_path;
        return EFI_SUCCESS;
    }
    if (guid->data1 == 0x09576e91u && handle == protocol_handles[1]) {
        *interface = hda_child_path;
        return EFI_SUCCESS;
    }
    if (guid->data1 == 0x2b2f68d6u && handle == usb_handles[0]) {
        *interface = &usb_io; return EFI_SUCCESS;
    }
    *interface = 0;
    return EFI_NOT_FOUND;
}

static void emit(void *context, const char *text) {
    size_t length = strlen(text);
    (void)context;
    assert(evidence_size + length < sizeof(evidence));
    memcpy(evidence + evidence_size, text, length + 1u);
    evidence_size += length;
}

int main(void) {
    EFI_BOOT_SERVICES services;
    oc_io_inventory_result result;
    EFI_STATUS status;
    memset(&services, 0, sizeof(services));
    memset(pci_devices, 0, sizeof(pci_devices));
    pci_devices[0].pci.read = pci_read;
    pci_devices[0].pci.write = pci_write;
    pci_devices[1].pci.read = pci_read;
    pci_devices[1].pci.write = pci_write;
    services.free_pool = pool_free;
    services.handle_protocol = handle_protocol;
    services.locate_handle_buffer = locate_handles;

    status = oc_io_inventory_collect(&services, emit, 0, &result);
    assert(status == EFI_SUCCESS);
    assert(writes == 0u);
    assert(result.pci_handles == 2u);
    assert(result.pci_read_failures == 0u);
    assert(result.pci_device_paths == 2u);
    assert(result.network_controllers == 1u);
    assert(result.audio_controllers == 1u);
    assert(result.hda_controllers == 1u);
    assert(result.snp_handles == 1u);
    assert(result.audio_io_handles == 2u);
    assert(result.usb_io_handles == 1u);
    assert(result.usb_audio_interfaces == 1u);
    assert(strstr(evidence, "OPENCONSOLE_IO_PCI_10EC_8125_02_00_00_DEVICE_PATH\n") != 0);
    assert(strstr(evidence, "OPENCONSOLE_IO_PCI_1022_15E3_04_03_00_DEVICE_PATH\n") != 0);
    assert(strstr(evidence, "OPENCONSOLE_IO_PCI_PROTOCOLS_10EC_8125_00_SNP_01_UNDI_00_UNDI31_00_AUDIO_IO_01_HDA_CONTROLLER_INFO_00_HDA_CODEC_INFO_00\n") != 0);
    assert(strstr(evidence, "OPENCONSOLE_IO_PCI_PROTOCOLS_1022_15E3_00_SNP_00_UNDI_00_UNDI31_00_AUDIO_IO_01_HDA_CONTROLLER_INFO_00_HDA_CODEC_INFO_00\n") != 0);
    assert(strstr(evidence, "OPENCONSOLE_IO_AUDIO_DRIVER_USE_EXISTING_AUDIOIO\n") != 0);
    assert(strstr(evidence, "OPENCONSOLE_IO_PROTOCOL_SNP_0000000000000001\n") != 0);
    assert(strstr(evidence, "OPENCONSOLE_IO_PROTOCOL_AUDIO_IO_0000000000000002\n") != 0);
    assert(strstr(evidence, "OPENCONSOLE_IO_USB_AUDIO_17E9_6006_02_00_01_01_20\n") != 0);
    assert(strstr(evidence, "OPENCONSOLE_IO_PROTOCOL_USB_AUDIO_INTERFACE_0000000000000001\n") != 0);
    assert(strstr(evidence, "OPENCONSOLE_IO_INVENTORY_END\n") != 0);

    memset(&result, 0, sizeof(result));
    assert(oc_io_inventory_recommend_audio_driver(&result) == OC_AUDIO_DRIVER_NONE);
    result.usb_audio_interfaces = 1u;
    assert(oc_io_inventory_recommend_audio_driver(&result) ==
           OC_AUDIO_DRIVER_USB_UAC_REQUIRED);
    result.hda_controllers = 1u;
    assert(oc_io_inventory_recommend_audio_driver(&result) ==
           OC_AUDIO_DRIVER_OPENCORE_AUDIODXE);
    result.audio_io_handles = 1u;
    assert(oc_io_inventory_recommend_audio_driver(&result) ==
           OC_AUDIO_DRIVER_EXISTING_AUDIOIO);
    assert(oc_io_inventory_recommend_audio_driver(0) == OC_AUDIO_DRIVER_NONE);
    puts("io inventory tests passed");
    return 0;
}
