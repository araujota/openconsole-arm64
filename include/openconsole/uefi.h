#ifndef OPENCONSOLE_UEFI_H
#define OPENCONSOLE_UEFI_H

#include <stdint.h>
#include "openconsole/target.h"

#if OC_TARGET_ARCH_X64
#define EFIAPI __attribute__((ms_abi))
#elif OC_TARGET_ARCH_ARM64
#define EFIAPI
#endif

typedef void *EFI_HANDLE;
typedef void *EFI_EVENT;
typedef uint64_t EFI_STATUS;
typedef uint64_t UINTN;
typedef UINTN EFI_TPL;
typedef uint16_t CHAR16;
typedef uint8_t BOOLEAN;

typedef struct { uint8_t addr[4]; } EFI_IPv4_ADDRESS;
typedef struct { uint8_t addr[32]; } EFI_MAC_ADDRESS;

#define EFI_SUCCESS ((EFI_STATUS)0)
#define EFI_ERROR_BIT ((EFI_STATUS)1u << 63)
#define EFI_BUFFER_TOO_SMALL (EFI_ERROR_BIT | 5u)
#define EFI_INVALID_PARAMETER (EFI_ERROR_BIT | 2u)
#define EFI_UNSUPPORTED (EFI_ERROR_BIT | 3u)
#define EFI_NOT_READY (EFI_ERROR_BIT | 6u)
#define EFI_DEVICE_ERROR (EFI_ERROR_BIT | 7u)
#define EFI_OUT_OF_RESOURCES (EFI_ERROR_BIT | 9u)
#define EFI_NO_MEDIA (EFI_ERROR_BIT | 12u)
#define EFI_NO_MAPPING (EFI_ERROR_BIT | 17u)
#define EFI_ALREADY_STARTED (EFI_ERROR_BIT | 20u)
#define EFI_NOT_FOUND (EFI_ERROR_BIT | 14u)
#define EFI_TIMEOUT (EFI_ERROR_BIT | 18u)
#define EFI_ABORTED (EFI_ERROR_BIT | 21u)
#define EFI_PROTOCOL_ERROR (EFI_ERROR_BIT | 24u)
#define EFI_INCOMPATIBLE_VERSION (EFI_ERROR_BIT | 25u)
#define EFI_SECURITY_VIOLATION (EFI_ERROR_BIT | 26u)
#define EFI_HTTP_ERROR (EFI_ERROR_BIT | 35u)
#define EFI_NETWORK_UNREACHABLE (EFI_ERROR_BIT | 100u)
#define EFI_CONNECTION_FIN (EFI_ERROR_BIT | 104u)
#define EFI_CONNECTION_REFUSED (EFI_ERROR_BIT | 106u)
#define EFI_ERROR(status) (((status) & EFI_ERROR_BIT) != 0u)
#define EFI_LOADER_DATA 2u
#define EFI_FILE_MODE_READ 1u
#define EFI_FILE_MODE_WRITE 2u
#define EFI_FILE_MODE_CREATE 0x8000000000000000ull
#define EFI_FILE_DIRECTORY 0x10u

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} EFI_GUID;

typedef struct {
    uint16_t scan_code;
    CHAR16 unicode_char;
} EFI_INPUT_KEY;

typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
typedef EFI_STATUS(EFIAPI *EFI_INPUT_RESET)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *, BOOLEAN);
typedef EFI_STATUS(EFIAPI *EFI_INPUT_READ_KEY)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *, EFI_INPUT_KEY *);

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_INPUT_RESET reset;
    EFI_INPUT_READ_KEY read_key_stroke;
    EFI_EVENT wait_for_key;
};

typedef struct {
    int32_t relative_movement_x;
    int32_t relative_movement_y;
    int32_t relative_movement_z;
    BOOLEAN left_button;
    BOOLEAN right_button;
} EFI_SIMPLE_POINTER_STATE;

typedef struct {
    uint64_t resolution_x;
    uint64_t resolution_y;
    uint64_t resolution_z;
    BOOLEAN left_button;
    BOOLEAN right_button;
} EFI_SIMPLE_POINTER_MODE;

