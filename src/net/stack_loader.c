#include "openconsole/net_stack_loader.h"
#include "openconsole/net_snp_bridge.h"

static void clear_bridge_report(oc_net_snp_bridge_report *report) {
    uint8_t *bytes = (uint8_t *)report;
    UINTN count = sizeof(*report);
    while (count-- != 0u) *bytes++ = 0u;
}

EFI_STATUS oc_net_bridge_nii_to_snp(int existing_snp,
                                    const EFI_HANDLE *nii_handles,
                                    UINTN nii_count,
                                    const oc_net_snp_bridge_ops *ops,
                                    oc_net_snp_bridge_report *report) {
    EFI_HANDLE wrapper = 0;
    uint8_t connected[OC_NET_SNP_MAX_NII_HANDLES];
    UINTN index;
    EFI_STATUS status;
    if (ops == 0 || report == 0 || ops->load_wrapper == 0 ||
        ops->connect == 0 || ops->snp_present == 0 ||
        ops->disconnect == 0 || ops->unload == 0 ||
        nii_count > OC_NET_SNP_MAX_NII_HANDLES ||
        (nii_count != 0u && nii_handles == 0)) return EFI_INVALID_PARAMETER;
    clear_bridge_report(report);
    report->nii_handles = nii_count;
    if (existing_snp) return EFI_SUCCESS;
    if (nii_count == 0u) return EFI_NOT_FOUND;
    for (index = 0u; index < nii_count; ++index) connected[index] = 0u;
    status = ops->load_wrapper(ops->context, &wrapper);
    if (EFI_ERROR(status) || wrapper == 0)
        return EFI_ERROR(status) ? status : EFI_NOT_FOUND;
    report->wrapper_image = wrapper;
    for (index = 0u; index < nii_count; ++index) {
        ++report->connect_attempts;
        status = ops->connect(ops->context, nii_handles[index], wrapper);
        if (EFI_ERROR(status)) {
            ++report->connect_failures;
            continue;
        }
        connected[index] = 1u;
        if (ops->snp_present(ops->context, nii_handles[index])) {
            UINTN prior;
            for (prior = 0u; prior < index; ++prior)
                if (connected[prior] && EFI_ERROR(ops->disconnect(
                        ops->context, nii_handles[prior], wrapper)))
                    ++report->rollback_failures;
            report->selected_controller = nii_handles[index];
            return EFI_SUCCESS;
        }
    }
    for (index = 0u; index < nii_count; ++index)
        if (connected[index] && EFI_ERROR(ops->disconnect(
                ops->context, nii_handles[index], wrapper)))
            ++report->rollback_failures;
    if (EFI_ERROR(ops->unload(ops->context, wrapper)))
        ++report->rollback_failures;
    report->wrapper_image = 0;
    return EFI_NOT_FOUND;
}

typedef struct {
    const CHAR16 *file_name;
    EFI_GUID produced_protocol;
} oc_net_stack_module;

static const CHAR16 dpc_name[] = {'D','p','c','D','x','e','.','e','f','i',0};
static const CHAR16 mnp_name[] = {'M','n','p','D','x','e','.','e','f','i',0};
static const CHAR16 arp_name[] = {'A','r','p','D','x','e','.','e','f','i',0};
static const CHAR16 dhcp4_name[] = {'D','h','c','p','4','D','x','e','.','e','f','i',0};
static const CHAR16 hash2_name[] = {'H','a','s','h','2','D','x','e','C','r','y','p','t','o','.','e','f','i',0};
static const CHAR16 ip4_name[] = {'I','p','4','D','x','e','.','e','f','i',0};
static const CHAR16 udp4_name[] = {'U','d','p','4','D','x','e','.','e','f','i',0};
static const CHAR16 tcp_name[] = {'T','c','p','D','x','e','.','e','f','i',0};
static const CHAR16 dns_name[] = {'D','n','s','D','x','e','.','e','f','i',0};
static const CHAR16 tls_name[] = {'T','l','s','D','x','e','.','e','f','i',0};
static const CHAR16 http_utilities_name[] =
    {'H','t','t','p','U','t','i','l','i','t','i','e','s','D','x','e','.','e','f','i',0};
