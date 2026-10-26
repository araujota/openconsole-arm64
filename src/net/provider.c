#include "openconsole/net_provider.h"

static oc_net_provider *active_provider;

static void zero_bytes(void *destination, UINTN size) {
    uint8_t *bytes = destination;
    while (size-- != 0u) *bytes++ = 0u;
}

static void copy_bytes(void *destination, const void *source, UINTN size) {
    uint8_t *out = destination;
    const uint8_t *in = source;
    while (size-- != 0u) *out++ = *in++;
}

static int add_overflows(UINTN left, UINTN right) {
    return right > (~(UINTN)0) - left;
}

static oc_net_session_slot *valid_session(OC_NET_SESSION *session) {
    oc_net_session_slot *slot;
    if (active_provider == 0 || session == 0 || session->slot >= OC_NET_MAX_SESSIONS)
        return 0;
    slot = &active_provider->sessions[session->slot];
    if (!slot->active || slot->generation != session->generation) return 0;
    return slot;
}

static oc_net_operation_slot *valid_operation(OC_NET_OPERATION *operation) {
    oc_net_operation_slot *slot;
    if (active_provider == 0 || operation == 0 ||
        operation->slot >= OC_NET_MAX_SESSIONS * OC_NET_MAX_OPERATIONS_PER_SESSION)
        return 0;
    slot = &active_provider->operations[operation->slot];
    if (!slot->active || slot->generation != operation->generation) return 0;
    return slot;
}

static oc_net_backend_request backend_request(const oc_net_operation_slot *operation) {
    oc_net_backend_request request;
    zero_bytes(&request, sizeof(request));
    request.kind = operation->kind;
    request.address_be = operation->address_be;
    request.port = operation->port;
    request.deadline_usec = operation->deadline_usec;
    request.response_bytes = operation->response_bytes;
    request.response_capacity = operation->response_capacity;
    if (operation->kind == OC_NET_BACKEND_DNS4) {
        request.name = operation->owned_bytes;
        request.name_size = operation->name_size;
    } else if (operation->kind == OC_NET_BACKEND_HTTPS) {
        request.name = operation->owned_bytes;
        request.name_size = operation->name_size;
        request.request_bytes = (const uint8_t *)operation->owned_bytes + operation->name_size;
        request.request_size = operation->owned_size - operation->name_size;
    } else {
        request.request_bytes = operation->owned_bytes;
        request.request_size = operation->owned_size;
    }
    return request;
}

static void release_operation_slot(oc_net_operation_slot *operation) {
    oc_net_session_slot *session = 0;
    if (!operation->active) return;
    if (operation->session_slot < OC_NET_MAX_SESSIONS)
        session = &active_provider->sessions[operation->session_slot];
    if (operation->pending && operation->backend_started && active_provider->platform.backend.cancel)
        active_provider->platform.backend.cancel(active_provider->platform.backend.context,
                                                   (UINTN)(operation - active_provider->operations));
    if (operation->backend_started && active_provider->platform.backend.release)
        active_provider->platform.backend.release(active_provider->platform.backend.context,
                                                   (UINTN)(operation - active_provider->operations));
    if (operation->owned_bytes != 0)
        active_provider->platform.release(active_provider->platform.context, operation->owned_bytes);
    if (session != 0 && session->active) {
        if (session->bytes_reserved >= operation->reservation)
            session->bytes_reserved -= operation->reservation;
        else
            session->bytes_reserved = 0u;
        if (session->operation_count != 0u) --session->operation_count;
    }
    zero_bytes(operation, sizeof(*operation));
}

