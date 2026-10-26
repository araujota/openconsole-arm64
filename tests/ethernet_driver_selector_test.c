#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "openconsole/ethernet_driver_selector.h"

typedef struct {
    const char *verify_fail;
    const char *load_fail;
    const char *connect_fail;
    const char *probe_fail;
    unsigned disconnect_fail;
    unsigned unload_fail;
    unsigned verify_calls;
    unsigned load_calls;
    unsigned connect_calls;
    unsigned probe_calls;
    unsigned disconnect_calls;
    unsigned unload_calls;
    unsigned diagnostics[9];
    const char *order[8];
    unsigned order_count;
} fixture;

static EFI_STATUS verify(void *context, const oc_eth_candidate *candidate) {
    fixture *f = context;
    ++f->verify_calls;
    f->order[f->order_count++] = candidate->manifest_id;
    return f->verify_fail != 0 && strcmp(f->verify_fail, candidate->manifest_id) == 0
        ? EFI_SECURITY_VIOLATION : EFI_SUCCESS;
}

static EFI_STATUS load(void *context, const oc_eth_candidate *candidate,
                       EFI_HANDLE *image) {
    fixture *f = context;
    ++f->load_calls;
    if (f->load_fail != 0 && strcmp(f->load_fail, candidate->manifest_id) == 0)
        return EFI_DEVICE_ERROR;
    *image = (EFI_HANDLE)candidate;
    return EFI_SUCCESS;
}

