#include "openconsole/io_mock_provider.h"

struct OC_NET_SESSION { UINTN slot; };
struct OC_NET_OPERATION { UINTN slot; uint32_t result; };
struct OC_AUDIO_VOICE { UINTN slot; uint64_t frames; uint64_t bytes; uint32_t state; };

static oc_io_mock_provider *active_provider;
static struct OC_NET_SESSION net_sessions[OC_NET_MAX_SESSIONS];
static struct OC_NET_OPERATION net_operations[OC_NET_MAX_SESSIONS * OC_NET_MAX_OPERATIONS_PER_SESSION];
static struct OC_AUDIO_VOICE audio_voices[OC_AUDIO_MAX_VOICES];

static void zero_bytes(void *destination, UINTN size) {
    uint8_t *bytes = destination;
    while (size-- != 0u) *bytes++ = 0u;
}

static EFI_STATUS EFIAPI net_query_capabilities(OC_NET_CAPABILITIES *out) {
    if (out == 0 || out->structure_size < sizeof(*out)) return EFI_INVALID_PARAMETER;
    zero_bytes(out, sizeof(*out));
    out->structure_size = sizeof(*out);
    out->abi_version = OC_NET_ABI_VERSION;
    out->capabilities = OC_NET_CAP_IPV4 | OC_NET_CAP_DHCP4 | OC_NET_CAP_DNS4 |
                        OC_NET_CAP_UDP4 | OC_NET_CAP_TCP4 | OC_NET_CAP_HTTPS |
                        OC_NET_CAP_TLS_IDENTITY_VALIDATION;
    out->max_sessions = OC_NET_MAX_SESSIONS;
    out->max_operations_per_session = OC_NET_MAX_OPERATIONS_PER_SESSION;
    out->max_dns_addresses = OC_NET_MAX_DNS_ADDRESSES;
    out->max_response_bytes = OC_NET_MAX_RESPONSE_BYTES;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI net_create_session(const OC_NET_SESSION_DESC *desc,
                                             OC_NET_SESSION **out) {
    UINTN i;
    if (active_provider == 0 || desc == 0 || out == 0 ||
        desc->structure_size < sizeof(*desc)) return EFI_INVALID_PARAMETER;
    if (!oc_net_abi_compatible(desc->requested_abi)) return EFI_UNSUPPORTED;
    for (i = 0u; i < OC_NET_MAX_SESSIONS; ++i) {
        if (!active_provider->sessions[i].active) {
            active_provider->sessions[i].active = 1u;
            active_provider->sessions[i].generation = active_provider->generation;
            net_sessions[i].slot = i;
            *out = &net_sessions[i];
            return EFI_SUCCESS;
        }
    }
    return EFI_OUT_OF_RESOURCES;
}

static EFI_STATUS EFIAPI net_destroy_session(OC_NET_SESSION *session) {
    if (active_provider == 0 || session == 0 || session->slot >= OC_NET_MAX_SESSIONS ||
        !active_provider->sessions[session->slot].active) return EFI_INVALID_PARAMETER;
    active_provider->sessions[session->slot].active = 0u;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI net_query_state(OC_NET_SESSION *session, OC_NET_STATE *out) {
    if (active_provider == 0 || session == 0 || out == 0 ||
        out->structure_size < sizeof(*out) || session->slot >= OC_NET_MAX_SESSIONS ||
        !active_provider->sessions[session->slot].active) return EFI_INVALID_PARAMETER;
    zero_bytes(out, sizeof(*out));
    out->structure_size = sizeof(*out);
    out->link_up = 1u;
    out->configured = 1u;
    out->address_be = 0x0200000au;
    out->subnet_mask_be = 0x00ffffffu;
    out->gateway_be = 0x0100000au;
    out->owner_generation = active_provider->sessions[session->slot].generation;
    return EFI_SUCCESS;
}

static EFI_STATUS create_operation(OC_NET_SESSION *session, OC_NET_OPERATION **out) {
    UINTN i;
    if (active_provider == 0 || session == 0 || out == 0 ||
        session->slot >= OC_NET_MAX_SESSIONS ||
        !active_provider->sessions[session->slot].active) return EFI_INVALID_PARAMETER;
    for (i = 0u; i < sizeof(active_provider->operations) / sizeof(active_provider->operations[0]); ++i) {
        if (!active_provider->operations[i].active) {
            active_provider->operations[i].active = 1u;
            active_provider->operations[i].generation = active_provider->generation;
            net_operations[i].slot = i;
            net_operations[i].result = OC_NET_RESULT_PENDING;
            *out = &net_operations[i];
            return EFI_SUCCESS;
        }
    }
    return EFI_OUT_OF_RESOURCES;
}

static EFI_STATUS EFIAPI net_configure(OC_NET_SESSION *session,
                                       const OC_NET_DEADLINE *deadline,
                                       OC_NET_OPERATION **out) {
    if (deadline == 0 || deadline->structure_size < sizeof(*deadline)) return EFI_INVALID_PARAMETER;
    return create_operation(session, out);
}

static EFI_STATUS EFIAPI net_resolve(OC_NET_SESSION *session, const char *name,
                                     UINTN name_size, OC_NET_DNS_RESULT *result,
                                     const OC_NET_DEADLINE *deadline,
                                     OC_NET_OPERATION **out) {
    if (name == 0 || name_size == 0u || name_size > OC_NET_MAX_HOST_BYTES ||
        result == 0 || result->structure_size < sizeof(*result) || deadline == 0)
        return EFI_INVALID_PARAMETER;
    return create_operation(session, out);
}

static EFI_STATUS EFIAPI net_transfer(OC_NET_SESSION *session,
                                      const OC_NET_TRANSFER_DESC *desc,
                                      OC_NET_OPERATION **out) {
    if (desc == 0 || desc->structure_size < sizeof(*desc)) return EFI_INVALID_PARAMETER;
    return create_operation(session, out);
}

static EFI_STATUS EFIAPI net_https(OC_NET_SESSION *session,
                                   const OC_NET_HTTPS_DESC *desc,
                                   OC_NET_OPERATION **out) {
    if (desc == 0 || desc->structure_size < sizeof(*desc)) return EFI_INVALID_PARAMETER;
    return create_operation(session, out);
}

static EFI_STATUS EFIAPI net_pump(OC_NET_SESSION *session, uint64_t now_usec) {
    UINTN i;
    (void)session;
    (void)now_usec;
    for (i = 0u; i < sizeof(net_operations) / sizeof(net_operations[0]); ++i)
        if (active_provider->operations[i].active) net_operations[i].result = OC_NET_RESULT_OK;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI net_query_operation(OC_NET_OPERATION *operation,
                                              OC_NET_OPERATION_STATUS *out) {
    if (active_provider == 0 || operation == 0 || out == 0 ||
        out->structure_size < sizeof(*out) ||
        operation->slot >= sizeof(net_operations) / sizeof(net_operations[0]) ||
        !active_provider->operations[operation->slot].active) return EFI_INVALID_PARAMETER;
    zero_bytes(out, sizeof(*out));
    out->structure_size = sizeof(*out);
    out->result = operation->result;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI net_cancel(OC_NET_OPERATION *operation) {
    if (active_provider == 0 || operation == 0 ||
        operation->slot >= sizeof(net_operations) / sizeof(net_operations[0]) ||
        !active_provider->operations[operation->slot].active) return EFI_INVALID_PARAMETER;
    operation->result = OC_NET_RESULT_CANCELLED;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI net_release(OC_NET_OPERATION *operation) {
    if (EFI_ERROR(net_cancel(operation))) return EFI_INVALID_PARAMETER;
    active_provider->operations[operation->slot].active = 0u;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI audio_query_capabilities(OC_AUDIO_CAPABILITIES *out) {
    if (out == 0 || out->structure_size < sizeof(*out)) return EFI_INVALID_PARAMETER;
    zero_bytes(out, sizeof(*out));
    out->structure_size = sizeof(*out);
    out->abi_version = OC_AUDIO_ABI_VERSION;
    out->capabilities = OC_AUDIO_CAP_COPY_SUBMIT;
    out->output_count = 1u;
    out->mix_rate = OC_AUDIO_MIX_RATE;
    out->mix_channels = OC_AUDIO_MIX_CHANNELS;
    out->sample_bits = 16u;
    out->max_voices = OC_AUDIO_MAX_VOICES;
    out->max_queue_buffers = OC_AUDIO_MAX_QUEUE_BUFFERS;
    out->max_queued_bytes_per_voice = OC_AUDIO_MAX_QUEUED_BYTES_PER_VOICE;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI audio_get_output(uint32_t index, OC_AUDIO_OUTPUT_INFO *out) {
    static const char name[] = "OpenConsole mock output";
    UINTN i;
    if (index != 0u || out == 0 || out->structure_size < sizeof(*out)) return EFI_NOT_FOUND;
    zero_bytes(out, sizeof(*out));
    out->structure_size = sizeof(*out);
    out->name_size = sizeof(name) - 1u;
    for (i = 0u; i < sizeof(name); ++i) out->name[i] = name[i];
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI audio_select_output(uint32_t index) {
    return index == 0u ? EFI_SUCCESS : EFI_NOT_FOUND;
}

static EFI_STATUS EFIAPI audio_create_voice(const OC_AUDIO_VOICE_DESC *desc,
                                            OC_AUDIO_VOICE **out) {
    UINTN i;
    if (active_provider == 0 || desc == 0 || out == 0 ||
        desc->structure_size < sizeof(*desc)) return EFI_INVALID_PARAMETER;
    if (!oc_audio_abi_compatible(desc->requested_abi)) return EFI_UNSUPPORTED;
    if (desc->sample_bits != 16u || (desc->channels != 1u && desc->channels != 2u))
        return EFI_UNSUPPORTED;
    for (i = 0u; i < OC_AUDIO_MAX_VOICES; ++i) {
        if (!active_provider->voices[i].active) {
            active_provider->voices[i].active = 1u;
            active_provider->voices[i].generation = active_provider->generation;
            audio_voices[i].slot = i;
            audio_voices[i].frames = 0u;
            audio_voices[i].bytes = 0u;
            audio_voices[i].state = OC_AUDIO_VOICE_PLAYING;
            *out = &audio_voices[i];
            return EFI_SUCCESS;
        }
    }
    return EFI_OUT_OF_RESOURCES;
}

static EFI_STATUS EFIAPI audio_destroy_voice(OC_AUDIO_VOICE *voice) {
    if (active_provider == 0 || voice == 0 || voice->slot >= OC_AUDIO_MAX_VOICES ||
        !active_provider->voices[voice->slot].active) return EFI_INVALID_PARAMETER;
    active_provider->voices[voice->slot].active = 0u;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI audio_submit(OC_AUDIO_VOICE *voice,
                                      const OC_AUDIO_BUFFER *buffer,
                                      uint32_t *result) {
    if (active_provider == 0 || voice == 0 || buffer == 0 || result == 0 ||
        buffer->structure_size < sizeof(*buffer) || buffer->samples == 0 ||
        (buffer->flags & OC_AUDIO_SUBMIT_COPY) == 0u) return EFI_INVALID_PARAMETER;
    voice->frames += buffer->frame_count;
    voice->bytes += buffer->frame_count * 4u;
    *result = OC_AUDIO_RESULT_OK;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI audio_set_gain_pan(OC_AUDIO_VOICE *voice, int32_t gain, int32_t pan) {
    (void)gain; (void)pan;
    return voice == 0 ? EFI_INVALID_PARAMETER : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI audio_pause(OC_AUDIO_VOICE *voice) { if (voice == 0) return EFI_INVALID_PARAMETER; voice->state = OC_AUDIO_VOICE_PAUSED; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI audio_resume(OC_AUDIO_VOICE *voice) { if (voice == 0) return EFI_INVALID_PARAMETER; voice->state = OC_AUDIO_VOICE_PLAYING; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI audio_flush(OC_AUDIO_VOICE *voice) { if (voice == 0) return EFI_INVALID_PARAMETER; voice->frames = voice->bytes = 0u; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI audio_stop(OC_AUDIO_VOICE *voice) { if (voice == 0) return EFI_INVALID_PARAMETER; voice->state = OC_AUDIO_VOICE_STOPPED; return audio_flush(voice); }

static EFI_STATUS EFIAPI audio_query_voice(OC_AUDIO_VOICE *voice,
                                           OC_AUDIO_VOICE_STATUS *out) {
    if (active_provider == 0 || voice == 0 || out == 0 || out->structure_size < sizeof(*out))
        return EFI_INVALID_PARAMETER;
    zero_bytes(out, sizeof(*out));
    out->structure_size = sizeof(*out);
    out->state = voice->state;
    out->queued_frames = voice->frames;
    out->queued_bytes = voice->bytes;
    out->owner_generation = active_provider->voices[voice->slot].generation;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI audio_pump(uint64_t now_usec) { (void)now_usec; return EFI_SUCCESS; }

EFI_STATUS oc_io_mock_initialize(oc_io_mock_provider *provider) {
    if (provider == 0 || (active_provider != 0 && active_provider != provider)) return EFI_INVALID_PARAMETER;
    zero_bytes(provider, sizeof(*provider));
    active_provider = provider;
    provider->net.structure_size = sizeof(provider->net);
    provider->net.abi_version = OC_NET_ABI_VERSION;
    provider->net.query_capabilities = net_query_capabilities;
    provider->net.create_session = net_create_session;
    provider->net.destroy_session = net_destroy_session;
    provider->net.query_state = net_query_state;
    provider->net.configure_dhcp4 = net_configure;
    provider->net.resolve_dns4 = net_resolve;
    provider->net.start_udp4 = net_transfer;
    provider->net.start_tcp4 = net_transfer;
    provider->net.start_https = net_https;
    provider->net.pump = net_pump;
    provider->net.query_operation = net_query_operation;
    provider->net.cancel_operation = net_cancel;
    provider->net.release_operation = net_release;
    provider->audio.structure_size = sizeof(provider->audio);
    provider->audio.abi_version = OC_AUDIO_ABI_VERSION;
    provider->audio.query_capabilities = audio_query_capabilities;
    provider->audio.get_output = audio_get_output;
    provider->audio.select_output = audio_select_output;
    provider->audio.create_voice = audio_create_voice;
    provider->audio.destroy_voice = audio_destroy_voice;
    provider->audio.submit = audio_submit;
    provider->audio.set_gain_pan = audio_set_gain_pan;
    provider->audio.pause = audio_pause;
    provider->audio.resume = audio_resume;
    provider->audio.flush = audio_flush;
    provider->audio.stop = audio_stop;
    provider->audio.query_voice = audio_query_voice;
    provider->audio.pump = audio_pump;
    return EFI_SUCCESS;
}

EFI_STATUS oc_io_mock_begin_generation(oc_io_mock_provider *provider, uint64_t generation) {
    if (provider == 0 || active_provider != provider || generation == 0u) return EFI_INVALID_PARAMETER;
    provider->generation = generation;
    return EFI_SUCCESS;
}

void oc_io_mock_cleanup_generation(oc_io_mock_provider *provider, uint64_t generation) {
    UINTN i;
    if (provider == 0) return;
    for (i = 0u; i < OC_NET_MAX_SESSIONS; ++i)
        if (provider->sessions[i].generation == generation) provider->sessions[i].active = 0u;
    for (i = 0u; i < sizeof(provider->operations) / sizeof(provider->operations[0]); ++i)
        if (provider->operations[i].generation == generation) provider->operations[i].active = 0u;
    for (i = 0u; i < OC_AUDIO_MAX_VOICES; ++i)
        if (provider->voices[i].generation == generation) provider->voices[i].active = 0u;
}

UINTN oc_io_mock_active_resources(const oc_io_mock_provider *provider, uint64_t generation) {
    UINTN i, count = 0u;
    if (provider == 0) return 0u;
    for (i = 0u; i < OC_NET_MAX_SESSIONS; ++i) count += provider->sessions[i].active && provider->sessions[i].generation == generation;
    for (i = 0u; i < sizeof(provider->operations) / sizeof(provider->operations[0]); ++i) count += provider->operations[i].active && provider->operations[i].generation == generation;
    for (i = 0u; i < OC_AUDIO_MAX_VOICES; ++i) count += provider->voices[i].active && provider->voices[i].generation == generation;
    return count;
}
