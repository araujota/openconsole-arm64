#include "openconsole/uefi_net_backend.h"

#define EFI_LOCATE_BY_PROTOCOL 2u
#define EVT_NOTIFY_SIGNAL 0x00000200u
#define TPL_CALLBACK 8u

static const EFI_GUID snp_guid =
    {0xa19832b9u,0xac25u,0x11d3u,{0x9a,0x2d,0x00,0x90,0x27,0x3f,0xc1,0x4d}};

static void zero_bytes(void *destination, UINTN size) {
    uint8_t *bytes = destination;
    while (size-- != 0u) *bytes++ = 0u;
}

static void set_service(oc_uefi_net_service *service,
                        EFI_GUID binding, EFI_GUID protocol) {
    service->service_binding_guid = binding;
    service->protocol_guid = protocol;
}

static void initialize_guids(oc_uefi_net_backend *backend) {
    set_service(&backend->dhcp4,
        (EFI_GUID){0x9d9a39d8u,0xbd42u,0x4a73u,{0xa4,0xd5,0x8e,0xe9,0x4b,0xe1,0x13,0x80}},
        (EFI_GUID){0x8a219718u,0x4ef5u,0x4761u,{0x91,0xc8,0xc0,0xf0,0x4b,0xda,0x9e,0x56}});
    set_service(&backend->dns4,
        (EFI_GUID){0xb625b186u,0xe063u,0x44f7u,{0x89,0x05,0x6a,0x74,0xdc,0x6f,0x52,0xb4}},
        (EFI_GUID){0xae3d28ccu,0xe05bu,0x4fa1u,{0xa0,0x11,0x7e,0xb5,0x5a,0x3f,0x14,0x01}});
    set_service(&backend->udp4,
        (EFI_GUID){0x83f01464u,0x99bdu,0x45e5u,{0xb3,0x83,0xaf,0x63,0x05,0xd8,0xe9,0xe6}},
        (EFI_GUID){0x3ad9df29u,0x4501u,0x478du,{0xb1,0xf8,0x7f,0x7f,0xe7,0x0e,0x50,0xf3}});
    set_service(&backend->tcp4,
        (EFI_GUID){0x00720665u,0x67ebu,0x4a99u,{0xba,0xf7,0xd3,0xc3,0x3a,0x1c,0x7c,0xc9}},
        (EFI_GUID){0x65530bc7u,0xa359u,0x410fu,{0xb0,0x10,0x5a,0xad,0xc7,0xec,0x2b,0x62}});
    set_service(&backend->http,
        (EFI_GUID){0xbdc8e6afu,0xd9bcu,0x4379u,{0xa7,0x2a,0xe0,0xc4,0xe7,0x5d,0xae,0x1c}},
        (EFI_GUID){0x7a59b29bu,0x910bu,0x4171u,{0x82,0x42,0xa8,0x5a,0x0d,0xf2,0x5b,0x5b}});
    set_service(&backend->tls,
        (EFI_GUID){0x952cb795u,0xff36u,0x48cfu,{0xa2,0x49,0x4d,0xf4,0x86,0xd6,0xab,0x8d}},
        (EFI_GUID){0x00ca959fu,0x6cfau,0x4db1u,{0x95,0xbc,0xe4,0x6c,0x47,0x51,0x43,0x90}});
}

static void probe_service(EFI_BOOT_SERVICES *boot_services, EFI_HANDLE controller,
                          oc_uefi_net_service *service) {
    void *interface = 0;
    if (!EFI_ERROR(boot_services->handle_protocol(
            controller, &service->service_binding_guid, &interface)))
        service->binding = interface;
    else if (!EFI_ERROR(boot_services->locate_protocol(
                 &service->service_binding_guid, 0, &interface)))
        service->binding = interface;
}

static EFI_STATUS backend_query_state(void *context, OC_NET_STATE *state) {
    oc_uefi_net_backend *backend = context;
    if (backend == 0 || backend->snp == 0 || backend->snp->mode == 0 || state == 0)
        return EFI_NOT_READY;
    state->structure_size = sizeof(*state);
    state->link_up = backend->snp->mode->media_present_supported ?
                     backend->snp->mode->media_present : 1u;
    state->configured = backend->address_be != 0u;
    state->address_be = backend->address_be;
    state->subnet_mask_be = backend->subnet_mask_be;
    state->gateway_be = backend->gateway_be;
    return EFI_SUCCESS;
}

static uint32_t ipv4_value(const EFI_IPv4_ADDRESS *address) {
    return (uint32_t)address->addr[0] |
           ((uint32_t)address->addr[1] << 8) |
           ((uint32_t)address->addr[2] << 16) |
           ((uint32_t)address->addr[3] << 24);
}

static void ipv4_from_value(EFI_IPv4_ADDRESS *address, uint32_t value) {
    address->addr[0] = (uint8_t)value;
    address->addr[1] = (uint8_t)(value >> 8);
    address->addr[2] = (uint8_t)(value >> 16);
    address->addr[3] = (uint8_t)(value >> 24);
}

static uint32_t dhcp_dns_server(const OC_EFI_DHCP4_PACKET *packet) {
    const UINTN fixed_bytes = 240u;
    UINTN offset;
    if (packet == 0 || packet->length < fixed_bytes || packet->size < packet->length)
        return 0u;
    offset = fixed_bytes;
    while (offset < packet->length) {
        uint8_t code = packet->packet[offset++];
        uint8_t length;
        if (code == 0u) continue;
        if (code == 255u || offset >= packet->length) break;
        length = packet->packet[offset++];
        if ((UINTN)length > packet->length - offset) break;
        if (code == 6u && length >= 4u)
            return (uint32_t)packet->packet[offset] |
                   ((uint32_t)packet->packet[offset + 1u] << 8) |
                   ((uint32_t)packet->packet[offset + 2u] << 16) |
                   ((uint32_t)packet->packet[offset + 3u] << 24);
        offset += length;
    }
    return 0u;
}

static void EFIAPI operation_completed(EFI_EVENT event, void *context) {
    oc_uefi_net_operation *operation = context;
    (void)event;
    operation->completed = 1u;
}

