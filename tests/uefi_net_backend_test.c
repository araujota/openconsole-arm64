#include "openconsole/uefi_net_backend.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static EFI_HANDLE handles[] = {(EFI_HANDLE)(UINTN)1u, (EFI_HANDLE)(UINTN)2u};
static OC_EFI_SIMPLE_NETWORK networks[2];
static OC_EFI_SIMPLE_NETWORK_MODE modes[2];
static OC_EFI_SERVICE_BINDING bindings[6];
static OC_EFI_DHCP4_PROTOCOL dhcp;
static OC_EFI_DNS4_PROTOCOL dns;
static OC_EFI_UDP4_PROTOCOL udp;
static OC_EFI_TCP4_PROTOCOL tcp;
static OC_EFI_HTTP_PROTOCOL http;
static OC_EFI_DNS_HOST_TO_ADDR_DATA dns_response;
static EFI_IPv4_ADDRESS dns_addresses[2];
static OC_EFI_UDP4_RECEIVE_DATA udp_receive_data;
static char udp_reply[] = "pong";
static OC_EFI_HTTP_HEADER http_headers[1];
static unsigned free_calls;
static unsigned create_child_calls;
static unsigned destroy_child_calls;
static unsigned close_event_calls;
static unsigned recycle_signal_calls;
static int http_malformed;
static int http_fin;
static int http_fin_active;
static int tcp_refuse;
static EFI_EVENT_NOTIFY event_notify;
static void *event_context;

