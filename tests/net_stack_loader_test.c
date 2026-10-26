#include <assert.h>
#include <stdio.h>
#include "openconsole/net_stack_loader.h"

typedef struct {
    unsigned present_mask;
    unsigned load_count;
    unsigned loaded_mask;
    unsigned fail_index;
} fixture;

static unsigned guid_index(const EFI_GUID *guid) {
    static const uint32_t ids[OC_NET_STACK_MODULE_COUNT] = {
        0x480f8ae9u, 0xf36ff770u, 0xf44c00eeu, 0xc51711e7u, 0x83f01464u,
        0x9d9a39d8u, 0xda836f8du, 0x00720665u, 0xb625b186u, 0x952cb795u,
        0x3e35c163u, 0xbdc8e6afu
    };
    unsigned i;
    for (i = 0u; i < OC_NET_STACK_MODULE_COUNT; ++i)
        if (ids[i] == guid->data1) return i;
    return OC_NET_STACK_MODULE_COUNT;
}

static int protocol_present(void *context, const EFI_GUID *guid) {
    fixture *f = context;
    unsigned index = guid_index(guid);
    return index < OC_NET_STACK_MODULE_COUNT &&
           (f->present_mask & (1u << index)) != 0u;
}

static EFI_STATUS load_driver(void *context, const CHAR16 *name,
                              const EFI_GUID *guid) {
    fixture *f = context;
    unsigned index = guid_index(guid);
    assert(name != NULL);
    assert(index < OC_NET_STACK_MODULE_COUNT);
    if (index == f->fail_index) return EFI_DEVICE_ERROR;
    f->present_mask |= 1u << index;
    f->loaded_mask |= 1u << index;
    ++f->load_count;
    return EFI_SUCCESS;
}

static EFI_STATUS connect_loaded(void *context) {
    (void)context;
    return EFI_SUCCESS;
}

int main(void) {
    oc_net_stack_loader_ops ops;
    oc_net_stack_load_report report;
    fixture f = {(1u << OC_NET_STACK_MODULE_COUNT) - 1u, 0u, 0u, 99u};
    ops.context = &f;
    ops.protocol_present = protocol_present;
    ops.load_driver = load_driver;
    ops.connect_loaded = connect_loaded;
    assert(oc_net_stack_load_missing(&ops, &report) == EFI_SUCCESS);
    assert(report.already_present == OC_NET_STACK_MODULE_COUNT);
    assert(report.loaded == 0u && f.load_count == 0u);

    /* Existing DPC/MNP proves the active SNP path is not rebound; only the
     * missing upper stack is requested, in dependency order. */
    f = (fixture){3u, 0u, 0u, 99u};
    assert(oc_net_stack_load_missing(&ops, &report) == EFI_SUCCESS);
    assert(report.already_present == 2u && report.loaded == 10u);
    assert((f.loaded_mask & 3u) == 0u);

    f = (fixture){1u, 0u, 0u, 4u};
    assert(oc_net_stack_load_missing(&ops, &report) == EFI_DEVICE_ERROR);
    assert(report.failed_index == 4u);
    assert((f.loaded_mask & 1u) == 0u);
    puts("net stack loader tests passed");
    return 0;
}
