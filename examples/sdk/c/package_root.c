#include "package_root.h"

#define MEDIA_DEVICE_PATH 0x04u
#define MEDIA_FILEPATH_DP 0x04u
#define END_DEVICE_PATH_TYPE 0x7fu
#define END_ENTIRE_DEVICE_PATH_SUBTYPE 0xffu
#define MAX_IMAGE_PATH_CHARS 512u

typedef struct {
    uint8_t type;
    uint8_t subtype;
    uint8_t length[2];
} oc_device_path_node;

static const EFI_GUID loaded_image_guid =
    {0x5b1b31a1u,0x9562u,0x11d2u,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const EFI_GUID simple_fs_guid =
    {0x964e5b22u,0x6459u,0x11d2u,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

static UINTN node_length(const oc_device_path_node *node) {
    return (UINTN)node->length[0] | ((UINTN)node->length[1] << 8u);
}

EFI_STATUS oc_example_open_package_root(EFI_HANDLE image,
                                        EFI_SYSTEM_TABLE *system_table,
                                        EFI_FILE_PROTOCOL **package_root) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *filesystem = 0;
    EFI_FILE_PROTOCOL *volume = 0;
    const oc_device_path_node *node;
    CHAR16 path[MAX_IMAGE_PATH_CHARS];
    UINTN used = 0u, index, slash = (UINTN)-1;
    EFI_STATUS status;
    if (image == 0 || system_table == 0 || system_table->boot_services == 0 ||
        package_root == 0) return EFI_INVALID_PARAMETER;
    *package_root = 0;
    status = system_table->boot_services->handle_protocol(
        image, &loaded_image_guid, (void **)&loaded);
    if (EFI_ERROR(status) || loaded == 0 || loaded->file_path == 0) return EFI_NOT_FOUND;
    status = system_table->boot_services->handle_protocol(
        loaded->device_handle, &simple_fs_guid, (void **)&filesystem);
    if (EFI_ERROR(status) || filesystem == 0) return EFI_NOT_FOUND;
    status = filesystem->open_volume(filesystem, &volume);
    if (EFI_ERROR(status)) return status;

    node = (const oc_device_path_node *)loaded->file_path;
    for (;;) {
        UINTN length = node_length(node);
        if (length < sizeof(*node)) { status = EFI_PROTOCOL_ERROR; goto fail; }
        if (node->type == END_DEVICE_PATH_TYPE) {
            if (node->subtype != END_ENTIRE_DEVICE_PATH_SUBTYPE) {
                status = EFI_PROTOCOL_ERROR; goto fail;
            }
            break;
        }
        if (node->type == MEDIA_DEVICE_PATH && node->subtype == MEDIA_FILEPATH_DP) {
            const CHAR16 *part = (const CHAR16 *)((const uint8_t *)node + sizeof(*node));
            UINTN count = (length - sizeof(*node)) / sizeof(CHAR16);
            for (index = 0u; index < count && part[index] != 0u; ++index) {
                if (used + 1u >= MAX_IMAGE_PATH_CHARS) {
                    status = EFI_OUT_OF_RESOURCES; goto fail;
                }
                path[used++] = part[index] == (CHAR16)'/' ? (CHAR16)'\\' : part[index];
            }
        }
        node = (const oc_device_path_node *)((const uint8_t *)node + length);
    }
    path[used] = 0u;
    for (index = 0u; index < used; ++index)
        if (path[index] == (CHAR16)'\\') slash = index;
    if (slash == (UINTN)-1 || slash == 0u) {
        *package_root = volume;
        return EFI_SUCCESS;
    }
    path[slash] = 0u;
    status = volume->open(volume, package_root, path, EFI_FILE_MODE_READ, 0u);
    volume->close(volume);
    return status;
fail:
    volume->close(volume);
    return status;
}
