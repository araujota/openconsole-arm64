#include "openconsole/net_provider.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    size_t allocations;
    size_t releases;
    size_t cancels;
    size_t backend_releases;
    uint32_t result;
    uint64_t required;
    int link_up;
} fixture;

static void *allocate_bytes(void *context, UINTN size) {
    fixture *state = context;
    void *memory = malloc((size_t)size);
    if (memory != 0) ++state->allocations;
    return memory;
}

static void release_bytes(void *context, void *memory) {
    fixture *state = context;
    if (memory != 0) { ++state->releases; free(memory); }
}

static EFI_STATUS start(void *context, UINTN id, const oc_net_backend_request *request) {
    (void)context; (void)id; (void)request;
    return EFI_SUCCESS;
}

static EFI_STATUS pump(void *context, UINTN id,
                       const oc_net_backend_request *request,
                       oc_net_backend_progress *progress) {
    fixture *state = context;
    (void)id; (void)request;
    progress->result = state->result;
    progress->bytes_required = state->required;
    return EFI_SUCCESS;
}

static void cancel(void *context, UINTN id) {
    fixture *state = context;
    (void)id;
    ++state->cancels;
}

static void release_backend(void *context, UINTN id) {
    fixture *state = context;
    (void)id;
    ++state->backend_releases;
}

static EFI_STATUS query_state(void *context, OC_NET_STATE *out) {
    fixture *state = context;
    out->link_up = (uint32_t)state->link_up;
    return EFI_SUCCESS;
}

static OC_NET_SESSION *new_session(oc_net_provider *provider, uint64_t generation) {
    OC_NET_SESSION_DESC desc = {sizeof(desc), OC_NET_ABI_VERSION, 4096u, 8u, 0u};
    OC_NET_SESSION *session = 0;
    assert(oc_net_provider_begin_generation(provider, generation) == EFI_SUCCESS);
    assert(provider->protocol.create_session(&desc, &session) == EFI_SUCCESS);
    return session;
}

static OC_NET_OPERATION_STATUS operation_status(oc_net_provider *provider,
                                                OC_NET_OPERATION *operation) {
    OC_NET_OPERATION_STATUS status;
    memset(&status, 0, sizeof(status));
    status.structure_size = sizeof(status);
    assert(provider->protocol.query_operation(operation, &status) == EFI_SUCCESS);
    return status;
}

int main(void) {
    fixture state;
    oc_net_platform platform;
    oc_net_provider provider;
    OC_NET_SESSION *session;
    OC_NET_OPERATION *operation;
    OC_NET_DEADLINE deadline = {sizeof(deadline), 0u, 10u};
    OC_NET_DNS_RESULT dns_result;
    OC_NET_STATE link;
    unsigned char guarded[6] = {0xa5u, 0u, 0u, 0u, 0u, 0x5au};
    OC_NET_TRANSFER_DESC transfer;
    memset(&state, 0, sizeof(state));
    memset(&platform, 0, sizeof(platform));
    platform.context = &state;
    platform.allocate = allocate_bytes;
    platform.release = release_bytes;
    platform.backend.context = &state;
    platform.backend.capabilities = OC_NET_CAP_IPV4 | OC_NET_CAP_DHCP4 |
        OC_NET_CAP_DNS4 | OC_NET_CAP_UDP4 | OC_NET_CAP_TCP4 | OC_NET_CAP_HTTPS;
    platform.backend.start = start;
    platform.backend.pump = pump;
    platform.backend.cancel = cancel;
    platform.backend.release = release_backend;
    platform.backend.query_state = query_state;
    assert(oc_net_provider_initialize(&provider, &platform) == EFI_SUCCESS);

    session = new_session(&provider, 1u);
    memset(&link, 0, sizeof(link));
    link.structure_size = sizeof(link);
    assert(provider.protocol.query_state(session, &link) == EFI_SUCCESS);
    assert(link.link_up == 0u);

    assert(provider.protocol.configure_dhcp4(session, &deadline, &operation) == EFI_SUCCESS);
    assert(provider.protocol.pump(session, 10u) == EFI_SUCCESS);
    assert(operation_status(&provider, operation).result == OC_NET_RESULT_DEADLINE_EXCEEDED);
    assert(provider.protocol.release_operation(operation) == EFI_SUCCESS);

    memset(&dns_result, 0, sizeof(dns_result));
    dns_result.structure_size = sizeof(dns_result);
    deadline.deadline_usec = 20u;
    assert(provider.protocol.resolve_dns4(session, "missing.test", 12u, &dns_result,
                                          &deadline, &operation) == EFI_SUCCESS);
    assert(provider.protocol.pump(session, 20u) == EFI_SUCCESS);
    assert(operation_status(&provider, operation).result == OC_NET_RESULT_DEADLINE_EXCEEDED);
    assert(provider.protocol.release_operation(operation) == EFI_SUCCESS);

    memset(&transfer, 0, sizeof(transfer));
    transfer.structure_size = sizeof(transfer);
    transfer.address_be = 0x0100007fu;
    transfer.port = 1u;
    transfer.response_bytes = &guarded[1];
    transfer.response_capacity = 4u;
    transfer.deadline.structure_size = sizeof(transfer.deadline);
    transfer.deadline.deadline_usec = 100u;
    state.result = OC_NET_RESULT_CONNECTION_REFUSED;
    assert(provider.protocol.start_tcp4(session, &transfer, &operation) == EFI_SUCCESS);
    assert(provider.protocol.pump(session, 1u) == EFI_SUCCESS);
    assert(operation_status(&provider, operation).result == OC_NET_RESULT_CONNECTION_REFUSED);
    assert(provider.protocol.release_operation(operation) == EFI_SUCCESS);

    state.result = OC_NET_RESULT_PENDING;
    assert(provider.protocol.start_udp4(session, &transfer, &operation) == EFI_SUCCESS);
    assert(provider.protocol.cancel_operation(operation) == EFI_SUCCESS);
    assert(operation_status(&provider, operation).result == OC_NET_RESULT_CANCELLED);
    assert(provider.protocol.release_operation(operation) == EFI_SUCCESS);

    state.result = OC_NET_RESULT_OK;
    state.required = 5u;
    assert(provider.protocol.start_udp4(session, &transfer, &operation) == EFI_SUCCESS);
    assert(provider.protocol.pump(session, 1u) == EFI_SUCCESS);
    assert(operation_status(&provider, operation).result == OC_NET_RESULT_RESPONSE_TOO_LARGE);
    assert(guarded[0] == 0xa5u && guarded[5] == 0x5au);
    assert(provider.protocol.release_operation(operation) == EFI_SUCCESS);
    state.required = 0u;

    state.result = OC_NET_RESULT_PENDING;
    assert(provider.protocol.start_tcp4(session, &transfer, &operation) == EFI_SUCCESS);
    assert(oc_net_provider_active_resources(&provider, 1u) == 2u);
    oc_net_provider_cleanup_generation(&provider, 1u);
    assert(oc_net_provider_active_resources(&provider, 1u) == 0u);
    assert(state.allocations == state.releases);
    assert(state.cancels >= 4u);
    assert(state.backend_releases >= 6u);
    oc_net_provider_shutdown(&provider);
    puts("PASS: network failure mapping, bounds, cancellation, and pending-child cleanup");
    return 0;
}
