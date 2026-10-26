#include "openconsole/io_inventory.h"

#define EFI_LOCATE_BY_PROTOCOL 2u
#define EFI_PCI_IO_WIDTH_UINT32 2u

static const EFI_GUID pci_io_guid =
    {0x4cf5b200u, 0x68b8u, 0x4ca5u, {0x9e,0xec,0xb2,0x3e,0x3f,0x50,0x02,0x9a}};
static const EFI_GUID device_path_guid =
    {0x09576e91u, 0x6d3fu, 0x11d2u, {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const EFI_GUID usb_io_guid =
    {0x2b2f68d6u,0x0cd2u,0x44cfu,{0x8e,0x8b,0xbb,0xa2,0x0b,0x1b,0x5b,0x75}};

#pragma pack(push, 1)
typedef struct {
    uint8_t length, descriptor_type;
    uint16_t usb_bcd;
    uint8_t device_class, device_subclass, device_protocol, max_packet_size;
    uint16_t vendor_id, product_id, device_bcd;
    uint8_t manufacturer, product, serial_number, configuration_count;
} usb_device_descriptor;
typedef struct {
    uint8_t length, descriptor_type, interface_number, alternate_setting;
    uint8_t endpoint_count, interface_class, interface_subclass, interface_protocol;
    uint8_t interface_string;
} usb_interface_descriptor;
#pragma pack(pop)

typedef struct usb_io_protocol usb_io_protocol;
typedef EFI_STATUS(EFIAPI *usb_get_device_descriptor)(usb_io_protocol *, usb_device_descriptor *);
typedef EFI_STATUS(EFIAPI *usb_get_interface_descriptor)(usb_io_protocol *, usb_interface_descriptor *);
struct usb_io_protocol {
    void *control_transfer, *bulk_transfer, *async_interrupt_transfer;
    void *sync_interrupt_transfer, *isochronous_transfer, *async_isochronous_transfer;
    usb_get_device_descriptor get_device_descriptor;
    void *get_config_descriptor;
    usb_get_interface_descriptor get_interface_descriptor;
};

typedef struct {
    const char *name;
    EFI_GUID guid;
    UINTN *count;
} protocol_probe;

typedef struct {
    uint8_t type;
    uint8_t subtype;
    uint8_t length[2];
} oc_device_path_node;

static void zero_bytes(void *destination, UINTN size) {
    uint8_t *bytes = destination;
    while (size-- != 0u) *bytes++ = 0u;
}

static void append_text(char **cursor, const char *text) {
    while (*text != '\0') *(*cursor)++ = *text++;
}

static void append_hex(char **cursor, uint64_t value, UINTN digits) {
    static const char hex[] = "0123456789ABCDEF";
    while (digits-- != 0u) *(*cursor)++ = hex[(value >> (digits * 4u)) & 15u];
}

static void emit_count(oc_io_inventory_emit emit, void *context,
                       const char *name, UINTN count) {
    char line[96];
    char *cursor = line;
    append_text(&cursor, "OPENCONSOLE_IO_PROTOCOL_");
    append_text(&cursor, name);
    *cursor++ = '_';
    append_hex(&cursor, count, sizeof(UINTN) * 2u);
    *cursor++ = '\n';
    *cursor = '\0';
    emit(context, line);
}

static void emit_audio_driver_recommendation(oc_io_inventory_emit emit,
                                             void *context,
                                             const oc_io_inventory_result *result) {
    oc_audio_driver_recommendation recommendation =
        oc_io_inventory_recommend_audio_driver(result);
    if (recommendation == OC_AUDIO_DRIVER_EXISTING_AUDIOIO) {
        emit(context, "OPENCONSOLE_IO_AUDIO_DRIVER_USE_EXISTING_AUDIOIO\n");
    } else if (recommendation == OC_AUDIO_DRIVER_OPENCORE_AUDIODXE) {
        emit(context, "OPENCONSOLE_IO_AUDIO_DRIVER_DOWNLOAD_OPENCORE_AUDIODXE\n");
    } else if (recommendation == OC_AUDIO_DRIVER_USB_UAC_REQUIRED) {
        emit(context, "OPENCONSOLE_IO_AUDIO_DRIVER_USB_UAC_BACKEND_REQUIRED\n");
    } else {
        emit(context, "OPENCONSOLE_IO_AUDIO_DRIVER_NO_COMPATIBLE_BACKEND_IDENTIFIED\n");
    }
    if (result->usb_audio_interfaces != 0u)
        emit(context, "OPENCONSOLE_IO_AUDIO_USB_REQUIRES_SEPARATE_UAC_BACKEND\n");
}

oc_audio_driver_recommendation oc_io_inventory_recommend_audio_driver(
    const oc_io_inventory_result *result) {
    if (result == 0) return OC_AUDIO_DRIVER_NONE;
    if (result->audio_io_handles != 0u) return OC_AUDIO_DRIVER_EXISTING_AUDIOIO;
    if (result->hda_controllers != 0u) return OC_AUDIO_DRIVER_OPENCORE_AUDIODXE;
    if (result->usb_audio_interfaces != 0u) return OC_AUDIO_DRIVER_USB_UAC_REQUIRED;
    return OC_AUDIO_DRIVER_NONE;
}

static UINTN count_handles(EFI_BOOT_SERVICES *boot_services,
                           const EFI_GUID *guid) {
    EFI_HANDLE *handles = 0;
    UINTN count = 0u;
    EFI_STATUS status = boot_services->locate_handle_buffer(
        EFI_LOCATE_BY_PROTOCOL, guid, 0, &count, &handles);
    if (!EFI_ERROR(status) && handles != 0) (void)boot_services->free_pool(handles);
    return EFI_ERROR(status) ? 0u : count;
}

static void emit_pci(oc_io_inventory_emit emit, void *context,
                     uint16_t vendor, uint16_t device, uint8_t base_class,
                     uint8_t sub_class, uint8_t interface, int has_device_path) {
    char line[112];
    char *cursor = line;
    append_text(&cursor, "OPENCONSOLE_IO_PCI_");
    append_hex(&cursor, vendor, 4u);
    *cursor++ = '_';
    append_hex(&cursor, device, 4u);
    *cursor++ = '_';
    append_hex(&cursor, base_class, 2u);
    *cursor++ = '_';
    append_hex(&cursor, sub_class, 2u);
    *cursor++ = '_';
    append_hex(&cursor, interface, 2u);
    append_text(&cursor, has_device_path ? "_DEVICE_PATH\n" : "_NO_DEVICE_PATH\n");
    *cursor = '\0';
    emit(context, line);
}

static void emit_usb_audio(oc_io_inventory_emit emit, void *context,
                           const usb_device_descriptor *device,
                           const usb_interface_descriptor *interface) {
    char line[128], *cursor = line;
    append_text(&cursor, "OPENCONSOLE_IO_USB_AUDIO_");
    append_hex(&cursor, device->vendor_id, 4u); *cursor++ = '_';
    append_hex(&cursor, device->product_id, 4u); *cursor++ = '_';
    append_hex(&cursor, interface->interface_number, 2u); *cursor++ = '_';
    append_hex(&cursor, interface->alternate_setting, 2u); *cursor++ = '_';
    append_hex(&cursor, interface->interface_class, 2u); *cursor++ = '_';
    append_hex(&cursor, interface->interface_subclass, 2u); *cursor++ = '_';
    append_hex(&cursor, interface->interface_protocol, 2u); *cursor++ = '\n';
    *cursor = '\0'; emit(context, line);
}

static void collect_usb_audio(EFI_BOOT_SERVICES *boot_services,
                              oc_io_inventory_emit emit, void *context,
                              oc_io_inventory_result *result) {
    EFI_HANDLE *handles = 0;
    UINTN count = 0u, i;
    if (EFI_ERROR(boot_services->locate_handle_buffer(
            EFI_LOCATE_BY_PROTOCOL, &usb_io_guid, 0, &count, &handles))) return;
    result->usb_io_handles = count;
    for (i = 0u; i < count; ++i) {
        usb_io_protocol *usb = 0;
        usb_device_descriptor device;
        usb_interface_descriptor interface;
        zero_bytes(&device, sizeof(device)); zero_bytes(&interface, sizeof(interface));
        if (EFI_ERROR(boot_services->handle_protocol(handles[i], &usb_io_guid,
                (void **)&usb)) || usb == 0 || usb->get_device_descriptor == 0 ||
            usb->get_interface_descriptor == 0 ||
            EFI_ERROR(usb->get_device_descriptor(usb, &device)) ||
            EFI_ERROR(usb->get_interface_descriptor(usb, &interface)) ||
            interface.interface_class != 0x01u) continue;
        ++result->usb_audio_interfaces;
        emit_usb_audio(emit, context, &device, &interface);
    }
    if (handles != 0) (void)boot_services->free_pool(handles);
}

static UINTN device_path_prefix_size(const void *path) {
    const uint8_t *bytes = path;
    UINTN offset = 0u;
    if (path == 0) return 0u;
    while (offset + sizeof(oc_device_path_node) <= 4096u) {
        const oc_device_path_node *node = (const oc_device_path_node *)(bytes + offset);
        UINTN length = (UINTN)node->length[0] | ((UINTN)node->length[1] << 8u);
        if (length < sizeof(*node) || offset + length > 4096u) return 0u;
        if (node->type == 0x7fu && node->subtype == 0xffu) return offset;
        offset += length;
    }
    return 0u;
}

static int path_has_prefix(const void *candidate, const void *controller) {
    const uint8_t *left = candidate;
    const uint8_t *right = controller;
    UINTN prefix = device_path_prefix_size(controller);
    UINTN candidate_size = device_path_prefix_size(candidate);
    UINTN i;
    if (prefix == 0u || candidate_size < prefix) return 0;
    for (i = 0u; i < prefix; ++i) if (left[i] != right[i]) return 0;
    return 1;
}

static UINTN count_protocol_descendants(EFI_BOOT_SERVICES *boot_services,
                                        const EFI_GUID *protocol_guid,
                                        const void *controller_path) {
    EFI_HANDLE *handles = 0;
    UINTN count = 0u, matches = 0u, i;
    EFI_STATUS status = boot_services->locate_handle_buffer(
        EFI_LOCATE_BY_PROTOCOL, protocol_guid, 0, &count, &handles);
    if (EFI_ERROR(status)) return 0u;
    for (i = 0u; i < count; ++i) {
        void *path = 0;
        if (!EFI_ERROR(boot_services->handle_protocol(
                handles[i], &device_path_guid, &path)) &&
            path_has_prefix(path, controller_path)) ++matches;
    }
    if (handles != 0) (void)boot_services->free_pool(handles);
    return matches;
}

static void emit_pci_protocols(oc_io_inventory_emit emit, void *context,
                               EFI_BOOT_SERVICES *boot_services,
                               uint16_t vendor, uint16_t device,
                               uint8_t revision, const void *controller_path,
                               const protocol_probe *probes, UINTN probe_count) {
    char line[128];
    char *cursor = line;
    UINTN i;
    append_text(&cursor, "OPENCONSOLE_IO_PCI_PROTOCOLS_");
    append_hex(&cursor, vendor, 4u); *cursor++ = '_';
    append_hex(&cursor, device, 4u); *cursor++ = '_';
    append_hex(&cursor, revision, 2u);
    for (i = 0u; i < probe_count; ++i) {
        UINTN matches;
        if (i > 2u && i < 11u) continue;
        matches = count_protocol_descendants(boot_services, &probes[i].guid,
                                             controller_path);
        *cursor++ = '_'; append_text(&cursor, probes[i].name); *cursor++ = '_';
        append_hex(&cursor, matches, 2u);
    }
    *cursor++ = '\n'; *cursor = '\0';
    emit(context, line);
}

EFI_STATUS oc_io_inventory_collect(EFI_BOOT_SERVICES *boot_services,
                                   oc_io_inventory_emit emit,
                                   void *emit_context,
                                   oc_io_inventory_result *result) {
    EFI_HANDLE *handles = 0;
    UINTN handle_count = 0u;
    UINTN i;
    EFI_STATUS status;
    protocol_probe probes[] = {
        {"SNP", {0xa19832b9u,0xac25u,0x11d3u,{0x9a,0x2d,0x00,0x90,0x27,0x3f,0xc1,0x4d}}, &result->snp_handles},
        {"UNDI", {0xe18541cdu,0xf755u,0x4f73u,{0x92,0x8d,0x64,0x3c,0x8a,0x79,0xb2,0x29}}, &result->undi_handles},
        {"UNDI31", {0x1aced566u,0x76edu,0x4218u,{0xbc,0x81,0x76,0x7f,0x1f,0x97,0x7a,0x89}}, &result->undi31_handles},
        {"MNP_SB", {0xf36ff770u,0xa7e1u,0x42cfu,{0x9e,0xd2,0x56,0xf0,0xf2,0x71,0xf4,0x4c}}, &result->mnp_service_bindings},
        {"DHCP4_SB", {0x9d9a39d8u,0xbd42u,0x4a73u,{0xa4,0xd5,0x8e,0xe9,0x4b,0xe1,0x13,0x80}}, &result->dhcp4_service_bindings},
        {"IP4_SB", {0xc51711e7u,0xb4bfu,0x404au,{0xbf,0xb8,0x0a,0x04,0x8e,0xf1,0xff,0xe4}}, &result->ip4_service_bindings},
        {"UDP4_SB", {0x83f01464u,0x99bdu,0x45e5u,{0xb3,0x83,0xaf,0x63,0x05,0xd8,0xe9,0xe6}}, &result->udp4_service_bindings},
        {"TCP4_SB", {0x00720665u,0x67ebu,0x4a99u,{0xba,0xf7,0xd3,0xc3,0x3a,0x1c,0x7c,0xc9}}, &result->tcp4_service_bindings},
        {"DNS4_SB", {0xb625b186u,0xe063u,0x44f7u,{0x89,0x05,0x6a,0x74,0xdc,0x6f,0x52,0xb4}}, &result->dns4_service_bindings},
        {"HTTP_SB", {0xbdc8e6afu,0xd9bcu,0x4379u,{0xa7,0x2a,0xe0,0xc4,0xe7,0x5d,0xae,0x1c}}, &result->http_service_bindings},
        {"TLS_SB", {0x952cb795u,0xff36u,0x48cfu,{0xa2,0x49,0x4d,0xf4,0x86,0xd6,0xab,0x8d}}, &result->tls_service_bindings},
        {"AUDIO_IO", {0x22266891u,0x2032u,0x4baeu,{0xb7,0xb5,0x43,0x74,0xe7,0x32,0x09,0x49}}, &result->audio_io_handles},
        {"HDA_CONTROLLER_INFO", {0xe5fc2cafu,0x0291u,0x46f2u,{0x87,0xf8,0x10,0xc7,0x58,0x72,0x58,0x04}}, &result->hda_controller_info_handles},
        {"HDA_CODEC_INFO", {0x6c9cdde1u,0xe8a5u,0x43e5u,{0xbe,0x88,0xda,0x15,0xbc,0x1c,0x02,0x50}}, &result->hda_codec_info_handles}
    };

    if (boot_services == 0 || emit == 0 || result == 0) return EFI_INVALID_PARAMETER;
    zero_bytes(result, sizeof(*result));
    emit(emit_context, "OPENCONSOLE_IO_INVENTORY_BEGIN\n");
    status = boot_services->locate_handle_buffer(
        EFI_LOCATE_BY_PROTOCOL, &pci_io_guid, 0, &handle_count, &handles);
    if (!EFI_ERROR(status)) {
        result->pci_handles = handle_count;
        for (i = 0u; i < handle_count; ++i) {
            EFI_PCI_IO_PROTOCOL *pci = 0;
            void *device_path = 0;
            uint32_t config[4] = {0u, 0u, 0u, 0u};
            int has_device_path;
            status = boot_services->handle_protocol(handles[i], &pci_io_guid,
                                                     (void **)&pci);
            if (EFI_ERROR(status) || pci == 0 || EFI_ERROR(pci->pci.read(
                    pci, EFI_PCI_IO_WIDTH_UINT32, 0u, 4u, config))) {
                ++result->pci_read_failures;
                continue;
            }
            has_device_path = !EFI_ERROR(boot_services->handle_protocol(
                handles[i], &device_path_guid, &device_path));
            if (has_device_path) ++result->pci_device_paths;
            if ((uint8_t)(config[2] >> 24) == 0x02u) ++result->network_controllers;
            if ((uint8_t)(config[2] >> 24) == 0x04u) {
                ++result->audio_controllers;
                if ((uint8_t)(config[2] >> 16) == 0x03u) ++result->hda_controllers;
            }
            emit_pci(emit, emit_context, (uint16_t)config[0],
                     (uint16_t)(config[0] >> 16), (uint8_t)(config[2] >> 24),
                     (uint8_t)(config[2] >> 16), (uint8_t)(config[2] >> 8),
                     has_device_path);
            emit_pci_protocols(emit, emit_context, boot_services,
                (uint16_t)config[0], (uint16_t)(config[0] >> 16),
                (uint8_t)config[2], has_device_path ? device_path : 0,
                probes, sizeof(probes) / sizeof(probes[0]));
        }
        if (handles != 0) (void)boot_services->free_pool(handles);
    }
    for (i = 0u; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        *probes[i].count = count_handles(boot_services, &probes[i].guid);
        emit_count(emit, emit_context, probes[i].name, *probes[i].count);
    }
    collect_usb_audio(boot_services, emit, emit_context, result);
    emit_count(emit, emit_context, "PCI", result->pci_handles);
    emit_count(emit, emit_context, "PCI_READ_FAILURE", result->pci_read_failures);
    emit_count(emit, emit_context, "PCI_DEVICE_PATH", result->pci_device_paths);
    emit_count(emit, emit_context, "NETWORK_CONTROLLER", result->network_controllers);
    emit_count(emit, emit_context, "AUDIO_CONTROLLER", result->audio_controllers);
    emit_count(emit, emit_context, "HDA_CONTROLLER", result->hda_controllers);
    emit_count(emit, emit_context, "USB_IO", result->usb_io_handles);
    emit_count(emit, emit_context, "USB_AUDIO_INTERFACE", result->usb_audio_interfaces);
    emit_audio_driver_recommendation(emit, emit_context, result);
    emit(emit_context, "OPENCONSOLE_IO_INVENTORY_END\n");
    return EFI_SUCCESS;
}
