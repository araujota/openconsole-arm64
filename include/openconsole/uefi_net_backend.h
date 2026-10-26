#ifndef OPENCONSOLE_UEFI_NET_BACKEND_H
#define OPENCONSOLE_UEFI_NET_BACKEND_H

#include "openconsole/net_provider.h"

typedef struct OC_EFI_SERVICE_BINDING OC_EFI_SERVICE_BINDING;
typedef EFI_STATUS(EFIAPI *OC_EFI_CREATE_CHILD)(OC_EFI_SERVICE_BINDING *, EFI_HANDLE *);
typedef EFI_STATUS(EFIAPI *OC_EFI_DESTROY_CHILD)(OC_EFI_SERVICE_BINDING *, EFI_HANDLE);
struct OC_EFI_SERVICE_BINDING {
    OC_EFI_CREATE_CHILD create_child;
    OC_EFI_DESTROY_CHILD destroy_child;
};

/* Layout from UEFI 2.11 Simple Network Protocol. These declarations are kept
 * local so the launcher remains freestanding rather than depending on EDK II. */
typedef struct {
    uint32_t state;
    uint32_t hw_address_size;
    uint32_t media_header_size;
    uint32_t max_packet_size;
    uint32_t nvram_size;
    uint32_t nvram_access_size;
    uint32_t receive_filter_mask;
    uint32_t receive_filter_setting;
    uint32_t max_mcast_filter_count;
    uint32_t mcast_filter_count;
    EFI_MAC_ADDRESS mcast_filter[16];
    EFI_MAC_ADDRESS current_address;
    EFI_MAC_ADDRESS broadcast_address;
    EFI_MAC_ADDRESS permanent_address;
    uint8_t if_type;
    BOOLEAN mac_address_changeable;
    BOOLEAN multiple_tx_supported;
    BOOLEAN media_present_supported;
    BOOLEAN media_present;
} OC_EFI_SIMPLE_NETWORK_MODE;

typedef struct {
    uint64_t revision;
    void *start;
    void *stop;
    void *initialize;
    void *reset;
    void *shutdown;
    void *receive_filters;
    void *station_address;
    void *statistics;
    void *mcast_ip_to_mac;
    void *nvdata;
    void *get_status;
    void *transmit;
    void *receive;
    EFI_EVENT wait_for_packet;
    OC_EFI_SIMPLE_NETWORK_MODE *mode;
} OC_EFI_SIMPLE_NETWORK;

typedef struct OC_EFI_DHCP4_PROTOCOL OC_EFI_DHCP4_PROTOCOL;
typedef struct {
    uint32_t size;
    uint32_t length;
    uint8_t packet[1];
} OC_EFI_DHCP4_PACKET;
typedef struct {
    uint32_t discover_try_count;
    uint32_t *discover_timeout;
    uint32_t request_try_count;
    uint32_t *request_timeout;
    EFI_IPv4_ADDRESS client_address;
    void *callback;
    void *callback_context;
    uint32_t option_count;
    void *option_list;
} OC_EFI_DHCP4_CONFIG_DATA;

typedef struct {
    uint32_t state;
    OC_EFI_DHCP4_CONFIG_DATA config_data;
    EFI_IPv4_ADDRESS client_address;
    EFI_MAC_ADDRESS client_mac_address;
    EFI_IPv4_ADDRESS server_address;
    EFI_IPv4_ADDRESS router_address;
    EFI_IPv4_ADDRESS subnet_mask;
    uint32_t lease_time;
    OC_EFI_DHCP4_PACKET *reply_packet;
} OC_EFI_DHCP4_MODE_DATA;

typedef EFI_STATUS(EFIAPI *OC_EFI_DHCP4_GET_MODE_DATA)(OC_EFI_DHCP4_PROTOCOL *,
                                                        OC_EFI_DHCP4_MODE_DATA *);
typedef EFI_STATUS(EFIAPI *OC_EFI_DHCP4_CONFIGURE)(OC_EFI_DHCP4_PROTOCOL *,
                                                    OC_EFI_DHCP4_CONFIG_DATA *);
typedef EFI_STATUS(EFIAPI *OC_EFI_DHCP4_START)(OC_EFI_DHCP4_PROTOCOL *, EFI_EVENT);
typedef EFI_STATUS(EFIAPI *OC_EFI_DHCP4_STOP)(OC_EFI_DHCP4_PROTOCOL *);
struct OC_EFI_DHCP4_PROTOCOL {
    OC_EFI_DHCP4_GET_MODE_DATA get_mode_data;
    OC_EFI_DHCP4_CONFIGURE configure;
    OC_EFI_DHCP4_START start;
    void *renew_rebind;
    void *release;
    OC_EFI_DHCP4_STOP stop;
    void *build;
    void *transmit_receive;
    void *parse;
};

