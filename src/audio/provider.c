#include "openconsole/audio_provider.h"

#define Q16_ONE 65536
#define OC_AUDIO_RENDER_CHUNK OC_AUDIO_OUTPUT_CHUNK_FRAMES

static oc_audio_provider *active_provider;

static void zero_bytes(void *destination, UINTN size) {
    uint8_t *bytes = destination;
    while (size-- != 0u) *bytes++ = 0u;
}

static int valid_voice(const OC_AUDIO_VOICE *voice) {
    return active_provider != 0 && voice != 0 &&
           voice->slot < OC_AUDIO_MAX_VOICES &&
           voice == &active_provider->voices[voice->slot] && voice->active;
}

static void release_queue(oc_audio_provider *provider, oc_audio_voice *voice) {
    UINTN i;
    for (i = 0u; i < OC_AUDIO_MAX_QUEUE_BUFFERS; ++i) {
        if (voice->queue[i].samples != 0)
            provider->platform.release(provider->platform.context,
                                       voice->queue[i].samples);
        zero_bytes(&voice->queue[i], sizeof(voice->queue[i]));
    }
    voice->queue_head = 0u;
    voice->queue_count = 0u;
    voice->queued_frames = 0u;
    voice->queued_bytes = 0u;
    voice->phase_q32 = 0u;
    voice->underrun_latched = 0u;
}

