#ifndef OPENCONSOLE_AUDIOIO_BACKEND_H
#define OPENCONSOLE_AUDIOIO_BACKEND_H

#include "openconsole/audio_provider.h"

#define OC_AUDIO_IO_PROTOCOL_REVISION 4u
#define OC_AUDIO_IO_BUFFER_COUNT 3u
#define OC_AUDIO_IO_BUFFER_FRAMES OC_AUDIO_OUTPUT_CHUNK_FRAMES
#define OC_AUDIO_IO_TPL_NOTIFY 16u
#define OC_AUDIO_IO_MAX_OUTPUTS 32u

typedef enum {
    OC_AUDIO_IO_TYPE_OUTPUT = 0
} oc_audio_io_type;

typedef enum {
    OC_AUDIO_IO_BITS_16 = 1u << 1
} oc_audio_io_bits;

typedef enum {
    OC_AUDIO_IO_FREQ_48KHZ = 1u << 6
} oc_audio_io_frequency;

typedef struct {
    oc_audio_io_type type;
    uint32_t supported_bits;
    uint32_t supported_frequencies;
    uint32_t device;
    uint32_t location;
    uint32_t surface;
} oc_audio_io_port;

typedef struct oc_audio_io_protocol oc_audio_io_protocol;
typedef void(EFIAPI *oc_audio_io_callback)(oc_audio_io_protocol *, void *);
typedef EFI_STATUS(EFIAPI *oc_audio_io_get_outputs)(
    oc_audio_io_protocol *, oc_audio_io_port **, UINTN *);
typedef EFI_STATUS(EFIAPI *oc_audio_io_setup_playback)(
    oc_audio_io_protocol *, uint64_t, int8_t, uint32_t, uint32_t, uint8_t, UINTN);
typedef EFI_STATUS(EFIAPI *oc_audio_io_start_playback)(
    oc_audio_io_protocol *, void *, UINTN, UINTN);
typedef EFI_STATUS(EFIAPI *oc_audio_io_start_playback_async)(
    oc_audio_io_protocol *, void *, UINTN, UINTN, oc_audio_io_callback, void *);
typedef EFI_STATUS(EFIAPI *oc_audio_io_stop_playback)(oc_audio_io_protocol *);

struct oc_audio_io_protocol {
    UINTN revision;
    oc_audio_io_get_outputs get_outputs;
    void *raw_gain_to_decibels;
    oc_audio_io_setup_playback setup_playback;
    oc_audio_io_start_playback start_playback;
    oc_audio_io_start_playback_async start_playback_async;
    oc_audio_io_stop_playback stop_playback;
};

typedef struct oc_audioio_backend oc_audioio_backend;

typedef struct {
    oc_audio_io_protocol *audio_io;
    uint64_t output_mask;
    uint16_t pci_vendor;
    uint16_t pci_device;
    uint8_t pci_revision;
    uint8_t pci_class;
    uint8_t pci_subclass;
    uint8_t port_index;
    uint8_t device;
    uint8_t location;
    uint8_t surface;
    char name[64];
} oc_audioio_output;

typedef struct {
    oc_audioio_backend *owner;
    int16_t samples[OC_AUDIO_IO_BUFFER_FRAMES * OC_AUDIO_MIX_CHANNELS];
    UINTN frames;
    uint8_t state;
} oc_audioio_buffer;

struct oc_audioio_backend {
    EFI_BOOT_SERVICES *boot_services;
    oc_audio_io_protocol *audio_io;
    oc_audioio_buffer buffers[OC_AUDIO_IO_BUFFER_COUNT];
    UINTN queue_head;
    UINTN queue_tail;
    UINTN queued;
    UINTN active_index;
    uint64_t output_mask;
    oc_audioio_output outputs[OC_AUDIO_IO_MAX_OUTPUTS];
    UINTN output_count;
    UINTN selected_output;
    uint8_t initialized;
    uint8_t stopping;
};

extern const EFI_GUID OC_AUDIO_IO_PROTOCOL_GUID;

EFI_STATUS oc_audioio_backend_initialize(oc_audioio_backend *, EFI_BOOT_SERVICES *);
void oc_audioio_backend_get_platform(oc_audioio_backend *, oc_audio_platform *);
EFI_STATUS oc_audioio_backend_stop(oc_audioio_backend *);
void oc_audioio_backend_shutdown(oc_audioio_backend *);
UINTN oc_audioio_backend_pending_buffers(const oc_audioio_backend *);

#endif
