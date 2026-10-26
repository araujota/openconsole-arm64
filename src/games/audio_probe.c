#include "openconsole/audio_protocol.h"

#define Q16_ONE 65536
#define TOTAL_FRAMES (OC_AUDIO_OUTPUT_CHUNK_FRAMES * 21u)
#define EFI_EVENT_TIMER 0x80000000u
#define EFI_TPL_CALLBACK 8u
#define EFI_TIMER_RELATIVE 2u
#define EVIDENCE_CAPACITY 16384u
#define CONFIG_CAPACITY 256u

static const EFI_GUID loaded_image_guid =
    {0x5b1b31a1u,0x9562u,0x11d2u,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const EFI_GUID simple_fs_guid =
    {0x964e5b22u,0x6459u,0x11d2u,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static EFI_FILE_PROTOCOL *evidence_file;

static int16_t music_samples[TOTAL_FRAMES * 2u];
static int16_t effect_samples[TOTAL_FRAMES];

static void debug_putc(char value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"((uint16_t)0x402));
}

static void debug_write(const char *text) {
    while (*text != '\0') debug_putc(*text++);
}

static void evidence(const char *text) {
    UINTN size = 0u, written;
    while (text[size] != '\0') { debug_putc(text[size]); ++size; }
    if (evidence_file == 0 || size == 0u) return;
    written = size;
    if (EFI_ERROR(evidence_file->write(evidence_file, &written, text)) || written != size)
        evidence_file = 0;
}

static void evidence_u32(uint32_t value) {
    char text[11]; UINTN digits = 0u, i;
    do { text[digits++] = (char)('0' + value % 10u); value /= 10u; } while (value != 0u);
    for (i = 0u; i < digits / 2u; ++i) {
        char swap = text[i]; text[i] = text[digits - i - 1u]; text[digits - i - 1u] = swap;
    }
    text[digits] = '\0'; evidence(text);
}

static int contains(const char *text, const char *needle) {
    UINTN i, j;
    if (needle == 0 || needle[0] == '\0') return 1;
    for (i = 0u; text[i] != '\0'; ++i) {
        for (j = 0u; needle[j] != '\0' && text[i+j] == needle[j]; ++j) { }
        if (needle[j] == '\0') return 1;
    }
    return 0;
}

static int same_text(const char *left, const char *right) {
    UINTN index = 0u;
    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) return 0;
        ++index;
    }
    return left[index] == right[index];
}

static EFI_STATUS open_root(EFI_HANDLE image_handle, EFI_BOOT_SERVICES *bs,
                            EFI_FILE_PROTOCOL **root) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_STATUS status = bs->handle_protocol(image_handle, &loaded_image_guid, (void **)&loaded);
    if (!EFI_ERROR(status) && loaded != 0 && loaded->device_handle != 0)
        status = bs->handle_protocol(loaded->device_handle, &simple_fs_guid, (void **)&fs);
    if (EFI_ERROR(status) || fs == 0)
        status = bs->locate_protocol(&simple_fs_guid, 0, (void **)&fs);
    return EFI_ERROR(status) || fs == 0 ? EFI_NOT_FOUND : fs->open_volume(fs, root);
}

static void open_evidence(EFI_FILE_PROTOCOL *root) {
    static const CHAR16 path[] = L"\\OPENCONSOLE-AUDIO-EVIDENCE.LOG";
    static uint8_t existing[EVIDENCE_CAPACITY];
    UINTN size = sizeof(existing), used = 0u;
    if (root == 0 || EFI_ERROR(root->open(root, &evidence_file, path,
            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0u))) return;
    if (EFI_ERROR(evidence_file->read(evidence_file, &size, existing))) { evidence_file = 0; return; }
    while (used < size && existing[used] != 0u) ++used;
    if (used > EVIDENCE_CAPACITY - 2048u) used = 0u;
    if (EFI_ERROR(evidence_file->set_position(evidence_file, used))) evidence_file = 0;
}

static int decimal_u32(const char *text, uint32_t *value) {
    uint32_t parsed = 0u;
    UINTN index = 0u;
    if (text == 0 || text[0] == '\0') return 0;
    while (text[index] != '\0') {
        uint32_t digit;
        if (text[index] < '0' || text[index] > '9') return 0;
        digit = (uint32_t)(text[index] - '0');
        if (parsed > (0xffffffffu - digit) / 10u) return 0;
        parsed = parsed * 10u + digit;
        ++index;
    }
    *value = parsed;
    return 1;
}

