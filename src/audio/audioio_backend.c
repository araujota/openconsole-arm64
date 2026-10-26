#include "openconsole/audioio_backend.h"

#define BUFFER_FREE 0u
#define BUFFER_QUEUED 1u
#define BUFFER_ACTIVE 2u
#define NO_ACTIVE_BUFFER OC_AUDIO_IO_BUFFER_COUNT
#define EFI_LOCATE_BY_PROTOCOL 2u
#define EFI_PCI_IO_WIDTH_UINT32 2u

static const EFI_GUID pci_io_guid =
    {0x4cf5b200u,0x68b8u,0x4ca5u,{0x9e,0xec,0xb2,0x3e,0x3f,0x50,0x02,0x9a}};
static const EFI_GUID device_path_guid =
    {0x09576e91u,0x6d3fu,0x11d2u,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

typedef struct { uint8_t type, subtype, length[2]; } device_path_node;

const EFI_GUID OC_AUDIO_IO_PROTOCOL_GUID =
    {0x22266891u, 0x2032u, 0x4baeu, {0xb7,0xb5,0x43,0x74,0xe7,0x32,0x09,0x49}};

static void zero_bytes(void *destination, UINTN size) {
    uint8_t *bytes = destination;
    while (size-- != 0u) *bytes++ = 0u;
}

static UINTN path_size(const void *path) {
    const uint8_t *bytes = path;
    UINTN offset = 0u;
    if (path == 0) return 0u;
    while (offset + sizeof(device_path_node) <= 4096u) {
        const device_path_node *node = (const device_path_node *)(bytes + offset);
        UINTN length = node->length[0] | ((UINTN)node->length[1] << 8u);
        if (length < sizeof(*node) || offset + length > 4096u) return 0u;
        if (node->type == 0x7fu && node->subtype == 0xffu) return offset;
        offset += length;
    }
    return 0u;
}

static int path_prefix(const void *child, const void *parent, UINTN *length_out) {
    const uint8_t *left = child, *right = parent;
    UINTN parent_size = path_size(parent), child_size = path_size(child), i;
    if (parent_size == 0u || child_size < parent_size) return 0;
    for (i = 0u; i < parent_size; ++i) if (left[i] != right[i]) return 0;
    if (length_out != 0) *length_out = parent_size;
    return 1;
}

static void append_text(char **cursor, const char *text) {
    while (*text != '\0') *(*cursor)++ = *text++;
}

static void append_hex(char **cursor, uint32_t value, UINTN digits) {
    static const char hex[] = "0123456789ABCDEF";
    while (digits-- != 0u) *(*cursor)++ = hex[(value >> (digits * 4u)) & 15u];
}

static const char *device_name(uint32_t device) {
    static const char *names[] = {"line","speaker","headphones","spdif","mic","hdmi","other"};
    return device < sizeof(names) / sizeof(names[0]) ? names[device] : "unknown";
}

static void name_output(oc_audioio_output *output) {
    char *cursor = output->name;
    if (output->pci_class == 0x0cu && output->pci_subclass == 0x03u)
        append_text(&cursor, "USB Audio ");
    else if (output->pci_class == 0x04u && output->pci_subclass == 0x03u)
        append_text(&cursor, "HDA ");
    else append_text(&cursor, "Audio ");
    if (output->pci_vendor != 0xffffu) {
        append_hex(&cursor, output->pci_vendor, 4u); *cursor++ = ':';
        append_hex(&cursor, output->pci_device, 4u); append_text(&cursor, " rev ");
        append_hex(&cursor, output->pci_revision, 2u); *cursor++ = ' ';
    } else append_text(&cursor, "PCI unknown ");
    append_text(&cursor, device_name(output->device)); append_text(&cursor, " port ");
    append_hex(&cursor, output->port_index, 2u); append_text(&cursor, " loc ");
    append_hex(&cursor, output->location, 2u); append_text(&cursor, " surf ");
    append_hex(&cursor, output->surface, 2u); *cursor = '\0';
}

static void identify_controller(EFI_BOOT_SERVICES *bs, EFI_HANDLE audio_handle,
                                oc_audioio_output *output) {
    EFI_HANDLE *handles = 0;
    UINTN count = 0u, i, best = 0u;
    void *audio_path = 0;
    output->pci_vendor = 0xffffu;
    output->pci_device = 0xffffu;
    if (EFI_ERROR(bs->handle_protocol(audio_handle, &device_path_guid, &audio_path)) ||
        EFI_ERROR(bs->locate_handle_buffer(EFI_LOCATE_BY_PROTOCOL, &pci_io_guid, 0,
                                           &count, &handles))) return;
    for (i = 0u; i < count; ++i) {
        EFI_PCI_IO_PROTOCOL *pci = 0;
        void *pci_path = 0;
        UINTN prefix = 0u;
        uint32_t config[4] = {0u,0u,0u,0u};
        if (EFI_ERROR(bs->handle_protocol(handles[i], &device_path_guid, &pci_path)) ||
            !path_prefix(audio_path, pci_path, &prefix) || prefix < best ||
            EFI_ERROR(bs->handle_protocol(handles[i], &pci_io_guid, (void **)&pci)) ||
            pci == 0 || EFI_ERROR(pci->pci.read(pci, EFI_PCI_IO_WIDTH_UINT32,
                                               0u, 4u, config))) continue;
        best = prefix;
        output->pci_vendor = (uint16_t)config[0];
        output->pci_device = (uint16_t)(config[0] >> 16);
        output->pci_revision = (uint8_t)config[2];
        output->pci_subclass = (uint8_t)(config[2] >> 16);
        output->pci_class = (uint8_t)(config[2] >> 24);
    }
    if (handles != 0) (void)bs->free_pool(handles);
}

static EFI_TPL lock_backend(oc_audioio_backend *backend) {
    return backend->boot_services->raise_tpl(OC_AUDIO_IO_TPL_NOTIFY);
}

static void unlock_backend(oc_audioio_backend *backend, EFI_TPL old_tpl) {
    backend->boot_services->restore_tpl(old_tpl);
}

static EFI_STATUS start_next(oc_audioio_backend *backend);

static void EFIAPI playback_complete(oc_audio_io_protocol *audio_io, void *context) {
    oc_audioio_buffer *buffer = context;
    oc_audioio_backend *backend;
    (void)audio_io;
    if (buffer == 0 || buffer->owner == 0) return;
    backend = buffer->owner;
    if (backend->stopping || !backend->initialized) return;
    buffer->state = BUFFER_FREE;
    buffer->frames = 0u;
    backend->active_index = NO_ACTIVE_BUFFER;
}

static EFI_STATUS start_next(oc_audioio_backend *backend) {
    oc_audioio_buffer *buffer;
    UINTN index;
    EFI_STATUS status;
    if (backend->active_index != NO_ACTIVE_BUFFER || backend->queued == 0u)
        return EFI_SUCCESS;
    index = backend->queue_head;
    buffer = &backend->buffers[index];
    if (buffer->state != BUFFER_QUEUED) return EFI_DEVICE_ERROR;
    buffer->state = BUFFER_ACTIVE;
    backend->active_index = index;
    status = backend->audio_io->start_playback_async(
        backend->audio_io, buffer->samples,
        buffer->frames * OC_AUDIO_MIX_CHANNELS * sizeof(int16_t), 0u,
        playback_complete, buffer);
    if (EFI_ERROR(status)) {
        buffer->state = BUFFER_QUEUED;
        backend->active_index = NO_ACTIVE_BUFFER;
    } else {
        backend->queue_head = (index + 1u) % OC_AUDIO_IO_BUFFER_COUNT;
        --backend->queued;
    }
    return status;
}

static void *backend_allocate(void *context, UINTN size) {
    oc_audioio_backend *backend = context;
    void *memory = 0;
    if (EFI_ERROR(backend->boot_services->allocate_pool(EFI_LOADER_DATA, size, &memory)))
        return 0;
    return memory;
}

static void backend_release(void *context, void *memory) {
    oc_audioio_backend *backend = context;
    if (memory != 0) (void)backend->boot_services->free_pool(memory);
}

static EFI_STATUS backend_write(void *context, const int16_t *samples, UINTN frames) {
    oc_audioio_backend *backend = context;
    oc_audioio_buffer *buffer;
    EFI_TPL old_tpl;
    EFI_STATUS status;
    UINTN i;
    if (backend == 0 || samples == 0 || frames == 0u ||
        frames > OC_AUDIO_IO_BUFFER_FRAMES || !backend->initialized || backend->stopping)
        return EFI_INVALID_PARAMETER;
    old_tpl = lock_backend(backend);
    buffer = &backend->buffers[backend->queue_tail];
    if (buffer->state != BUFFER_FREE) {
        unlock_backend(backend, old_tpl);
        return EFI_NOT_READY;
    }
    for (i = 0u; i < frames * OC_AUDIO_MIX_CHANNELS; ++i)
        buffer->samples[i] = samples[i];
    buffer->frames = frames;
    buffer->state = BUFFER_QUEUED;
    backend->queue_tail = (backend->queue_tail + 1u) % OC_AUDIO_IO_BUFFER_COUNT;
    ++backend->queued;
    status = start_next(backend);
    unlock_backend(backend, old_tpl);
    return status;
}

static int backend_can_write(void *context) {
    oc_audioio_backend *backend = context;
    EFI_TPL old_tpl;
    int ready;
    if (backend == 0 || !backend->initialized || backend->stopping) return 0;
    old_tpl = lock_backend(backend);
    if (backend->active_index == NO_ACTIVE_BUFFER && backend->queued != 0u &&
        EFI_ERROR(start_next(backend))) {
        unlock_backend(backend, old_tpl);
        return 0;
    }
    ready = backend->buffers[backend->queue_tail].state == BUFFER_FREE;
    unlock_backend(backend, old_tpl);
    return ready;
}

static UINTN backend_output_count(void *context) {
    oc_audioio_backend *backend = context;
    return backend != 0 && backend->initialized ? backend->output_count : 0u;
}

static EFI_STATUS backend_output_info(void *context, UINTN index,
                                      char *name, UINTN capacity) {
    oc_audioio_backend *backend = context;
    UINTN i;
    if (backend == 0 || !backend->initialized || index >= backend->output_count ||
        name == 0 || capacity == 0u) return EFI_NOT_FOUND;
    for (i = 0u; i + 1u < capacity && backend->outputs[index].name[i] != '\0'; ++i)
        name[i] = backend->outputs[index].name[i];
    name[i] = '\0';
    return EFI_SUCCESS;
}

static EFI_STATUS backend_select_output(void *context, UINTN index) {
    oc_audioio_backend *backend = context;
    oc_audioio_output *output;
    EFI_STATUS status;
    if (backend == 0 || !backend->initialized || index >= backend->output_count)
        return EFI_NOT_FOUND;
    if (index == backend->selected_output) return EFI_SUCCESS;
    status = oc_audioio_backend_stop(backend);
    if (EFI_ERROR(status)) return status;
    output = &backend->outputs[index];
    status = output->audio_io->setup_playback(output->audio_io, output->output_mask, 0,
        OC_AUDIO_IO_FREQ_48KHZ, OC_AUDIO_IO_BITS_16, OC_AUDIO_MIX_CHANNELS, 0u);
    if (EFI_ERROR(status)) return status;
    backend->audio_io = output->audio_io;
    backend->output_mask = output->output_mask;
    backend->selected_output = index;
    return EFI_SUCCESS;
}

EFI_STATUS oc_audioio_backend_initialize(oc_audioio_backend *backend,
                                         EFI_BOOT_SERVICES *boot_services) {
    EFI_HANDLE *handles = 0;
    UINTN handle_count = 0u, index;
    EFI_STATUS status;
    if (backend == 0 || boot_services == 0 || boot_services->locate_handle_buffer == 0 ||
        boot_services->handle_protocol == 0 ||
        boot_services->allocate_pool == 0 || boot_services->free_pool == 0 ||
        boot_services->raise_tpl == 0 || boot_services->restore_tpl == 0)
        return EFI_INVALID_PARAMETER;
    zero_bytes(backend, sizeof(*backend));
    backend->boot_services = boot_services;
    backend->active_index = NO_ACTIVE_BUFFER;
    for (index = 0u; index < OC_AUDIO_IO_BUFFER_COUNT; ++index)
        backend->buffers[index].owner = backend;
    status = boot_services->locate_handle_buffer(EFI_LOCATE_BY_PROTOCOL,
        &OC_AUDIO_IO_PROTOCOL_GUID, 0, &handle_count, &handles);
    if (EFI_ERROR(status)) goto fail;
    for (index = 0u; index < handle_count && backend->output_count < OC_AUDIO_IO_MAX_OUTPUTS; ++index) {
        oc_audio_io_protocol *audio_io = 0;
        oc_audio_io_port *ports = 0;
        UINTN port_count = 0u, port;
        if (EFI_ERROR(boot_services->handle_protocol(handles[index],
                &OC_AUDIO_IO_PROTOCOL_GUID, (void **)&audio_io)) || audio_io == 0 ||
            audio_io->revision < OC_AUDIO_IO_PROTOCOL_REVISION || audio_io->get_outputs == 0 ||
            audio_io->setup_playback == 0 || audio_io->start_playback_async == 0 ||
            audio_io->stop_playback == 0) continue;
        status = audio_io->get_outputs(audio_io, &ports, &port_count);
        if (EFI_ERROR(status)) continue;
        for (port = 0u; port < port_count && port < 64u &&
             backend->output_count < OC_AUDIO_IO_MAX_OUTPUTS; ++port) {
            oc_audioio_output *output;
            if (ports[port].type != OC_AUDIO_IO_TYPE_OUTPUT ||
                (ports[port].supported_bits & OC_AUDIO_IO_BITS_16) == 0u ||
                (ports[port].supported_frequencies & OC_AUDIO_IO_FREQ_48KHZ) == 0u) continue;
            output = &backend->outputs[backend->output_count++];
            output->audio_io = audio_io;
            output->output_mask = (uint64_t)1u << port;
            output->port_index = (uint8_t)port;
            output->device = (uint8_t)ports[port].device;
            output->location = (uint8_t)ports[port].location;
            output->surface = (uint8_t)ports[port].surface;
            identify_controller(boot_services, handles[index], output);
            name_output(output);
        }
        if (ports != 0) (void)boot_services->free_pool(ports);
    }
    if (handles != 0) { (void)boot_services->free_pool(handles); handles = 0; }
    if (backend->output_count == 0u) { status = EFI_NOT_FOUND; goto fail; }
    backend->audio_io = backend->outputs[0].audio_io;
    backend->output_mask = backend->outputs[0].output_mask;
    status = backend->audio_io->setup_playback(
        backend->audio_io, backend->output_mask, 0,
        OC_AUDIO_IO_FREQ_48KHZ, OC_AUDIO_IO_BITS_16,
        OC_AUDIO_MIX_CHANNELS, 0u);
    if (EFI_ERROR(status)) goto fail;
    backend->initialized = 1u;
    return EFI_SUCCESS;

fail:
    if (handles != 0) (void)boot_services->free_pool(handles);
    zero_bytes(backend, sizeof(*backend));
    return status;
}

void oc_audioio_backend_get_platform(oc_audioio_backend *backend,
                                     oc_audio_platform *platform) {
    if (platform == 0) return;
    zero_bytes(platform, sizeof(*platform));
    if (backend == 0 || !backend->initialized) return;
    platform->context = backend;
    platform->allocate = backend_allocate;
    platform->release = backend_release;
    platform->write = backend_write;
    platform->output_name = "HDA AudioIo 48kHz stereo";
    platform->output_latency_frames =
        OC_AUDIO_IO_BUFFER_COUNT * OC_AUDIO_IO_BUFFER_FRAMES;
    platform->can_write = backend_can_write;
    platform->output_count = backend_output_count;
    platform->output_info = backend_output_info;
    platform->select_output = backend_select_output;
}

EFI_STATUS oc_audioio_backend_stop(oc_audioio_backend *backend) {
    EFI_STATUS status;
    UINTN index;
    EFI_TPL old_tpl;
    if (backend == 0 || !backend->initialized || backend->audio_io == 0)
        return EFI_NOT_READY;
    old_tpl = lock_backend(backend);
    backend->stopping = 1u;
    status = backend->audio_io->stop_playback(backend->audio_io);
    backend->queue_head = 0u;
    backend->queue_tail = 0u;
    backend->queued = 0u;
    backend->active_index = NO_ACTIVE_BUFFER;
    for (index = 0u; index < OC_AUDIO_IO_BUFFER_COUNT; ++index) {
        backend->buffers[index].state = BUFFER_FREE;
        backend->buffers[index].frames = 0u;
    }
    backend->stopping = 0u;
    unlock_backend(backend, old_tpl);
    return status;
}

void oc_audioio_backend_shutdown(oc_audioio_backend *backend) {
    if (backend == 0) return;
    if (backend->initialized) (void)oc_audioio_backend_stop(backend);
    zero_bytes(backend, sizeof(*backend));
}

UINTN oc_audioio_backend_pending_buffers(const oc_audioio_backend *backend) {
    if (backend == 0 || !backend->initialized) return 0u;
    return backend->queued + (backend->active_index != NO_ACTIVE_BUFFER ? 1u : 0u);
}
