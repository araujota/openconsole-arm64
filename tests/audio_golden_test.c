#include "openconsole/audio_provider.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEGMENT_FRAMES 4800u
#define TOTAL_FRAMES (SEGMENT_FRAMES * 3u + 2400u)

static size_t live_allocations;
static size_t live_bytes;

static void *tracked_allocate(void *context, UINTN size) {
    size_t *block;
    (void)context;
    block = malloc(sizeof(*block) + (size_t)size);
    if (block == NULL) return NULL;
    *block = (size_t)size;
    ++live_allocations;
    live_bytes += (size_t)size;
    return block + 1;
}

static void tracked_release(void *context, void *memory) {
    size_t *block = (size_t *)memory - 1;
    (void)context;
    assert(live_allocations != 0u && live_bytes >= *block);
    --live_allocations;
    live_bytes -= *block;
    free(block);
}

static EFI_STATUS discard_write(void *context, const int16_t *samples, UINTN frames) {
    (void)context; (void)samples; (void)frames;
    return EFI_SUCCESS;
}

static void put_u16(FILE *stream, uint16_t value) {
    fputc((int)(value & 255u), stream);
    fputc((int)(value >> 8), stream);
}

static void put_u32(FILE *stream, uint32_t value) {
    put_u16(stream, (uint16_t)value);
    put_u16(stream, (uint16_t)(value >> 16));
}

static void write_wav(const char *path, const int16_t *samples, uint32_t frames) {
    FILE *stream = fopen(path, "wb");
    uint32_t bytes = frames * 4u;
    assert(stream != NULL);
    fwrite("RIFF", 1u, 4u, stream); put_u32(stream, 36u + bytes);
    fwrite("WAVEfmt ", 1u, 8u, stream); put_u32(stream, 16u);
    put_u16(stream, 1u); put_u16(stream, 2u); put_u32(stream, 48000u);
    put_u32(stream, 48000u * 4u); put_u16(stream, 4u); put_u16(stream, 16u);
    fwrite("data", 1u, 4u, stream); put_u32(stream, bytes);
    assert(fwrite(samples, sizeof(*samples), frames * 2u, stream) == frames * 2u);
    assert(fclose(stream) == 0);
}

static OC_AUDIO_VOICE *create(oc_audio_provider *provider, int32_t pan) {
    OC_AUDIO_VOICE_DESC desc = {sizeof(desc), OC_AUDIO_ABI_VERSION, 48000u, 1u,
                                16u, 65536, pan, 65536u};
    OC_AUDIO_VOICE *voice = NULL;
    assert(provider->protocol.create_voice(&desc, &voice) == EFI_SUCCESS);
    return voice;
}

static void submit(oc_audio_provider *provider, OC_AUDIO_VOICE *voice,
                   const int16_t *samples, uint64_t frames) {
    OC_AUDIO_BUFFER buffer = {sizeof(buffer), OC_AUDIO_SUBMIT_COPY, samples,
                              frames, 0u};
    uint32_t result = 99u;
    assert(provider->protocol.submit(voice, &buffer, &result) == EFI_SUCCESS);
    assert(result == OC_AUDIO_RESULT_OK);
}

int main(int argc, char **argv) {
    oc_audio_provider provider;
    oc_audio_platform platform = {NULL, tracked_allocate, tracked_release,
                                  discard_write, "golden WAV", 0u, NULL,
                                  NULL, NULL, NULL};
    OC_AUDIO_VOICE *left, *right;
    OC_AUDIO_VOICE_STATUS status = {0};
    int16_t *tone_1k;
    int16_t *tone_500;
    int16_t *output;
    unsigned i;
    if (argc != 2) return 2;
    tone_1k = malloc(SEGMENT_FRAMES * 2u * sizeof(*tone_1k));
    tone_500 = malloc(SEGMENT_FRAMES * 2u * sizeof(*tone_500));
    output = calloc(TOTAL_FRAMES * 2u, sizeof(*output));
    assert(tone_1k != NULL && tone_500 != NULL && output != NULL);
    for (i = 0u; i < SEGMENT_FRAMES * 2u; ++i) {
        tone_1k[i] = (int16_t)(12000.0 * sin(2.0 * 3.141592653589793 * 1000.0 * i / 48000.0));
        tone_500[i] = (int16_t)(9000.0 * sin(2.0 * 3.141592653589793 * 500.0 * i / 48000.0));
    }
    assert(oc_audio_provider_initialize(&provider, &platform) == EFI_SUCCESS);
    assert(oc_audio_provider_begin_generation(&provider, 1u) == EFI_SUCCESS);
    left = create(&provider, -65536);
    submit(&provider, left, tone_1k, SEGMENT_FRAMES * 2u);
    assert(oc_audio_provider_render(&provider, output, SEGMENT_FRAMES) == EFI_SUCCESS);
    right = create(&provider, 65536);
    submit(&provider, right, tone_500, SEGMENT_FRAMES * 2u);
    assert(oc_audio_provider_render(&provider, output + SEGMENT_FRAMES * 2u,
                                    SEGMENT_FRAMES) == EFI_SUCCESS);
    assert(provider.protocol.destroy_voice(left) == EFI_SUCCESS);
    assert(oc_audio_provider_render(&provider, output + SEGMENT_FRAMES * 4u,
                                    SEGMENT_FRAMES) == EFI_SUCCESS);
    assert(oc_audio_provider_render(&provider, output + SEGMENT_FRAMES * 6u,
                                    2400u) == EFI_SUCCESS);
    status.structure_size = sizeof(status);
    assert(provider.protocol.query_voice(right, &status) == EFI_SUCCESS);
    assert(status.underrun_count == 1u);
    assert(provider.protocol.destroy_voice(right) == EFI_SUCCESS);
    write_wav(argv[1], output, TOTAL_FRAMES);

    for (i = 0u; i < 100u; ++i) {
        OC_AUDIO_VOICE *voice;
        assert(oc_audio_provider_begin_generation(&provider, i + 2u) == EFI_SUCCESS);
        voice = create(&provider, 0);
        submit(&provider, voice, tone_1k, 48u);
        assert(oc_audio_provider_render(&provider, output, 16u) == EFI_SUCCESS);
        oc_audio_provider_cleanup_generation(&provider, i + 2u);
        assert(oc_audio_provider_active_voices(&provider, i + 2u) == 0u);
        assert(live_allocations == 0u && live_bytes == 0u);
    }
    oc_audio_provider_shutdown(&provider);
    assert(live_allocations == 0u && live_bytes == 0u);
    free(output); free(tone_500); free(tone_1k);
    puts("PASS: generated audio golden WAV and 100 clean mixer lifecycles");
    return 0;
}