typedef struct OC_EFI_DNS4_PROTOCOL OC_EFI_DNS4_PROTOCOL;
typedef struct {
    UINTN dns_server_list_count;
    EFI_IPv4_ADDRESS *dns_server_list;
    BOOLEAN use_default_setting;
    BOOLEAN enable_dns_cache;
    uint8_t protocol;
    EFI_IPv4_ADDRESS station_ip;
    EFI_IPv4_ADDRESS subnet_mask;
    uint16_t local_port;
    uint32_t retry_count;
    uint32_t retry_interval;
} OC_EFI_DNS4_CONFIG_DATA;

typedef struct {
    uint32_t ip_count;
    EFI_IPv4_ADDRESS *ip_list;
} OC_EFI_DNS_HOST_TO_ADDR_DATA;

typedef struct {
    EFI_EVENT event;
    EFI_STATUS status;
    uint32_t retry_count;
    uint32_t retry_interval;
    union {
        OC_EFI_DNS_HOST_TO_ADDR_DATA *h2a_data;
        void *other;
    } response;
} OC_EFI_DNS4_COMPLETION_TOKEN;

typedef EFI_STATUS(EFIAPI *OC_EFI_DNS4_CONFIGURE)(OC_EFI_DNS4_PROTOCOL *,
                                                   OC_EFI_DNS4_CONFIG_DATA *);
typedef EFI_STATUS(EFIAPI *OC_EFI_DNS4_HOST_NAME_TO_IP)(
    OC_EFI_DNS4_PROTOCOL *, CHAR16 *, OC_EFI_DNS4_COMPLETION_TOKEN *);
typedef EFI_STATUS(EFIAPI *OC_EFI_DNS4_POLL)(OC_EFI_DNS4_PROTOCOL *);
typedef EFI_STATUS(EFIAPI *OC_EFI_DNS4_CANCEL)(OC_EFI_DNS4_PROTOCOL *,
                                               OC_EFI_DNS4_COMPLETION_TOKEN *);
struct OC_EFI_DNS4_PROTOCOL {
    void *get_mode_data;
    OC_EFI_DNS4_CONFIGURE configure;
    OC_EFI_DNS4_HOST_NAME_TO_IP host_name_to_ip;
    void *ip_to_host_name;
    void *general_lookup;
    void *update_dns_cache;
    OC_EFI_DNS4_POLL poll;
    OC_EFI_DNS4_CANCEL cancel;
};

typedef struct OC_EFI_UDP4_PROTOCOL OC_EFI_UDP4_PROTOCOL;
typedef struct {
    BOOLEAN accept_broadcast;
    BOOLEAN accept_promiscuous;
    BOOLEAN accept_any_port;
    BOOLEAN allow_duplicate_port;
    uint8_t type_of_service;
    uint8_t time_to_live;
    BOOLEAN do_not_fragment;
    uint32_t receive_timeout;
    uint32_t transmit_timeout;
    BOOLEAN use_default_address;
    EFI_IPv4_ADDRESS station_address;
    EFI_IPv4_ADDRESS subnet_mask;
    uint16_t station_port;
    EFI_IPv4_ADDRESS remote_address;
    uint16_t remote_port;
} OC_EFI_UDP4_CONFIG_DATA;

typedef struct {
    uint32_t fragment_length;
    void *fragment_buffer;
} OC_EFI_UDP4_FRAGMENT_DATA;

typedef struct {
    void *udp_session_data;
    EFI_IPv4_ADDRESS *gateway_address;
    uint32_t data_length;
    uint32_t fragment_count;
    OC_EFI_UDP4_FRAGMENT_DATA fragment_table[1];
} OC_EFI_UDP4_TRANSMIT_DATA;

typedef struct {
    EFI_TIME timestamp;
    EFI_EVENT recycle_signal;
    uint8_t udp_session[12];
    uint32_t data_length;
    uint32_t fragment_count;
    OC_EFI_UDP4_FRAGMENT_DATA fragment_table[1];
} OC_EFI_UDP4_RECEIVE_DATA;

typedef struct {
    EFI_EVENT event;
    EFI_STATUS status;
    union {
        OC_EFI_UDP4_RECEIVE_DATA *rx_data;
        OC_EFI_UDP4_TRANSMIT_DATA *tx_data;
    } packet;
} OC_EFI_UDP4_COMPLETION_TOKEN;

typedef EFI_STATUS(EFIAPI *OC_EFI_UDP4_CONFIGURE)(OC_EFI_UDP4_PROTOCOL *,
                                                   OC_EFI_UDP4_CONFIG_DATA *);
typedef EFI_STATUS(EFIAPI *OC_EFI_UDP4_IO)(OC_EFI_UDP4_PROTOCOL *,
                                           OC_EFI_UDP4_COMPLETION_TOKEN *);
