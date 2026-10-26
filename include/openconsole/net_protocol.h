#ifndef OPENCONSOLE_NET_PROTOCOL_H
#define OPENCONSOLE_NET_PROTOCOL_H

#include "openconsole/uefi.h"

#define OC_NET_ABI_MAJOR 0u
#define OC_NET_ABI_MINOR 1u
#define OC_NET_ABI_VERSION ((OC_NET_ABI_MAJOR << 16) | OC_NET_ABI_MINOR)
#define OC_NET_MAX_SESSIONS 8u
#define OC_NET_MAX_OPERATIONS_PER_SESSION 16u
#define OC_NET_MAX_DNS_ADDRESSES 8u
#define OC_NET_MAX_HOST_BYTES 253u
#define OC_NET_MAX_URL_BYTES 2048u
#define OC_NET_MAX_HEADER_BYTES 8192u
#define OC_NET_MAX_RESPONSE_BYTES (16u * 1024u * 1024u)

#define OC_NET_CAP_IPV4 0x00000001u
#define OC_NET_CAP_DHCP4 0x00000002u
#define OC_NET_CAP_DNS4 0x00000004u
#define OC_NET_CAP_UDP4 0x00000008u
#define OC_NET_CAP_TCP4 0x00000010u
#define OC_NET_CAP_HTTPS 0x00000020u
#define OC_NET_CAP_TLS_IDENTITY_VALIDATION 0x00000040u

typedef struct OC_NET_SESSION OC_NET_SESSION;
typedef struct OC_NET_OPERATION OC_NET_OPERATION;

typedef enum {
    OC_NET_RESULT_OK = 0,
    OC_NET_RESULT_PENDING = 1,
    OC_NET_RESULT_CANCELLED = 2,
    OC_NET_RESULT_DEADLINE_EXCEEDED = 3,
    OC_NET_RESULT_LINK_DOWN = 4,
    OC_NET_RESULT_ADDRESS_UNAVAILABLE = 5,
    OC_NET_RESULT_NAME_NOT_FOUND = 6,
    OC_NET_RESULT_CONNECTION_REFUSED = 7,
    OC_NET_RESULT_RESPONSE_TOO_LARGE = 8,
    OC_NET_RESULT_MALFORMED_RESPONSE = 9,
    OC_NET_RESULT_TLS_IDENTITY_REJECTED = 10,
    OC_NET_RESULT_QUOTA_EXCEEDED = 11,
    OC_NET_RESULT_BACKEND_FAILURE = 12,
    OC_NET_RESULT_PROTOCOL_ERROR = 13
} OC_NET_RESULT;

typedef struct {
    uint32_t structure_size;
    uint32_t abi_version;
    uint32_t capabilities;
    uint32_t max_sessions;
    uint32_t max_operations_per_session;
    uint32_t max_dns_addresses;
    uint64_t max_response_bytes;
    uint32_t reserved[4];
} OC_NET_CAPABILITIES;

typedef struct {
    uint32_t structure_size;
    uint32_t requested_abi;
    uint64_t byte_quota;
    uint32_t operation_quota;
    uint32_t reserved;
} OC_NET_SESSION_DESC;

typedef struct {
    uint32_t structure_size;
    uint32_t link_up;
    uint32_t configured;
    uint32_t address_be;
    uint32_t subnet_mask_be;
    uint32_t gateway_be;
    uint64_t owner_generation;
} OC_NET_STATE;

typedef struct {
    uint32_t structure_size;
    uint32_t reserved;
    uint64_t deadline_usec;
} OC_NET_DEADLINE;

typedef struct {
    uint32_t structure_size;
    uint32_t address_count;
    uint32_t addresses_be[OC_NET_MAX_DNS_ADDRESSES];
    uint32_t reserved;
} OC_NET_DNS_RESULT;

typedef struct {
    uint32_t structure_size;
    uint32_t result;
    uint64_t bytes_transferred;
    uint64_t bytes_required;
    uint32_t protocol_status;
    uint32_t reserved;
} OC_NET_OPERATION_STATUS;

typedef struct {
    uint32_t structure_size;
    uint32_t address_be;
    uint16_t port;
    uint16_t reserved;
    const void *request_bytes;
    uint64_t request_size;
    void *response_bytes;
    uint64_t response_capacity;
    OC_NET_DEADLINE deadline;
} OC_NET_TRANSFER_DESC;

