#include "openconsole/audioio_backend.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static oc_audio_io_protocol mock_audio_io;
static oc_audio_io_callback completion;
static void *completion_context;
static const int16_t *active_samples;
static UINTN active_bytes;
static UINTN start_calls;
static UINTN stop_calls;
static uint64_t setup_mask;
static EFI_STATUS next_start_status;
static EFI_STATUS locate_status;
static EFI_STATUS outputs_status;
static EFI_STATUS setup_status;
static EFI_STATUS free_status;
static int supported_output = 1;
static UINTN pool_allocations;

static EFI_TPL EFIAPI mock_raise_tpl(EFI_TPL tpl) {
    assert(tpl == OC_AUDIO_IO_TPL_NOTIFY);
    return 4u;
}

static void EFIAPI mock_restore_tpl(EFI_TPL tpl) {
    assert(tpl == 4u);
}

static EFI_STATUS EFIAPI mock_allocate_pool(uint32_t type, UINTN size, void **out) {
    (void)type;
    *out = malloc((size_t)size);
    if (*out != NULL) ++pool_allocations;
    return *out == NULL ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_free_pool(void *memory) {
    free(memory);
    assert(pool_allocations != 0u);
    --pool_allocations;
    return free_status;
}

static EFI_STATUS EFIAPI mock_locate_handles(uint32_t search, const EFI_GUID *guid,
                                             void *key, UINTN *count, EFI_HANDLE **out) {
    (void)search; (void)key;
    if (memcmp(guid, &OC_AUDIO_IO_PROTOCOL_GUID, sizeof(*guid)) != 0)
        return EFI_NOT_FOUND; /* no PCI/device path in this focused fixture */
    if (EFI_ERROR(locate_status)) return locate_status;
    *out = malloc(sizeof(**out));
    assert(*out != NULL);
    ++pool_allocations;
    (*out)[0] = (EFI_HANDLE)(uintptr_t)1u;
    *count = 1u;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_handle_protocol(EFI_HANDLE handle, const EFI_GUID *guid,
                                               void **out) {
    assert(handle == (EFI_HANDLE)(uintptr_t)1u);
    if (memcmp(guid, &OC_AUDIO_IO_PROTOCOL_GUID, sizeof(*guid)) != 0)
        return EFI_NOT_FOUND;
    *out = &mock_audio_io;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_get_outputs(oc_audio_io_protocol *self,
                                          oc_audio_io_port **out, UINTN *count) {
    (void)self;
    if (EFI_ERROR(outputs_status)) return outputs_status;
    *out = malloc(3u * sizeof(**out));
    assert(*out != NULL);
    ++pool_allocations;
    memset(*out, 0, 3u * sizeof(**out));
    (*out)[0].type = OC_AUDIO_IO_TYPE_OUTPUT;
    (*out)[0].supported_bits = OC_AUDIO_IO_BITS_16;
    (*out)[0].supported_frequencies = 0u;
    (*out)[1].type = OC_AUDIO_IO_TYPE_OUTPUT;
    (*out)[1].supported_bits = OC_AUDIO_IO_BITS_16;
    (*out)[1].supported_frequencies = supported_output ? OC_AUDIO_IO_FREQ_48KHZ : 0u;
    (*out)[1].device = 1u;
    (*out)[2].type = OC_AUDIO_IO_TYPE_OUTPUT;
    (*out)[2].supported_bits = OC_AUDIO_IO_BITS_16;
    (*out)[2].supported_frequencies = supported_output ? OC_AUDIO_IO_FREQ_48KHZ : 0u;
    (*out)[2].device = 5u;
    *count = 3u;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_setup(oc_audio_io_protocol *self, uint64_t mask,
                                    int8_t gain, uint32_t frequency, uint32_t bits,
                                    uint8_t channels, UINTN delay) {
    (void)self;
    assert(gain == 0 && frequency == OC_AUDIO_IO_FREQ_48KHZ);
    assert(bits == OC_AUDIO_IO_BITS_16 && channels == 2u && delay == 0u);
    setup_mask = mask;
    return setup_status;
}

static EFI_STATUS EFIAPI mock_start_async(oc_audio_io_protocol *self, void *data,
                                          UINTN bytes, UINTN position,
                                          oc_audio_io_callback callback, void *context) {
    EFI_STATUS status = next_start_status;
    (void)self;
    assert(position == 0u && callback != NULL && context != NULL);
    ++start_calls;
    next_start_status = EFI_SUCCESS;
    if (EFI_ERROR(status)) return status;
    active_samples = data;
    active_bytes = bytes;
    completion = callback;
    completion_context = context;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_stop(oc_audio_io_protocol *self) {
    (void)self;
    ++stop_calls;
    completion = NULL;
    completion_context = NULL;
    return EFI_SUCCESS;
}

static void complete_active(void) {
    oc_audio_io_callback callback = completion;
    void *context = completion_context;
    assert(callback != NULL);
    completion = NULL;
    completion_context = NULL;
    callback(&mock_audio_io, context);
}

static void assert_failed_initialize(EFI_BOOT_SERVICES *services, EFI_STATUS expected) {
    oc_audioio_backend failed;
    oc_audio_platform platform;
    memset(&failed, 0xA5, sizeof(failed));
    assert(oc_audioio_backend_initialize(&failed, services) == expected);
    assert(failed.boot_services == NULL && failed.audio_io == NULL &&
           failed.initialized == 0u && failed.output_mask == 0u);
    assert(oc_audioio_backend_pending_buffers(&failed) == 0u);
    memset(&platform, 0xA5, sizeof(platform));
    oc_audioio_backend_get_platform(&failed, &platform);
    assert(platform.context == NULL && platform.write == NULL &&
           platform.allocate == NULL && platform.release == NULL);
    assert(pool_allocations == 0u);
}

int main(void) {
    EFI_BOOT_SERVICES services;
    oc_audioio_backend backend;
    oc_audio_platform platform;
    int16_t first[8] = {1,2,3,4,5,6,7,8};
    int16_t second[4] = {11,12,13,14};
    int16_t third[4] = {21,22,23,24};
    int16_t fourth[4] = {31,32,33,34};

    memset(&services, 0, sizeof(services));
    memset(&mock_audio_io, 0, sizeof(mock_audio_io));
    services.raise_tpl = mock_raise_tpl;
    services.restore_tpl = mock_restore_tpl;
    services.allocate_pool = mock_allocate_pool;
    services.free_pool = mock_free_pool;
    services.locate_handle_buffer = mock_locate_handles;
    services.handle_protocol = mock_handle_protocol;
    mock_audio_io.revision = OC_AUDIO_IO_PROTOCOL_REVISION;
    mock_audio_io.get_outputs = mock_get_outputs;
    mock_audio_io.setup_playback = mock_setup;
    mock_audio_io.start_playback_async = mock_start_async;
    mock_audio_io.stop_playback = mock_stop;

    assert(oc_audioio_backend_initialize(&backend, &services) == EFI_SUCCESS);
    assert(setup_mask == 2u);
    oc_audioio_backend_get_platform(&backend, &platform);
    assert(platform.write != NULL && platform.output_latency_frames == 12288u);
    assert(platform.output_count(platform.context) == 2u);
    {
        char name[64];
        assert(platform.output_info(platform.context, 0u, name, sizeof(name)) == EFI_SUCCESS);
        assert(strstr(name, "speaker port 01") != NULL);
        assert(platform.output_info(platform.context, 1u, name, sizeof(name)) == EFI_SUCCESS);
        assert(strstr(name, "hdmi port 02") != NULL);
        assert(platform.select_output(platform.context, 1u) == EFI_SUCCESS);
        assert(setup_mask == 4u);
        assert(platform.select_output(platform.context, 0u) == EFI_SUCCESS);
        assert(setup_mask == 2u);
    }
    assert(platform.write(platform.context, first, 4u) == EFI_SUCCESS);
    assert(start_calls == 1u && active_bytes == sizeof(first));
    assert(memcmp(active_samples, first, sizeof(first)) == 0);
    first[0] = -1;
    assert(active_samples[0] == 1); /* backend owns a stable copied buffer */
    assert(platform.write(platform.context, second, 2u) == EFI_SUCCESS);
    assert(platform.write(platform.context, third, 2u) == EFI_SUCCESS);
    assert(oc_audioio_backend_pending_buffers(&backend) == 3u);
    assert(platform.write(platform.context, fourth, 2u) == EFI_NOT_READY);

    complete_active();
    assert(start_calls == 1u);
    assert(platform.write(platform.context, fourth, 2u) == EFI_SUCCESS);
    assert(start_calls == 2u && active_bytes == sizeof(second));
    assert(memcmp(active_samples, second, sizeof(second)) == 0);
    complete_active();
    assert(platform.write(platform.context, first, 2u) == EFI_SUCCESS);
    assert(start_calls == 3u && active_bytes == sizeof(third));
    assert(oc_audioio_backend_pending_buffers(&backend) == 3u);

    assert(oc_audioio_backend_stop(&backend) == EFI_SUCCESS);
    assert(stop_calls == 3u && oc_audioio_backend_pending_buffers(&backend) == 0u);

    next_start_status = EFI_DEVICE_ERROR;
    assert(platform.write(platform.context, fourth, 2u) == EFI_DEVICE_ERROR);
    assert(oc_audioio_backend_pending_buffers(&backend) == 1u);
    assert(platform.can_write(platform.context));
    assert(oc_audioio_backend_pending_buffers(&backend) == 1u);
    assert(oc_audioio_backend_stop(&backend) == EFI_SUCCESS);
    assert(stop_calls == 4u);
    oc_audioio_backend_shutdown(&backend);
    assert(stop_calls == 5u);
    assert(pool_allocations == 0u);

    locate_status = EFI_NOT_FOUND;
    assert_failed_initialize(&services, EFI_NOT_FOUND);
    locate_status = EFI_SUCCESS;
    outputs_status = EFI_DEVICE_ERROR;
    assert_failed_initialize(&services, EFI_NOT_FOUND);
    outputs_status = EFI_SUCCESS;
    supported_output = 0;
    assert_failed_initialize(&services, EFI_NOT_FOUND);
    supported_output = 1;
    setup_status = EFI_DEVICE_ERROR;
    assert_failed_initialize(&services, EFI_DEVICE_ERROR);
    setup_status = EFI_SUCCESS;
    mock_audio_io.revision = OC_AUDIO_IO_PROTOCOL_REVISION - 1u;
    assert_failed_initialize(&services, EFI_NOT_FOUND);
    mock_audio_io.revision = OC_AUDIO_IO_PROTOCOL_REVISION;
    mock_audio_io.start_playback_async = NULL;
    assert_failed_initialize(&services, EFI_NOT_FOUND);

    puts("PASS: AudioIo discovery, rotation, backpressure, stop, and failure rollback");
    return 0;
}