static EFI_STATUS EFIAPI query_capabilities(OC_AUDIO_CAPABILITIES *out) {
    if (out == 0 || out->structure_size < sizeof(*out)) return EFI_INVALID_PARAMETER;
    zero_bytes(out, sizeof(*out));
    out->structure_size = sizeof(*out);
    out->abi_version = OC_AUDIO_ABI_VERSION;
    out->capabilities = OC_AUDIO_CAP_COPY_SUBMIT;
    out->output_count = active_provider->platform.output_count != 0 ?
        (uint32_t)active_provider->platform.output_count(active_provider->platform.context) : 1u;
    out->mix_rate = OC_AUDIO_MIX_RATE;
    out->mix_channels = OC_AUDIO_MIX_CHANNELS;
    out->sample_bits = 16u;
    out->max_voices = OC_AUDIO_MAX_VOICES;
    out->max_queue_buffers = OC_AUDIO_MAX_QUEUE_BUFFERS;
    out->max_queued_bytes_per_voice = OC_AUDIO_MAX_QUEUED_BYTES_PER_VOICE;
    out->provider_underrun_count = active_provider->underrun_count;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI get_output(uint32_t index, OC_AUDIO_OUTPUT_INFO *out) {
    const char *name;
    UINTN length = 0u;
    if (active_provider == 0 || out == 0 ||
        out->structure_size < sizeof(*out)) return EFI_NOT_FOUND;
    zero_bytes(out, sizeof(*out));
    out->structure_size = sizeof(*out);
    out->output_id = index;
    if (active_provider->platform.output_info != 0) {
        EFI_STATUS status = active_provider->platform.output_info(
            active_provider->platform.context, index, out->name, sizeof(out->name));
        if (EFI_ERROR(status)) return status;
        while (out->name[length] != '\0' && length + 1u < sizeof(out->name)) ++length;
        out->name_size = (uint32_t)length;
        return EFI_SUCCESS;
    }
    if (index != 0u) return EFI_NOT_FOUND;
    name = active_provider->platform.output_name;
    while (name[length] != '\0' && length + 1u < sizeof(out->name)) {
        out->name[length] = name[length];
        ++length;
    }
    out->name_size = (uint32_t)length;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI select_output(uint32_t index) {
    if (active_provider == 0) return EFI_NOT_FOUND;
    if (active_provider->platform.select_output != 0)
        return active_provider->platform.select_output(active_provider->platform.context, index);
    return index == 0u ? EFI_SUCCESS : EFI_NOT_FOUND;
}

static EFI_STATUS EFIAPI create_voice(const OC_AUDIO_VOICE_DESC *desc,
                                      OC_AUDIO_VOICE **out) {
    UINTN i;
    if (active_provider == 0 || desc == 0 || out == 0 ||
        desc->structure_size < sizeof(*desc)) return EFI_INVALID_PARAMETER;
    if (!oc_audio_abi_compatible(desc->requested_abi)) return EFI_UNSUPPORTED;
    if (desc->sample_bits != 16u || (desc->channels != 1u && desc->channels != 2u) ||
        desc->sample_rate < OC_AUDIO_MIN_SOURCE_RATE ||
        desc->sample_rate > OC_AUDIO_MAX_SOURCE_RATE ||
        desc->gain_q16 < 0 || desc->gain_q16 > (Q16_ONE * 4) ||
        desc->pan_q16 < -Q16_ONE || desc->pan_q16 > Q16_ONE)
        return EFI_UNSUPPORTED;
    for (i = 0u; i < OC_AUDIO_MAX_VOICES; ++i) {
        oc_audio_voice *voice = &active_provider->voices[i];
        if (voice->active) continue;
        zero_bytes(voice, sizeof(*voice));
        voice->slot = i;
        voice->generation = active_provider->generation;
        voice->sample_rate = desc->sample_rate;
        voice->channels = desc->channels;
        voice->gain_q16 = desc->gain_q16;
        voice->pan_q16 = desc->pan_q16;
        voice->byte_quota = desc->byte_quota == 0u ||
                            desc->byte_quota > OC_AUDIO_MAX_QUEUED_BYTES_PER_VOICE ?
                            OC_AUDIO_MAX_QUEUED_BYTES_PER_VOICE : desc->byte_quota;
        voice->state = OC_AUDIO_VOICE_PLAYING;
        voice->active = 1u;
        *out = voice;
        return EFI_SUCCESS;
    }
    return EFI_OUT_OF_RESOURCES;
}

static EFI_STATUS EFIAPI destroy_voice(OC_AUDIO_VOICE *handle) {
    oc_audio_voice *voice = (oc_audio_voice *)handle;
    if (!valid_voice(handle)) return EFI_INVALID_PARAMETER;
    release_queue(active_provider, voice);
    zero_bytes(voice, sizeof(*voice));
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI submit(OC_AUDIO_VOICE *handle,
                                const OC_AUDIO_BUFFER *buffer, uint32_t *result) {
    oc_audio_voice *voice = (oc_audio_voice *)handle;
    oc_audio_queue_entry *entry;
    uint64_t bytes;
    UINTN i;
    if (!valid_voice(handle) || buffer == 0 || result == 0 ||
        buffer->structure_size < sizeof(*buffer) || buffer->samples == 0 ||
        buffer->frame_count == 0u || buffer->presentation_frame != 0u ||
        buffer->flags != OC_AUDIO_SUBMIT_COPY) return EFI_INVALID_PARAMETER;
    if (buffer->frame_count > (uint64_t)(~(UINTN)0) /
        ((UINTN)voice->channels * sizeof(int16_t))) return EFI_INVALID_PARAMETER;
    bytes = buffer->frame_count * voice->channels * sizeof(int16_t);
    if (voice->queue_count == OC_AUDIO_MAX_QUEUE_BUFFERS ||
        bytes > voice->byte_quota - voice->queued_bytes) {
        *result = OC_AUDIO_RESULT_BACKPRESSURE;
        return EFI_SUCCESS;
    }
    i = (voice->queue_head + voice->queue_count) % OC_AUDIO_MAX_QUEUE_BUFFERS;
    entry = &voice->queue[i];
    entry->samples = active_provider->platform.allocate(
        active_provider->platform.context, (UINTN)bytes);
    if (entry->samples == 0) return EFI_OUT_OF_RESOURCES;
    for (i = 0u; i < (UINTN)(bytes / sizeof(int16_t)); ++i)
        entry->samples[i] = buffer->samples[i];
    entry->frames = buffer->frame_count;
    entry->bytes = bytes;
    ++voice->queue_count;
    voice->queued_frames += buffer->frame_count;
    voice->queued_bytes += bytes;
    voice->underrun_latched = 0u;
    if (voice->state == OC_AUDIO_VOICE_STOPPED) voice->state = OC_AUDIO_VOICE_PLAYING;
    *result = OC_AUDIO_RESULT_OK;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI set_gain_pan(OC_AUDIO_VOICE *handle, int32_t gain, int32_t pan) {
    oc_audio_voice *voice = (oc_audio_voice *)handle;
    if (!valid_voice(handle) || gain < 0 || gain > Q16_ONE * 4 ||
        pan < -Q16_ONE || pan > Q16_ONE) return EFI_INVALID_PARAMETER;
    voice->gain_q16 = gain;
    voice->pan_q16 = pan;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI pause_voice(OC_AUDIO_VOICE *handle) {
    oc_audio_voice *voice = (oc_audio_voice *)handle;
    if (!valid_voice(handle)) return EFI_INVALID_PARAMETER;
    voice->state = OC_AUDIO_VOICE_PAUSED;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI resume_voice(OC_AUDIO_VOICE *handle) {
    oc_audio_voice *voice = (oc_audio_voice *)handle;
    if (!valid_voice(handle)) return EFI_INVALID_PARAMETER;
    voice->state = OC_AUDIO_VOICE_PLAYING;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI flush_voice(OC_AUDIO_VOICE *handle) {
    oc_audio_voice *voice = (oc_audio_voice *)handle;
    if (!valid_voice(handle)) return EFI_INVALID_PARAMETER;
    release_queue(active_provider, voice);
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI stop_voice(OC_AUDIO_VOICE *handle) {
    oc_audio_voice *voice = (oc_audio_voice *)handle;
    EFI_STATUS status = flush_voice(handle);
    if (!EFI_ERROR(status)) voice->state = OC_AUDIO_VOICE_STOPPED;
    return status;
}

static EFI_STATUS EFIAPI query_voice(OC_AUDIO_VOICE *handle,
                                     OC_AUDIO_VOICE_STATUS *out) {
    oc_audio_voice *voice = (oc_audio_voice *)handle;
    if (!valid_voice(handle) || out == 0 || out->structure_size < sizeof(*out))
        return EFI_INVALID_PARAMETER;
    zero_bytes(out, sizeof(*out));
    out->structure_size = sizeof(*out);
    out->state = voice->state;
    out->source_frames_consumed = voice->source_frames_consumed +
                                  (voice->phase_q32 >> 32);
    out->queued_frames = voice->queued_frames - (voice->phase_q32 >> 32);
    out->queued_bytes = voice->queued_bytes;
    out->underrun_count = voice->underrun_count;
    out->output_latency_frames = active_provider->platform.output_latency_frames;
    out->owner_generation = voice->generation;
    return EFI_SUCCESS;
}

static int32_t interpolate(const oc_audio_voice *voice,
                           const oc_audio_queue_entry *entry, UINTN channel) {
    uint64_t index = voice->phase_q32 >> 32;
    uint32_t fraction = (uint32_t)voice->phase_q32;
    uint64_t next = index + 1u < entry->frames ? index + 1u : index;
    int32_t first = entry->samples[index * voice->channels + channel];
    int32_t second = entry->samples[next * voice->channels + channel];
    return first + (int32_t)(((int64_t)(second - first) * fraction) /
                             ((int64_t)1 << 32));
}

static void consume_finished(oc_audio_provider *provider, oc_audio_voice *voice) {
    while (voice->queue_count != 0u) {
        oc_audio_queue_entry *entry = &voice->queue[voice->queue_head];
        uint64_t limit = entry->frames << 32;
        if (voice->phase_q32 < limit) break;
        voice->phase_q32 -= limit;
        voice->source_frames_consumed += entry->frames;
        voice->queued_frames -= entry->frames;
        voice->queued_bytes -= entry->bytes;
        provider->platform.release(provider->platform.context, entry->samples);
        zero_bytes(entry, sizeof(*entry));
        voice->queue_head = (voice->queue_head + 1u) % OC_AUDIO_MAX_QUEUE_BUFFERS;
        --voice->queue_count;
    }
}

static int16_t saturate(int64_t value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)value;
}

EFI_STATUS oc_audio_provider_render(oc_audio_provider *provider,
                                    int16_t *output, UINTN frames) {
    UINTN frame, slot;
    if (provider == 0 || provider != active_provider || output == 0)
        return EFI_INVALID_PARAMETER;
    for (frame = 0u; frame < frames; ++frame) {
        int64_t mix_left = 0;
        int64_t mix_right = 0;
        for (slot = 0u; slot < OC_AUDIO_MAX_VOICES; ++slot) {
            oc_audio_voice *voice = &provider->voices[slot];
            oc_audio_queue_entry *entry;
            int32_t left, right;
            int64_t left_gain, right_gain;
            if (!voice->active || voice->state != OC_AUDIO_VOICE_PLAYING) continue;
            consume_finished(provider, voice);
            if (voice->queue_count == 0u) {
                if (!voice->underrun_latched) {
                    ++voice->underrun_count;
                    ++provider->underrun_count;
                    voice->underrun_latched = 1u;
                }
                continue;
            }
            entry = &voice->queue[voice->queue_head];
            left = interpolate(voice, entry, 0u);
            right = voice->channels == 2u ? interpolate(voice, entry, 1u) : left;
            left_gain = voice->gain_q16;
            right_gain = voice->gain_q16;
            if (voice->pan_q16 > 0)
                left_gain = left_gain * (Q16_ONE - voice->pan_q16) / Q16_ONE;
            else if (voice->pan_q16 < 0)
                right_gain = right_gain * (Q16_ONE + voice->pan_q16) / Q16_ONE;
            mix_left += (left * left_gain) / Q16_ONE;
            mix_right += (right * right_gain) / Q16_ONE;
            voice->phase_q32 += ((uint64_t)voice->sample_rate << 32) /
                                OC_AUDIO_MIX_RATE;
            consume_finished(provider, voice);
        }
        output[frame * 2u] = saturate(mix_left);
        output[frame * 2u + 1u] = saturate(mix_right);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI pump(uint64_t now_usec) {
    int16_t samples[OC_AUDIO_RENDER_CHUNK * OC_AUDIO_MIX_CHANNELS];
    uint64_t elapsed;
    uint64_t wanted;
    if (active_provider == 0) return EFI_NOT_READY;
    if (!active_provider->pump_initialized) {
        active_provider->last_pump_usec = now_usec;
        active_provider->pump_initialized = 1u;
        return EFI_SUCCESS;
    }
    if (now_usec < active_provider->last_pump_usec) return EFI_INVALID_PARAMETER;
    elapsed = now_usec - active_provider->last_pump_usec;
    wanted = elapsed * OC_AUDIO_MIX_RATE + active_provider->fractional_output_frames;
    active_provider->last_pump_usec = now_usec;
    active_provider->fractional_output_frames = wanted % 1000000u;
    wanted /= 1000000u;
    active_provider->pending_output_frames += wanted;
    while (active_provider->pending_output_frames >= OC_AUDIO_RENDER_CHUNK) {
        UINTN frames = OC_AUDIO_RENDER_CHUNK;
        EFI_STATUS status;
        if (active_provider->platform.can_write != 0 &&
            !active_provider->platform.can_write(active_provider->platform.context))
            break;
        status = oc_audio_provider_render(active_provider, samples, frames);
        if (EFI_ERROR(status)) return status;
        status = active_provider->platform.write(active_provider->platform.context,
                                                 samples, frames);
        if (EFI_ERROR(status)) return status;
        active_provider->pending_output_frames -= frames;
    }
    return EFI_SUCCESS;
}

EFI_STATUS oc_audio_provider_initialize(oc_audio_provider *provider,
                                        const oc_audio_platform *platform) {
    if (provider == 0 || platform == 0 || platform->allocate == 0 ||
        platform->release == 0 || platform->write == 0 ||
        platform->output_name == 0 ||
        (active_provider != 0 && active_provider != provider)) return EFI_INVALID_PARAMETER;
    zero_bytes(provider, sizeof(*provider));
    provider->platform = *platform;
    provider->protocol.structure_size = sizeof(provider->protocol);
    provider->protocol.abi_version = OC_AUDIO_ABI_VERSION;
    provider->protocol.query_capabilities = query_capabilities;
    provider->protocol.get_output = get_output;
    provider->protocol.select_output = select_output;
    provider->protocol.create_voice = create_voice;
    provider->protocol.destroy_voice = destroy_voice;
    provider->protocol.submit = submit;
    provider->protocol.set_gain_pan = set_gain_pan;
    provider->protocol.pause = pause_voice;
    provider->protocol.resume = resume_voice;
    provider->protocol.flush = flush_voice;
    provider->protocol.stop = stop_voice;
    provider->protocol.query_voice = query_voice;
    provider->protocol.pump = pump;
    active_provider = provider;
    return EFI_SUCCESS;
}

EFI_STATUS oc_audio_provider_begin_generation(oc_audio_provider *provider,
                                              uint64_t generation) {
    if (provider == 0 || provider != active_provider || generation == 0u)
        return EFI_INVALID_PARAMETER;
    provider->generation = generation;
    return EFI_SUCCESS;
}

void oc_audio_provider_cleanup_generation(oc_audio_provider *provider,
                                          uint64_t generation) {
    UINTN i;
    if (provider == 0 || provider != active_provider) return;
    for (i = 0u; i < OC_AUDIO_MAX_VOICES; ++i)
        if (provider->voices[i].active && provider->voices[i].generation == generation)
            (void)destroy_voice(&provider->voices[i]);
}

UINTN oc_audio_provider_active_voices(const oc_audio_provider *provider,
                                      uint64_t generation) {
    UINTN i, count = 0u;
    if (provider == 0) return 0u;
    for (i = 0u; i < OC_AUDIO_MAX_VOICES; ++i)
        count += provider->voices[i].active &&
                 provider->voices[i].generation == generation;
    return count;
}

void oc_audio_provider_shutdown(oc_audio_provider *provider) {
    UINTN i;
    if (provider == 0) return;
    for (i = 0u; i < OC_AUDIO_MAX_VOICES; ++i)
        if (provider->voices[i].active) (void)destroy_voice(&provider->voices[i]);
    if (active_provider == provider) active_provider = 0;
    zero_bytes(provider, sizeof(*provider));
}