typedef struct {
    uint32_t structure_size;
    uint32_t flags;
    const char *url;
    uint32_t url_size;
    const void *request_body;
    uint64_t request_body_size;
    void *response_body;
    uint64_t response_capacity;
    OC_NET_DEADLINE deadline;
} OC_NET_HTTPS_DESC;

/* All request strings and source bytes are copied before a start call returns.
 * Destination buffers are borrowed until completion, cancellation, operation
 * release, or generation cleanup, whichever happens first. */
typedef EFI_STATUS(EFIAPI *OC_NET_QUERY_CAPABILITIES)(OC_NET_CAPABILITIES *);
typedef EFI_STATUS(EFIAPI *OC_NET_CREATE_SESSION)(const OC_NET_SESSION_DESC *, OC_NET_SESSION **);
typedef EFI_STATUS(EFIAPI *OC_NET_DESTROY_SESSION)(OC_NET_SESSION *);
typedef EFI_STATUS(EFIAPI *OC_NET_QUERY_STATE)(OC_NET_SESSION *, OC_NET_STATE *);
typedef EFI_STATUS(EFIAPI *OC_NET_CONFIGURE_DHCP4)(OC_NET_SESSION *, const OC_NET_DEADLINE *, OC_NET_OPERATION **);
typedef EFI_STATUS(EFIAPI *OC_NET_RESOLVE_DNS4)(OC_NET_SESSION *, const char *, UINTN, OC_NET_DNS_RESULT *, const OC_NET_DEADLINE *, OC_NET_OPERATION **);
typedef EFI_STATUS(EFIAPI *OC_NET_START_UDP4)(OC_NET_SESSION *, const OC_NET_TRANSFER_DESC *, OC_NET_OPERATION **);
typedef EFI_STATUS(EFIAPI *OC_NET_START_TCP4)(OC_NET_SESSION *, const OC_NET_TRANSFER_DESC *, OC_NET_OPERATION **);
typedef EFI_STATUS(EFIAPI *OC_NET_START_HTTPS)(OC_NET_SESSION *, const OC_NET_HTTPS_DESC *, OC_NET_OPERATION **);
typedef EFI_STATUS(EFIAPI *OC_NET_PUMP)(OC_NET_SESSION *, uint64_t);
typedef EFI_STATUS(EFIAPI *OC_NET_QUERY_OPERATION)(OC_NET_OPERATION *, OC_NET_OPERATION_STATUS *);
typedef EFI_STATUS(EFIAPI *OC_NET_CANCEL_OPERATION)(OC_NET_OPERATION *);
typedef EFI_STATUS(EFIAPI *OC_NET_RELEASE_OPERATION)(OC_NET_OPERATION *);

typedef struct {
    uint32_t structure_size;
    uint32_t abi_version;
    OC_NET_QUERY_CAPABILITIES query_capabilities;
    OC_NET_CREATE_SESSION create_session;
    OC_NET_DESTROY_SESSION destroy_session;
    OC_NET_QUERY_STATE query_state;
    OC_NET_CONFIGURE_DHCP4 configure_dhcp4;
    OC_NET_RESOLVE_DNS4 resolve_dns4;
    OC_NET_START_UDP4 start_udp4;
    OC_NET_START_TCP4 start_tcp4;
    OC_NET_START_HTTPS start_https;
    OC_NET_PUMP pump;
    OC_NET_QUERY_OPERATION query_operation;
    OC_NET_CANCEL_OPERATION cancel_operation;
    OC_NET_RELEASE_OPERATION release_operation;
} OC_NET_PROTOCOL;

static inline int oc_net_abi_compatible(uint32_t requested_abi) {
    return (requested_abi >> 16) == OC_NET_ABI_MAJOR &&
           (requested_abi & 0xffffu) <= OC_NET_ABI_MINOR;
}

/* 8cd4632a-b6a6-4ba3-bc2f-ae2315a20152 */
static const EFI_GUID OC_NET_PROTOCOL_GUID =
    {0x8cd4632au,0xb6a6u,0x4ba3u,{0xbc,0x2f,0xae,0x23,0x15,0xa2,0x01,0x52}};

#endif
