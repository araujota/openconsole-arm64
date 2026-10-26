#include "openconsole/manifest.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    FILE *file;
    long file_size;
    char *data;
    size_t bytes_read;
    oc_game_manifest manifest;
    oc_manifest_result parsed;
    if (argc != 2) {
        fprintf(stderr, "usage: %s GAME.TOML\n", argv[0]);
        return 2;
    }
    file = fopen(argv[1], "rb");
    if (file == NULL) {
        perror(argv[1]);
        return 2;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0 ||
        file_size > (long)OC_MANIFEST_MAX_BYTES || fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "%s: unreadable or larger than %u bytes\n", argv[1],
                (unsigned)OC_MANIFEST_MAX_BYTES);
        fclose(file);
        return 2;
    }
    data = malloc((size_t)file_size + 1u);
    if (data == NULL) {
        fclose(file);
        return 2;
    }
    bytes_read = fread(data, 1u, (size_t)file_size, file);
    fclose(file);
    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "%s: short read\n", argv[1]);
        free(data);
        return 2;
    }
    parsed = oc_manifest_parse(data, bytes_read, &manifest);
    free(data);
    if (parsed.code != OC_MANIFEST_OK) {
        fprintf(stderr, "%s:%u: %s\n", argv[1], parsed.line,
                oc_manifest_error_string(parsed.code));
        return 1;
    }
    printf("OK %s: %s (%s, %s, accel=%s, ABI=%u)\n", manifest.id,
           manifest.title, manifest.entry,
           manifest.graphics_api == OC_GRAPHICS_GLES2 ? "gles2" : "gop",
           manifest.accel ? "true" : "false", manifest.min_graphics_abi);
    return 0;
}

