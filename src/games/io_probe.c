#include "openconsole/audio_protocol.h"
#include "openconsole/net_protocol.h"

static void debug_putc(char value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"((uint16_t)0x402));
}

static void debug_write(const char *text) {
    while (*text != '\0') debug_putc(*text++);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    static const int16_t samples[] = {1000, -1000, 2000, -2000};
    OC_NET_PROTOCOL *net = 0;
    OC_AUDIO_PROTOCOL *audio = 0;
    OC_NET_SESSION *session = 0;
    OC_NET_OPERATION *operation = 0;
    OC_AUDIO_VOICE *voice = 0;
    OC_NET_SESSION_DESC session_desc = {sizeof(session_desc), OC_NET_ABI_VERSION, 4096u, 2u, 0u};
    OC_NET_DEADLINE deadline = {sizeof(deadline), 0u, 1000000u};
    OC_AUDIO_VOICE_DESC voice_desc = {sizeof(voice_desc), OC_AUDIO_ABI_VERSION, 48000u, 2u, 16u, 65536, 0, 4096u};
    OC_AUDIO_BUFFER buffer = {sizeof(buffer), OC_AUDIO_SUBMIT_COPY, samples, 2u, 0u};
    EFI_INPUT_KEY key;
    UINTN index = 0u;
    uint32_t submit_result = OC_AUDIO_RESULT_BACKEND_FAILURE;
    (void)image_handle;
    if (EFI_ERROR(system_table->boot_services->locate_protocol(
            &OC_NET_PROTOCOL_GUID, 0, (void **)&net)) ||
        EFI_ERROR(system_table->boot_services->locate_protocol(
            &OC_AUDIO_PROTOCOL_GUID, 0, (void **)&audio))) {
        debug_write("OPENCONSOLE_IO_CHILD_PROTOCOL_MISSING\n");
        return EFI_NOT_FOUND;
    }
    if (net->abi_version != OC_NET_ABI_VERSION || audio->abi_version != OC_AUDIO_ABI_VERSION ||
        EFI_ERROR(net->create_session(&session_desc, &session)) ||
        EFI_ERROR(net->configure_dhcp4(session, &deadline, &operation)) ||
        EFI_ERROR(audio->create_voice(&voice_desc, &voice)) ||
        EFI_ERROR(audio->submit(voice, &buffer, &submit_result)) ||
        submit_result != OC_AUDIO_RESULT_OK) {
        debug_write("OPENCONSOLE_IO_CHILD_CALL_FAILED\n");
        return EFI_UNSUPPORTED;
    }
    debug_write("OPENCONSOLE_IO_CHILD_RESOURCES_LIVE\n");
    debug_write("OPENCONSOLE_CHILD_READY\n");
    for (;;) {
        if (!EFI_ERROR(system_table->boot_services->wait_for_event(
                1u, &system_table->con_in->wait_for_key, &index)) &&
            !EFI_ERROR(system_table->con_in->read_key_stroke(system_table->con_in, &key))) break;
    }
    debug_write("OPENCONSOLE_CHILD_KEY_RECEIVED\n");
    /* Deliberately leak all three resources. The parent owns their generation. */
    return EFI_SUCCESS;
}