typedef struct EFI_SIMPLE_POINTER_PROTOCOL EFI_SIMPLE_POINTER_PROTOCOL;
typedef EFI_STATUS(EFIAPI *EFI_SIMPLE_POINTER_RESET)(
    EFI_SIMPLE_POINTER_PROTOCOL *, BOOLEAN);
typedef EFI_STATUS(EFIAPI *EFI_SIMPLE_POINTER_GET_STATE)(
    EFI_SIMPLE_POINTER_PROTOCOL *, EFI_SIMPLE_POINTER_STATE *);

struct EFI_SIMPLE_POINTER_PROTOCOL {
    EFI_SIMPLE_POINTER_RESET reset;
    EFI_SIMPLE_POINTER_GET_STATE get_state;
    EFI_EVENT wait_for_input;
    EFI_SIMPLE_POINTER_MODE *mode;
};

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} EFI_TABLE_HEADER;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef EFI_STATUS(EFIAPI *EFI_TEXT_RESET)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self,
    uint8_t extended_verification);
typedef EFI_STATUS(EFIAPI *EFI_TEXT_STRING)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self,
    const CHAR16 *string);

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET reset;
    EFI_TEXT_STRING output_string;
    void *test_string;
    void *query_mode;
    void *set_mode;
    void *set_attribute;
    void *clear_screen;
    void *set_cursor_position;
    void *enable_cursor;
    void *mode;
};

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef EFI_STATUS(EFIAPI *EFI_FILE_OPEN)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **,
                                          const CHAR16 *, uint64_t, uint64_t);
typedef EFI_STATUS(EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_DELETE)(EFI_FILE_PROTOCOL *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_READ)(EFI_FILE_PROTOCOL *, UINTN *, void *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_WRITE)(EFI_FILE_PROTOCOL *, UINTN *, const void *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_SET_POSITION)(EFI_FILE_PROTOCOL *, uint64_t);
typedef EFI_STATUS(EFIAPI *EFI_FILE_GET_INFO)(EFI_FILE_PROTOCOL *, const EFI_GUID *,
                                              UINTN *, void *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_FLUSH)(EFI_FILE_PROTOCOL *);

struct EFI_FILE_PROTOCOL {
    uint64_t revision;
    EFI_FILE_OPEN open;
    EFI_FILE_CLOSE close;
    EFI_FILE_DELETE delete_file;
    EFI_FILE_READ read;
    EFI_FILE_WRITE write;
    void *get_position;
    EFI_FILE_SET_POSITION set_position;
    EFI_FILE_GET_INFO get_info;
    void *set_info;
    EFI_FILE_FLUSH flush;
    void *open_ex;
    void *read_ex;
    void *write_ex;
    void *flush_ex;
};

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef EFI_STATUS(EFIAPI *EFI_OPEN_VOLUME)(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *,
                                            EFI_FILE_PROTOCOL **);
struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    uint64_t revision;
    EFI_OPEN_VOLUME open_volume;
};

typedef struct {
    uint32_t revision;
    EFI_HANDLE parent_handle;
    void *system_table;
    EFI_HANDLE device_handle;
    void *file_path;
    void *reserved;
    uint32_t load_options_size;
    void *load_options;
    void *image_base;
    uint64_t image_size;
    uint32_t image_code_type;
    uint32_t image_data_type;
    void *unload;
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second, pad1;
    uint32_t nanosecond;
    int16_t time_zone;
    uint8_t daylight, pad2;
} EFI_TIME;

typedef EFI_STATUS(EFIAPI *EFI_GET_TIME)(EFI_TIME *, void *);

typedef struct {
    uint64_t size;
    uint64_t file_size;
    uint64_t physical_size;
    EFI_TIME create_time;
    EFI_TIME last_access_time;
    EFI_TIME modification_time;
    uint64_t attribute;
    CHAR16 file_name[1];
} EFI_FILE_INFO;

