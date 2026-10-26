#ifndef OPENCONSOLE_NET_PROVIDER_H
#define OPENCONSOLE_NET_PROVIDER_H

#include "openconsole/net_protocol.h"

typedef enum {
    OC_NET_BACKEND_DHCP4 = 1,
    OC_NET_BACKEND_DNS4 = 2,
    OC_NET_BACKEND_UDP4 = 3,
    OC_NET_BACKEND_TCP4 = 4,
    OC_NET_BACKEND_HTTPS = 5
} oc_net_backend_kind;

typedef struct {
    oc_net_backend_kind kind;
    uint32_t address_be;
    uint16_t port;
    const void *request_bytes;
    UINTN request_size;
    const char *name;
    UINTN name_size;
    void *response_bytes;
    UINTN response_capacity;
    uint64_t deadline_usec;
} oc_net_backend_request;

typedef struct {
    uint32_t result;
    uint32_t protocol_status;
    uint64_t bytes_transferred;
    uint64_t bytes_required;
} oc_net_backend_progress;

typedef EFI_STATUS (*oc_net_backend_start)(void *, UINTN,
                                           const oc_net_backend_request *);
typedef EFI_STATUS (*oc_net_backend_pump)(void *, UINTN,
                                          const oc_net_backend_request *,
                                          oc_net_backend_progress *);
typedef void (*oc_net_backend_cancel)(void *, UINTN);
typedef void (*oc_net_backend_release_operation)(void *, UINTN);
typedef EFI_STATUS (*oc_net_backend_query_state)(void *, OC_NET_STATE *);
typedef void *(*oc_net_allocate)(void *, UINTN);
typedef void (*oc_net_release)(void *, void *);

typedef struct {
    void *context;
    uint32_t capabilities;
    oc_net_backend_start start;
    oc_net_backend_pump pump;
    oc_net_backend_cancel cancel;
    oc_net_backend_release_operation release;
    oc_net_backend_query_state query_state;
} oc_net_backend;

typedef struct {
    void *context;
    oc_net_allocate allocate;
    oc_net_release release;
    oc_net_backend backend;
} oc_net_platform;

typedef struct {
    uint8_t active;
    uint8_t reserved[7];
    uint64_t generation;
    uint64_t byte_quota;
    uint64_t bytes_reserved;
    uint32_t operation_quota;
    uint32_t operation_count;
} oc_net_session_slot;

typedef struct {
    uint8_t active;
    uint8_t pending;
    uint8_t backend_started;
    uint8_t reserved0;
    uint32_t result;
    uint32_t protocol_status;
    uint32_t session_slot;
    uint32_t reserved1;
    uint64_t generation;
    uint64_t deadline_usec;
    uint64_t bytes_transferred;
    uint64_t bytes_required;
    uint64_t reservation;
    oc_net_backend_kind kind;
    uint32_t address_be;
    uint16_t port;
    uint16_t reserved2;
    void *owned_bytes;
    UINTN owned_size;
    UINTN name_size;
    void *response_bytes;
    UINTN response_capacity;
} oc_net_operation_slot;

struct OC_NET_SESSION {
    UINTN slot;
    uint64_t generation;
};

struct OC_NET_OPERATION {
    UINTN slot;
    uint64_t generation;
};

typedef struct {
    OC_NET_PROTOCOL protocol;
    oc_net_platform platform;
    uint64_t generation;
    oc_net_session_slot sessions[OC_NET_MAX_SESSIONS];
    OC_NET_SESSION session_handles[OC_NET_MAX_SESSIONS];
    oc_net_operation_slot operations[OC_NET_MAX_SESSIONS * OC_NET_MAX_OPERATIONS_PER_SESSION];
    OC_NET_OPERATION operation_handles[OC_NET_MAX_SESSIONS * OC_NET_MAX_OPERATIONS_PER_SESSION];
} oc_net_provider;

EFI_STATUS oc_net_provider_initialize(oc_net_provider *, const oc_net_platform *);
EFI_STATUS oc_net_provider_begin_generation(oc_net_provider *, uint64_t);
void oc_net_provider_cleanup_generation(oc_net_provider *, uint64_t);
void oc_net_provider_shutdown(oc_net_provider *);
UINTN oc_net_provider_active_resources(const oc_net_provider *, uint64_t);

#endif
