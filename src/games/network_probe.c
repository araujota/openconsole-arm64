#include "openconsole/net_protocol.h"

#define FIXTURE_ADDRESS ((uint32_t)10u | ((uint32_t)2u << 16) | ((uint32_t)2u << 24))
#define EXPECTED_ADDRESS ((uint32_t)10u | ((uint32_t)2u << 16) | ((uint32_t)15u << 24))

static void debug_putc(char value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"((uint16_t)0x402));
}

static void debug_write(const char *text) {
    while (*text != '\0') debug_putc(*text++);
}

static void debug_hex32(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    int shift;
    for (shift = 28; shift >= 0; shift -= 4) debug_putc(digits[(value >> shift) & 15u]);
    debug_putc('\n');
}

static int bytes_equal(const void *left, const void *right, UINTN size) {
    const uint8_t *a = left;
    const uint8_t *b = right;
    while (size-- != 0u) if (*a++ != *b++) return 0;
    return 1;
}

static EFI_STATUS await_operation(OC_NET_PROTOCOL *net, OC_NET_SESSION *session,
                                  OC_NET_OPERATION *operation,
                                  EFI_BOOT_SERVICES *boot_services,
                                  uint64_t *now_usec,
                                  OC_NET_OPERATION_STATUS *result) {
    for (;;) {
        EFI_STATUS status;
        result->structure_size = sizeof(*result);
        status = net->query_operation(operation, result);
        if (EFI_ERROR(status) || result->result != OC_NET_RESULT_PENDING) return status;
        *now_usec += 1000u;
        status = net->pump(session, *now_usec);
        if (EFI_ERROR(status)) return status;
        (void)boot_services->stall(1000u);
    }
}

