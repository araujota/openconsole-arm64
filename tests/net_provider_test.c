#include "openconsole/net_provider.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    size_t allocations;
    size_t releases;
    size_t cancels;
    size_t pumps;
    int finish;
    int check_copy;
} fixture;

static void *allocate_bytes(void *context, UINTN size) {
    fixture *state = context;
    void *memory = malloc((size_t)size);
    if (memory != 0) ++state->allocations;
    return memory;
}

static void release_bytes(void *context, void *memory) {
    fixture *state = context;
    if (memory != 0) {
        ++state->releases;
        free(memory);
    }
}

static EFI_STATUS backend_start(void *context, UINTN id,
                                const oc_net_backend_request *request) {
    (void)context;
    (void)id;
    assert(request->deadline_usec != 0u);
    return EFI_SUCCESS;
}

static EFI_STATUS backend_pump(void *context, UINTN id,
                               const oc_net_backend_request *request,
                               oc_net_backend_progress *progress) {
    fixture *state = context;
    (void)id;
    ++state->pumps;
    if (state->check_copy) {
        static const unsigned char expected[] = {1u, 2u, 3u, 4u};
        assert(request->request_size == sizeof(expected));
        assert(memcmp(request->request_bytes, expected, sizeof(expected)) == 0);
    }
    if (state->finish) {
        static const char reply[] = "ok";
        assert(request->response_capacity >= sizeof(reply) - 1u);
        memcpy(request->response_bytes, reply, sizeof(reply) - 1u);
        progress->bytes_transferred = sizeof(reply) - 1u;
        progress->bytes_required = sizeof(reply) - 1u;
        progress->result = OC_NET_RESULT_OK;
    } else {
        progress->result = OC_NET_RESULT_PENDING;
    }
    return EFI_SUCCESS;
}

static void backend_cancel(void *context, UINTN id) {
    fixture *state = context;
    (void)id;
    ++state->cancels;
}

static EFI_STATUS backend_state(void *context, OC_NET_STATE *state) {
    (void)context;
    state->link_up = 1u;
    state->configured = 1u;
    return EFI_SUCCESS;
}

static oc_net_platform make_platform(fixture *state) {
    oc_net_platform platform;
    memset(&platform, 0, sizeof(platform));
    platform.context = state;
    platform.allocate = allocate_bytes;
    platform.release = release_bytes;
    platform.backend.context = state;
    platform.backend.capabilities = OC_NET_CAP_IPV4 | OC_NET_CAP_DHCP4 |
                                    OC_NET_CAP_DNS4 | OC_NET_CAP_UDP4 |
                                    OC_NET_CAP_TCP4 | OC_NET_CAP_HTTPS |
                                    OC_NET_CAP_TLS_IDENTITY_VALIDATION;
    platform.backend.start = backend_start;
    platform.backend.pump = backend_pump;
    platform.backend.cancel = backend_cancel;
    platform.backend.query_state = backend_state;
    return platform;
}

static OC_NET_SESSION *create_session(oc_net_provider *provider, uint64_t generation,
                                      uint64_t bytes, uint32_t operations) {
    OC_NET_SESSION_DESC desc = {sizeof(desc), OC_NET_ABI_VERSION, bytes, operations, 0u};
    OC_NET_SESSION *session = 0;
    assert(oc_net_provider_begin_generation(provider, generation) == EFI_SUCCESS);
    assert(provider->protocol.create_session(&desc, &session) == EFI_SUCCESS);
    return session;
}

static OC_NET_OPERATION *start_udp(oc_net_provider *provider, OC_NET_SESSION *session,
                                   unsigned char *source, UINTN source_size,
                                   char *destination, UINTN capacity, uint64_t deadline) {
    OC_NET_TRANSFER_DESC desc;
    OC_NET_OPERATION *operation = 0;
    memset(&desc, 0, sizeof(desc));
    desc.structure_size = sizeof(desc);
    desc.address_be = 0x0100007fu;
    desc.port = 9000u;
    desc.request_bytes = source;
    desc.request_size = source_size;
    desc.response_bytes = destination;
    desc.response_capacity = capacity;
    desc.deadline.structure_size = sizeof(desc.deadline);
    desc.deadline.deadline_usec = deadline;
    assert(provider->protocol.start_udp4(session, &desc, &operation) == EFI_SUCCESS);
    return operation;
}

