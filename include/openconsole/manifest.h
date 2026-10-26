#ifndef OPENCONSOLE_MANIFEST_H
#define OPENCONSOLE_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#define OC_MANIFEST_MAX_BYTES (64u * 1024u)
#define OC_MANIFEST_MAX_PROTOCOLS 8u
#define OC_MANIFEST_MAX_CAPABILITIES 8u
#define OC_MANIFEST_DESCRIPTION_MAX 511u

typedef enum {
    OC_GRAPHICS_GOP = 0,
    OC_GRAPHICS_GLES2 = 1
} oc_graphics_api;

typedef enum {
    OC_ACCEL_FORBIDDEN = 0,
    OC_ACCEL_OPTIONAL = 1,
    OC_ACCEL_REQUIRED = 2
} oc_accel_policy;

typedef struct {
    char id[65];
    char format_version[9];
    char package_format[9];
    char title[129];
    char entry[257];
    char architecture[17];
    char developer[129];
    char description[OC_MANIFEST_DESCRIPTION_MAX + 1u];
    char version[33];
    char website[257];
    char required_protocols[OC_MANIFEST_MAX_PROTOCOLS][65];
    uint32_t required_protocol_count;
    char required_capabilities[OC_MANIFEST_MAX_CAPABILITIES][65];
    uint32_t required_capability_count;
    char optional_capabilities[OC_MANIFEST_MAX_CAPABILITIES][65];
    uint32_t optional_capability_count;
    char jacket[257];
    char controls[257];
    oc_graphics_api graphics_api;
    uint32_t min_graphics_abi;
    oc_accel_policy accel_policy;
    /* Deprecated v1-compatible mirror: nonzero for optional/required. */
    uint8_t accel;
} oc_game_manifest;

typedef enum {
    OC_MANIFEST_OK = 0,
    OC_MANIFEST_TOO_LARGE,
    OC_MANIFEST_SYNTAX,
    OC_MANIFEST_UNKNOWN_KEY,
    OC_MANIFEST_DUPLICATE_KEY,
    OC_MANIFEST_MISSING_KEY,
    OC_MANIFEST_VALUE_TOO_LONG,
    OC_MANIFEST_INVALID_ID,
    OC_MANIFEST_INVALID_PATH,
    OC_MANIFEST_INVALID_VALUE,
    OC_MANIFEST_TOO_MANY_PROTOCOLS,
    OC_MANIFEST_INCOMPATIBLE
} oc_manifest_error;

typedef struct {
    oc_manifest_error code;
    uint32_t line;
} oc_manifest_result;

oc_manifest_result oc_manifest_parse(
    const char *source, size_t source_size, oc_game_manifest *out);
const char *oc_manifest_error_string(oc_manifest_error error);
int oc_manifest_relative_path_is_safe(const char *path);

#endif