static EFI_STATUS EFIAPI create_child(OC_EFI_SERVICE_BINDING *binding, EFI_HANDLE *child) {
    assert(binding == &bindings[0] || binding == &bindings[1] ||
           binding == &bindings[2] || binding == &bindings[3] ||
           binding == &bindings[4]);
    ++create_child_calls;
    *child = (EFI_HANDLE)(UINTN)(binding == &bindings[0] ? 99u :
                                 (binding == &bindings[1] ? 98u :
                                  (binding == &bindings[2] ? 97u :
                                   (binding == &bindings[3] ? 96u : 95u))));
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI destroy_child(OC_EFI_SERVICE_BINDING *binding, EFI_HANDLE child) {
    assert((binding == &bindings[0] && (UINTN)child == 99u) ||
           (binding == &bindings[1] && (UINTN)child == 98u) ||
           (binding == &bindings[2] && (UINTN)child == 97u) ||
           (binding == &bindings[3] && (UINTN)child == 96u) ||
           (binding == &bindings[4] && (UINTN)child == 95u));
    ++destroy_child_calls;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI signal_event(EFI_EVENT event) {
    assert((UINTN)event == 88u);
    ++recycle_signal_calls;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI create_event(uint32_t type, UINTN tpl, EFI_EVENT_NOTIFY notify,
                                      void *context, EFI_EVENT *event) {
    assert(type == 0x200u);
    assert(tpl == 8u);
    event_notify = notify;
    event_context = context;
    *event = (EFI_EVENT)(UINTN)77u;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI close_event(EFI_EVENT event) {
    assert((UINTN)event == 77u);
    ++close_event_calls;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI dhcp_get_mode(OC_EFI_DHCP4_PROTOCOL *self,
                                       OC_EFI_DHCP4_MODE_DATA *mode) {
    (void)self;
    memset(mode, 0, sizeof(*mode));
    mode->state = 4u;
    mode->client_address.addr[0] = 10u;
    mode->client_address.addr[3] = 2u;
    mode->subnet_mask.addr[0] = 255u;
    mode->subnet_mask.addr[1] = 255u;
    mode->subnet_mask.addr[2] = 255u;
    mode->router_address.addr[0] = 10u;
    mode->router_address.addr[3] = 1u;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI dhcp_configure(OC_EFI_DHCP4_PROTOCOL *self,
                                        OC_EFI_DHCP4_CONFIG_DATA *config) {
    (void)self; (void)config;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI dhcp_start(OC_EFI_DHCP4_PROTOCOL *self, EFI_EVENT event) {
    (void)self;
    assert((UINTN)event == 77u);
    event_notify(event, event_context);
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI dhcp_stop(OC_EFI_DHCP4_PROTOCOL *self) {
    (void)self;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI dns_configure(OC_EFI_DNS4_PROTOCOL *self,
                                       OC_EFI_DNS4_CONFIG_DATA *config) {
    (void)self; (void)config;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI dns_host_to_ip(OC_EFI_DNS4_PROTOCOL *self, CHAR16 *name,
                                        OC_EFI_DNS4_COMPLETION_TOKEN *token) {
    static const char expected[] = "fixture.test";
    UINTN i;
    (void)self;
    for (i = 0u; expected[i] != '\0'; ++i) assert(name[i] == (CHAR16)expected[i]);
    assert(name[i] == 0u);
    memset(&dns_response, 0, sizeof(dns_response));
    memset(dns_addresses, 0, sizeof(dns_addresses));
    dns_addresses[0].addr[0] = 10u;
    dns_addresses[0].addr[3] = 9u;
    dns_response.ip_count = 1u;
    dns_response.ip_list = dns_addresses;
    token->status = EFI_SUCCESS;
    token->response.h2a_data = &dns_response;
    event_notify(token->event, event_context);
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI dns_poll(OC_EFI_DNS4_PROTOCOL *self) {
    (void)self;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI dns_cancel(OC_EFI_DNS4_PROTOCOL *self,
                                    OC_EFI_DNS4_COMPLETION_TOKEN *token) {
    (void)self; (void)token;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI udp_configure(OC_EFI_UDP4_PROTOCOL *self,
                                       OC_EFI_UDP4_CONFIG_DATA *config) {
    (void)self;
    if (config != 0) {
        assert(config->use_default_address == 0u);
        assert(config->station_address.addr[0] == 10u);
        assert(config->station_address.addr[3] == 2u);
        assert(config->subnet_mask.addr[0] == 255u);
        assert(config->remote_address.addr[0] == 10u);
        assert(config->remote_address.addr[3] == 9u);
        assert(config->remote_port == 9000u);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI udp_transmit(OC_EFI_UDP4_PROTOCOL *self,
                                      OC_EFI_UDP4_COMPLETION_TOKEN *token) {
    (void)self;
    assert(token->packet.tx_data->data_length == 4u);
    assert(memcmp(token->packet.tx_data->fragment_table[0].fragment_buffer, "ping", 4u) == 0);
    token->status = EFI_SUCCESS;
    event_notify(token->event, event_context);
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI udp_receive(OC_EFI_UDP4_PROTOCOL *self,
                                     OC_EFI_UDP4_COMPLETION_TOKEN *token) {
    (void)self;
    memset(&udp_receive_data, 0, sizeof(udp_receive_data));
    udp_receive_data.recycle_signal = (EFI_EVENT)(UINTN)88u;
    udp_receive_data.data_length = 4u;
    udp_receive_data.fragment_count = 1u;
    udp_receive_data.fragment_table[0].fragment_length = 4u;
    udp_receive_data.fragment_table[0].fragment_buffer = udp_reply;
    token->packet.rx_data = &udp_receive_data;
    token->status = EFI_SUCCESS;
    event_notify(token->event, event_context);
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI udp_cancel(OC_EFI_UDP4_PROTOCOL *self,
                                    OC_EFI_UDP4_COMPLETION_TOKEN *token) {
    (void)self; (void)token;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI udp_poll(OC_EFI_UDP4_PROTOCOL *self) {
    (void)self;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI tcp_configure(OC_EFI_TCP4_PROTOCOL *self,
                                       OC_EFI_TCP4_CONFIG_DATA *config) {
    (void)self;
    if (config != 0) {
        assert(config->access_point.use_default_address == 0u);
        assert(config->access_point.station_address.addr[0] == 10u);
        assert(config->access_point.station_address.addr[3] == 2u);
        assert(config->access_point.subnet_mask.addr[0] == 255u);
        assert(config->access_point.active_flag == 1u);
        assert(config->access_point.remote_address.addr[0] == 10u);
        assert(config->access_point.remote_address.addr[3] == 9u);
        assert(config->access_point.remote_port == 9001u);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI tcp_connect(OC_EFI_TCP4_PROTOCOL *self,
                                     OC_EFI_TCP4_CONNECTION_TOKEN *token) {
    (void)self;
    token->completion.status = tcp_refuse ? EFI_CONNECTION_REFUSED : EFI_SUCCESS;
    event_notify(token->completion.event, event_context);
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI tcp_transmit(OC_EFI_TCP4_PROTOCOL *self,
                                      OC_EFI_TCP4_IO_TOKEN *token) {
    (void)self;
    assert(token->packet.tx_data->data_length == 5u);
    assert(memcmp(token->packet.tx_data->fragment_table[0].fragment_buffer, "hello", 5u) == 0);
    token->completion.status = EFI_SUCCESS;
    event_notify(token->completion.event, event_context);
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI tcp_receive(OC_EFI_TCP4_PROTOCOL *self,
                                     OC_EFI_TCP4_IO_TOKEN *token) {
    static const char reply[] = "stream";
    (void)self;
    assert(token->packet.rx_data->data_length >= sizeof(reply) - 1u);
    memcpy(token->packet.rx_data->fragment_table[0].fragment_buffer,
           reply, sizeof(reply) - 1u);
    token->packet.rx_data->data_length = sizeof(reply) - 1u;
    token->packet.rx_data->fragment_table[0].fragment_length = sizeof(reply) - 1u;
    token->completion.status = EFI_SUCCESS;
    event_notify(token->completion.event, event_context);
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI tcp_cancel(OC_EFI_TCP4_PROTOCOL *self,
                                    OC_EFI_TCP4_COMPLETION_TOKEN *token) {
    (void)self; (void)token;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI tcp_poll(OC_EFI_TCP4_PROTOCOL *self) {
    (void)self;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI http_configure(OC_EFI_HTTP_PROTOCOL *self,
                                        OC_EFI_HTTP_CONFIG_DATA *config) {
    (void)self;
    if (config != 0) {
        assert(config->http_version == 1u);
        assert(config->local_address_is_ipv6 == 0u);
        assert(config->access_point.ipv4->use_default_address == 0u);
        assert(config->access_point.ipv4->local_address.addr[0] == 10u);
        assert(config->access_point.ipv4->local_address.addr[1] == 0u);
        assert(config->access_point.ipv4->local_address.addr[2] == 0u);
        assert(config->access_point.ipv4->local_address.addr[3] == 2u);
        assert(config->access_point.ipv4->local_subnet.addr[0] == 255u);
        assert(config->access_point.ipv4->local_subnet.addr[1] == 255u);
        assert(config->access_point.ipv4->local_subnet.addr[2] == 255u);
        assert(config->access_point.ipv4->local_subnet.addr[3] == 0u);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI http_request(OC_EFI_HTTP_PROTOCOL *self,
                                      OC_EFI_HTTP_TOKEN *token) {
    static const char expected[] = "https://fixture.test/data";
    UINTN i;
    (void)self;
    assert(token->message->data.request->method == 0u);
    for (i = 0u; expected[i] != '\0'; ++i)
        assert(token->message->data.request->url[i] == (CHAR16)expected[i]);
    assert(token->message->data.request->url[i] == 0u);
    assert(token->message->header_count == 1u);
    assert(strcmp(token->message->headers[0].field_name, "Host") == 0);
    assert(strcmp(token->message->headers[0].field_value, "fixture.test") == 0);
    token->status = EFI_SUCCESS;
    event_notify(token->event, event_context);
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI http_response(OC_EFI_HTTP_PROTOCOL *self,
                                       OC_EFI_HTTP_TOKEN *token) {
    (void)self;
    assert(token->message->body_length >= 5u);
    memcpy(token->message->body, "https", 5u);
    http_headers[0].field_name = "Content-Length";
    http_headers[0].field_value = http_malformed ? "five" : "5";
    token->message->headers = http_headers;
    token->message->header_count = 1u;
    token->message->body_length = 5u;
    token->message->data.response->status_code = 3u;
    token->status = http_fin ? EFI_CONNECTION_FIN : EFI_SUCCESS;
    http_fin_active = http_fin;
    event_notify(token->event, event_context);
    return http_fin ? EFI_CONNECTION_FIN : EFI_SUCCESS;
}

static EFI_STATUS EFIAPI http_cancel(OC_EFI_HTTP_PROTOCOL *self,
                                     OC_EFI_HTTP_TOKEN *token) {
    (void)self; (void)token;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI http_poll(OC_EFI_HTTP_PROTOCOL *self) {
    (void)self;
    return http_fin_active ? EFI_CONNECTION_FIN : EFI_SUCCESS;
}

_Static_assert(offsetof(OC_EFI_SIMPLE_NETWORK_MODE, media_present) == 652u,
               "SNP mode ABI drift");
_Static_assert(offsetof(OC_EFI_SIMPLE_NETWORK, mode) == 120u,
               "SNP protocol ABI drift");

static EFI_STATUS EFIAPI free_pool(void *memory) {
    assert(memory == handles || memory == &dns_response || memory == dns_addresses ||
           memory == http_headers);
    ++free_calls;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI locate_handles(uint32_t type, const EFI_GUID *guid,
                                        void *key, UINTN *count, EFI_HANDLE **out) {
    (void)key;
    assert(type == 2u);
    assert(guid->data1 == 0xa19832b9u);
    *count = 2u;
    *out = handles;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI handle_protocol(EFI_HANDLE handle, const EFI_GUID *guid,
                                         void **out) {
    if ((UINTN)handle == 99u && guid->data1 == 0x8a219718u) {
        *out = &dhcp;
        return EFI_SUCCESS;
    }
    if ((UINTN)handle == 98u && guid->data1 == 0xae3d28ccu) {
        *out = &dns;
        return EFI_SUCCESS;
    }
    if ((UINTN)handle == 97u && guid->data1 == 0x3ad9df29u) {
        *out = &udp;
        return EFI_SUCCESS;
    }
    if ((UINTN)handle == 96u && guid->data1 == 0x65530bc7u) {
        *out = &tcp;
        return EFI_SUCCESS;
    }
    if ((UINTN)handle == 95u && guid->data1 == 0x7a59b29bu) {
        *out = &http;
        return EFI_SUCCESS;
    }
    if (guid->data1 == 0xa19832b9u) {
        *out = &networks[(UINTN)handle - 1u];
        return EFI_SUCCESS;
    }
    if (handle != handles[1]) return EFI_NOT_FOUND;
    if (guid->data1 == 0x9d9a39d8u) *out = &bindings[0];
    else if (guid->data1 == 0xb625b186u) *out = &bindings[1];
    else if (guid->data1 == 0x83f01464u) *out = &bindings[2];
    else if (guid->data1 == 0x00720665u) *out = &bindings[3];
    else if (guid->data1 == 0xbdc8e6afu) *out = &bindings[4];
    else if (guid->data1 == 0x952cb795u) *out = &bindings[5];
    else return EFI_NOT_FOUND;
    return EFI_SUCCESS;
}

int main(void) {
    EFI_BOOT_SERVICES services;
    oc_uefi_net_backend backend;
    oc_net_backend platform;
    OC_NET_STATE state;
    OC_NET_DNS_RESULT dns_result;
    oc_net_backend_request request;
    oc_net_backend_progress progress;
    memset(&services, 0, sizeof(services));
    memset(networks, 0, sizeof(networks));
    memset(modes, 0, sizeof(modes));
    modes[0].media_present_supported = 1u;
    modes[0].media_present = 0u;
    modes[1].media_present_supported = 1u;
    modes[1].media_present = 1u;
    networks[0].mode = &modes[0];
    networks[1].mode = &modes[1];
    services.free_pool = free_pool;
    services.create_event = create_event;
    services.close_event = close_event;
    services.signal_event = signal_event;
    services.locate_handle_buffer = locate_handles;
    services.handle_protocol = handle_protocol;
    bindings[0].create_child = create_child;
    bindings[0].destroy_child = destroy_child;
    bindings[1].create_child = create_child;
    bindings[1].destroy_child = destroy_child;
    bindings[2].create_child = create_child;
    bindings[2].destroy_child = destroy_child;
    bindings[3].create_child = create_child;
    bindings[3].destroy_child = destroy_child;
    bindings[4].create_child = create_child;
    bindings[4].destroy_child = destroy_child;
    memset(&dhcp, 0, sizeof(dhcp));
    dhcp.get_mode_data = dhcp_get_mode;
    dhcp.configure = dhcp_configure;
    dhcp.start = dhcp_start;
    dhcp.stop = dhcp_stop;
    memset(&dns, 0, sizeof(dns));
    dns.configure = dns_configure;
    dns.host_name_to_ip = dns_host_to_ip;
    dns.poll = dns_poll;
    dns.cancel = dns_cancel;
    memset(&udp, 0, sizeof(udp));
    udp.configure = udp_configure;
    udp.transmit = udp_transmit;
    udp.receive = udp_receive;
    udp.cancel = udp_cancel;
    udp.poll = udp_poll;
    memset(&tcp, 0, sizeof(tcp));
    tcp.configure = tcp_configure;
    tcp.connect = tcp_connect;
    tcp.transmit = tcp_transmit;
    tcp.receive = tcp_receive;
    tcp.cancel = tcp_cancel;
    tcp.poll = tcp_poll;
    memset(&http, 0, sizeof(http));
    http.configure = http_configure;
    http.request = http_request;
    http.response = http_response;
    http.cancel = http_cancel;
    http.poll = http_poll;
    assert(oc_uefi_net_backend_initialize(&backend, &services) == EFI_SUCCESS);
    assert(free_calls == 1u);
    assert(backend.controller == handles[1]);
    assert(backend.snp == &networks[1]);
    assert(backend.capabilities == (OC_NET_CAP_IPV4 | OC_NET_CAP_DHCP4 |
        OC_NET_CAP_DNS4 | OC_NET_CAP_UDP4 | OC_NET_CAP_TCP4 |
        OC_NET_CAP_HTTPS | OC_NET_CAP_TLS_IDENTITY_VALIDATION));
    oc_uefi_net_backend_get_platform(&backend, &platform);
    memset(&state, 0, sizeof(state));
    state.structure_size = sizeof(state);
    assert(platform.query_state(platform.context, &state) == EFI_SUCCESS);
    assert(state.link_up == 1u);
    memset(&request, 0, sizeof(request));
    request.kind = OC_NET_BACKEND_DHCP4;
    request.deadline_usec = 100u;
    assert(platform.start(platform.context, 5u, &request) == EFI_SUCCESS);
    memset(&progress, 0, sizeof(progress));
    assert(platform.pump(platform.context, 5u, &request, &progress) == EFI_SUCCESS);
    assert(progress.result == OC_NET_RESULT_OK);
    state.structure_size = sizeof(state);
    assert(platform.query_state(platform.context, &state) == EFI_SUCCESS);
    assert(state.configured == 1u);
    assert(state.address_be == 0x0200000au);
    platform.release(platform.context, 5u);
    assert(create_child_calls == 1u);
    assert(destroy_child_calls == 1u);
    assert(close_event_calls == 1u);
    memset(&request, 0, sizeof(request));
    memset(&dns_result, 0, sizeof(dns_result));
    dns_result.structure_size = sizeof(dns_result);
    request.kind = OC_NET_BACKEND_DNS4;
    request.name = "fixture.test";
    request.name_size = 12u;
    request.response_bytes = &dns_result;
    request.response_capacity = sizeof(dns_result);
    request.deadline_usec = 200u;
    assert(platform.start(platform.context, 6u, &request) == EFI_SUCCESS);
    memset(&progress, 0, sizeof(progress));
    assert(platform.pump(platform.context, 6u, &request, &progress) == EFI_SUCCESS);
    assert(progress.result == OC_NET_RESULT_OK);
    assert(dns_result.address_count == 1u);
    assert(dns_result.addresses_be[0] == 0x0900000au);
    platform.release(platform.context, 6u);
    assert(create_child_calls == 2u);
    assert(destroy_child_calls == 2u);
    assert(close_event_calls == 2u);
    assert(free_calls == 3u);
    {
        char udp_response[8] = {0};
        memset(&request, 0, sizeof(request));
        request.kind = OC_NET_BACKEND_UDP4;
        request.address_be = 0x0900000au;
        request.port = 9000u;
        request.request_bytes = "ping";
        request.request_size = 4u;
        request.response_bytes = udp_response;
        request.response_capacity = sizeof(udp_response);
        request.deadline_usec = 300u;
        assert(platform.start(platform.context, 7u, &request) == EFI_SUCCESS);
        memset(&progress, 0, sizeof(progress));
        assert(platform.pump(platform.context, 7u, &request, &progress) == EFI_SUCCESS);
        assert(progress.result == OC_NET_RESULT_PENDING);
        assert(platform.pump(platform.context, 7u, &request, &progress) == EFI_SUCCESS);
        assert(progress.result == OC_NET_RESULT_OK);
        assert(progress.bytes_transferred == 4u);
        assert(memcmp(udp_response, "pong", 4u) == 0);
        platform.release(platform.context, 7u);
    }
    assert(create_child_calls == 3u);
    assert(destroy_child_calls == 3u);
    assert(close_event_calls == 4u);
    assert(recycle_signal_calls == 1u);
    {
        char tcp_response[16] = {0};
        unsigned step;
        memset(&request, 0, sizeof(request));
        request.kind = OC_NET_BACKEND_TCP4;
        request.address_be = 0x0900000au;
        request.port = 9001u;
        request.request_bytes = "hello";
        request.request_size = 5u;
        request.response_bytes = tcp_response;
        request.response_capacity = sizeof(tcp_response);
        request.deadline_usec = 400u;
        assert(platform.start(platform.context, 8u, &request) == EFI_SUCCESS);
        for (step = 0u; step < 3u; ++step) {
            memset(&progress, 0, sizeof(progress));
            assert(platform.pump(platform.context, 8u, &request, &progress) == EFI_SUCCESS);
        }
        assert(progress.result == OC_NET_RESULT_OK);
        assert(progress.bytes_transferred == 6u);
        assert(memcmp(tcp_response, "stream", 6u) == 0);
        platform.release(platform.context, 8u);
    }
    assert(create_child_calls == 4u);
    assert(destroy_child_calls == 4u);
    assert(close_event_calls == 7u);
    {
        char http_body[16] = {0};
        http_fin = 1;
        memset(&request, 0, sizeof(request));
        request.kind = OC_NET_BACKEND_HTTPS;
        request.name = "https://fixture.test/data";
        request.name_size = 25u;
        request.response_bytes = http_body;
        request.response_capacity = sizeof(http_body);
        request.deadline_usec = 500u;
        assert(platform.start(platform.context, 9u, &request) == EFI_SUCCESS);
        memset(&progress, 0, sizeof(progress));
        assert(platform.pump(platform.context, 9u, &request, &progress) == EFI_SUCCESS);
        assert(progress.result == OC_NET_RESULT_PENDING);
        assert(platform.pump(platform.context, 9u, &request, &progress) == EFI_SUCCESS);
        assert(progress.result == OC_NET_RESULT_OK);
        assert(progress.bytes_transferred == 5u);
        assert(memcmp(http_body, "https", 5u) == 0);
        platform.release(platform.context, 9u);
        http_fin = 0;
        http_fin_active = 0;
    }
    assert(create_child_calls == 5u);
    assert(destroy_child_calls == 5u);
    assert(close_event_calls == 9u);
    assert(free_calls == 4u);
    {
        char body[16] = {0};
        http_malformed = 1;
        memset(&request, 0, sizeof(request));
        request.kind = OC_NET_BACKEND_HTTPS;
        request.name = "https://fixture.test/data";
        request.name_size = 25u;
        request.response_bytes = body;
        request.response_capacity = sizeof(body);
        request.deadline_usec = 600u;
        assert(platform.start(platform.context, 10u, &request) == EFI_SUCCESS);
        memset(&progress, 0, sizeof(progress));
        assert(platform.pump(platform.context, 10u, &request, &progress) == EFI_SUCCESS);
        assert(platform.pump(platform.context, 10u, &request, &progress) == EFI_SUCCESS);
        assert(progress.result == OC_NET_RESULT_MALFORMED_RESPONSE);
        platform.release(platform.context, 10u);
        http_malformed = 0;
    }
    assert(create_child_calls == 6u);
    assert(destroy_child_calls == 6u);
    assert(close_event_calls == 11u);
    assert(free_calls == 5u);
    {
        char body[4] = {0};
        tcp_refuse = 1;
        memset(&request, 0, sizeof(request));
        request.kind = OC_NET_BACKEND_TCP4;
        request.address_be = 0x0900000au;
        request.port = 9001u;
        request.response_bytes = body;
        request.response_capacity = sizeof(body);
        request.deadline_usec = 700u;
        assert(platform.start(platform.context, 11u, &request) == EFI_SUCCESS);
        memset(&progress, 0, sizeof(progress));
        assert(platform.pump(platform.context, 11u, &request, &progress) == EFI_SUCCESS);
        assert(progress.result == OC_NET_RESULT_CONNECTION_REFUSED);
        platform.release(platform.context, 11u);
        tcp_refuse = 0;
    }
    assert(create_child_calls == 7u);
    assert(destroy_child_calls == 7u);
    assert(close_event_calls == 12u);
    puts("PASS: linked SNP controller and colocated UEFI network stack discovery");
    return 0;
}