static EFI_STATUS EFIAPI query_capabilities(OC_NET_CAPABILITIES *out) {
    if (out == 0 || out->structure_size < sizeof(*out)) return EFI_INVALID_PARAMETER;
    zero_bytes(out, sizeof(*out));
    out->structure_size = sizeof(*out);
    out->abi_version = OC_NET_ABI_VERSION;
    out->capabilities = active_provider->platform.backend.capabilities;
    out->max_sessions = OC_NET_MAX_SESSIONS;
    out->max_operations_per_session = OC_NET_MAX_OPERATIONS_PER_SESSION;
    out->max_dns_addresses = OC_NET_MAX_DNS_ADDRESSES;
    out->max_response_bytes = OC_NET_MAX_RESPONSE_BYTES;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI create_session(const OC_NET_SESSION_DESC *desc,
                                        OC_NET_SESSION **out) {
    UINTN i;
    uint32_t quota;
    if (active_provider == 0 || desc == 0 || out == 0 ||
        desc->structure_size < sizeof(*desc)) return EFI_INVALID_PARAMETER;
    if (!oc_net_abi_compatible(desc->requested_abi)) return EFI_UNSUPPORTED;
    quota = desc->operation_quota == 0u ? OC_NET_MAX_OPERATIONS_PER_SESSION : desc->operation_quota;
    if (quota > OC_NET_MAX_OPERATIONS_PER_SESSION || desc->byte_quota > OC_NET_MAX_RESPONSE_BYTES)
        return EFI_INVALID_PARAMETER;
    for (i = 0u; i < OC_NET_MAX_SESSIONS; ++i) {
        oc_net_session_slot *slot = &active_provider->sessions[i];
        if (!slot->active) {
            zero_bytes(slot, sizeof(*slot));
            slot->active = 1u;
            slot->generation = active_provider->generation;
            slot->byte_quota = desc->byte_quota == 0u ? OC_NET_MAX_RESPONSE_BYTES : desc->byte_quota;
            slot->operation_quota = quota;
            active_provider->session_handles[i].slot = i;
            active_provider->session_handles[i].generation = slot->generation;
            *out = &active_provider->session_handles[i];
            return EFI_SUCCESS;
        }
    }
    return EFI_OUT_OF_RESOURCES;
}

static EFI_STATUS EFIAPI destroy_session(OC_NET_SESSION *session_handle) {
    oc_net_session_slot *session = valid_session(session_handle);
    UINTN i;
    if (session == 0) return EFI_INVALID_PARAMETER;
    for (i = 0u; i < sizeof(active_provider->operations) / sizeof(active_provider->operations[0]); ++i)
        if (active_provider->operations[i].active &&
            active_provider->operations[i].session_slot == session_handle->slot)
            release_operation_slot(&active_provider->operations[i]);
    zero_bytes(session, sizeof(*session));
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI query_state(OC_NET_SESSION *session, OC_NET_STATE *out) {
    oc_net_session_slot *slot = valid_session(session);
    EFI_STATUS status;
    if (slot == 0 || out == 0 || out->structure_size < sizeof(*out)) return EFI_INVALID_PARAMETER;
    if (active_provider->platform.backend.query_state == 0) return EFI_UNSUPPORTED;
    status = active_provider->platform.backend.query_state(active_provider->platform.backend.context, out);
    if (!EFI_ERROR(status)) out->owner_generation = slot->generation;
    return status;
}

static EFI_STATUS start_operation(OC_NET_SESSION *session_handle,
                                  oc_net_backend_kind kind,
                                  const void *first, UINTN first_size,
                                  const void *second, UINTN second_size,
                                  UINTN name_size, uint32_t address_be, uint16_t port,
                                  void *response, UINTN response_capacity,
                                  uint64_t deadline_usec, OC_NET_OPERATION **out) {
    oc_net_session_slot *session = valid_session(session_handle);
    oc_net_operation_slot *operation = 0;
    UINTN owned_size;
    UINTN reservation;
    UINTN i;
    EFI_STATUS status;
    oc_net_backend_request request;
    if (session == 0 || out == 0 || deadline_usec == 0u ||
        add_overflows(first_size, second_size)) return EFI_INVALID_PARAMETER;
    owned_size = first_size + second_size;
    if (add_overflows(owned_size, response_capacity)) return EFI_INVALID_PARAMETER;
    reservation = owned_size + response_capacity;
    if (response_capacity > OC_NET_MAX_RESPONSE_BYTES ||
        session->operation_count >= session->operation_quota ||
        reservation > session->byte_quota - session->bytes_reserved) return EFI_OUT_OF_RESOURCES;
    for (i = 0u; i < sizeof(active_provider->operations) / sizeof(active_provider->operations[0]); ++i)
        if (!active_provider->operations[i].active) { operation = &active_provider->operations[i]; break; }
    if (operation == 0) return EFI_OUT_OF_RESOURCES;
    zero_bytes(operation, sizeof(*operation));
    if (owned_size != 0u) {
        operation->owned_bytes = active_provider->platform.allocate(active_provider->platform.context, owned_size);
        if (operation->owned_bytes == 0) return EFI_OUT_OF_RESOURCES;
        if (first_size != 0u) copy_bytes(operation->owned_bytes, first, first_size);
        if (second_size != 0u) copy_bytes((uint8_t *)operation->owned_bytes + first_size, second, second_size);
    }
    operation->active = 1u;
    operation->pending = 1u;
    operation->result = OC_NET_RESULT_PENDING;
    operation->session_slot = (uint32_t)session_handle->slot;
    operation->generation = session->generation;
    operation->deadline_usec = deadline_usec;
    operation->reservation = reservation;
    operation->kind = kind;
    operation->address_be = address_be;
    operation->port = port;
    operation->owned_size = owned_size;
    operation->name_size = name_size;
    operation->response_bytes = response;
    operation->response_capacity = response_capacity;
    session->bytes_reserved += reservation;
    ++session->operation_count;
    i = (UINTN)(operation - active_provider->operations);
    active_provider->operation_handles[i].slot = i;
    active_provider->operation_handles[i].generation = operation->generation;
    request = backend_request(operation);
    status = active_provider->platform.backend.start(
        active_provider->platform.backend.context, i, &request);
    if (EFI_ERROR(status)) {
        release_operation_slot(operation);
        return status;
    }
    operation->backend_started = 1u;
    *out = &active_provider->operation_handles[i];
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI configure_dhcp4(OC_NET_SESSION *session,
                                         const OC_NET_DEADLINE *deadline,
                                         OC_NET_OPERATION **out) {
    if (deadline == 0 || deadline->structure_size < sizeof(*deadline)) return EFI_INVALID_PARAMETER;
    return start_operation(session, OC_NET_BACKEND_DHCP4, 0, 0u, 0, 0u, 0u,
                           0u, 0u, 0, 0u, deadline->deadline_usec, out);
}

static EFI_STATUS EFIAPI resolve_dns4(OC_NET_SESSION *session, const char *name,
                                      UINTN name_size, OC_NET_DNS_RESULT *result,
                                      const OC_NET_DEADLINE *deadline,
                                      OC_NET_OPERATION **out) {
    if (name == 0 || name_size == 0u || name_size > OC_NET_MAX_HOST_BYTES ||
        result == 0 || result->structure_size < sizeof(*result) || deadline == 0 ||
        deadline->structure_size < sizeof(*deadline)) return EFI_INVALID_PARAMETER;
    return start_operation(session, OC_NET_BACKEND_DNS4, name, name_size, 0, 0u,
                           name_size, 0u, 0u, result, sizeof(*result),
                           deadline->deadline_usec, out);
}

static EFI_STATUS start_transfer(OC_NET_SESSION *session,
                                 const OC_NET_TRANSFER_DESC *desc,
                                 oc_net_backend_kind kind,
                                 OC_NET_OPERATION **out) {
    if (desc == 0 || desc->structure_size < sizeof(*desc) ||
        (desc->request_size != 0u && desc->request_bytes == 0) ||
        (desc->response_capacity != 0u && desc->response_bytes == 0) ||
        desc->request_size > ~(UINTN)0 || desc->response_capacity > ~(UINTN)0 ||
        desc->deadline.structure_size < sizeof(desc->deadline)) return EFI_INVALID_PARAMETER;
    return start_operation(session, kind, desc->request_bytes, (UINTN)desc->request_size,
                           0, 0u, 0u, desc->address_be, desc->port,
                           desc->response_bytes, (UINTN)desc->response_capacity,
                           desc->deadline.deadline_usec, out);
}

static EFI_STATUS EFIAPI start_udp4(OC_NET_SESSION *session,
                                    const OC_NET_TRANSFER_DESC *desc,
                                    OC_NET_OPERATION **out) {
    return start_transfer(session, desc, OC_NET_BACKEND_UDP4, out);
}
static EFI_STATUS EFIAPI start_tcp4(OC_NET_SESSION *session,
                                    const OC_NET_TRANSFER_DESC *desc,
                                    OC_NET_OPERATION **out) {
    return start_transfer(session, desc, OC_NET_BACKEND_TCP4, out);
}

static EFI_STATUS EFIAPI start_https(OC_NET_SESSION *session,
                                     const OC_NET_HTTPS_DESC *desc,
                                     OC_NET_OPERATION **out) {
    if (desc == 0 || desc->structure_size < sizeof(*desc) || desc->url == 0 ||
        desc->url_size == 0u || desc->url_size > OC_NET_MAX_URL_BYTES ||
        (desc->request_body_size != 0u && desc->request_body == 0) ||
        (desc->response_capacity != 0u && desc->response_body == 0) ||
        desc->request_body_size > ~(UINTN)0 || desc->response_capacity > ~(UINTN)0 ||
        desc->deadline.structure_size < sizeof(desc->deadline)) return EFI_INVALID_PARAMETER;
    return start_operation(session, OC_NET_BACKEND_HTTPS, desc->url, desc->url_size,
                           desc->request_body, (UINTN)desc->request_body_size,
                           desc->url_size, 0u, 0u, desc->response_body,
                           (UINTN)desc->response_capacity,
                           desc->deadline.deadline_usec, out);
}

static EFI_STATUS EFIAPI pump(OC_NET_SESSION *session_handle, uint64_t now_usec) {
    oc_net_session_slot *session = valid_session(session_handle);
    UINTN i;
    if (session == 0) return EFI_INVALID_PARAMETER;
    for (i = 0u; i < sizeof(active_provider->operations) / sizeof(active_provider->operations[0]); ++i) {
        oc_net_operation_slot *operation = &active_provider->operations[i];
        oc_net_backend_progress progress;
        oc_net_backend_request request;
        EFI_STATUS status;
        if (!operation->active || !operation->pending ||
            operation->session_slot != session_handle->slot) continue;
        if (now_usec >= operation->deadline_usec) {
            if (operation->backend_started && active_provider->platform.backend.cancel)
                active_provider->platform.backend.cancel(active_provider->platform.backend.context, i);
            operation->pending = 0u;
            operation->result = OC_NET_RESULT_DEADLINE_EXCEEDED;
            continue;
        }
        zero_bytes(&progress, sizeof(progress));
        progress.result = OC_NET_RESULT_PENDING;
        request = backend_request(operation);
        status = active_provider->platform.backend.pump(
            active_provider->platform.backend.context, i, &request, &progress);
        if (EFI_ERROR(status)) {
            operation->pending = 0u;
            operation->result = OC_NET_RESULT_BACKEND_FAILURE;
            operation->protocol_status = (uint32_t)status;
            continue;
        }
        operation->bytes_required = progress.bytes_required;
        operation->protocol_status = progress.protocol_status;
        if (progress.bytes_transferred > operation->response_capacity ||
            progress.bytes_required > operation->response_capacity) {
            if (active_provider->platform.backend.cancel)
                active_provider->platform.backend.cancel(active_provider->platform.backend.context, i);
            operation->pending = 0u;
            operation->result = OC_NET_RESULT_RESPONSE_TOO_LARGE;
            operation->bytes_transferred = 0u;
        } else {
            operation->bytes_transferred = progress.bytes_transferred;
            operation->result = progress.result;
            if (progress.result != OC_NET_RESULT_PENDING) operation->pending = 0u;
        }
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI query_operation(OC_NET_OPERATION *handle,
                                         OC_NET_OPERATION_STATUS *out) {
    oc_net_operation_slot *operation = valid_operation(handle);
    if (operation == 0 || out == 0 || out->structure_size < sizeof(*out))
        return EFI_INVALID_PARAMETER;
    zero_bytes(out, sizeof(*out));
    out->structure_size = sizeof(*out);
    out->result = operation->result;
    out->bytes_transferred = operation->bytes_transferred;
    out->bytes_required = operation->bytes_required;
    out->protocol_status = operation->protocol_status;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI cancel_operation(OC_NET_OPERATION *handle) {
    oc_net_operation_slot *operation = valid_operation(handle);
    if (operation == 0) return EFI_INVALID_PARAMETER;
    if (operation->pending) {
        if (operation->backend_started && active_provider->platform.backend.cancel)
            active_provider->platform.backend.cancel(active_provider->platform.backend.context, handle->slot);
        operation->pending = 0u;
        operation->result = OC_NET_RESULT_CANCELLED;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI release_operation(OC_NET_OPERATION *handle) {
    oc_net_operation_slot *operation = valid_operation(handle);
    if (operation == 0) return EFI_INVALID_PARAMETER;
    release_operation_slot(operation);
    return EFI_SUCCESS;
}

EFI_STATUS oc_net_provider_initialize(oc_net_provider *provider,
                                      const oc_net_platform *platform) {
    if (provider == 0 || platform == 0 || platform->allocate == 0 ||
        platform->release == 0 || platform->backend.start == 0 ||
        platform->backend.pump == 0 ||
        (active_provider != 0 && active_provider != provider)) return EFI_INVALID_PARAMETER;
    zero_bytes(provider, sizeof(*provider));
    provider->platform = *platform;
    provider->protocol.structure_size = sizeof(provider->protocol);
    provider->protocol.abi_version = OC_NET_ABI_VERSION;
    provider->protocol.query_capabilities = query_capabilities;
    provider->protocol.create_session = create_session;
    provider->protocol.destroy_session = destroy_session;
    provider->protocol.query_state = query_state;
    provider->protocol.configure_dhcp4 = configure_dhcp4;
    provider->protocol.resolve_dns4 = resolve_dns4;
    provider->protocol.start_udp4 = start_udp4;
    provider->protocol.start_tcp4 = start_tcp4;
    provider->protocol.start_https = start_https;
    provider->protocol.pump = pump;
    provider->protocol.query_operation = query_operation;
    provider->protocol.cancel_operation = cancel_operation;
    provider->protocol.release_operation = release_operation;
    active_provider = provider;
    return EFI_SUCCESS;
}

EFI_STATUS oc_net_provider_begin_generation(oc_net_provider *provider, uint64_t generation) {
    if (provider == 0 || provider != active_provider || generation == 0u)
        return EFI_INVALID_PARAMETER;
    provider->generation = generation;
    return EFI_SUCCESS;
}

void oc_net_provider_cleanup_generation(oc_net_provider *provider, uint64_t generation) {
    UINTN i;
    if (provider == 0 || provider != active_provider) return;
    for (i = 0u; i < sizeof(provider->operations) / sizeof(provider->operations[0]); ++i)
        if (provider->operations[i].active && provider->operations[i].generation == generation)
            release_operation_slot(&provider->operations[i]);
    for (i = 0u; i < OC_NET_MAX_SESSIONS; ++i)
        if (provider->sessions[i].active && provider->sessions[i].generation == generation)
            zero_bytes(&provider->sessions[i], sizeof(provider->sessions[i]));
}

void oc_net_provider_shutdown(oc_net_provider *provider) {
    UINTN i;
    if (provider == 0 || provider != active_provider) return;
    for (i = 0u; i < sizeof(provider->operations) / sizeof(provider->operations[0]); ++i)
        release_operation_slot(&provider->operations[i]);
    zero_bytes(provider->sessions, sizeof(provider->sessions));
    active_provider = 0;
}

UINTN oc_net_provider_active_resources(const oc_net_provider *provider,
                                       uint64_t generation) {
    UINTN i, count = 0u;
    if (provider == 0) return 0u;
    for (i = 0u; i < OC_NET_MAX_SESSIONS; ++i)
        count += provider->sessions[i].active && provider->sessions[i].generation == generation;
    for (i = 0u; i < sizeof(provider->operations) / sizeof(provider->operations[0]); ++i)
        count += provider->operations[i].active && provider->operations[i].generation == generation;
    return count;
}