typedef struct {
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixel_format;
    uint32_t pixel_information[4];
    uint32_t pixels_per_scan_line;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    uint32_t max_mode;
    uint32_t mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
    UINTN size_of_info;
    uint64_t frame_buffer_base;
    UINTN frame_buffer_size;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    uint8_t blue, green, red, reserved;
} EFI_GRAPHICS_OUTPUT_BLT_PIXEL;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
typedef EFI_STATUS(EFIAPI *EFI_GRAPHICS_OUTPUT_QUERY_MODE)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL *, uint32_t, UINTN *,
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **);
typedef EFI_STATUS(EFIAPI *EFI_GRAPHICS_OUTPUT_SET_MODE)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL *, uint32_t);
typedef EFI_STATUS(EFIAPI *EFI_GRAPHICS_OUTPUT_BLT)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL *, EFI_GRAPHICS_OUTPUT_BLT_PIXEL *, uint32_t,
    UINTN, UINTN, UINTN, UINTN, UINTN, UINTN, UINTN);

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_GRAPHICS_OUTPUT_QUERY_MODE query_mode;
    EFI_GRAPHICS_OUTPUT_SET_MODE set_mode;
    EFI_GRAPHICS_OUTPUT_BLT blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode;
};

typedef EFI_STATUS(EFIAPI *EFI_ALLOCATE_POOL)(uint32_t, UINTN, void **);
typedef EFI_STATUS(EFIAPI *EFI_FREE_POOL)(void *);
typedef EFI_TPL(EFIAPI *EFI_RAISE_TPL)(EFI_TPL);
typedef void(EFIAPI *EFI_RESTORE_TPL)(EFI_TPL);
typedef void(EFIAPI *EFI_EVENT_NOTIFY)(EFI_EVENT, void *);
typedef EFI_STATUS(EFIAPI *EFI_CREATE_EVENT)(uint32_t, UINTN, EFI_EVENT_NOTIFY,
                                             void *, EFI_EVENT *);
typedef EFI_STATUS(EFIAPI *EFI_SET_TIMER)(EFI_EVENT, uint32_t, uint64_t);
typedef EFI_STATUS(EFIAPI *EFI_CLOSE_EVENT)(EFI_EVENT);
typedef EFI_STATUS(EFIAPI *EFI_SIGNAL_EVENT)(EFI_EVENT);
typedef EFI_STATUS(EFIAPI *EFI_WAIT_FOR_EVENT)(UINTN, EFI_EVENT *, UINTN *);
typedef EFI_STATUS(EFIAPI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE, const EFI_GUID *, void **);
typedef EFI_STATUS(EFIAPI *EFI_LOCATE_PROTOCOL)(const EFI_GUID *, void *, void **);
typedef EFI_STATUS(EFIAPI *EFI_LOCATE_HANDLE_BUFFER)(uint32_t, const EFI_GUID *,
                                                     void *, UINTN *, EFI_HANDLE **);
typedef EFI_STATUS(EFIAPI *EFI_IMAGE_LOAD)(BOOLEAN, EFI_HANDLE, void *, void *, UINTN,
                                           EFI_HANDLE *);
typedef EFI_STATUS(EFIAPI *EFI_IMAGE_START)(EFI_HANDLE, UINTN *, CHAR16 **);
typedef EFI_STATUS(EFIAPI *EFI_IMAGE_UNLOAD)(EFI_HANDLE);
typedef EFI_STATUS(EFIAPI *EFI_CONNECT_CONTROLLER)(EFI_HANDLE, EFI_HANDLE *,
                                                    void *, BOOLEAN);
typedef EFI_STATUS(EFIAPI *EFI_DISCONNECT_CONTROLLER)(EFI_HANDLE, EFI_HANDLE,
                                                       EFI_HANDLE);
typedef EFI_STATUS(EFIAPI *EFI_STALL)(UINTN);
typedef EFI_STATUS(EFIAPI *EFI_INSTALL_PROTOCOL_INTERFACE)(
    EFI_HANDLE *, EFI_GUID *, uint32_t, void *);