typedef EFI_STATUS(EFIAPI *OC_EFI_UDP4_CANCEL)(OC_EFI_UDP4_PROTOCOL *,
                                               OC_EFI_UDP4_COMPLETION_TOKEN *);
typedef EFI_STATUS(EFIAPI *OC_EFI_UDP4_POLL)(OC_EFI_UDP4_PROTOCOL *);
struct OC_EFI_UDP4_PROTOCOL {
    void *get_mode_data;
    OC_EFI_UDP4_CONFIGURE configure;
    void *groups;
    void *routes;
    OC_EFI_UDP4_IO transmit;
    OC_EFI_UDP4_IO receive;
    OC_EFI_UDP4_CANCEL cancel;
    OC_EFI_UDP4_POLL poll;
};

typedef struct OC_EFI_TCP4_PROTOCOL OC_EFI_TCP4_PROTOCOL;
typedef struct {
    BOOLEAN use_default_address;
    EFI_IPv4_ADDRESS station_address;
    EFI_IPv4_ADDRESS subnet_mask;
    uint16_t station_port;
    EFI_IPv4_ADDRESS remote_address;
    uint16_t remote_port;
    BOOLEAN active_flag;
} OC_EFI_TCP4_ACCESS_POINT;
typedef struct {
    uint8_t type_of_service;
    uint8_t time_to_live;
    OC_EFI_TCP4_ACCESS_POINT access_point;
    void *control_option;
} OC_EFI_TCP4_CONFIG_DATA;
typedef struct { EFI_EVENT event; EFI_STATUS status; } OC_EFI_TCP4_COMPLETION_TOKEN;
typedef struct { OC_EFI_TCP4_COMPLETION_TOKEN completion; } OC_EFI_TCP4_CONNECTION_TOKEN;
typedef struct { uint32_t fragment_length; void *fragment_buffer; } OC_EFI_TCP4_FRAGMENT_DATA;
typedef struct {
    BOOLEAN urgent_flag;
    uint32_t data_length;
    uint32_t fragment_count;
    OC_EFI_TCP4_FRAGMENT_DATA fragment_table[1];
} OC_EFI_TCP4_RECEIVE_DATA;
typedef struct {
    BOOLEAN push;
    BOOLEAN urgent;
    uint32_t data_length;
    uint32_t fragment_count;
    OC_EFI_TCP4_FRAGMENT_DATA fragment_table[1];
} OC_EFI_TCP4_TRANSMIT_DATA;
typedef struct {
    OC_EFI_TCP4_COMPLETION_TOKEN completion;
    union {
        OC_EFI_TCP4_RECEIVE_DATA *rx_data;
        OC_EFI_TCP4_TRANSMIT_DATA *tx_data;
    } packet;
} OC_EFI_TCP4_IO_TOKEN;
typedef EFI_STATUS(EFIAPI *OC_EFI_TCP4_CONFIGURE)(OC_EFI_TCP4_PROTOCOL *,
                                                   OC_EFI_TCP4_CONFIG_DATA *);
typedef EFI_STATUS(EFIAPI *OC_EFI_TCP4_CONNECT)(OC_EFI_TCP4_PROTOCOL *,
                                                 OC_EFI_TCP4_CONNECTION_TOKEN *);
typedef EFI_STATUS(EFIAPI *OC_EFI_TCP4_IO)(OC_EFI_TCP4_PROTOCOL *, OC_EFI_TCP4_IO_TOKEN *);
typedef EFI_STATUS(EFIAPI *OC_EFI_TCP4_CANCEL)(OC_EFI_TCP4_PROTOCOL *,
                                                OC_EFI_TCP4_COMPLETION_TOKEN *);
typedef EFI_STATUS(EFIAPI *OC_EFI_TCP4_POLL)(OC_EFI_TCP4_PROTOCOL *);
struct OC_EFI_TCP4_PROTOCOL {
    void *get_mode_data;
    OC_EFI_TCP4_CONFIGURE configure;
    void *routes;
    OC_EFI_TCP4_CONNECT connect;
    void *accept;
    OC_EFI_TCP4_IO transmit;
    OC_EFI_TCP4_IO receive;
    void *close;
    OC_EFI_TCP4_CANCEL cancel;
    OC_EFI_TCP4_POLL poll;
};

