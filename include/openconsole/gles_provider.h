#ifndef OPENCONSOLE_GLES_PROVIDER_H
#define OPENCONSOLE_GLES_PROVIDER_H

#include "openconsole/gles2_protocol.h"

typedef EFI_STATUS (*OC_GLES_ALLOCATE)(void *, UINTN, void **);
typedef void (*OC_GLES_RELEASE)(void *, void *);
typedef EFI_STATUS (*OC_GLES_BLIT_BGRA)(void *, const void *, uint32_t, uint32_t);

typedef struct {
    void *user;
    OC_GLES_ALLOCATE allocate;
    OC_GLES_RELEASE release;
    OC_GLES_BLIT_BGRA blit_bgra;
    uint32_t display_width;
    uint32_t display_height;
} OC_GLES_PLATFORM_SERVICES;

typedef struct {
    OC_GLES2_PROTOCOL protocol;
    OC_GLES_PLATFORM_SERVICES platform;
    OC_GLES_CONTEXT *current;
    uint64_t outstanding_bytes;
    uint32_t live_contexts;
} OC_GLES_PROVIDER;

EFI_STATUS oc_gles_provider_initialize(OC_GLES_PROVIDER *, const OC_GLES_PLATFORM_SERVICES *);
uint64_t oc_gles_provider_outstanding_bytes(const OC_GLES_PROVIDER *);

#endif
