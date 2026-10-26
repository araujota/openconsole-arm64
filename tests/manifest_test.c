#include "openconsole/manifest.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static const char valid_gles[] =
    "id = \"asteroids_2\"\n"
    "title = \"Asteroids 2\" # jacket title\n"
    "entry = \"bin/game.efi\"\n"
    "architecture = \"x64\"\n"
    "required_protocols = [\"suitcase.gles\", \"input.keyboard\"]\n"
    "graphics_api = \"gles2\"\n"
    "accel = true\n"
    "min_graphics_abi = 1\n";

static const char enriched[] =
    "id = \"orbit\"\n"
    "title = \"Orbit\"\n"
    "entry = \"game.efi\"\n"
    "architecture = \"x64\"\n"
    "required_protocols = [\"openconsole.gles2\"]\n"
    "graphics_api = \"gles2\"\n"
    "accel = true\n"
    "min_graphics_abi = 1\n"
    "developer = \"OpenConsole Labs\"\n"
    "description = \"A bounded metadata fixture.\"\n"
    "version = \"1.2.3\"\n"
    "website = \"https://openconsole.test/orbit\"\n";

static const char valid_v2[] =
    "format_version = \"2.0\"\n"
    "package_format = \"ocg2\"\n"
    "id = \"orbit\"\n"
    "title = \"Orbit\"\n"
    "version = \"2.0.1\"\n"
    "entry = \"bin/game.efi\"\n"
    "architecture = \"x64\"\n"
    "required_capabilities = [\"openconsole.gles2@0.1\"]\n"
    "optional_capabilities = [\"openconsole.net@0.1\", \"openconsole.audio@0.2\"]\n"
    "graphics_api = \"gles2\"\n"
    "accel = \"required\"\n"
    "min_graphics_abi = 1\n"
    "jacket = \"art/jacket.ocj\"\n"
    "controls = \"config/controls.cfg\"\n";

static void parses_valid_manifest(void) {
    oc_game_manifest manifest;
    oc_manifest_result parsed = oc_manifest_parse(valid_gles, sizeof(valid_gles) - 1u, &manifest);
    CHECK(parsed.code == OC_MANIFEST_OK);
    CHECK(strcmp(manifest.id, "asteroids_2") == 0);
    CHECK(strcmp(manifest.entry, "bin/game.efi") == 0);
    CHECK(manifest.graphics_api == OC_GRAPHICS_GLES2);
    CHECK(manifest.accel == 1u);
    CHECK(manifest.required_protocol_count == 2u);
    CHECK(manifest.min_graphics_abi == 1u);
}

static void parses_enriched_and_legacy_manifests(void) {
    oc_game_manifest manifest;
    oc_manifest_result parsed = oc_manifest_parse(enriched, sizeof(enriched) - 1u,
                                                   &manifest);
    CHECK(parsed.code == OC_MANIFEST_OK);
    CHECK(strcmp(manifest.developer, "OpenConsole Labs") == 0);
    CHECK(strcmp(manifest.description, "A bounded metadata fixture.") == 0);
    CHECK(strcmp(manifest.version, "1.2.3") == 0);
    CHECK(strcmp(manifest.website, "https://openconsole.test/orbit") == 0);
    parsed = oc_manifest_parse(valid_gles, sizeof(valid_gles) - 1u, &manifest);
    CHECK(parsed.code == OC_MANIFEST_OK);
    CHECK(manifest.developer[0] == '\0' && manifest.description[0] == '\0');
}

static void parses_v2_manifest(void) {
    oc_game_manifest manifest;
    oc_manifest_result parsed = oc_manifest_parse(valid_v2, sizeof(valid_v2) - 1u,
                                                   &manifest);
    CHECK(parsed.code == OC_MANIFEST_OK);
    CHECK(strcmp(manifest.format_version, "2.0") == 0);
    CHECK(strcmp(manifest.package_format, "ocg2") == 0);
    CHECK(manifest.required_capability_count == 1u);
    CHECK(manifest.optional_capability_count == 2u);
    CHECK(strcmp(manifest.optional_capabilities[1], "openconsole.audio@0.2") == 0);
    CHECK(manifest.accel_policy == OC_ACCEL_REQUIRED && manifest.accel == 1u);
    CHECK(strcmp(manifest.jacket, "art/jacket.ocj") == 0);
    CHECK(strcmp(manifest.controls, "config/controls.cfg") == 0);
}

static void rejects_replacement(const char *needle, const char *replacement,
                                oc_manifest_error expected) {
    char buffer[2048];
    char *position;
    size_t before;
    size_t needle_size = strlen(needle);
    size_t replacement_size = strlen(replacement);
    oc_game_manifest manifest;
    oc_manifest_result parsed;
    strcpy(buffer, valid_gles);
    position = strstr(buffer, needle);
    CHECK(position != NULL);
    if (position == NULL) return;
    before = (size_t)(position - buffer);
    memmove(position + replacement_size, position + needle_size,
            strlen(position + needle_size) + 1u);
    memcpy(position, replacement, replacement_size);
    parsed = oc_manifest_parse(buffer, strlen(buffer), &manifest);
    if (parsed.code != expected) {
        fprintf(stderr, "replacement at %zu: expected %s, got %s (line %u)\n",
                before, oc_manifest_error_string(expected),
                oc_manifest_error_string(parsed.code), parsed.line);
        ++failures;
    }
}

static void rejects_unsafe_paths(void) {
    static const char *const paths[] = {
        "../game.efi", "/game.efi", "C:/game.efi", "bin//game.efi",
        "bin/./game.efi", "bin/../game.efi", "bin\\game.efi", "bin/"
    };
    size_t i;
    for (i = 0u; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        CHECK(!oc_manifest_relative_path_is_safe(paths[i]));
    }
    CHECK(oc_manifest_relative_path_is_safe("bin/game.efi"));
}

int main(void) {
    parses_valid_manifest();
    parses_enriched_and_legacy_manifests();
    parses_v2_manifest();
    rejects_unsafe_paths();
    rejects_replacement("bin/game.efi", "../game.efi", OC_MANIFEST_INVALID_PATH);
    rejects_replacement("architecture = \"x64\"", "architecture = \"aa64\"", OC_MANIFEST_INCOMPATIBLE);
    rejects_replacement("graphics_api = \"gles2\"", "graphics_api = \"gop\"", OC_MANIFEST_INCOMPATIBLE);
    rejects_replacement("id = \"asteroids_2\"", "id = \"Bad ID\"", OC_MANIFEST_INVALID_ID);
    rejects_replacement("title = \"Asteroids 2\"", "mystery = \"value\"", OC_MANIFEST_UNKNOWN_KEY);
    rejects_replacement("min_graphics_abi = 1", "min_graphics_abi = 999999999999", OC_MANIFEST_SYNTAX);
    if (failures != 0u) {
        fprintf(stderr, "%u manifest test(s) failed\n", failures);
        return 1;
    }
    puts("PASS: manifest parser and path validation");
    return 0;
}