static void cleanup_operation(oc_uefi_net_backend *backend, UINTN operation_id,
                              int stop) {
    oc_uefi_net_operation *operation;
    if (operation_id >= OC_NET_MAX_SESSIONS * OC_NET_MAX_OPERATIONS_PER_SESSION) return;
    operation = &backend->operations[operation_id];
    if (!operation->active) return;
    if (operation->kind == OC_NET_BACKEND_DHCP4 && operation->protocol != 0 &&
        operation->configured) {
        OC_EFI_DHCP4_PROTOCOL *dhcp = operation->protocol;
        (void)dhcp->stop(dhcp);
        (void)dhcp->configure(dhcp, 0);
    }
    if (operation->kind == OC_NET_BACKEND_DNS4 && operation->protocol != 0 &&
        operation->configured) {
        OC_EFI_DNS4_PROTOCOL *dns = operation->protocol;
        if (stop && !operation->completed) (void)dns->cancel(dns, &operation->dns_token);
        (void)dns->configure(dns, 0);
    }
    if (operation->kind == OC_NET_BACKEND_UDP4 && operation->protocol != 0 &&
        operation->configured) {
        OC_EFI_UDP4_PROTOCOL *udp = operation->protocol;
        if (stop && !operation->completed) (void)udp->cancel(udp, &operation->udp_token);
        if (operation->phase == 2u && operation->completed &&
            operation->udp_token.packet.rx_data != 0 && !operation->reserved) {
            (void)backend->boot_services->signal_event(
                operation->udp_token.packet.rx_data->recycle_signal);
            operation->reserved = 1u;
        }
        (void)udp->configure(udp, 0);
    }
    if (operation->kind == OC_NET_BACKEND_TCP4 && operation->protocol != 0 &&
        operation->configured) {
        OC_EFI_TCP4_PROTOCOL *tcp = operation->protocol;
        if (stop && !operation->completed) {
            OC_EFI_TCP4_COMPLETION_TOKEN *token = operation->phase == 1u ?
                &operation->tcp_connect.completion : &operation->tcp_io.completion;
            (void)tcp->cancel(tcp, token);
        }
        (void)tcp->configure(tcp, 0);
    }
    if (operation->kind == OC_NET_BACKEND_HTTPS && operation->protocol != 0 &&
        operation->configured) {
        OC_EFI_HTTP_PROTOCOL *http = operation->protocol;
        if (stop && !operation->completed) (void)http->cancel(http, &operation->http_token);
        (void)http->configure(http, 0);
    }
    if (operation->http_message.headers != 0)
        (void)backend->boot_services->free_pool(operation->http_message.headers);
    if (operation->dns_token.response.h2a_data != 0) {
        if (operation->dns_token.response.h2a_data->ip_list != 0)
            (void)backend->boot_services->free_pool(
                operation->dns_token.response.h2a_data->ip_list);
        (void)backend->boot_services->free_pool(operation->dns_token.response.h2a_data);
    }
    if (operation->event != 0) (void)backend->boot_services->close_event(operation->event);
    if (operation->child != 0 && operation->binding != 0)
        (void)operation->binding->destroy_child(operation->binding, operation->child);
    zero_bytes(operation, sizeof(*operation));
}

static EFI_STATUS create_completion_event(oc_uefi_net_backend *backend,
                                          oc_uefi_net_operation *operation) {
    return backend->boot_services->create_event(
        EVT_NOTIFY_SIGNAL, TPL_CALLBACK, operation_completed, operation,
        &operation->event);
}

static EFI_STATUS replace_completion_event(oc_uefi_net_backend *backend,
                                           oc_uefi_net_operation *operation) {
    if (operation->event != 0) (void)backend->boot_services->close_event(operation->event);
    operation->event = 0;
    operation->completed = 0u;
    return create_completion_event(backend, operation);
}

static EFI_STATUS start_dns4(oc_uefi_net_backend *backend, UINTN operation_id,
                             oc_uefi_net_service *service,
                             const oc_net_backend_request *request) {
    oc_uefi_net_operation *operation;
    OC_EFI_DNS4_PROTOCOL *dns = 0;
    OC_EFI_DNS4_CONFIG_DATA config;
    UINTN i;
    EFI_STATUS status;
    if (operation_id >= OC_NET_MAX_SESSIONS * OC_NET_MAX_OPERATIONS_PER_SESSION ||
        request->name == 0 || request->name_size == 0u ||
        request->name_size > OC_NET_MAX_HOST_BYTES) return EFI_INVALID_PARAMETER;
    operation = &backend->operations[operation_id];
    if (operation->active) return EFI_ALREADY_STARTED;
    zero_bytes(operation, sizeof(*operation));
    operation->active = 1u;
    operation->kind = OC_NET_BACKEND_DNS4;
    operation->binding = service->binding;
    for (i = 0u; i < request->name_size; ++i) {
        if ((uint8_t)request->name[i] >= 0x80u) {
            status = EFI_INVALID_PARAMETER;
            goto fail;
        }
        operation->dns_name[i] = (CHAR16)(uint8_t)request->name[i];
    }
    operation->dns_name[request->name_size] = 0u;
    status = service->binding->create_child(service->binding, &operation->child);
    if (EFI_ERROR(status)) goto fail;
    status = backend->boot_services->handle_protocol(
        operation->child, &service->protocol_guid, (void **)&dns);
    if (EFI_ERROR(status)) goto fail;
    operation->protocol = dns;
    zero_bytes(&config, sizeof(config));
    if (backend->address_be != 0u && backend->subnet_mask_be != 0u &&
        backend->dns_server_be != 0u) {
        config.dns_server_list_count = 1u;
        ipv4_from_value(&operation->dns_server, backend->dns_server_be);
        config.dns_server_list = &operation->dns_server;
        config.use_default_setting = 0u;
        ipv4_from_value(&config.station_ip, backend->address_be);
        ipv4_from_value(&config.subnet_mask, backend->subnet_mask_be);
    } else {
        config.use_default_setting = 1u;
    }
    config.enable_dns_cache = 1u;
    config.protocol = 17u;
    status = dns->configure(dns, &config);
    if (EFI_ERROR(status)) goto fail;
    operation->configured = 1u;
    status = create_completion_event(backend, operation);
    if (EFI_ERROR(status)) goto fail;
    operation->dns_token.event = operation->event;
    status = dns->host_name_to_ip(dns, operation->dns_name, &operation->dns_token);
    if (EFI_ERROR(status)) goto fail;
    return EFI_SUCCESS;
fail:
    cleanup_operation(backend, operation_id, 1);
    return status;
}

