#ifndef OPENCONSOLE_IO_MOCK_PROVIDER_H
#define OPENCONSOLE_IO_MOCK_PROVIDER_H

#include "openconsole/audio_protocol.h"
#include "openconsole/net_protocol.h"

typedef struct {
    uint8_t active;
    uint64_t generation;
} oc_mock_slot;

typedef struct {
    OC_NET_PROTOCOL net;
    OC_AUDIO_PROTOCOL audio;
    uint64_t generation;
    oc_mock_slot sessions[OC_NET_MAX_SESSIONS];
    oc_mock_slot operations[OC_NET_MAX_SESSIONS * OC_NET_MAX_OPERATIONS_PER_SESSION];
    oc_mock_slot voices[OC_AUDIO_MAX_VOICES];
} oc_io_mock_provider;

EFI_STATUS oc_io_mock_initialize(oc_io_mock_provider *provider);
EFI_STATUS oc_io_mock_begin_generation(oc_io_mock_provider *provider, uint64_t generation);
void oc_io_mock_cleanup_generation(oc_io_mock_provider *provider, uint64_t generation);
UINTN oc_io_mock_active_resources(const oc_io_mock_provider *provider, uint64_t generation);

#endif