static EFI_STATUS release_expect(OC_NET_PROTOCOL *net, OC_NET_OPERATION *operation,
                                 const OC_NET_OPERATION_STATUS *status,
                                 uint32_t expected) {
    EFI_STATUS release_status = net->release_operation(operation);
    if (EFI_ERROR(release_status)) return release_status;
    return status->result == expected ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    static const char udp_request[] = "openconsole-udp-probe";
    static const char udp_response[] = "openconsole-udp-reply";
    static const char bounds_request[] = "openconsole-bounds-probe";
    static const char tcp_request[] =
        "GET /openconsole HTTP/1.1\r\nHost: fixture.openconsole.test\r\nConnection: close\r\n\r\n";
    static const char tcp_response_prefix[] = "HTTP/1.1 200 OK";
    static const char fixture_name[] = "localhost";
    OC_NET_PROTOCOL *net = 0;
    OC_NET_SESSION *session = 0;
    OC_NET_OPERATION *operation = 0;
    OC_NET_SESSION_DESC session_desc = {sizeof(session_desc), OC_NET_ABI_VERSION,
                                        65536u, 8u, 0u};
    OC_NET_CAPABILITIES capabilities = {sizeof(capabilities), 0u, 0u, 0u, 0u, 0u, 0u, {0u}};
    OC_NET_OPERATION_STATUS operation_status;
    OC_NET_STATE state = {sizeof(state), 0u, 0u, 0u, 0u, 0u, 0u};
    OC_NET_DNS_RESULT dns = {sizeof(dns), 0u, {0u}, 0u};
    OC_NET_DEADLINE deadline;
    OC_NET_TRANSFER_DESC transfer;
    uint8_t response[512];
    uint64_t now_usec = 0u;
    uint64_t operation_started;
    EFI_STATUS status;
    (void)image_handle;

    status = system_table->boot_services->locate_protocol(
        &OC_NET_PROTOCOL_GUID, 0, (void **)&net);
    if (EFI_ERROR(status) || net == 0 || net->abi_version != OC_NET_ABI_VERSION) goto fail;
    if (EFI_ERROR(net->query_capabilities(&capabilities)) ||
        (capabilities.capabilities & (OC_NET_CAP_DHCP4 | OC_NET_CAP_DNS4 |
          OC_NET_CAP_UDP4 | OC_NET_CAP_TCP4)) !=
        (OC_NET_CAP_DHCP4 | OC_NET_CAP_DNS4 | OC_NET_CAP_UDP4 | OC_NET_CAP_TCP4)) goto fail;
#if OPENCONSOLE_HTTPS_TEST
    if ((capabilities.capabilities & (OC_NET_CAP_HTTPS |
        OC_NET_CAP_TLS_IDENTITY_VALIDATION)) !=
        (OC_NET_CAP_HTTPS | OC_NET_CAP_TLS_IDENTITY_VALIDATION)) goto fail;
#endif
    if (EFI_ERROR(net->create_session(&session_desc, &session))) goto fail;

    operation_started = now_usec;
    deadline = (OC_NET_DEADLINE){sizeof(deadline), 0u, now_usec + 15000000u};
    status = net->configure_dhcp4(session, &deadline, &operation);
    if (EFI_ERROR(status)) {
        debug_write("OPENCONSOLE_NET_CHILD_DHCP_START_ERROR_");
        debug_hex32((uint32_t)status);
        goto fail;
    }
    status = await_operation(net, session, operation, system_table->boot_services,
                             &now_usec, &operation_status);
    if (EFI_ERROR(status)) {
        debug_write("OPENCONSOLE_NET_CHILD_DHCP_PUMP_ERROR_");
        debug_hex32((uint32_t)status);
        goto fail;
    }
    if (EFI_ERROR(release_expect(net, operation, &operation_status, OC_NET_RESULT_OK))) {
        debug_write("OPENCONSOLE_NET_CHILD_DHCP_RESULT_ERROR_");
        debug_hex32(operation_status.result);
        goto fail;
    }
    operation = 0;
    if (EFI_ERROR(net->query_state(session, &state)) || !state.configured ||
        state.address_be != EXPECTED_ADDRESS) goto fail;
    debug_write("OPENCONSOLE_NET_CHILD_DHCP_OK\n");
    debug_write("OPENCONSOLE_NET_CHILD_DHCP_USEC_");
    debug_hex32((uint32_t)(now_usec - operation_started));

#if OPENCONSOLE_HTTPS_TEST
    {
        static const char valid_url[] = "https://10.0.2.2:9443/update";
        static const char expired_url[] = "https://10.0.2.2:9444/update";
        static const char wrong_host_url[] = "https://10.0.2.2:9445/update";
        static const char expected[] = "openconsole-https-valid";
        const char *urls[3] = {valid_url, expired_url, wrong_host_url};
        UINTN url_sizes[3] = {sizeof(valid_url)-1u, sizeof(expired_url)-1u,
                              sizeof(wrong_host_url)-1u};
        UINTN test_index;
        for (test_index = 0u; test_index < 3u; ++test_index) {
            OC_NET_HTTPS_DESC https = {sizeof(https), 0u, urls[test_index],
                (uint32_t)url_sizes[test_index], 0, 0u, response, sizeof(response),
                {sizeof(OC_NET_DEADLINE), 0u, now_usec + 15000000u}};
            operation = 0;
            {
                UINTN mapping_attempt;
                for (mapping_attempt = 0u; mapping_attempt < 100u; ++mapping_attempt) {
                    status = net->start_https(session, &https, &operation);
                    if (status != EFI_NO_MAPPING) break;
                    now_usec += 100000u;
                    (void)net->pump(session, now_usec);
                    (void)system_table->boot_services->stall(100000u);
                }
            }
            if (EFI_ERROR(status)) {
                debug_write("OPENCONSOLE_HTTPS_START_ERROR_"); debug_hex32((uint32_t)status);
                goto fail;
            }
            status = await_operation(net, session, operation,
                system_table->boot_services, &now_usec, &operation_status);
            if (EFI_ERROR(status)) {
                debug_write("OPENCONSOLE_HTTPS_PUMP_ERROR_"); debug_hex32((uint32_t)status);
                goto fail;
            }
            debug_write("OPENCONSOLE_HTTPS_RESULT_"); debug_hex32(operation_status.result);
            if (test_index == 0u) {
                if (operation_status.result != OC_NET_RESULT_OK ||
                    operation_status.bytes_transferred != sizeof(expected)-1u ||
                    !bytes_equal(response, expected, sizeof(expected)-1u)) goto fail;
                debug_write("OPENCONSOLE_HTTPS_VALID_OK\n");
            } else {
                if (operation_status.result != OC_NET_RESULT_TLS_IDENTITY_REJECTED) {
                    debug_write("OPENCONSOLE_HTTPS_REJECTION_RESULT_");
                    debug_hex32(operation_status.result);
                    goto fail;
                }
                debug_write(test_index == 1u ? "OPENCONSOLE_HTTPS_EXPIRED_REJECTED\n" :
                    "OPENCONSOLE_HTTPS_WRONG_HOST_REJECTED\n");
            }
            if (EFI_ERROR(net->release_operation(operation))) goto fail;
            operation = 0;
        }
        if (EFI_ERROR(net->destroy_session(session))) goto fail;
        debug_write("OPENCONSOLE_NET_CHILD_COMPLETE\n");
        return EFI_SUCCESS;
    }
#endif

    operation_started = now_usec;
    deadline.deadline_usec = now_usec + 10000000u;
    status = net->resolve_dns4(session, fixture_name, sizeof(fixture_name) - 1u,
                              &dns, &deadline, &operation);
    if (EFI_ERROR(status)) {
        debug_write("OPENCONSOLE_NET_CHILD_DNS_START_ERROR_");
        debug_hex32((uint32_t)status);
        goto fail;
    }
    status = await_operation(net, session, operation, system_table->boot_services,
                             &now_usec, &operation_status);
    if (EFI_ERROR(status)) {
        debug_write("OPENCONSOLE_NET_CHILD_DNS_PUMP_ERROR_");
        debug_hex32((uint32_t)status);
        goto fail;
    }
    debug_write("OPENCONSOLE_NET_CHILD_DNS_RESULT_");
    debug_hex32(operation_status.result);
    debug_write("OPENCONSOLE_NET_CHILD_DNS_COUNT_");
    debug_hex32(dns.address_count);
    if (dns.address_count != 0u) {
        debug_write("OPENCONSOLE_NET_CHILD_DNS_ADDRESS_");
        debug_hex32(dns.addresses_be[0]);
    }
    if (EFI_ERROR(release_expect(net, operation, &operation_status, OC_NET_RESULT_OK)) ||
        dns.address_count == 0u ||
        dns.addresses_be[0] != ((uint32_t)127u | ((uint32_t)1u << 24))) goto fail;
    operation = 0;
    debug_write("OPENCONSOLE_NET_CHILD_DNS_OK\n");
    debug_write("OPENCONSOLE_NET_CHILD_DNS_USEC_");
    debug_hex32((uint32_t)(now_usec - operation_started));

    operation_started = now_usec;
    transfer = (OC_NET_TRANSFER_DESC){sizeof(transfer), FIXTURE_ADDRESS, 9000u, 0u,
        udp_request, sizeof(udp_request) - 1u, response, sizeof(response),
        {sizeof(OC_NET_DEADLINE), 0u, now_usec + 5000000u}};
    if (EFI_ERROR(net->start_udp4(session, &transfer, &operation)) ||
        EFI_ERROR(await_operation(net, session, operation, system_table->boot_services,
                                  &now_usec, &operation_status)) ||
        operation_status.bytes_transferred != sizeof(udp_response) - 1u ||
        !bytes_equal(response, udp_response, sizeof(udp_response) - 1u) ||
        EFI_ERROR(release_expect(net, operation, &operation_status, OC_NET_RESULT_OK))) goto fail;
    operation = 0;
    debug_write("OPENCONSOLE_NET_CHILD_UDP_OK\n");
    debug_write("OPENCONSOLE_NET_CHILD_UDP_USEC_");
    debug_hex32((uint32_t)(now_usec - operation_started));

    operation_started = now_usec;
    transfer = (OC_NET_TRANSFER_DESC){sizeof(transfer), FIXTURE_ADDRESS, 9001u, 0u,
        tcp_request, sizeof(tcp_request) - 1u, response, sizeof(response),
        {sizeof(OC_NET_DEADLINE), 0u, now_usec + 5000000u}};
    if (EFI_ERROR(net->start_tcp4(session, &transfer, &operation)) ||
        EFI_ERROR(await_operation(net, session, operation, system_table->boot_services,
                                  &now_usec, &operation_status)) ||
        operation_status.bytes_transferred < sizeof(tcp_response_prefix) - 1u ||
        !bytes_equal(response, tcp_response_prefix, sizeof(tcp_response_prefix) - 1u) ||
        EFI_ERROR(release_expect(net, operation, &operation_status, OC_NET_RESULT_OK))) goto fail;
    operation = 0;
    debug_write("OPENCONSOLE_NET_CHILD_HTTP_OK\n");
    debug_write("OPENCONSOLE_NET_CHILD_HTTP_USEC_");
    debug_hex32((uint32_t)(now_usec - operation_started));

    operation_started = now_usec;
    transfer = (OC_NET_TRANSFER_DESC){sizeof(transfer), FIXTURE_ADDRESS, 9000u, 0u,
        bounds_request, sizeof(bounds_request) - 1u, response, 1u,
        {sizeof(OC_NET_DEADLINE), 0u, now_usec + 5000000u}};
    if (EFI_ERROR(net->start_udp4(session, &transfer, &operation)) ||
        EFI_ERROR(await_operation(net, session, operation, system_table->boot_services,
                                  &now_usec, &operation_status)) ||
        operation_status.bytes_required <= 1u ||
        EFI_ERROR(release_expect(net, operation, &operation_status,
                                 OC_NET_RESULT_RESPONSE_TOO_LARGE))) goto fail;
    operation = 0;
    debug_write("OPENCONSOLE_NET_CHILD_BOUNDS_OK\n");
    debug_write("OPENCONSOLE_NET_CHILD_BOUNDS_USEC_");
    debug_hex32((uint32_t)(now_usec - operation_started));

    transfer = (OC_NET_TRANSFER_DESC){sizeof(transfer), FIXTURE_ADDRESS, 9999u, 0u,
        udp_request, sizeof(udp_request) - 1u, response, sizeof(response),
        {sizeof(OC_NET_DEADLINE), 0u, now_usec + 5000000u}};
    if (EFI_ERROR(net->start_udp4(session, &transfer, &operation)) ||
        EFI_ERROR(net->cancel_operation(operation))) goto fail;
    operation_status.structure_size = sizeof(operation_status);
    if (EFI_ERROR(net->query_operation(operation, &operation_status)) ||
        EFI_ERROR(release_expect(net, operation, &operation_status, OC_NET_RESULT_CANCELLED))) goto fail;
    operation = 0;
    debug_write("OPENCONSOLE_NET_CHILD_CANCEL_OK\n");

    operation_started = now_usec;
    transfer.deadline.deadline_usec = now_usec + 100000u;
    if (EFI_ERROR(net->start_udp4(session, &transfer, &operation)) ||
        EFI_ERROR(await_operation(net, session, operation, system_table->boot_services,
                                  &now_usec, &operation_status)) ||
        EFI_ERROR(release_expect(net, operation, &operation_status,
                                 OC_NET_RESULT_DEADLINE_EXCEEDED))) goto fail;
    operation = 0;
    debug_write("OPENCONSOLE_NET_CHILD_TIMEOUT_OK\n");
    debug_write("OPENCONSOLE_NET_CHILD_TIMEOUT_USEC_");
    debug_hex32((uint32_t)(now_usec - operation_started));

    if (EFI_ERROR(net->destroy_session(session))) goto fail;
    debug_write("OPENCONSOLE_NET_CHILD_COMPLETE\n");
    return EFI_SUCCESS;
fail:
    debug_write("OPENCONSOLE_NET_CHILD_FAILED\n");
    if (operation != 0) (void)net->release_operation(operation);
    if (session != 0) (void)net->destroy_session(session);
    return EFI_DEVICE_ERROR;
}
