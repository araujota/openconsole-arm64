# OCJ1 raw jacket format

OCJ1 is the intentionally small v1 jacket format used by the EFI launcher. It
avoids pulling PNG/JPEG decompression, color-management, and allocator complexity
into the first G1 image. Desktop packaging tools should perform conversion.

All fields are byte-packed:

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 4 | ASCII `OCJ1` |
| 4 | 2 | Width, unsigned little-endian |
| 6 | 2 | Height, unsigned little-endian |
| 8 | `width × height × 4` | Row-major BGRA8888 pixels |

The current loader requires nonzero dimensions no greater than 512×512, an exact
payload length, and a total file size no greater than 1 MiB. Alpha is reserved
and currently ignored. Images are nearest-neighbor scaled to a square GOP card.
Malformed or missing jackets receive a generated fallback tile and never prevent
a valid game from appearing.

`tools/jacket-gen.c` creates the sample jacket without an image library. A later
desktop packaging command can accept PNG/JPEG and emit OCJ1; those decoders do
not need to become firmware dependencies.