static EFI_STATUS start_dhcp4(oc_uefi_net_backend *backend, UINTN operation_id,
                              oc_uefi_net_service *service) {
    oc_uefi_net_operation *operation;
    OC_EFI_DHCP4_PROTOCOL *dhcp = 0;
    OC_EFI_DHCP4_CONFIG_DATA config;
    EFI_STATUS status;
    if (operation_id >= OC_NET_MAX_SESSIONS * OC_NET_MAX_OPERATIONS_PER_SESSION)
        return EFI_INVALID_PARAMETER;
    operation = &backend->operations[operation_id];
    if (operation->active) return EFI_ALREADY_STARTED;
    zero_bytes(operation, sizeof(*operation));
    operation->active = 1u;
    operation->kind = OC_NET_BACKEND_DHCP4;
    operation->binding = service->binding;
    status = service->binding->create_child(service->binding, &operation->child);
    if (EFI_ERROR(status)) goto fail;
    status = backend->boot_services->handle_protocol(
        operation->child, &service->protocol_guid, (void **)&dhcp);
    if (EFI_ERROR(status)) goto fail;
    operation->protocol = dhcp;
    zero_bytes(&config, sizeof(config));
    status = dhcp->configure(dhcp, &config);
    if (EFI_ERROR(status)) goto fail;
    operation->configured = 1u;
    status = create_completion_event(backend, operation);
    if (EFI_ERROR(status)) goto fail;
    status = dhcp->start(dhcp, operation->event);
    if (EFI_ERROR(status)) goto fail;
    return EFI_SUCCESS;
fail:
    cleanup_operation(backend, operation_id, 1);
    return status;
}

static EFI_STATUS start_udp4(oc_uefi_net_backend *backend, UINTN operation_id,
                             oc_uefi_net_service *service,
                             const oc_net_backend_request *request) {
    oc_uefi_net_operation *operation;
    OC_EFI_UDP4_PROTOCOL *udp = 0;
    OC_EFI_UDP4_CONFIG_DATA config;
    EFI_STATUS status;
    if (operation_id >= OC_NET_MAX_SESSIONS * OC_NET_MAX_OPERATIONS_PER_SESSION ||
        request->request_size > 0xffffffffu) return EFI_INVALID_PARAMETER;
    operation = &backend->operations[operation_id];
    if (operation->active) return EFI_ALREADY_STARTED;
    zero_bytes(operation, sizeof(*operation));
    operation->active = 1u;
    operation->kind = OC_NET_BACKEND_UDP4;
    operation->binding = service->binding;
    status = service->binding->create_child(service->binding, &operation->child);
    if (EFI_ERROR(status)) goto fail;
    status = backend->boot_services->handle_protocol(
        operation->child, &service->protocol_guid, (void **)&udp);
    if (EFI_ERROR(status)) goto fail;
    operation->protocol = udp;
    zero_bytes(&config, sizeof(config));
    config.accept_broadcast = 1u;
    config.time_to_live = 64u;
    if (backend->address_be != 0u && backend->subnet_mask_be != 0u) {
        config.use_default_address = 0u;
        ipv4_from_value(&config.station_address, backend->address_be);
        ipv4_from_value(&config.subnet_mask, backend->subnet_mask_be);
    } else {
        config.use_default_address = 1u;
    }
    ipv4_from_value(&config.remote_address, request->address_be);
    config.remote_port = request->port;
    status = udp->configure(udp, &config);
    if (EFI_ERROR(status)) goto fail;
    operation->configured = 1u;
    status = create_completion_event(backend, operation);
    if (EFI_ERROR(status)) goto fail;
    operation->udp_tx.data_length = (uint32_t)request->request_size;
    operation->udp_tx.fragment_count = 1u;
    operation->udp_tx.fragment_table[0].fragment_length = (uint32_t)request->request_size;
    operation->udp_tx.fragment_table[0].fragment_buffer = (void *)(UINTN)request->request_bytes;
    operation->udp_token.event = operation->event;
    operation->udp_token.packet.tx_data = &operation->udp_tx;
    operation->phase = 1u;
    status = udp->transmit(udp, &operation->udp_token);
    if (EFI_ERROR(status)) goto fail;
    return EFI_SUCCESS;
fail:
    cleanup_operation(backend, operation_id, 1);
    return status;
}