typedef EFI_STATUS(EFIAPI *EFI_SET_VARIABLE)(CHAR16 *, const EFI_GUID *, uint32_t,
                                             UINTN, void *);
#define EFI_NATIVE_INTERFACE 0u

typedef struct {
    EFI_TABLE_HEADER hdr;
    EFI_RAISE_TPL raise_tpl;
    EFI_RESTORE_TPL restore_tpl;
    void *allocate_pages;
    void *free_pages;
    void *get_memory_map;
    EFI_ALLOCATE_POOL allocate_pool;
    EFI_FREE_POOL free_pool;
    EFI_CREATE_EVENT create_event;
    EFI_SET_TIMER set_timer;
    EFI_WAIT_FOR_EVENT wait_for_event;
    EFI_SIGNAL_EVENT signal_event;
    EFI_CLOSE_EVENT close_event;
    void *check_event;
    EFI_INSTALL_PROTOCOL_INTERFACE install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    EFI_HANDLE_PROTOCOL handle_protocol;
    void *reserved;
    void *register_protocol_notify;
    void *locate_handle;
    void *locate_device_path;
    void *install_configuration_table;
    EFI_IMAGE_LOAD load_image;
    EFI_IMAGE_START start_image;
    void *exit;
    EFI_IMAGE_UNLOAD unload_image;
    void *exit_boot_services;
    void *get_next_monotonic_count;
    EFI_STALL stall;
    void *set_watchdog_timer;
    EFI_CONNECT_CONTROLLER connect_controller;
    EFI_DISCONNECT_CONTROLLER disconnect_controller;
    void *open_protocol;
    void *close_protocol;
    void *open_protocol_information;
    void *protocols_per_handle;
    EFI_LOCATE_HANDLE_BUFFER locate_handle_buffer;
    EFI_LOCATE_PROTOCOL locate_protocol;
    void *install_multiple_protocol_interfaces;
    void *uninstall_multiple_protocol_interfaces;
    void *calculate_crc32;
    void *copy_mem;
    void *set_mem;
    void *create_event_ex;
} EFI_BOOT_SERVICES;

typedef struct EFI_PCI_IO_PROTOCOL EFI_PCI_IO_PROTOCOL;
typedef EFI_STATUS(EFIAPI *EFI_PCI_IO_PROTOCOL_CONFIG)(
    EFI_PCI_IO_PROTOCOL *, uint32_t, uint32_t, UINTN, void *);
typedef struct {
    EFI_PCI_IO_PROTOCOL_CONFIG read;
    EFI_PCI_IO_PROTOCOL_CONFIG write;
} EFI_PCI_IO_PROTOCOL_ACCESS;

struct EFI_PCI_IO_PROTOCOL {
    void *poll_mem;
    void *poll_io;
    EFI_PCI_IO_PROTOCOL_ACCESS mem;
    EFI_PCI_IO_PROTOCOL_ACCESS io;
    EFI_PCI_IO_PROTOCOL_ACCESS pci;
};

typedef struct {
    EFI_TABLE_HEADER hdr;
    EFI_GET_TIME get_time;
    void *set_time;
    void *get_wakeup_time;
    void *set_wakeup_time;
    void *set_virtual_address_map;
    void *convert_pointer;
    void *get_variable;
    void *get_next_variable_name;
    EFI_SET_VARIABLE set_variable;
    void *get_next_high_monotonic_count;
    void *reset_system;
} EFI_RUNTIME_SERVICES;

typedef struct {
    EFI_TABLE_HEADER hdr;
    CHAR16 *firmware_vendor;
    uint32_t firmware_revision;
    uint32_t padding;
    EFI_HANDLE console_in_handle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *con_in;
    EFI_HANDLE console_out_handle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *con_out;
    EFI_HANDLE standard_error_handle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *std_err;
    EFI_RUNTIME_SERVICES *runtime_services;
    EFI_BOOT_SERVICES *boot_services;
    UINTN number_of_table_entries;
    void *configuration_table;
} EFI_SYSTEM_TABLE;

#endif