static int read_selection(EFI_FILE_PROTOCOL *root, char *pci, char *device,
                          uint32_t *output_index, int *has_output_index) {
    static const CHAR16 path[] = L"\\OPENCONSOLE-AUDIO.CFG";
    static char bytes[CONFIG_CAPACITY];
    EFI_FILE_PROTOCOL *file = 0; UINTN size = sizeof(bytes) - 1u, cursor = 0u;
    pci[0] = '\0'; device[0] = '\0'; *output_index = 0u; *has_output_index = 0;
    if (root == 0 || EFI_ERROR(root->open(root, &file, path, EFI_FILE_MODE_READ, 0u))) return 0;
    if (EFI_ERROR(file->read(file, &size, bytes)) || size >= sizeof(bytes)) size = 0u;
    (void)file->close(file); bytes[size] = '\0';
    while (cursor < size) {
        char *key = bytes + cursor, *value = 0; UINTN i;
        while (cursor < size && bytes[cursor] != '\n' && bytes[cursor] != '\r') {
            if (bytes[cursor] == '=' && value == 0) { bytes[cursor] = '\0'; value = bytes + cursor + 1u; }
            ++cursor;
        }
        while (cursor < size && (bytes[cursor] == '\n' || bytes[cursor] == '\r')) bytes[cursor++] = '\0';
        if (value == 0) continue;
        if (same_text(key, "pci_id")) {
            for (i=0u;i<9u&&value[i]!='\0';++i) pci[i]=value[i]; pci[i]='\0';
        } else if (same_text(key, "device")) {
            for (i=0u;i<15u&&value[i]!='\0';++i) device[i]=value[i]; device[i]='\0';
        } else if (same_text(key, "output_index")) {
            *has_output_index = decimal_u32(value, output_index);
        }
    }
    return 1;
}

static void close_evidence(EFI_FILE_PROTOCOL *root) {
    if (evidence_file != 0) { (void)evidence_file->flush(evidence_file); (void)evidence_file->close(evidence_file); }
    evidence_file = 0; if (root != 0) (void)root->close(root);
}

static void fill_reference(void) {
    UINTN frame;
    uint32_t music_phase = 0u;
    uint32_t effect_phase = 0u;
    for (frame = 0u; frame < TOTAL_FRAMES; ++frame) {
        int music_on = (frame >= 12000u && frame < 60000u) || frame >= 72000u;
        int effect_on = frame >= 28800u && frame < 43200u;
        int16_t music = music_on && (music_phase & 0x80000000u) != 0u ? 4000 :
                        music_on ? -4000 : 0;
        int16_t effect = effect_on && (effect_phase & 0x80000000u) != 0u ? 6000 :
                         effect_on ? -6000 : 0;
        music_samples[frame * 2u] = music;
        music_samples[frame * 2u + 1u] = music;
        effect_samples[frame] = effect;
        music_phase += (uint32_t)(((uint64_t)440u << 32) / OC_AUDIO_MIX_RATE);
        effect_phase += (uint32_t)(((uint64_t)880u << 32) / OC_AUDIO_MIX_RATE);
    }
}