static EFI_STATUS advance_udp4(oc_uefi_net_backend *backend,
                               oc_uefi_net_operation *operation,
                               const oc_net_backend_request *request,
                               oc_net_backend_progress *progress) {
    OC_EFI_UDP4_PROTOCOL *udp = operation->protocol;
    EFI_STATUS status = udp->poll(udp);
    if (EFI_ERROR(status) && status != EFI_NOT_READY) return status;
    if (!operation->completed) {
        progress->result = OC_NET_RESULT_PENDING;
        return EFI_SUCCESS;
    }
    if (operation->udp_token.status != EFI_SUCCESS) {
        progress->protocol_status = (uint32_t)operation->udp_token.status;
        progress->result = OC_NET_RESULT_BACKEND_FAILURE;
        return EFI_SUCCESS;
    }
    if (operation->phase == 1u) {
        (void)backend->boot_services->close_event(operation->event);
        operation->event = 0;
        operation->completed = 0u;
        zero_bytes(&operation->udp_token, sizeof(operation->udp_token));
        status = create_completion_event(backend, operation);
        if (EFI_ERROR(status)) return status;
        operation->udp_token.event = operation->event;
        operation->phase = 2u;
        status = udp->receive(udp, &operation->udp_token);
        if (EFI_ERROR(status)) return status;
        progress->result = OC_NET_RESULT_PENDING;
        return EFI_SUCCESS;
    }
    if (operation->phase == 2u) {
        OC_EFI_UDP4_RECEIVE_DATA *data = operation->udp_token.packet.rx_data;
        UINTN i;
        UINTN offset = 0u;
        if (data == 0) {
            progress->result = OC_NET_RESULT_MALFORMED_RESPONSE;
            return EFI_SUCCESS;
        }
        progress->bytes_required = data->data_length;
        if (data->data_length > request->response_capacity) {
            progress->result = OC_NET_RESULT_RESPONSE_TOO_LARGE;
        } else {
            for (i = 0u; i < data->fragment_count; ++i) {
                UINTN j;
                OC_EFI_UDP4_FRAGMENT_DATA *fragment = &data->fragment_table[i];
                if (fragment->fragment_length > data->data_length - offset) {
                    progress->result = OC_NET_RESULT_MALFORMED_RESPONSE;
                    break;
                }
                for (j = 0u; j < fragment->fragment_length; ++j)
                    ((uint8_t *)request->response_bytes)[offset + j] =
                        ((const uint8_t *)fragment->fragment_buffer)[j];
                offset += fragment->fragment_length;
            }
            if (offset == data->data_length) {
                progress->bytes_transferred = offset;
                progress->result = OC_NET_RESULT_OK;
            } else if (progress->result != OC_NET_RESULT_MALFORMED_RESPONSE) {
                progress->result = OC_NET_RESULT_MALFORMED_RESPONSE;
            }
        }
        (void)backend->boot_services->signal_event(data->recycle_signal);
        operation->reserved = 1u;
        operation->phase = 3u;
        return EFI_SUCCESS;
    }
    return EFI_NOT_READY;
}

static EFI_STATUS start_tcp4(oc_uefi_net_backend *backend, UINTN operation_id,
                             oc_uefi_net_service *service,
                             const oc_net_backend_request *request) {
    oc_uefi_net_operation *operation;
    OC_EFI_TCP4_PROTOCOL *tcp = 0;
    OC_EFI_TCP4_CONFIG_DATA config;
    EFI_STATUS status;
    if (operation_id >= OC_NET_MAX_SESSIONS * OC_NET_MAX_OPERATIONS_PER_SESSION ||
        request->request_size > 0xffffffffu || request->response_capacity > 0xffffffffu)
        return EFI_INVALID_PARAMETER;
    operation = &backend->operations[operation_id];
    if (operation->active) return EFI_ALREADY_STARTED;
    zero_bytes(operation, sizeof(*operation));
    operation->active = 1u;
    operation->kind = OC_NET_BACKEND_TCP4;
    operation->binding = service->binding;
    status = service->binding->create_child(service->binding, &operation->child);
    if (EFI_ERROR(status)) goto fail;
    status = backend->boot_services->handle_protocol(
        operation->child, &service->protocol_guid, (void **)&tcp);
    if (EFI_ERROR(status)) goto fail;
    operation->protocol = tcp;
    zero_bytes(&config, sizeof(config));
    config.time_to_live = 64u;
    if (backend->address_be != 0u && backend->subnet_mask_be != 0u) {
        config.access_point.use_default_address = 0u;
        ipv4_from_value(&config.access_point.station_address, backend->address_be);
        ipv4_from_value(&config.access_point.subnet_mask, backend->subnet_mask_be);
    } else {
        config.access_point.use_default_address = 1u;
    }
    config.access_point.active_flag = 1u;
    config.access_point.remote_port = request->port;
    ipv4_from_value(&config.access_point.remote_address, request->address_be);
    status = tcp->configure(tcp, &config);
    if (EFI_ERROR(status)) goto fail;
    operation->configured = 1u;
    status = create_completion_event(backend, operation);
    if (EFI_ERROR(status)) goto fail;
    operation->tcp_connect.completion.event = operation->event;
    operation->phase = 1u;
    status = tcp->connect(tcp, &operation->tcp_connect);
    if (EFI_ERROR(status)) goto fail;
    return EFI_SUCCESS;
fail:
    cleanup_operation(backend, operation_id, 1);
    return status;
}

static uint32_t tcp_error_result(EFI_STATUS status) {
    if (status == EFI_CONNECTION_REFUSED) return OC_NET_RESULT_CONNECTION_REFUSED;
    if (status == EFI_TIMEOUT) return OC_NET_RESULT_DEADLINE_EXCEEDED;
    if (status == EFI_NO_MEDIA || status == EFI_NETWORK_UNREACHABLE)
        return OC_NET_RESULT_LINK_DOWN;
    return OC_NET_RESULT_BACKEND_FAILURE;
}

