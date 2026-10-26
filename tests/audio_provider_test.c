#include "openconsole/audio_provider.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t live_allocations;
static size_t live_bytes;
static size_t peak_bytes;
static int16_t sink[4096];
static size_t sink_frames;

static void *test_allocate(void *context, UINTN size) {
    size_t *allocation;
    (void)context;
    allocation = malloc(sizeof(*allocation) + (size_t)size);
    if (allocation == NULL) return NULL;
    *allocation = (size_t)size;
    ++live_allocations;
    live_bytes += (size_t)size;
    if (live_bytes > peak_bytes) peak_bytes = live_bytes;
    return allocation + 1;
}

static void test_release(void *context, void *memory) {
    size_t *allocation;
    (void)context;
    assert(memory != NULL);
    allocation = (size_t *)memory - 1;
    assert(live_allocations != 0u && live_bytes >= *allocation);
    --live_allocations;
    live_bytes -= *allocation;
    free(allocation);
}

static EFI_STATUS test_write(void *context, const int16_t *samples, UINTN frames) {
    (void)context;
    assert(sink_frames + frames <= sizeof(sink) / sizeof(sink[0]) / 2u);
    memcpy(sink + sink_frames * 2u, samples, (size_t)frames * 2u * sizeof(*samples));
    sink_frames += frames;
    return EFI_SUCCESS;
}

static OC_AUDIO_VOICE *new_voice(oc_audio_provider *provider, uint32_t rate,
                                 uint16_t channels, int32_t gain, int32_t pan,
                                 uint64_t quota) {
    OC_AUDIO_VOICE_DESC desc = {sizeof(desc), OC_AUDIO_ABI_VERSION, rate, channels,
                                16u, gain, pan, quota};
    OC_AUDIO_VOICE *voice = NULL;
    assert(provider->protocol.create_voice(&desc, &voice) == EFI_SUCCESS);
    assert(voice != NULL);
    return voice;
}

static void submit(oc_audio_provider *provider, OC_AUDIO_VOICE *voice,
                   int16_t *samples, uint64_t frames) {
    OC_AUDIO_BUFFER buffer = {sizeof(buffer), OC_AUDIO_SUBMIT_COPY, samples,
                              frames, 0u};
    uint32_t result = 99u;
    assert(provider->protocol.submit(voice, &buffer, &result) == EFI_SUCCESS);
    assert(result == OC_AUDIO_RESULT_OK);
}

