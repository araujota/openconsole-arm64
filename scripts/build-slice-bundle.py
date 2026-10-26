#!/usr/bin/env python3
import pathlib
import struct
import sys


def main():
    if len(sys.argv) != 6:
        raise SystemExit("usage: build-slice-bundle.py MANIFEST CONTROLS JACKET EFI OUTPUT")
    parts = [pathlib.Path(name).read_bytes() for name in sys.argv[1:5]]
    if not parts[3].startswith(b"MZ"):
        raise SystemExit("game EFI is not a PE/COFF image")
    header = b"OCG1" + struct.pack("<4I", *(len(part) for part in parts))
    output = pathlib.Path(sys.argv[5])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(header + b"".join(parts))
    print(f"wrote {output} ({output.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