int main(void) {
    fixture state;
    oc_net_platform platform;
    oc_net_provider provider;
    OC_NET_SESSION *session;
    OC_NET_OPERATION *operation;
    OC_NET_OPERATION_STATUS status;
    unsigned char source[] = {1u, 2u, 3u, 4u};
    char destination[16] = {0};
    unsigned cycle;
    memset(&state, 0, sizeof(state));
    platform = make_platform(&state);
    assert(oc_net_provider_initialize(&provider, &platform) == EFI_SUCCESS);

    session = create_session(&provider, 1u, 128u, 2u);
    state.check_copy = 1;
    operation = start_udp(&provider, session, source, sizeof(source),
                          destination, sizeof(destination), 100u);
    memset(source, 0xff, sizeof(source));
    state.finish = 1;
    assert(provider.protocol.pump(session, 10u) == EFI_SUCCESS);
    memset(&status, 0, sizeof(status));
    status.structure_size = sizeof(status);
    assert(provider.protocol.query_operation(operation, &status) == EFI_SUCCESS);
    assert(status.result == OC_NET_RESULT_OK);
    assert(status.bytes_transferred == 2u);
    assert(memcmp(destination, "ok", 2u) == 0);
    assert(provider.protocol.release_operation(operation) == EFI_SUCCESS);
    assert(provider.protocol.destroy_session(session) == EFI_SUCCESS);
    assert(state.allocations == state.releases);

    state.finish = 0;
    state.check_copy = 0;
    session = create_session(&provider, 2u, 128u, 2u);
    source[0] = 1u;
    operation = start_udp(&provider, session, source, 1u, destination, 1u, 20u);
    assert(provider.protocol.pump(session, 20u) == EFI_SUCCESS);
    status.structure_size = sizeof(status);
    assert(provider.protocol.query_operation(operation, &status) == EFI_SUCCESS);
    assert(status.result == OC_NET_RESULT_DEADLINE_EXCEEDED);
    assert(state.cancels == 1u);
    assert(provider.protocol.release_operation(operation) == EFI_SUCCESS);

    operation = start_udp(&provider, session, source, 1u, destination, 1u, 100u);
    assert(provider.protocol.cancel_operation(operation) == EFI_SUCCESS);
    status.structure_size = sizeof(status);
    assert(provider.protocol.query_operation(operation, &status) == EFI_SUCCESS);
    assert(status.result == OC_NET_RESULT_CANCELLED);
    assert(provider.protocol.release_operation(operation) == EFI_SUCCESS);
    assert(provider.protocol.destroy_session(session) == EFI_SUCCESS);

    session = create_session(&provider, 3u, 4u, 1u);
    {
        OC_NET_TRANSFER_DESC desc;
        OC_NET_OPERATION *rejected = 0;
        memset(&desc, 0, sizeof(desc));
        desc.structure_size = sizeof(desc);
        desc.request_bytes = source;
        desc.request_size = 4u;
        desc.response_bytes = destination;
        desc.response_capacity = 1u;
        desc.deadline.structure_size = sizeof(desc.deadline);
        desc.deadline.deadline_usec = 100u;
        assert(provider.protocol.start_udp4(session, &desc, &rejected) == EFI_OUT_OF_RESOURCES);
    }
    assert(provider.protocol.destroy_session(session) == EFI_SUCCESS);

    for (cycle = 0u; cycle < 100u; ++cycle) {
        uint64_t generation = 100u + cycle;
        session = create_session(&provider, generation, 64u, 2u);
        operation = start_udp(&provider, session, source, 1u, destination, 1u, 1000u);
        (void)operation;
        assert(oc_net_provider_active_resources(&provider, generation) == 2u);
        oc_net_provider_cleanup_generation(&provider, generation);
        assert(oc_net_provider_active_resources(&provider, generation) == 0u);
    }
    assert(state.allocations == state.releases);
    oc_net_provider_shutdown(&provider);
    assert(state.allocations == state.releases);
    puts("PASS: bounded cooperative network provider and 100-generation teardown");
    return 0;
}
