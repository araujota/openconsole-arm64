#include "openconsole/ethernet_driver_selector.h"

static void zero_bytes(void *value, UINTN size) {
    uint8_t *bytes = value;
    while (size-- != 0u) *bytes++ = 0u;
}

static int text_less(const char *left, const char *right) {
    UINTN index = 0u;
    if (left == 0) return right != 0;
    if (right == 0) return 0;
    while (left[index] != '\0' && right[index] != '\0' &&
           left[index] == right[index]) ++index;
    return (uint8_t)left[index] < (uint8_t)right[index];
}

static int exact_match(const oc_eth_controller *controller,
                       const oc_eth_candidate *candidate,
                       oc_eth_architecture architecture) {
    return candidate->manifest_id != 0 && candidate->artifact != 0 &&
        candidate->artifact_size != 0u && candidate->architecture == architecture &&
        candidate->vendor == controller->vendor &&
        candidate->device == controller->device &&
        candidate->subsystem_vendor == controller->subsystem_vendor &&
        candidate->subsystem_device == controller->subsystem_device &&
        controller->revision >= candidate->revision_min &&
        controller->revision <= candidate->revision_max &&
        candidate->expected_protocols != 0u;
}

static int comes_before(const oc_eth_candidate *left,
                        const oc_eth_candidate *right) {
    if (left->priority != right->priority)
        return left->priority < right->priority;
    return text_less(left->manifest_id, right->manifest_id);
}

static void emit(const oc_eth_selector_ops *ops, oc_eth_diagnostic diagnostic,
                 UINTN controller, UINTN candidate, EFI_STATUS status) {
    if (ops->emit != 0)
        ops->emit(ops->context, diagnostic, controller, candidate, status);
}

static void reject_loaded(const oc_eth_selector_ops *ops, EFI_HANDLE controller,
                          EFI_HANDLE image, int connected, UINTN controller_index,
                          UINTN candidate_index, oc_eth_selection_report *report) {
    EFI_STATUS status;
    if (connected) {
        status = ops->disconnect(ops->context, controller, image);
        if (EFI_ERROR(status)) {
            ++report->cleanup_failures;
            emit(ops, OC_ETH_DIAG_CLEANUP_FAILED, controller_index,
                 candidate_index, status);
        }
    }
    status = ops->unload(ops->context, image);
    if (EFI_ERROR(status)) {
        ++report->cleanup_failures;
        emit(ops, OC_ETH_DIAG_CLEANUP_FAILED, controller_index,
             candidate_index, status);
    }
}

EFI_STATUS oc_eth_select_drivers(const oc_eth_controller *controllers,
                                 UINTN controller_count,
                                 const oc_eth_candidate *candidates,
                                 UINTN candidate_count,
                                 oc_eth_architecture architecture,
                                 const oc_eth_selector_ops *ops,
                                 oc_eth_selection_report *report) {
    UINTN controller_index;
    if (controllers == 0 || candidates == 0 || ops == 0 || report == 0 ||
        controller_count > OC_ETH_MAX_CONTROLLERS ||
        candidate_count > OC_ETH_MAX_CANDIDATES ||
        (architecture != OC_ETH_ARCH_X64 && architecture != OC_ETH_ARCH_ARM64) ||
        ops->verify == 0 || ops->load == 0 || ops->connect == 0 ||
        ops->probe == 0 || ops->disconnect == 0 || ops->unload == 0)
        return EFI_INVALID_PARAMETER;

    zero_bytes(report, sizeof(*report));
    report->controller_count = controller_count;
    for (controller_index = 0u; controller_index < controller_count;
         ++controller_index) {
        const oc_eth_controller *controller = &controllers[controller_index];
        uint8_t attempted[OC_ETH_MAX_CANDIDATES];
        UINTN matches = 0u;
        UINTN pass;
        oc_eth_selection *selection = &report->selections[controller_index];
        selection->controller_index = controller_index;
        selection->candidate_index = candidate_count;
        if (controller->firmware_snp_present && controller->firmware_snp_usable) {
            selection->owner = OC_ETH_OWNER_FIRMWARE;
            ++report->firmware_selected;
            emit(ops, OC_ETH_DIAG_FIRMWARE_SELECTED, controller_index,
                 candidate_count, EFI_SUCCESS);
            continue;
        }
        zero_bytes(attempted, sizeof(attempted));
        for (pass = 0u; pass < candidate_count; ++pass) {
            UINTN index;
            UINTN best = candidate_count;
            EFI_HANDLE image = 0;
            EFI_STATUS status;
            for (index = 0u; index < candidate_count; ++index) {
                if (attempted[index] ||
                    !exact_match(controller, &candidates[index], architecture))
                    continue;
                if (best == candidate_count ||
                    comes_before(&candidates[index], &candidates[best])) best = index;
            }
            if (best == candidate_count) break;
            attempted[best] = 1u;
            ++matches;
            ++report->attempted;
            status = ops->verify(ops->context, &candidates[best]);
            if (EFI_ERROR(status)) {
                ++report->rejected;
                emit(ops, OC_ETH_DIAG_VERIFY_REJECTED, controller_index, best, status);
                continue;
            }
            status = ops->load(ops->context, &candidates[best], &image);
            if (EFI_ERROR(status) || image == 0) {
                ++report->rejected;
                emit(ops, OC_ETH_DIAG_LOAD_REJECTED, controller_index, best,
                     EFI_ERROR(status) ? status : EFI_NOT_FOUND);
                continue;
            }
            status = ops->connect(ops->context, controller->handle, image);
            if (EFI_ERROR(status)) {
                ++report->rejected;
                emit(ops, OC_ETH_DIAG_CONNECT_REJECTED, controller_index, best,
                     status);
                reject_loaded(ops, controller->handle, image, 0, controller_index,
                              best, report);
                continue;
            }
            status = ops->probe(ops->context, controller->handle,
                                candidates[best].expected_protocols);
            if (EFI_ERROR(status)) {
                ++report->rejected;
                emit(ops, OC_ETH_DIAG_PROTOCOL_REJECTED, controller_index, best,
                     status);
                reject_loaded(ops, controller->handle, image, 1, controller_index,
                              best, report);
                continue;
            }
            selection->owner = OC_ETH_OWNER_SUPPLEMENTAL;
            selection->candidate_index = best;
            selection->image = image;
            ++report->supplemental_selected;
            emit(ops, OC_ETH_DIAG_SUPPLEMENTAL_SELECTED, controller_index, best,
                 EFI_SUCCESS);
            break;
        }
        if (selection->owner == OC_ETH_OWNER_NONE) {
            ++report->unserved;
            if (matches == 0u)
                emit(ops, OC_ETH_DIAG_NO_EXACT_CANDIDATE, controller_index,
                     candidate_count, EFI_NOT_FOUND);
        }
    }
    /* Candidate rejection is deliberately not a fatal launcher result. */
    return EFI_SUCCESS;
}