static const CHAR16 http_name[] = {'H','t','t','p','D','x','e','.','e','f','i',0};

/* Dependency order from the pinned NetworkPkg DSC. This intentionally contains
 * no SNP/UNDI hardware driver: an existing SNP controller remains authoritative. */
static const oc_net_stack_module modules[OC_NET_STACK_MODULE_COUNT] = {
    {dpc_name, {0x480f8ae9u,0x0c46u,0x4aa9u,{0xbc,0x89,0xdb,0x9f,0xba,0x61,0x98,0x06}}},
    {mnp_name, {0xf36ff770u,0xa7e1u,0x42cfu,{0x9e,0xd2,0x56,0xf0,0xf2,0x71,0xf4,0x4c}}},
    {arp_name, {0xf44c00eeu,0x1f2cu,0x4a00u,{0xaa,0x09,0x1c,0x9f,0x3e,0x08,0x00,0xa3}}},
    {ip4_name, {0xc51711e7u,0xb4bfu,0x404au,{0xbf,0xb8,0x0a,0x04,0x8e,0xf1,0xff,0xe4}}},
    {udp4_name, {0x83f01464u,0x99bdu,0x45e5u,{0xb3,0x83,0xaf,0x63,0x05,0xd8,0xe9,0xe6}}},
    {dhcp4_name, {0x9d9a39d8u,0xbd42u,0x4a73u,{0xa4,0xd5,0x8e,0xe9,0x4b,0xe1,0x13,0x80}}},
    {hash2_name, {0xda836f8du,0x217fu,0x4ca0u,{0x99,0xc2,0x1c,0xa4,0xe1,0x60,0x77,0xea}}},
    {tcp_name, {0x00720665u,0x67ebu,0x4a99u,{0xba,0xf7,0xd3,0xc3,0x3a,0x1c,0x7c,0xc9}}},
    {dns_name, {0xb625b186u,0xe063u,0x44f7u,{0x89,0x05,0x6a,0x74,0xdc,0x6f,0x52,0xb4}}},
    {tls_name, {0x952cb795u,0xff36u,0x48cfu,{0xa2,0x49,0x4d,0xf4,0x86,0xd6,0xab,0x8d}}},
    {http_utilities_name, {0x3e35c163u,0x4074u,0x45ddu,{0x43,0x1e,0x23,0x98,0x9d,0xd8,0x6b,0x32}}},
    {http_name, {0xbdc8e6afu,0xd9bcu,0x4379u,{0xa7,0x2a,0xe0,0xc4,0xe7,0x5d,0xae,0x1c}}}
};

static void clear_report(oc_net_stack_load_report *report) {
    report->already_present = 0u;
    report->loaded = 0u;
    report->failed_index = OC_NET_STACK_MODULE_COUNT;
}

EFI_STATUS oc_net_stack_load_missing(const oc_net_stack_loader_ops *ops,
                                     oc_net_stack_load_report *report) {
    uint32_t index;
    EFI_STATUS status;
    if (ops == 0 || report == 0 || ops->protocol_present == 0 ||
        ops->load_driver == 0 || ops->connect_loaded == 0)
        return EFI_INVALID_PARAMETER;
    clear_report(report);
    for (index = 0u; index < OC_NET_STACK_MODULE_COUNT; ++index) {
        const oc_net_stack_module *module = &modules[index];
        if (ops->protocol_present(ops->context, &module->produced_protocol)) {
            ++report->already_present;
            continue;
        }
        status = ops->load_driver(ops->context, module->file_name,
                                  &module->produced_protocol);
        if (EFI_ERROR(status)) {
            report->failed_index = index;
            return status;
        }
        ++report->loaded;
    }
    if (report->loaded != 0u) {
        status = ops->connect_loaded(ops->context);
        if (EFI_ERROR(status)) return status;
    }
    for (index = 0u; index < OC_NET_STACK_MODULE_COUNT; ++index) {
        if (!ops->protocol_present(ops->context, &modules[index].produced_protocol)) {
            report->failed_index = index;
            return EFI_NOT_FOUND;
        }
    }
    return EFI_SUCCESS;
}