static EFI_STATUS advance_tcp4(oc_uefi_net_backend *backend,
                               oc_uefi_net_operation *operation,
                               const oc_net_backend_request *request,
                               oc_net_backend_progress *progress) {
    OC_EFI_TCP4_PROTOCOL *tcp = operation->protocol;
    OC_EFI_TCP4_COMPLETION_TOKEN *completion = operation->phase == 1u ?
        &operation->tcp_connect.completion : &operation->tcp_io.completion;
    EFI_STATUS status = tcp->poll(tcp);
    if (EFI_ERROR(status) && status != EFI_NOT_READY) return status;
    if (!operation->completed) {
        progress->result = OC_NET_RESULT_PENDING;
        return EFI_SUCCESS;
    }
    if (completion->status != EFI_SUCCESS &&
        !(operation->phase == 3u && completion->status == EFI_CONNECTION_FIN)) {
        progress->protocol_status = (uint32_t)completion->status;
        progress->result = tcp_error_result(completion->status);
        return EFI_SUCCESS;
    }
    if (operation->phase == 1u) {
        status = replace_completion_event(backend, operation);
        if (EFI_ERROR(status)) return status;
        zero_bytes(&operation->tcp_io, sizeof(operation->tcp_io));
        operation->tcp_tx.push = 1u;
        operation->tcp_tx.data_length = (uint32_t)request->request_size;
        operation->tcp_tx.fragment_count = 1u;
        operation->tcp_tx.fragment_table[0].fragment_length = (uint32_t)request->request_size;
        operation->tcp_tx.fragment_table[0].fragment_buffer = (void *)(UINTN)request->request_bytes;
        operation->tcp_io.completion.event = operation->event;
        operation->tcp_io.packet.tx_data = &operation->tcp_tx;
        operation->phase = 2u;
        status = tcp->transmit(tcp, &operation->tcp_io);
        if (EFI_ERROR(status)) return status;
        progress->result = OC_NET_RESULT_PENDING;
        return EFI_SUCCESS;
    }
    if (operation->phase == 2u) {
        status = replace_completion_event(backend, operation);
        if (EFI_ERROR(status)) return status;
        zero_bytes(&operation->tcp_io, sizeof(operation->tcp_io));
        zero_bytes(&operation->tcp_rx, sizeof(operation->tcp_rx));
        operation->tcp_rx.data_length = (uint32_t)request->response_capacity;
        operation->tcp_rx.fragment_count = 1u;
        operation->tcp_rx.fragment_table[0].fragment_length =
            (uint32_t)request->response_capacity;
        operation->tcp_rx.fragment_table[0].fragment_buffer = request->response_bytes;
        operation->tcp_io.completion.event = operation->event;
        operation->tcp_io.packet.rx_data = &operation->tcp_rx;
        operation->phase = 3u;
        status = tcp->receive(tcp, &operation->tcp_io);
        if (EFI_ERROR(status)) return status;
        progress->result = OC_NET_RESULT_PENDING;
        return EFI_SUCCESS;
    }
    if (operation->phase == 3u) {
        if (operation->tcp_rx.data_length > request->response_capacity) {
            progress->bytes_required = operation->tcp_rx.data_length;
            progress->result = OC_NET_RESULT_RESPONSE_TOO_LARGE;
        } else {
            progress->bytes_transferred = operation->tcp_rx.data_length;
            progress->bytes_required = operation->tcp_rx.data_length;
            progress->result = OC_NET_RESULT_OK;
        }
        operation->phase = 4u;
        return EFI_SUCCESS;
    }
    return EFI_NOT_READY;
}

