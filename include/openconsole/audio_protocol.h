#ifndef OPENCONSOLE_AUDIO_PROTOCOL_H
#define OPENCONSOLE_AUDIO_PROTOCOL_H

#include "openconsole/uefi.h"

#define OC_AUDIO_ABI_MAJOR 0u
#define OC_AUDIO_ABI_MINOR 2u
#define OC_AUDIO_ABI_VERSION ((OC_AUDIO_ABI_MAJOR << 16) | OC_AUDIO_ABI_MINOR)
#define OC_AUDIO_MIX_RATE 48000u
#define OC_AUDIO_MIX_CHANNELS 2u
#define OC_AUDIO_OUTPUT_CHUNK_FRAMES 4096u
#define OC_AUDIO_MAX_VOICES 16u
#define OC_AUDIO_MAX_QUEUE_BUFFERS 32u
#define OC_AUDIO_MAX_QUEUED_BYTES_PER_VOICE (2u * 1024u * 1024u)
#define OC_AUDIO_MIN_SOURCE_RATE 8000u
#define OC_AUDIO_MAX_SOURCE_RATE 192000u

#define OC_AUDIO_CAP_COPY_SUBMIT 0x00000001u
#define OC_AUDIO_CAP_BORROW_SUBMIT 0x00000002u
#define OC_AUDIO_SUBMIT_COPY 0x00000001u
#define OC_AUDIO_SUBMIT_BORROW 0x00000002u

typedef struct OC_AUDIO_VOICE OC_AUDIO_VOICE;

typedef enum {
    OC_AUDIO_RESULT_OK = 0,
    OC_AUDIO_RESULT_BACKPRESSURE = 1,
    OC_AUDIO_RESULT_UNSUPPORTED_FORMAT = 2,
    OC_AUDIO_RESULT_DEVICE_UNAVAILABLE = 3,
    OC_AUDIO_RESULT_INVALID_VOICE = 4,
    OC_AUDIO_RESULT_BACKEND_FAILURE = 5
} OC_AUDIO_RESULT;

typedef enum {
    OC_AUDIO_VOICE_PLAYING = 0,
    OC_AUDIO_VOICE_PAUSED = 1,
    OC_AUDIO_VOICE_STOPPED = 2
} OC_AUDIO_VOICE_STATE;

typedef struct {
    uint32_t structure_size;
    uint32_t abi_version;
    uint32_t capabilities;
    uint32_t output_count;
    uint32_t mix_rate;
    uint16_t mix_channels;
    uint16_t sample_bits;
    uint32_t max_voices;
    uint32_t max_queue_buffers;
    uint64_t max_queued_bytes_per_voice;
    uint64_t provider_underrun_count;
    uint32_t reserved[2];
} OC_AUDIO_CAPABILITIES;

typedef struct {
    uint32_t structure_size;
    uint32_t output_id;
    uint32_t flags;
    uint32_t name_size;
    char name[64];
} OC_AUDIO_OUTPUT_INFO;

typedef struct {
    uint32_t structure_size;
    uint32_t requested_abi;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t sample_bits;
    int32_t gain_q16;
    int32_t pan_q16;
    uint64_t byte_quota;
} OC_AUDIO_VOICE_DESC;

typedef struct {
    uint32_t structure_size;
    uint32_t flags;
    const int16_t *samples;
    uint64_t frame_count;
    uint64_t presentation_frame;
} OC_AUDIO_BUFFER;

typedef struct {
    uint32_t structure_size;
    uint32_t state;
    uint64_t source_frames_consumed;
    uint64_t queued_frames;
    uint64_t queued_bytes;
    uint64_t underrun_count;
    uint64_t output_latency_frames;
    uint64_t owner_generation;
} OC_AUDIO_VOICE_STATUS;

/* COPY submissions are owned by the provider after submit returns. BORROW
 * submissions remain caller-owned and immutable until their play cursor passes
 * the buffer, or until flush/stop/destroy/generation cleanup returns. */
typedef EFI_STATUS(EFIAPI *OC_AUDIO_QUERY_CAPABILITIES)(OC_AUDIO_CAPABILITIES *);
typedef EFI_STATUS(EFIAPI *OC_AUDIO_GET_OUTPUT)(uint32_t, OC_AUDIO_OUTPUT_INFO *);
typedef EFI_STATUS(EFIAPI *OC_AUDIO_SELECT_OUTPUT)(uint32_t);
typedef EFI_STATUS(EFIAPI *OC_AUDIO_CREATE_VOICE)(const OC_AUDIO_VOICE_DESC *, OC_AUDIO_VOICE **);
typedef EFI_STATUS(EFIAPI *OC_AUDIO_DESTROY_VOICE)(OC_AUDIO_VOICE *);
typedef EFI_STATUS(EFIAPI *OC_AUDIO_SUBMIT)(OC_AUDIO_VOICE *, const OC_AUDIO_BUFFER *, uint32_t *);
typedef EFI_STATUS(EFIAPI *OC_AUDIO_SET_GAIN_PAN)(OC_AUDIO_VOICE *, int32_t, int32_t);
typedef EFI_STATUS(EFIAPI *OC_AUDIO_PAUSE)(OC_AUDIO_VOICE *);
typedef EFI_STATUS(EFIAPI *OC_AUDIO_RESUME)(OC_AUDIO_VOICE *);
typedef EFI_STATUS(EFIAPI *OC_AUDIO_FLUSH)(OC_AUDIO_VOICE *);
typedef EFI_STATUS(EFIAPI *OC_AUDIO_STOP)(OC_AUDIO_VOICE *);
typedef EFI_STATUS(EFIAPI *OC_AUDIO_QUERY_VOICE)(OC_AUDIO_VOICE *, OC_AUDIO_VOICE_STATUS *);
typedef EFI_STATUS(EFIAPI *OC_AUDIO_PUMP)(uint64_t);

typedef struct {
    uint32_t structure_size;
    uint32_t abi_version;
    OC_AUDIO_QUERY_CAPABILITIES query_capabilities;
    OC_AUDIO_GET_OUTPUT get_output;
    OC_AUDIO_SELECT_OUTPUT select_output;
    OC_AUDIO_CREATE_VOICE create_voice;
    OC_AUDIO_DESTROY_VOICE destroy_voice;
    OC_AUDIO_SUBMIT submit;
    OC_AUDIO_SET_GAIN_PAN set_gain_pan;
    OC_AUDIO_PAUSE pause;
    OC_AUDIO_RESUME resume;
    OC_AUDIO_FLUSH flush;
    OC_AUDIO_STOP stop;
    OC_AUDIO_QUERY_VOICE query_voice;
    OC_AUDIO_PUMP pump;
} OC_AUDIO_PROTOCOL;

static inline int oc_audio_abi_compatible(uint32_t requested_abi) {
    return (requested_abi >> 16) == OC_AUDIO_ABI_MAJOR &&
           (requested_abi & 0xffffu) <= OC_AUDIO_ABI_MINOR;
}

/* e68817e8-cfc3-43c7-9aea-feb21612e686 */
static const EFI_GUID OC_AUDIO_PROTOCOL_GUID =
    {0xe68817e8u,0xcfc3u,0x43c7u,{0x9a,0xea,0xfe,0xb2,0x16,0x12,0xe6,0x86}};

#endif