typedef struct OC_EFI_HTTP_PROTOCOL OC_EFI_HTTP_PROTOCOL;
typedef struct {
    BOOLEAN use_default_address;
    EFI_IPv4_ADDRESS local_address;
    EFI_IPv4_ADDRESS local_subnet;
    uint16_t local_port;
} OC_EFI_HTTP4_ACCESS_POINT;
typedef struct {
    uint32_t http_version;
    uint32_t timeout_millisec;
    BOOLEAN local_address_is_ipv6;
    union { OC_EFI_HTTP4_ACCESS_POINT *ipv4; void *ipv6; } access_point;
} OC_EFI_HTTP_CONFIG_DATA;
typedef struct { uint32_t method; CHAR16 *url; } OC_EFI_HTTP_REQUEST_DATA;
typedef struct { uint32_t status_code; } OC_EFI_HTTP_RESPONSE_DATA;
typedef struct { char *field_name; char *field_value; } OC_EFI_HTTP_HEADER;
typedef struct {
    union { OC_EFI_HTTP_REQUEST_DATA *request; OC_EFI_HTTP_RESPONSE_DATA *response; } data;
    UINTN header_count;
    OC_EFI_HTTP_HEADER *headers;
    UINTN body_length;
    void *body;
} OC_EFI_HTTP_MESSAGE;
typedef struct { EFI_EVENT event; EFI_STATUS status; OC_EFI_HTTP_MESSAGE *message; }
    OC_EFI_HTTP_TOKEN;
typedef EFI_STATUS(EFIAPI *OC_EFI_HTTP_CONFIGURE)(OC_EFI_HTTP_PROTOCOL *,
                                                   OC_EFI_HTTP_CONFIG_DATA *);
typedef EFI_STATUS(EFIAPI *OC_EFI_HTTP_IO)(OC_EFI_HTTP_PROTOCOL *, OC_EFI_HTTP_TOKEN *);
typedef EFI_STATUS(EFIAPI *OC_EFI_HTTP_CANCEL)(OC_EFI_HTTP_PROTOCOL *, OC_EFI_HTTP_TOKEN *);
typedef EFI_STATUS(EFIAPI *OC_EFI_HTTP_POLL)(OC_EFI_HTTP_PROTOCOL *);
struct OC_EFI_HTTP_PROTOCOL {
    void *get_mode_data;
    OC_EFI_HTTP_CONFIGURE configure;
    OC_EFI_HTTP_IO request;
    OC_EFI_HTTP_CANCEL cancel;
    OC_EFI_HTTP_IO response;
    OC_EFI_HTTP_POLL poll;
};

typedef struct {
    EFI_GUID service_binding_guid;
    EFI_GUID protocol_guid;
    OC_EFI_SERVICE_BINDING *binding;
} oc_uefi_net_service;

typedef struct {
    uint8_t active;
    uint8_t completed;
    uint8_t configured;
    uint8_t reserved;
    oc_net_backend_kind kind;
    OC_EFI_SERVICE_BINDING *binding;
    EFI_HANDLE child;
    void *protocol;
    EFI_EVENT event;
    CHAR16 dns_name[OC_NET_MAX_HOST_BYTES + 1u];
    EFI_IPv4_ADDRESS dns_server;
    OC_EFI_DNS4_COMPLETION_TOKEN dns_token;
    uint8_t phase;
    OC_EFI_UDP4_COMPLETION_TOKEN udp_token;
    OC_EFI_UDP4_TRANSMIT_DATA udp_tx;
    OC_EFI_TCP4_CONNECTION_TOKEN tcp_connect;
    OC_EFI_TCP4_IO_TOKEN tcp_io;
    OC_EFI_TCP4_TRANSMIT_DATA tcp_tx;
    OC_EFI_TCP4_RECEIVE_DATA tcp_rx;
    CHAR16 http_url[OC_NET_MAX_URL_BYTES + 1u];
    OC_EFI_HTTP_TOKEN http_token;
    OC_EFI_HTTP_MESSAGE http_message;
    OC_EFI_HTTP_HEADER http_request_header;
    OC_EFI_HTTP_REQUEST_DATA http_request;
    OC_EFI_HTTP_RESPONSE_DATA http_response;
    uint64_t http_received;
    uint64_t http_expected;
    uint8_t http_expected_known;
    uint8_t http_chunked;
} oc_uefi_net_operation;

typedef struct {
    EFI_BOOT_SERVICES *boot_services;
    EFI_HANDLE controller;
    OC_EFI_SIMPLE_NETWORK *snp;
    oc_uefi_net_service dhcp4;
    oc_uefi_net_service dns4;
    oc_uefi_net_service udp4;
    oc_uefi_net_service tcp4;
    oc_uefi_net_service http;
    oc_uefi_net_service tls;
    uint32_t capabilities;
    uint32_t address_be;
    uint32_t subnet_mask_be;
    uint32_t gateway_be;
    uint32_t dns_server_be;
    oc_uefi_net_operation operations[OC_NET_MAX_SESSIONS * OC_NET_MAX_OPERATIONS_PER_SESSION];
} oc_uefi_net_backend;

EFI_STATUS oc_uefi_net_backend_initialize(oc_uefi_net_backend *, EFI_BOOT_SERVICES *);
void oc_uefi_net_backend_get_platform(oc_uefi_net_backend *, oc_net_backend *);

#endif