static EFI_STATUS submit_chunk(OC_AUDIO_PROTOCOL *audio, OC_AUDIO_VOICE *voice,
                               const int16_t *samples, UINTN frames) {
    OC_AUDIO_BUFFER buffer = {sizeof(buffer), OC_AUDIO_SUBMIT_COPY, samples, frames, 0u};
    uint32_t result = OC_AUDIO_RESULT_BACKEND_FAILURE;
    EFI_STATUS status = audio->submit(voice, &buffer, &result);
    if (EFI_ERROR(status)) return status;
    return result == OC_AUDIO_RESULT_OK ? EFI_SUCCESS : EFI_NOT_READY;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    OC_AUDIO_PROTOCOL *audio = 0;
    OC_AUDIO_VOICE *music = 0;
    OC_AUDIO_VOICE *effect = 0;
    OC_AUDIO_VOICE_DESC music_desc = {sizeof(music_desc), OC_AUDIO_ABI_VERSION,
        OC_AUDIO_MIX_RATE, 2u, 16u, Q16_ONE, 0, 1024u * 1024u};
    OC_AUDIO_VOICE_DESC effect_desc = {sizeof(effect_desc), OC_AUDIO_ABI_VERSION,
        OC_AUDIO_MIX_RATE, 1u, 16u, Q16_ONE, -Q16_ONE, 1024u * 1024u};
    uint64_t now_usec = 0u;
    EFI_EVENT pace_event = 0;
    UINTN event_index = 0u;
    OC_AUDIO_VOICE_STATUS voice_status = {sizeof(voice_status), 0u, 0u, 0u,
                                           0u, 0u, 0u, 0u};
    OC_AUDIO_CAPABILITIES caps = {0};
    EFI_FILE_PROTOCOL *root = 0;
    char pci[10], device[16];
    uint32_t configured_output = 0u;
    int config_present, has_output_index;
    uint32_t output;
    (void)open_root(image_handle, system_table->boot_services, &root);
    open_evidence(root);
    config_present = read_selection(root, pci, device, &configured_output, &has_output_index);
    caps.structure_size = sizeof(caps);
    evidence("OPENCONSOLE_PHYSICAL_AUDIO_RUN_BEGIN\n");
    if (EFI_ERROR(system_table->boot_services->locate_protocol(
            &OC_AUDIO_PROTOCOL_GUID, 0, (void **)&audio)) || audio == 0) {
        debug_write("OPENCONSOLE_AUDIO_CHILD_PROTOCOL_MISSING\n");
        evidence("OPENCONSOLE_PHYSICAL_AUDIO_PROTOCOL_MISSING\n"); close_evidence(root);
        return EFI_NOT_FOUND;
    }
    if (EFI_ERROR(audio->query_capabilities(&caps))) { close_evidence(root); return EFI_DEVICE_ERROR; }
    evidence("OPENCONSOLE_PHYSICAL_AUDIO_OUTPUT_COUNT_"); evidence_u32(caps.output_count); evidence("\n");
    for (output = 0u; output < caps.output_count; ++output) {
        OC_AUDIO_OUTPUT_INFO info = {sizeof(info),0u,0u,0u,{0}};
        if (EFI_ERROR(audio->get_output(output, &info))) continue;
        evidence("OPENCONSOLE_PHYSICAL_AUDIO_OUTPUT_"); evidence_u32(output); evidence("_"); evidence(info.name); evidence("\n");
        if (config_present && has_output_index && output == configured_output &&
            pci[0] != '\0' && device[0] != '\0' &&
            contains(info.name, pci) && contains(info.name, device)) {
            if (EFI_ERROR(audio->select_output(output))) { close_evidence(root); return EFI_DEVICE_ERROR; }
            evidence("OPENCONSOLE_PHYSICAL_AUDIO_SELECTED_"); evidence_u32(output); evidence("_"); evidence(info.name); evidence("\n");
            pci[0] = '\0';
        }
    }
    if (config_present && (!has_output_index || pci[0] != '\0')) {
        evidence("OPENCONSOLE_PHYSICAL_AUDIO_SELECTION_NOT_FOUND\n"); close_evidence(root); return EFI_NOT_FOUND;
    }
    if (audio->abi_version != OC_AUDIO_ABI_VERSION ||
        EFI_ERROR(audio->create_voice(&music_desc, &music)) ||
        EFI_ERROR(audio->create_voice(&effect_desc, &effect))) {
        debug_write("OPENCONSOLE_AUDIO_CHILD_SETUP_FAILED\n");
        close_evidence(root);
        return EFI_UNSUPPORTED;
    }
    if (EFI_ERROR(system_table->boot_services->create_event(
            EFI_EVENT_TIMER, EFI_TPL_CALLBACK, 0, 0, &pace_event)))
        { close_evidence(root); return EFI_OUT_OF_RESOURCES; }
    fill_reference();
    if (EFI_ERROR(submit_chunk(audio, music, music_samples, TOTAL_FRAMES)) ||
        EFI_ERROR(submit_chunk(audio, effect, effect_samples, TOTAL_FRAMES))) {
        debug_write("OPENCONSOLE_AUDIO_CHILD_SUBMIT_FAILED\n");
        close_evidence(root);
        return EFI_OUT_OF_RESOURCES;
    }
    (void)audio->pump(0u);
    debug_write("OPENCONSOLE_AUDIO_CHILD_READY\n");
    while (now_usec < 10000000u) {
        /* OVMF/QEMU's HDA backend is host-paced while its guest timer runs
         * faster on this CI host. Pace the fixture conservatively so DMA is
         * externally captured rather than merely accepted by the controller. */
        if (EFI_ERROR(system_table->boot_services->set_timer(
                pace_event, EFI_TIMER_RELATIVE, 1700000u)) ||
            EFI_ERROR(system_table->boot_services->wait_for_event(
                1u, &pace_event, &event_index))) {
            (void)system_table->boot_services->close_event(pace_event);
            close_evidence(root);
            return EFI_DEVICE_ERROR;
        }
        now_usec += 21333u;
        (void)audio->pump(now_usec);
        if (!EFI_ERROR(audio->query_voice(music, &voice_status)) &&
            voice_status.source_frames_consumed >= TOTAL_FRAMES) break;
    }
    if (voice_status.source_frames_consumed < TOTAL_FRAMES) {
        debug_write("OPENCONSOLE_AUDIO_CHILD_DRAIN_TIMEOUT\n");
        (void)system_table->boot_services->close_event(pace_event);
        close_evidence(root); return EFI_TIMEOUT;
    }
    (void)system_table->boot_services->set_timer(
        pace_event, EFI_TIMER_RELATIVE, 1000000u);
    (void)system_table->boot_services->wait_for_event(1u, &pace_event, &event_index);
    (void)system_table->boot_services->close_event(pace_event);
    debug_write("OPENCONSOLE_AUDIO_CHILD_REFERENCE_COMPLETE\n");
    evidence("OPENCONSOLE_PHYSICAL_AUDIO_FRAMES_CONSUMED_"); evidence_u32((uint32_t)voice_status.source_frames_consumed); evidence("\n");
    evidence("OPENCONSOLE_PHYSICAL_AUDIO_UNDERRUNS_"); evidence_u32((uint32_t)voice_status.underrun_count); evidence("\n");
    evidence("OPENCONSOLE_PHYSICAL_AUDIO_RUN_COMPLETE\n"); close_evidence(root);
    /* Return with playback active so the parent must stop DMA and reclaim voices. */
    return EFI_SUCCESS;
}