int main(void) {
    oc_audio_provider provider;
    oc_audio_platform platform = {NULL, test_allocate, test_release, test_write,
                                  "host PCM sink", 128u, NULL, NULL, NULL, NULL};
    OC_AUDIO_VOICE *left;
    OC_AUDIO_VOICE *right;
    OC_AUDIO_VOICE *voices[OC_AUDIO_MAX_VOICES];
    OC_AUDIO_VOICE_STATUS status = {sizeof(status), 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    int16_t left_samples[] = {20000, 20000, 20000, 20000};
    int16_t right_samples[] = {20000, 20000, 20000, 20000};
    int16_t output[32];
    int16_t ramp[] = {0, 10000};
    unsigned i;

    assert(oc_audio_provider_initialize(&provider, &platform) == EFI_SUCCESS);
    assert(oc_audio_provider_begin_generation(&provider, 11u) == EFI_SUCCESS);
    left = new_voice(&provider, 48000u, 1u, 65536, -65536, 4096u);
    right = new_voice(&provider, 48000u, 1u, 65536, 65536, 4096u);
    submit(&provider, left, left_samples, 4u);
    submit(&provider, right, right_samples, 4u);
    left_samples[0] = -1; /* COPY ownership: mutation must not reach the mix. */
    assert(oc_audio_provider_render(&provider, output, 4u) == EFI_SUCCESS);
    for (i = 0u; i < 4u; ++i) {
        assert(output[i * 2u] == 20000);
        assert(output[i * 2u + 1u] == 20000);
    }
    assert(provider.protocol.query_voice(left, &status) == EFI_SUCCESS);
    assert(status.source_frames_consumed == 4u && status.queued_bytes == 0u);
    assert(status.output_latency_frames == 128u && status.owner_generation == 11u);
    assert(provider.protocol.destroy_voice(left) == EFI_SUCCESS);
    assert(provider.protocol.destroy_voice(right) == EFI_SUCCESS);

    /* 24-kHz mono is linearly resampled to four 48-kHz stereo frames. */
    left = new_voice(&provider, 24000u, 1u, 65536, 0, 4096u);
    submit(&provider, left, ramp, 2u);
    assert(oc_audio_provider_render(&provider, output, 4u) == EFI_SUCCESS);
    assert(output[0] == 0 && output[1] == 0);
    assert(output[2] == 5000 && output[3] == 5000);
    assert(output[4] == 10000 && output[5] == 10000);
    assert(output[6] == 10000 && output[7] == 10000);
    assert(provider.protocol.query_voice(left, &status) == EFI_SUCCESS);
    assert(status.source_frames_consumed == 2u && status.queued_frames == 0u);
    assert(oc_audio_provider_render(&provider, output, 2u) == EFI_SUCCESS);
    assert(output[0] == 0 && output[1] == 0 && status.owner_generation == 11u);
    assert(provider.protocol.query_voice(left, &status) == EFI_SUCCESS);
    assert(status.underrun_count == 1u);
    submit(&provider, left, ramp, 2u);
    assert(oc_audio_provider_render(&provider, output, 5u) == EFI_SUCCESS);
    assert(provider.protocol.query_voice(left, &status) == EFI_SUCCESS);
    assert(status.underrun_count == 2u); /* recovery followed by a new underrun */
    {
        OC_AUDIO_CAPABILITIES caps = {0};
        caps.structure_size = sizeof(caps);
        assert(provider.protocol.query_capabilities(&caps) == EFI_SUCCESS);
        assert(caps.provider_underrun_count == 2u);
    }
    assert(provider.protocol.destroy_voice(left) == EFI_SUCCESS);

    /* Queue count is independently bounded and returns explicit backpressure. */
    left = new_voice(&provider, 48000u, 1u, 65536, 0, 4096u);
    for (i = 0u; i < OC_AUDIO_MAX_QUEUE_BUFFERS; ++i) submit(&provider, left, ramp, 1u);
    {
        OC_AUDIO_BUFFER buffer = {sizeof(buffer), OC_AUDIO_SUBMIT_COPY, ramp, 1u, 0u};
        uint32_t result = 99u;
        assert(provider.protocol.submit(left, &buffer, &result) == EFI_SUCCESS);
        assert(result == OC_AUDIO_RESULT_BACKPRESSURE);
    }
    assert(provider.protocol.flush(left) == EFI_SUCCESS);
    assert(provider.protocol.destroy_voice(left) == EFI_SUCCESS);

    /* Two centered voices must saturate rather than wrap. */
    left = new_voice(&provider, 48000u, 1u, 65536, 0, 4096u);
    right = new_voice(&provider, 48000u, 1u, 65536, 0, 4096u);
    submit(&provider, left, left_samples + 1, 2u);
    submit(&provider, right, right_samples, 2u);
    assert(oc_audio_provider_render(&provider, output, 2u) == EFI_SUCCESS);
    assert(output[0] == 32767 && output[1] == 32767);
    assert(provider.protocol.pause(left) == EFI_SUCCESS);
    assert(oc_audio_provider_render(&provider, output, 1u) == EFI_SUCCESS);
    assert(output[0] == 0 && output[1] == 0);
    assert(provider.protocol.resume(left) == EFI_SUCCESS);
    assert(provider.protocol.flush(left) == EFI_SUCCESS);
    assert(provider.protocol.stop(right) == EFI_SUCCESS);
    assert(provider.protocol.destroy_voice(left) == EFI_SUCCESS);
    assert(provider.protocol.destroy_voice(right) == EFI_SUCCESS);

    /* All sixteen slots are generation-owned and the seventeenth is rejected. */
    for (i = 0u; i < OC_AUDIO_MAX_VOICES; ++i)
        voices[i] = new_voice(&provider, 44100u, 2u, 65536, 0, 128u);
    {
        OC_AUDIO_VOICE_DESC desc = {sizeof(desc), OC_AUDIO_ABI_VERSION, 48000u,
                                    2u, 16u, 65536, 0, 128u};
        OC_AUDIO_VOICE *extra = NULL;
        assert(provider.protocol.create_voice(&desc, &extra) == EFI_OUT_OF_RESOURCES);
    }
    submit(&provider, voices[0], right_samples, 2u);
    assert(oc_audio_provider_active_voices(&provider, 11u) == OC_AUDIO_MAX_VOICES);
    oc_audio_provider_cleanup_generation(&provider, 11u);
    assert(oc_audio_provider_active_voices(&provider, 11u) == 0u);
    assert(live_allocations == 0u && live_bytes == 0u && peak_bytes != 0u);
    oc_audio_provider_shutdown(&provider);
    puts("PASS: 16-voice copied PCM mixing, saturation, pan, controls, and cleanup");
    return 0;
}
