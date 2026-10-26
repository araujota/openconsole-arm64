#include <stdint.h>
#include <stdio.h>

int main(int argc, char **argv) {
    const unsigned side = 192u;
    FILE *out;
    unsigned x, y;
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT.OCJ\n", argv[0]);
        return 2;
    }
    out = fopen(argv[1], "wb");
    if (out == NULL) { perror(argv[1]); return 2; }
    fputs("OCJ1", out);
    fputc(side & 255u, out); fputc(side >> 8, out);
    fputc(side & 255u, out); fputc(side >> 8, out);
    for (y = 0u; y < side; ++y) {
        for (x = 0u; x < side; ++x) {
            int ring = x > 34u && x < 157u && y > 24u && y < 167u &&
                       !(x > 68u && x < 123u && y > 58u && y < 133u);
            uint8_t blue = (uint8_t)(34u + y / 5u);
            uint8_t green = ring ? 218u : (uint8_t)(42u + x / 8u);
            uint8_t red = ring ? 94u : (uint8_t)(25u + (x + y) / 12u);
            fputc(blue, out); fputc(green, out); fputc(red, out); fputc(0, out);
        }
    }
    if (fclose(out) != 0) return 2;
    return 0;
}

