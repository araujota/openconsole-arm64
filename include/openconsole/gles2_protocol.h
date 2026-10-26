#ifndef OPENCONSOLE_GLES2_PROTOCOL_H
#define OPENCONSOLE_GLES2_PROTOCOL_H

#include "openconsole/uefi.h"
#include "openconsole/gles2.h"

#define OC_GLES_ABI_MAJOR 0u
#define OC_GLES_ABI_MINOR 1u
#define OC_GLES_ABI_VERSION ((OC_GLES_ABI_MAJOR << 16) | OC_GLES_ABI_MINOR)

#define OC_GLES_CONTEXT_SOFTWARE 0x00000001u
#define OC_GLES_CONTEXT_ACCELERATED 0x00000002u
#define OC_GLES_MAX_DIMENSION 4096u
#define OC_GLES_MAX_TARGET_BYTES (64u * 1024u * 1024u)

typedef struct OC_GLES_CONTEXT OC_GLES_CONTEXT;

typedef struct {
    uint32_t structure_size;
    uint32_t requested_abi;
    uint32_t width;
    uint32_t height;
    uint32_t flags;
    uint32_t reserved[3];
} OC_GLES_CONTEXT_DESC;

typedef struct {
    uint32_t structure_size;
    uint32_t abi_version;
    uint32_t backend_flags;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t reserved[3];
} OC_GLES_CAPABILITIES;

typedef EFI_STATUS(EFIAPI *OC_GLES_QUERY_CAPABILITIES)(OC_GLES_CAPABILITIES *);
typedef EFI_STATUS(EFIAPI *OC_GLES_CREATE_CONTEXT)(const OC_GLES_CONTEXT_DESC *,
                                                   OC_GLES_CONTEXT **);
typedef EFI_STATUS(EFIAPI *OC_GLES_DESTROY_CONTEXT)(OC_GLES_CONTEXT *);
typedef EFI_STATUS(EFIAPI *OC_GLES_MAKE_CURRENT)(OC_GLES_CONTEXT *);
typedef void (OC_GL_API *OC_GLES_PROC)(void);
typedef OC_GLES_PROC(EFIAPI *OC_GLES_GET_PROC_ADDRESS)(const char *);
typedef EFI_STATUS(EFIAPI *OC_GLES_PRESENT)(OC_GLES_CONTEXT *);

typedef struct {
    uint32_t structure_size;
    uint32_t abi_version;
    OC_GLES_QUERY_CAPABILITIES query_capabilities;
    OC_GLES_CREATE_CONTEXT create_context;
    OC_GLES_DESTROY_CONTEXT destroy_context;
    OC_GLES_MAKE_CURRENT make_current;
    OC_GLES_GET_PROC_ADDRESS get_proc_address;
    OC_GLES_PRESENT present;
} OC_GLES2_PROTOCOL;

/* Provider owns contexts and all returned procedure addresses. Calls are
 * cooperative and single-threaded; at most one context is current. A client
 * must destroy its contexts before returning to the launcher. */

/* 6dfc2ca4-6c65-4f47-a628-8345f43c89d1 */
static const EFI_GUID OC_GLES2_PROTOCOL_GUID =
    {0x6dfc2ca4u, 0x6c65u, 0x4f47u, {0xa6,0x28,0x83,0x45,0xf4,0x3c,0x89,0xd1}};

#endif
