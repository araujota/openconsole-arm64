#ifndef OPENCONSOLE_AUDIO_PROVIDER_H
#define OPENCONSOLE_AUDIO_PROVIDER_H

#include "openconsole/audio_protocol.h"

typedef void *(*oc_audio_allocate)(void *, UINTN);
typedef void (*oc_audio_release)(void *, void *);
typedef EFI_STATUS (*oc_audio_write)(void *, const int16_t *, UINTN);
typedef int (*oc_audio_can_write)(void *);
typedef UINTN (*oc_audio_output_count)(void *);
typedef EFI_STATUS (*oc_audio_output_info)(void *, UINTN, char *, UINTN);
typedef EFI_STATUS (*oc_audio_select_output)(void *, UINTN);

typedef struct {
    void *context;
    oc_audio_allocate allocate;
    oc_audio_release release;
    oc_audio_write write;
    const char *output_name;
    uint64_t output_latency_frames;
    oc_audio_can_write can_write;
    oc_audio_output_count output_count;
    oc_audio_output_info output_info;
    oc_audio_select_output select_output;
} oc_audio_platform;

typedef struct {
    int16_t *samples;
    uint64_t frames;
    uint64_t bytes;
} oc_audio_queue_entry;

typedef struct OC_AUDIO_VOICE {
    UINTN slot;
    uint64_t generation;
    uint64_t source_frames_consumed;
    uint64_t phase_q32;
    uint64_t queued_frames;
    uint64_t queued_bytes;
    uint64_t underrun_count;
    uint64_t byte_quota;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t queue_head;
    uint16_t queue_count;
    int32_t gain_q16;
    int32_t pan_q16;
    uint32_t state;
    uint8_t active;
    uint8_t underrun_latched;
    oc_audio_queue_entry queue[OC_AUDIO_MAX_QUEUE_BUFFERS];
} oc_audio_voice;

typedef struct {
    OC_AUDIO_PROTOCOL protocol;
    oc_audio_platform platform;
    uint64_t generation;
    uint64_t last_pump_usec;
    uint64_t fractional_output_frames;
    uint64_t pending_output_frames;
    uint64_t underrun_count;
    uint8_t pump_initialized;
    oc_audio_voice voices[OC_AUDIO_MAX_VOICES];
} oc_audio_provider;

EFI_STATUS oc_audio_provider_initialize(oc_audio_provider *, const oc_audio_platform *);
EFI_STATUS oc_audio_provider_begin_generation(oc_audio_provider *, uint64_t);
void oc_audio_provider_cleanup_generation(oc_audio_provider *, uint64_t);
UINTN oc_audio_provider_active_voices(const oc_audio_provider *, uint64_t);
EFI_STATUS oc_audio_provider_render(oc_audio_provider *, int16_t *, UINTN);
void oc_audio_provider_shutdown(oc_audio_provider *);

#endif
