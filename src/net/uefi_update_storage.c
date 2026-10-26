#include "openconsole/uefi_update_storage.h"

static const EFI_GUID file_info_guid =
    {0x09576e92u,0x6d3fu,0x11d2u,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

static void bytes_zero(void *memory, size_t size) {
    uint8_t *bytes = memory;
    while (size-- != 0u) *bytes++ = 0u;
}
static int string_copy(char *out, size_t capacity, const char *in) {
    size_t i = 0u;
    if (capacity == 0u) return 0;
    while (in[i] != '\0') {
        if (i + 1u >= capacity) return 0;
        out[i] = in[i]; ++i;
    }
    out[i] = '\0'; return 1;
}
static int string_equal(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { ++a; ++b; }
    return *a == '\0' && *b == '\0';
}
static int path16(const char *path, CHAR16 out[OC_UPDATE_PATH_MAX]) {
    size_t i = 0u;
    while (path[i] != '\0') {
        if (i + 1u >= OC_UPDATE_PATH_MAX) return 0;
        out[i] = (CHAR16)(uint8_t)path[i]; ++i;
    }
    out[i] = 0u; return 1;
}

static EFI_STATUS ensure_directory(EFI_FILE_PROTOCOL *parent, const CHAR16 *name,
                                   EFI_FILE_PROTOCOL **directory) {
    EFI_STATUS status = parent->open(parent, directory, name,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, EFI_FILE_DIRECTORY);
    if (status == EFI_NOT_FOUND)
        status = parent->open(parent, directory, name,
            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
            EFI_FILE_DIRECTORY);
    return status;
}

static int begin_write(void *context, const char *path, const uint8_t *data,
                       size_t size) {
    oc_uefi_update_storage *storage = context;
    EFI_FILE_PROTOCOL *existing = 0, *file = 0;
    CHAR16 wide[OC_UPDATE_PATH_MAX];
    EFI_STATUS status;
    UINTN written = size;
    if (storage->pending_file != 0 || !path16(path, wide) ||
        !string_copy(storage->pending_path, sizeof(storage->pending_path), path))
        return 0;
    status = storage->root->open(storage->root, &existing, wide,
                                 EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0u);
    if (!EFI_ERROR(status)) {
        if (existing->delete_file == 0 || EFI_ERROR(existing->delete_file(existing)))
            return 0;
        existing = 0;
    } else if (status != EFI_NOT_FOUND) return 0;
    status = storage->root->open(storage->root, &file, wide,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0u);
    if (EFI_ERROR(status) || file == 0) return 0;
    status = file->write(file, &written, data);
    if (EFI_ERROR(status) || written != size) {
        (void)file->close(file); return 0;
    }
    storage->pending_file = file;
    return 1;
}

static int flush_write(void *context, const char *path) {
    oc_uefi_update_storage *storage = context;
    EFI_FILE_PROTOCOL *file = storage->pending_file;
    EFI_STATUS status, close_status;
    if (file == 0 || !string_equal(storage->pending_path, path)) return 0;
    status = file->flush(file);
    close_status = file->close(file);
    storage->pending_file = 0;
    storage->pending_path[0] = '\0';
    return !EFI_ERROR(status) && !EFI_ERROR(close_status);
}

static int read_file(void *context, const char *path, uint8_t *data,
                     size_t capacity, size_t *size) {
    oc_uefi_update_storage *storage = context;
    EFI_FILE_PROTOCOL *file = 0;
    CHAR16 wide[OC_UPDATE_PATH_MAX];
    UINTN read_size = capacity;
    EFI_STATUS status;
    if (data == 0 || size == 0 || !path16(path, wide)) return 0;
    status = storage->root->open(storage->root, &file, wide, EFI_FILE_MODE_READ, 0u);
    if (EFI_ERROR(status) || file == 0) return 0;
    status = file->read(file, &read_size, data);
    (void)file->close(file);
    if (EFI_ERROR(status)) return 0;
    *size = read_size; return 1;
}

static int digest_file(void *context, const char *path, uint64_t *file_size,
                       uint8_t digest[32]) {
    oc_uefi_update_storage *storage = context;
    EFI_FILE_PROTOCOL *file = 0;
    EFI_FILE_INFO *info = 0;
    void *data = 0;
    CHAR16 wide[OC_UPDATE_PATH_MAX];
    UINTN info_size = 0u, read_size;
    EFI_STATUS status;
    int result = 0;
    if (file_size == 0 || digest == 0 || !path16(path, wide)) return 0;
    status = storage->root->open(storage->root, &file, wide, EFI_FILE_MODE_READ, 0u);
    if (EFI_ERROR(status) || file == 0) return 0;
    status = file->get_info(file, &file_info_guid, &info_size, 0);
    if (status != EFI_BUFFER_TOO_SMALL || info_size < sizeof(*info)) goto done;
    status = storage->boot_services->allocate_pool(EFI_LOADER_DATA, info_size,
                                                   (void **)&info);
    if (EFI_ERROR(status)) goto done;
    status = file->get_info(file, &file_info_guid, &info_size, info);
    if (EFI_ERROR(status) || info->file_size == 0u ||
        info->file_size > storage->maximum_artifact_size ||
        info->file_size > (uint64_t)(~(UINTN)0)) goto done;
    read_size = (UINTN)info->file_size;
    status = storage->boot_services->allocate_pool(EFI_LOADER_DATA, read_size, &data);
    if (EFI_ERROR(status)) goto done;
    status = file->read(file, &read_size, data);
    if (EFI_ERROR(status) || read_size != (UINTN)info->file_size) goto done;
    status = storage->crypto->sha256(storage->crypto, data, read_size, digest);
    if (EFI_ERROR(status)) goto done;
    *file_size = info->file_size;
    result = 1;
done:
    if (data != 0) (void)storage->boot_services->free_pool(data);
    if (info != 0) (void)storage->boot_services->free_pool(info);
    (void)file->close(file);
    return result;
}

EFI_STATUS oc_uefi_update_storage_initialize(oc_uefi_update_storage *storage,
                                             EFI_BOOT_SERVICES *boot_services,
                                             EFI_FILE_PROTOCOL *root,
                                             OC_CRYPTO_PROTOCOL *crypto,
                                             uint64_t maximum_artifact_size) {
    static const CHAR16 efi_name[] = L"EFI";
    static const CHAR16 openconsole_name[] = L"OpenConsole";
    static const CHAR16 updates_name[] = L"Updates";
    EFI_FILE_PROTOCOL *efi = 0, *openconsole = 0, *updates = 0;
    EFI_STATUS status;
    if (storage == 0 || boot_services == 0 || root == 0 || crypto == 0 ||
        crypto->sha256 == 0 || maximum_artifact_size == 0u)
        return EFI_INVALID_PARAMETER;
    bytes_zero(storage, sizeof(*storage));
    storage->boot_services = boot_services;
    storage->root = root;
    storage->crypto = crypto;
    storage->maximum_artifact_size = maximum_artifact_size;
    status = ensure_directory(root, efi_name, &efi);
    if (!EFI_ERROR(status)) status = ensure_directory(efi, openconsole_name, &openconsole);
    if (!EFI_ERROR(status)) status = ensure_directory(openconsole, updates_name, &updates);
    if (updates != 0) (void)updates->close(updates);
    if (openconsole != 0) (void)openconsole->close(openconsole);
    if (efi != 0) (void)efi->close(efi);
    if (EFI_ERROR(status)) return status;
    storage->operations.context = storage;
    storage->operations.write_file = begin_write;
    storage->operations.flush_file = flush_write;
    storage->operations.read_file = read_file;
    storage->operations.digest_file = digest_file;
    return EFI_SUCCESS;
}

void oc_uefi_update_storage_shutdown(oc_uefi_update_storage *storage) {
    if (storage == 0) return;
    if (storage->pending_file != 0) (void)storage->pending_file->close(storage->pending_file);
    storage->pending_file = 0;
    storage->pending_path[0] = '\0';
}