static int ascii_equal_fold(const char *left, const char *right) {
    UINTN i = 0u;
    while (left[i] != '\0' && right[i] != '\0') {
        char a = left[i], b = right[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return 0;
        ++i;
    }
    return left[i] == right[i];
}

static int parse_decimal(const char *text, uint64_t *value) {
    uint64_t result = 0u;
    UINTN i = 0u;
    if (text == 0 || text[0] == '\0') return 0;
    while (text[i] != '\0') {
        uint32_t digit;
        if (text[i] < '0' || text[i] > '9') return 0;
        digit = (uint32_t)(text[i] - '0');
        if (result > (UINT64_MAX - digit) / 10u) return 0;
        result = result * 10u + digit;
        ++i;
    }
    *value = result;
    return 1;
}

static void release_http_headers(oc_uefi_net_backend *backend,
                                 oc_uefi_net_operation *operation) {
    if (operation->http_message.headers != 0) {
        (void)backend->boot_services->free_pool(operation->http_message.headers);
        operation->http_message.headers = 0;
        operation->http_message.header_count = 0u;
    }
}

static EFI_STATUS submit_http_response(oc_uefi_net_backend *backend,
                                       oc_uefi_net_operation *operation,
                                       const oc_net_backend_request *request,
                                       int initial_response) {
    OC_EFI_HTTP_PROTOCOL *http = operation->protocol;
    uint64_t remaining;
    EFI_STATUS status = replace_completion_event(backend, operation);
    if (EFI_ERROR(status)) return status;
    zero_bytes(&operation->http_token, sizeof(operation->http_token));
    zero_bytes(&operation->http_message, sizeof(operation->http_message));
    /* UEFI HTTP continuation calls must provide only Body/BodyLength. Reusing
     * ResponseData makes HttpDxe parse each TLS record as a new response. */
    operation->http_message.data.response = initial_response ?
        &operation->http_response : 0;
    operation->http_message.body = (uint8_t *)request->response_bytes + operation->http_received;
    remaining = request->response_capacity - operation->http_received;
    /* Pinned HttpDxe's HTTPS path consumes one TLS application-data record per
     * Response() call, but can leave BodyLength at the larger caller request.
     * Never let that ambiguity skip unwritten records in the destination. */
    if (remaining > 16384u) remaining = 16384u;
    operation->http_message.body_length = (UINTN)remaining;
    operation->http_token.event = operation->event;
    operation->http_token.message = &operation->http_message;
    operation->phase = 2u;
    return http->response(http, &operation->http_token);
}

static EFI_STATUS start_https(oc_uefi_net_backend *backend, UINTN operation_id,
                              oc_uefi_net_service *service,
                              const oc_net_backend_request *request) {
    oc_uefi_net_operation *operation;
    OC_EFI_HTTP_PROTOCOL *http = 0;
    OC_EFI_HTTP_CONFIG_DATA config;
    OC_EFI_HTTP4_ACCESS_POINT access;
    UINTN i;
    UINTN host_size;
    EFI_STATUS status;
    if (operation_id >= OC_NET_MAX_SESSIONS * OC_NET_MAX_OPERATIONS_PER_SESSION ||
        request->name == 0 || request->name_size < 8u ||
        request->name_size > OC_NET_MAX_URL_BYTES) return EFI_INVALID_PARAMETER;
    /* Only HTTPS enters this path. HttpDxe from the pinned EDK II revision sets
     * EFI_TLS_VERIFY_PEER and EfiTlsVerifyHost from this URL's host name. */
    if (!(request->name[0] == 'h' && request->name[1] == 't' &&
          request->name[2] == 't' && request->name[3] == 'p' &&
          request->name[4] == 's' && request->name[5] == ':' &&
          request->name[6] == '/' && request->name[7] == '/'))
        return EFI_SECURITY_VIOLATION;
    operation = &backend->operations[operation_id];
    if (operation->active) return EFI_ALREADY_STARTED;
    zero_bytes(operation, sizeof(*operation));
    operation->active = 1u;
    operation->kind = OC_NET_BACKEND_HTTPS;
    operation->binding = service->binding;
    for (i = 0u; i < request->name_size; ++i) {
        if ((uint8_t)request->name[i] >= 0x80u) {
            status = EFI_INVALID_PARAMETER;
            goto fail;
        }
        operation->http_url[i] = (CHAR16)(uint8_t)request->name[i];
    }
    operation->http_url[request->name_size] = 0u;
    host_size = 0u;
    for (i = 8u; i < request->name_size && request->name[i] != '/'; ++i) {
        if (host_size == OC_NET_MAX_HOST_BYTES) {
            status = EFI_INVALID_PARAMETER;
            goto fail;
        }
        ((char *)operation->dns_name)[host_size++] = request->name[i];
    }
    if (host_size == 0u) {
        status = EFI_INVALID_PARAMETER;
        goto fail;
    }
    ((char *)operation->dns_name)[host_size] = '\0';
    status = service->binding->create_child(service->binding, &operation->child);
    if (EFI_ERROR(status)) { operation->phase = 0xf1u; goto fail; }
    status = backend->boot_services->handle_protocol(
        operation->child, &service->protocol_guid, (void **)&http);
    if (EFI_ERROR(status)) { operation->phase = 0xf2u; goto fail; }
    operation->protocol = http;
    zero_bytes(&config, sizeof(config));
    zero_bytes(&access, sizeof(access));
    config.http_version = 1u;
    config.local_address_is_ipv6 = 0u;
    if (backend->address_be != 0u && backend->subnet_mask_be != 0u) {
        access.use_default_address = 0u;
        ipv4_from_value(&access.local_address, backend->address_be);
        ipv4_from_value(&access.local_subnet, backend->subnet_mask_be);
    } else {
        access.use_default_address = 1u;
    }
    config.access_point.ipv4 = &access;
    status = http->configure(http, &config);
    if (EFI_ERROR(status)) { operation->phase = 0xf3u; goto fail; }
    operation->configured = 1u;
    status = create_completion_event(backend, operation);
    if (EFI_ERROR(status)) { operation->phase = 0xf4u; goto fail; }
    operation->http_request.method = request->request_size == 0u ? 0u : 1u;
    operation->http_request.url = operation->http_url;
    operation->http_message.data.request = &operation->http_request;
    operation->http_request_header.field_name = "Host";
    operation->http_request_header.field_value = (char *)operation->dns_name;
    operation->http_message.header_count = 1u;
    operation->http_message.headers = &operation->http_request_header;
    operation->http_message.body = (void *)(UINTN)request->request_bytes;
    operation->http_message.body_length = request->request_size;
    operation->http_token.event = operation->event;
    operation->http_token.message = &operation->http_message;
    operation->phase = 1u;
    status = http->request(http, &operation->http_token);
    if (status == EFI_NO_MAPPING) {
        operation->phase = 0u;
        return EFI_SUCCESS;
    }
    if (status == EFI_ABORTED || status == EFI_SECURITY_VIOLATION) {
        operation->http_token.status = EFI_SECURITY_VIOLATION;
        operation->completed = 1u;
        return EFI_SUCCESS;
    }
    if (EFI_ERROR(status)) goto fail;
    return EFI_SUCCESS;
fail:
    cleanup_operation(backend, operation_id, 1);
    return status;
}

static EFI_STATUS advance_https(oc_uefi_net_backend *backend,
                                oc_uefi_net_operation *operation,
                                const oc_net_backend_request *request,
                                oc_net_backend_progress *progress) {
    OC_EFI_HTTP_PROTOCOL *http = operation->protocol;
    EFI_STATUS status;
    UINTN i;
    if (operation->completed && operation->http_token.status == EFI_SECURITY_VIOLATION) {
        progress->protocol_status = (uint32_t)EFI_SECURITY_VIOLATION;
        progress->result = OC_NET_RESULT_TLS_IDENTITY_REJECTED;
        return EFI_SUCCESS;
    }
    status = http->poll(http);
    if (status == EFI_CONNECTION_FIN && operation->phase == 2u)
        operation->completed = 1u;
    if (EFI_ERROR(status) && status != EFI_NOT_READY &&
        !(status == EFI_CONNECTION_FIN && operation->phase == 2u)) return status;
    if (operation->phase == 0u) {
        status = http->request(http, &operation->http_token);
        if (status == EFI_NO_MAPPING) {
            progress->result = OC_NET_RESULT_PENDING;
            return EFI_SUCCESS;
        }
        if (EFI_ERROR(status)) return status;
        operation->phase = 1u;
    }
    if (!operation->completed) {
        progress->result = OC_NET_RESULT_PENDING;
        return EFI_SUCCESS;
    }
    if (operation->http_token.status != EFI_SUCCESS &&
        operation->http_token.status != EFI_HTTP_ERROR &&
        !(operation->phase == 2u &&
          operation->http_token.status == EFI_CONNECTION_FIN)) {
        progress->protocol_status = (uint32_t)operation->http_token.status;
        progress->result = operation->http_token.status == EFI_SECURITY_VIOLATION ?
            OC_NET_RESULT_TLS_IDENTITY_REJECTED : OC_NET_RESULT_BACKEND_FAILURE;
        return EFI_SUCCESS;
    }
    if (operation->phase == 1u) {
        zero_bytes(&operation->http_response, sizeof(operation->http_response));
        status = submit_http_response(backend, operation, request, 1);
        if (status == EFI_CONNECTION_FIN) {
            operation->completed = 1u;
            progress->result = OC_NET_RESULT_PENDING;
            return EFI_SUCCESS;
        }
        if (!EFI_ERROR(status)) progress->result = OC_NET_RESULT_PENDING;
        return status;
    }
    if (operation->phase != 2u) return EFI_NOT_READY;
    if (operation->http_response.status_code < 3u ||
        operation->http_response.status_code > 9u) {
        progress->protocol_status = operation->http_response.status_code;
        progress->result = OC_NET_RESULT_PROTOCOL_ERROR;
        release_http_headers(backend, operation);
        return EFI_SUCCESS;
    }
    for (i = 0u; i < operation->http_message.header_count; ++i) {
        OC_EFI_HTTP_HEADER *header = &operation->http_message.headers[i];
        if (header->field_name == 0 || header->field_value == 0) {
            progress->result = OC_NET_RESULT_MALFORMED_RESPONSE;
            release_http_headers(backend, operation);
            return EFI_SUCCESS;
        }
        if (ascii_equal_fold(header->field_name, "content-length")) {
            if (!parse_decimal(header->field_value, &operation->http_expected)) {
                progress->result = OC_NET_RESULT_MALFORMED_RESPONSE;
                release_http_headers(backend, operation);
                return EFI_SUCCESS;
            }
            operation->http_expected_known = 1u;
        } else if (ascii_equal_fold(header->field_name, "transfer-encoding") &&
                   ascii_equal_fold(header->field_value, "chunked")) {
            operation->http_chunked = 1u;
        }
    }
    if (operation->http_message.body_length >
        request->response_capacity - operation->http_received) {
        progress->bytes_required = operation->http_received + operation->http_message.body_length;
        progress->result = OC_NET_RESULT_RESPONSE_TOO_LARGE;
        release_http_headers(backend, operation);
        return EFI_SUCCESS;
    }
    operation->http_received += operation->http_message.body_length;
    if (operation->http_expected_known && operation->http_expected > request->response_capacity) {
        progress->bytes_required = operation->http_expected;
        progress->result = OC_NET_RESULT_RESPONSE_TOO_LARGE;
        release_http_headers(backend, operation);
        return EFI_SUCCESS;
    }
    if ((operation->http_expected_known && operation->http_received < operation->http_expected) ||
        (operation->http_chunked && operation->http_message.body_length != 0u)) {
        release_http_headers(backend, operation);
        if (operation->http_received == request->response_capacity) {
            progress->bytes_required = request->response_capacity + 1u;
            progress->result = OC_NET_RESULT_RESPONSE_TOO_LARGE;
            return EFI_SUCCESS;
        }
        status = submit_http_response(backend, operation, request, 0);
        if (status == EFI_CONNECTION_FIN) {
            operation->completed = 1u;
            progress->result = OC_NET_RESULT_PENDING;
            return EFI_SUCCESS;
        }
        if (EFI_ERROR(status)) return status;
        progress->result = OC_NET_RESULT_PENDING;
        return EFI_SUCCESS;
    }
    if (!operation->http_expected_known && !operation->http_chunked &&
        operation->http_received == request->response_capacity) {
        progress->bytes_required = request->response_capacity + 1u;
        progress->result = OC_NET_RESULT_RESPONSE_TOO_LARGE;
    } else if (operation->http_expected_known &&
               operation->http_received != operation->http_expected) {
        progress->bytes_transferred = operation->http_received;
        progress->bytes_required = operation->http_expected;
        progress->result = OC_NET_RESULT_MALFORMED_RESPONSE;
    } else {
        progress->bytes_transferred = operation->http_received;
        progress->bytes_required = operation->http_received;
        progress->result = OC_NET_RESULT_OK;
    }
    release_http_headers(backend, operation);
    operation->phase = 3u;
    return EFI_SUCCESS;
}

static oc_uefi_net_service *service_for_request(oc_uefi_net_backend *backend,
                                                oc_net_backend_kind kind) {
    if (kind == OC_NET_BACKEND_DHCP4) return &backend->dhcp4;
    if (kind == OC_NET_BACKEND_DNS4) return &backend->dns4;
    if (kind == OC_NET_BACKEND_UDP4) return &backend->udp4;
    if (kind == OC_NET_BACKEND_TCP4) return &backend->tcp4;
    if (kind == OC_NET_BACKEND_HTTPS) return &backend->http;
    return 0;
}

static EFI_STATUS backend_start(void *context, UINTN operation_id,
                                const oc_net_backend_request *request) {
    oc_uefi_net_backend *backend = context;
    oc_uefi_net_service *service;
    (void)operation_id;
    if (backend == 0 || request == 0) return EFI_INVALID_PARAMETER;
    service = service_for_request(backend, request->kind);
    if (service == 0 || service->binding == 0) return EFI_UNSUPPORTED;
    if (request->kind == OC_NET_BACKEND_DHCP4)
        return start_dhcp4(backend, operation_id, service);
    if (request->kind == OC_NET_BACKEND_DNS4)
        return start_dns4(backend, operation_id, service, request);
    if (request->kind == OC_NET_BACKEND_UDP4)
        return start_udp4(backend, operation_id, service, request);
    if (request->kind == OC_NET_BACKEND_TCP4)
        return start_tcp4(backend, operation_id, service, request);
    if (request->kind == OC_NET_BACKEND_HTTPS)
        return start_https(backend, operation_id, service, request);
    return EFI_UNSUPPORTED;
}

static EFI_STATUS backend_pump(void *context, UINTN operation_id,
                               const oc_net_backend_request *request,
                               oc_net_backend_progress *progress) {
    oc_uefi_net_backend *backend = context;
    oc_uefi_net_operation *operation;
    (void)request;
    if (backend == 0 || operation_id >= OC_NET_MAX_SESSIONS * OC_NET_MAX_OPERATIONS_PER_SESSION)
        return EFI_INVALID_PARAMETER;
    operation = &backend->operations[operation_id];
    if (!operation->active) return EFI_NOT_READY;
    if (operation->kind == OC_NET_BACKEND_DHCP4 && operation->completed) {
        OC_EFI_DHCP4_PROTOCOL *dhcp = operation->protocol;
        OC_EFI_DHCP4_MODE_DATA mode;
        EFI_STATUS status;
        zero_bytes(&mode, sizeof(mode));
        status = dhcp->get_mode_data(dhcp, &mode);
        if (EFI_ERROR(status)) return status;
        if (mode.state != 4u) {
            progress->result = OC_NET_RESULT_ADDRESS_UNAVAILABLE;
            return EFI_SUCCESS;
        }
        backend->address_be = ipv4_value(&mode.client_address);
        backend->subnet_mask_be = ipv4_value(&mode.subnet_mask);
        backend->gateway_be = ipv4_value(&mode.router_address);
        backend->dns_server_be = dhcp_dns_server(mode.reply_packet);
        progress->result = OC_NET_RESULT_OK;
        return EFI_SUCCESS;
    }
    if (operation->kind == OC_NET_BACKEND_DNS4) {
        OC_EFI_DNS4_PROTOCOL *dns = operation->protocol;
        EFI_STATUS status = dns->poll(dns);
        UINTN i;
        if (EFI_ERROR(status) && status != EFI_NOT_READY) return status;
        if (!operation->completed) {
            progress->result = OC_NET_RESULT_PENDING;
            return EFI_SUCCESS;
        }
        if (operation->dns_token.status != EFI_SUCCESS) {
            progress->protocol_status = (uint32_t)operation->dns_token.status;
            progress->result = operation->dns_token.status == EFI_NOT_FOUND ?
                OC_NET_RESULT_NAME_NOT_FOUND :
                (operation->dns_token.status == EFI_TIMEOUT ?
                    OC_NET_RESULT_DEADLINE_EXCEEDED : OC_NET_RESULT_BACKEND_FAILURE);
            return EFI_SUCCESS;
        }
        if (operation->dns_token.response.h2a_data == 0) {
            progress->result = OC_NET_RESULT_MALFORMED_RESPONSE;
            return EFI_SUCCESS;
        }
        if (operation->dns_token.response.h2a_data->ip_count > OC_NET_MAX_DNS_ADDRESSES) {
            progress->bytes_required = request->response_capacity + 1u;
            progress->result = OC_NET_RESULT_RESPONSE_TOO_LARGE;
            return EFI_SUCCESS;
        }
        {
            OC_NET_DNS_RESULT *result = request->response_bytes;
            if (result == 0 || request->response_capacity < sizeof(*result)) {
                progress->bytes_required = sizeof(*result);
                progress->result = OC_NET_RESULT_RESPONSE_TOO_LARGE;
                return EFI_SUCCESS;
            }
            result->structure_size = sizeof(*result);
            result->address_count = operation->dns_token.response.h2a_data->ip_count;
            for (i = 0u; i < result->address_count; ++i)
                result->addresses_be[i] = ipv4_value(
                    &operation->dns_token.response.h2a_data->ip_list[i]);
            progress->bytes_transferred = sizeof(*result);
            progress->bytes_required = sizeof(*result);
            progress->result = OC_NET_RESULT_OK;
        }
        return EFI_SUCCESS;
    }
    if (operation->kind == OC_NET_BACKEND_UDP4)
        return advance_udp4(backend, operation, request, progress);
    if (operation->kind == OC_NET_BACKEND_TCP4)
        return advance_tcp4(backend, operation, request, progress);
    if (operation->kind == OC_NET_BACKEND_HTTPS)
        return advance_https(backend, operation, request, progress);
    progress->result = OC_NET_RESULT_PENDING;
    return EFI_SUCCESS;
}

static void backend_cancel(void *context, UINTN operation_id) {
    oc_uefi_net_backend *backend = context;
    if (backend != 0) cleanup_operation(backend, operation_id, 1);
}

static void backend_release(void *context, UINTN operation_id) {
    oc_uefi_net_backend *backend = context;
    if (backend != 0) cleanup_operation(backend, operation_id, 0);
}

EFI_STATUS oc_uefi_net_backend_initialize(oc_uefi_net_backend *backend,
                                          EFI_BOOT_SERVICES *boot_services) {
    EFI_HANDLE *handles = 0;
    UINTN count = 0u;
    UINTN i;
    EFI_STATUS status;
    if (backend == 0 || boot_services == 0) return EFI_INVALID_PARAMETER;
    zero_bytes(backend, sizeof(*backend));
    backend->boot_services = boot_services;
    initialize_guids(backend);
    status = boot_services->locate_handle_buffer(
        EFI_LOCATE_BY_PROTOCOL, &snp_guid, 0, &count, &handles);
    if (EFI_ERROR(status)) return status;
    for (i = 0u; i < count; ++i) {
        OC_EFI_SIMPLE_NETWORK *snp = 0;
        if (EFI_ERROR(boot_services->handle_protocol(handles[i], &snp_guid,
                                                      (void **)&snp)) ||
            snp == 0 || snp->mode == 0) continue;
        if (backend->snp == 0 || !backend->snp->mode->media_present_supported ||
            snp->mode->media_present) {
            backend->controller = handles[i];
            backend->snp = snp;
            if (snp->mode->media_present) break;
        }
    }
    (void)boot_services->free_pool(handles);
    if (backend->snp == 0) return EFI_NOT_FOUND;
    probe_service(boot_services, backend->controller, &backend->dhcp4);
    probe_service(boot_services, backend->controller, &backend->dns4);
    probe_service(boot_services, backend->controller, &backend->udp4);
    probe_service(boot_services, backend->controller, &backend->tcp4);
    probe_service(boot_services, backend->controller, &backend->http);
    probe_service(boot_services, backend->controller, &backend->tls);
    backend->capabilities = OC_NET_CAP_IPV4;
    if (backend->dhcp4.binding) backend->capabilities |= OC_NET_CAP_DHCP4;
    if (backend->dns4.binding) backend->capabilities |= OC_NET_CAP_DNS4;
    if (backend->udp4.binding) backend->capabilities |= OC_NET_CAP_UDP4;
    if (backend->tcp4.binding) backend->capabilities |= OC_NET_CAP_TCP4;
    if (backend->http.binding && backend->tls.binding)
        backend->capabilities |= OC_NET_CAP_HTTPS | OC_NET_CAP_TLS_IDENTITY_VALIDATION;
    return EFI_SUCCESS;
}

void oc_uefi_net_backend_get_platform(oc_uefi_net_backend *backend,
                                      oc_net_backend *out) {
    zero_bytes(out, sizeof(*out));
    out->context = backend;
    out->capabilities = backend->capabilities;
    out->start = backend_start;
    out->pump = backend_pump;
    out->cancel = backend_cancel;
    out->release = backend_release;
    out->query_state = backend_query_state;
}
