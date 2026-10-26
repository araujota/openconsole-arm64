#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t size) {
    unsigned char *out = destination;
    const unsigned char *in = source;
    while (size-- != 0u) *out++ = *in++;
    return destination;
}

void *memset(void *destination, int value, size_t size) {
    unsigned char *out = destination;
    while (size-- != 0u) *out++ = (unsigned char)value;
    return destination;
}

void *memmove(void *destination, const void *source, size_t size) {
    unsigned char *out = destination;
    const unsigned char *in = source;
    if (out < in) while (size-- != 0u) *out++ = *in++;
    else {
        out += size;
        in += size;
        while (size-- != 0u) *--out = *--in;
    }
    return destination;
}
