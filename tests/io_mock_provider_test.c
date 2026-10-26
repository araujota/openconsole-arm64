#include "openconsole/io_mock_provider.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    static const int16_t samples[] = {1, -1, 2, -2};
    oc_io_mock_provider provider;
    OC_NET_SESSION *session = 0;
    OC_NET_OPERATION *operation = 0;
    OC_AUDIO_VOICE *voice = 0;
    OC_NET_SESSION_DESC session_desc = {sizeof(session_desc), OC_NET_ABI_VERSION, 4096u, 2u, 0u};
    OC_NET_DEADLINE deadline = {sizeof(deadline), 0u, 100u};
    OC_AUDIO_VOICE_DESC voice_desc = {sizeof(voice_desc), OC_AUDIO_ABI_VERSION, 48000u, 2u, 16u, 65536, 0, 4096u};
    OC_AUDIO_BUFFER buffer = {sizeof(buffer), OC_AUDIO_SUBMIT_COPY, samples, 2u, 0u};
    uint32_t result = 99u;
    session_desc.requested_abi = OC_NET_ABI_VERSION + 1u;
    assert(oc_io_mock_initialize(&provider) == EFI_SUCCESS);
    assert(oc_io_mock_begin_generation(&provider, 7u) == EFI_SUCCESS);
    assert(provider.net.create_session(&session_desc, &session) == EFI_UNSUPPORTED);
    session_desc.requested_abi = OC_NET_ABI_VERSION;
    assert(provider.net.create_session(&session_desc, &session) == EFI_SUCCESS);
    assert(provider.net.configure_dhcp4(session, &deadline, &operation) == EFI_SUCCESS);
    voice_desc.requested_abi = (1u << 16);
    assert(provider.audio.create_voice(&voice_desc, &voice) == EFI_UNSUPPORTED);
    voice_desc.requested_abi = OC_AUDIO_ABI_VERSION;
    assert(provider.audio.create_voice(&voice_desc, &voice) == EFI_SUCCESS);
    assert(provider.audio.submit(voice, &buffer, &result) == EFI_SUCCESS);
    assert(result == OC_AUDIO_RESULT_OK);
    assert(oc_io_mock_active_resources(&provider, 7u) == 3u);
    oc_io_mock_cleanup_generation(&provider, 7u);
    assert(oc_io_mock_active_resources(&provider, 7u) == 0u);
    puts("PASS: mock network/audio generation cleanup");
    return 0;
}