static EFI_STATUS connect(void *context, EFI_HANDLE controller, EFI_HANDLE image) {
    fixture *f = context;
    const oc_eth_candidate *candidate = image;
    assert(controller != 0);
    ++f->connect_calls;
    return f->connect_fail != 0 &&
        strcmp(f->connect_fail, candidate->manifest_id) == 0
        ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

static EFI_STATUS probe(void *context, EFI_HANDLE controller, uint32_t protocols) {
    fixture *f = context;
    (void)controller;
    assert(protocols != 0u);
    ++f->probe_calls;
    /* The fixture associates the first successfully connected image with this
     * flag; only the named beta candidate reaches this failure in the test. */
    if (f->probe_fail != 0 && f->order_count != 0u &&
        strcmp(f->probe_fail, f->order[f->order_count - 1u]) == 0)
        return EFI_PROTOCOL_ERROR;
    return EFI_SUCCESS;
}

static EFI_STATUS disconnect(void *context, EFI_HANDLE controller, EFI_HANDLE image) {
    fixture *f = context;
    assert(controller != 0 && image != 0);
    ++f->disconnect_calls;
    return f->disconnect_fail ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

static EFI_STATUS unload(void *context, EFI_HANDLE image) {
    fixture *f = context;
    assert(image != 0);
    ++f->unload_calls;
    return f->unload_fail ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

static void diagnostic(void *context, oc_eth_diagnostic code, UINTN controller,
                       UINTN candidate, EFI_STATUS status) {
    fixture *f = context;
    (void)controller; (void)candidate; (void)status;
    assert(code > 0 && code < 9);
    ++f->diagnostics[code];
}

static oc_eth_selector_ops operations(fixture *f) {
    oc_eth_selector_ops ops = {f, verify, load, connect, probe,
                               disconnect, unload, diagnostic};
    return ops;
}

static oc_eth_candidate candidate(const char *id, uint32_t priority) {
    oc_eth_candidate item;
    memset(&item, 0, sizeof(item));
    item.manifest_id = id;
    item.artifact = id;
    item.artifact_size = 100u;
    item.architecture = OC_ETH_ARCH_X64;
    item.vendor = 0x10ecu;
    item.device = 0x8125u;
    item.subsystem_vendor = 0x1462u;
    item.subsystem_device = 0x7e12u;
    item.revision_min = 5u;
    item.revision_max = 5u;
    item.expected_protocols = 1u;
    item.priority = priority;
    return item;
}

int main(void) {
    oc_eth_controller controllers[2] = {
        {(EFI_HANDLE)1, 0x10ec, 0x8125, 0x1462, 0x7e12, 5, 1, 1},
        {(EFI_HANDLE)2, 0x10ec, 0x8125, 0x1462, 0x7e12, 5, 0, 0}
    };
    oc_eth_candidate candidates[7];
    oc_eth_selection_report report;
    fixture f;
    oc_eth_selector_ops ops;

    candidates[0] = candidate("zeta", 20u);
    candidates[1] = candidate("beta", 10u);
    candidates[2] = candidate("alpha", 10u);
    candidates[3] = candidate("wrong-subsystem", 1u);
    candidates[3].subsystem_device = 0xffffu;
    candidates[4] = candidate("wrong-arch", 1u);
    candidates[4].architecture = OC_ETH_ARCH_ARM64;
    candidates[5] = candidate("wrong-revision", 1u);
    candidates[5].revision_min = 6u;
    candidates[5].revision_max = 6u;
    candidates[6] = candidate("missing-protocol", 1u);
    candidates[6].expected_protocols = 0u;
    memset(&f, 0, sizeof(f));
    f.verify_fail = "alpha";
    f.probe_fail = "beta";
    ops = operations(&f);
    assert(oc_eth_select_drivers(controllers, 2u, candidates, 7u,
                                 OC_ETH_ARCH_X64, &ops, &report) == EFI_SUCCESS);
    assert(report.controller_count == 2u);
    assert(report.firmware_selected == 1u);
    assert(report.supplemental_selected == 1u);
    assert(report.unserved == 0u && report.attempted == 3u && report.rejected == 2u);
    assert(report.selections[0].owner == OC_ETH_OWNER_FIRMWARE);
    assert(report.selections[1].owner == OC_ETH_OWNER_SUPPLEMENTAL);
    assert(report.selections[1].candidate_index == 0u);
    assert(f.verify_calls == 3u && f.load_calls == 2u && f.connect_calls == 2u);
    assert(f.probe_calls == 2u && f.disconnect_calls == 1u && f.unload_calls == 1u);
    assert(strcmp(f.order[0], "alpha") == 0);
    assert(strcmp(f.order[1], "beta") == 0);
    assert(strcmp(f.order[2], "zeta") == 0);
    assert(f.diagnostics[OC_ETH_DIAG_FIRMWARE_SELECTED] == 1u);
    assert(f.diagnostics[OC_ETH_DIAG_VERIFY_REJECTED] == 1u);
    assert(f.diagnostics[OC_ETH_DIAG_PROTOCOL_REJECTED] == 1u);
    assert(f.diagnostics[OC_ETH_DIAG_SUPPLEMENTAL_SELECTED] == 1u);

    /* No exact match is nonfatal and preserves offline launcher operation. */
    memset(&f, 0, sizeof(f));
    ops = operations(&f);
    controllers[0].firmware_snp_usable = 0u;
    controllers[0].subsystem_device = 0x0001u;
    assert(oc_eth_select_drivers(controllers, 1u, candidates, 7u,
                                 OC_ETH_ARCH_X64, &ops, &report) == EFI_SUCCESS);
    assert(report.unserved == 1u && report.attempted == 0u);
    assert(f.diagnostics[OC_ETH_DIAG_NO_EXACT_CANDIDATE] == 1u);

    /* A connect failure unloads without disconnecting an unbound image. */
    memset(&f, 0, sizeof(f));
    f.connect_fail = "zeta";
    ops = operations(&f);
    controllers[0].subsystem_device = 0x7e12u;
    assert(oc_eth_select_drivers(controllers, 1u, candidates, 1u,
                                 OC_ETH_ARCH_X64, &ops, &report) == EFI_SUCCESS);
    assert(report.unserved == 1u && report.rejected == 1u);
    assert(f.unload_calls == 1u && f.disconnect_calls == 0u);

    /* Load rejection is reported without attempting to connect or clean an
     * image that the loader did not return. */
    memset(&f, 0, sizeof(f));
    f.load_fail = "zeta";
    ops = operations(&f);
    assert(oc_eth_select_drivers(controllers, 1u, candidates, 1u,
                                 OC_ETH_ARCH_X64, &ops, &report) == EFI_SUCCESS);
    assert(report.unserved == 1u && report.rejected == 1u);
    assert(f.connect_calls == 0u && f.disconnect_calls == 0u &&
           f.unload_calls == 0u);
    assert(f.diagnostics[OC_ETH_DIAG_LOAD_REJECTED] == 1u);

    /* Teardown errors are observable but do not prevent the next candidate
     * from being selected or the offline launcher from continuing. */
    memset(&f, 0, sizeof(f));
    f.probe_fail = "alpha";
    f.disconnect_fail = 1u;
    f.unload_fail = 1u;
    ops = operations(&f);
    assert(oc_eth_select_drivers(controllers, 1u, candidates, 3u,
                                 OC_ETH_ARCH_X64, &ops, &report) == EFI_SUCCESS);
    assert(report.supplemental_selected == 1u && report.unserved == 0u);
    assert(report.rejected == 1u && report.cleanup_failures == 2u);
    assert(f.diagnostics[OC_ETH_DIAG_CLEANUP_FAILED] == 2u);

    puts("PASS: firmware ownership, deterministic Ethernet selection, and rollback");
    return 0;
}
