#!/usr/bin/env python3
"""Fail-closed structural admission check for an externally supplied UEFI driver."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys


class InspectionError(RuntimeError):
    pass


def u16(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise InspectionError("truncated 16-bit PE field")
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise InspectionError("truncated 32-bit PE field")
    return struct.unpack_from("<I", data, offset)[0]


def inspect(path: Path, expected_sha256: str | None = None) -> dict:
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if expected_sha256 is not None and digest != expected_sha256.lower():
        raise InspectionError("SHA-256 does not match the reviewed artifact")
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise InspectionError("missing DOS/PE image header")
    pe = u32(data, 0x3C)
    if pe < 0x40 or pe + 24 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise InspectionError("missing or out-of-range PE signature")

    machine = u16(data, pe + 4)
    sections_count = u16(data, pe + 6)
    optional_size = u16(data, pe + 20)
    optional = pe + 24
    if machine != 0x8664:
        raise InspectionError(f"machine 0x{machine:04x} is not x86-64")
    if sections_count == 0 or sections_count > 96:
        raise InspectionError("invalid PE section count")
    if optional_size < 120 or optional + optional_size > len(data):
        raise InspectionError("truncated PE32+ optional header")
    if u16(data, optional) != 0x20B:
        raise InspectionError("image is not PE32+")

    entry_rva = u32(data, optional + 16)
    subsystem = u16(data, optional + 68)
    size_of_image = u32(data, optional + 56)
    size_of_headers = u32(data, optional + 60)
    directory_count = u32(data, optional + 108)
    if subsystem != 11:
        raise InspectionError(f"subsystem {subsystem} is not EFI boot-service driver (11)")
    if entry_rva == 0:
        raise InspectionError("driver has no entry point")
    if size_of_image == 0 or size_of_headers == 0 or size_of_headers > len(data):
        raise InspectionError("invalid PE image/header size")
    if directory_count < 6:
        raise InspectionError("PE data-directory table is incomplete")

    directories = optional + 112
    if directories + 6 * 8 > optional + optional_size:
        raise InspectionError("truncated PE data-directory table")
    import_rva, import_size = struct.unpack_from("<II", data, directories + 8)
    certificate_offset, certificate_size = struct.unpack_from("<II", data, directories + 4 * 8)
    relocation_rva, relocation_size = struct.unpack_from("<II", data, directories + 5 * 8)
    if import_rva != 0 or import_size != 0:
        raise InspectionError("driver has a non-empty PE import directory")
    if relocation_rva == 0 or relocation_size == 0:
        raise InspectionError("driver has no relocation directory")
    if certificate_size and (certificate_offset < size_of_headers or
                             certificate_offset + certificate_size > len(data)):
        raise InspectionError("Authenticode certificate table is out of range")

    section_table = optional + optional_size
    if section_table + sections_count * 40 > len(data):
        raise InspectionError("truncated PE section table")
    sections = []
    mapped_ranges = []
    for index in range(sections_count):
        offset = section_table + index * 40
        name = data[offset:offset + 8].split(b"\0", 1)[0].decode("ascii", "replace")
        virtual_size = u32(data, offset + 8)
        virtual_address = u32(data, offset + 12)
        raw_size = u32(data, offset + 16)
        raw_offset = u32(data, offset + 20)
        if raw_size and (raw_offset < size_of_headers or raw_offset + raw_size > len(data)):
            raise InspectionError(f"section {name or index} raw data is out of range")
        mapped_size = max(virtual_size, raw_size)
        if mapped_size:
            mapped_ranges.append((virtual_address, virtual_address + mapped_size))
        sections.append({"name": name, "virtual_address": virtual_address,
                         "virtual_size": virtual_size, "raw_offset": raw_offset,
                         "raw_size": raw_size})

    def mapped(rva: int, size: int) -> bool:
        end = rva + size
        return end >= rva and any(rva >= start and end <= stop
                                  for start, stop in mapped_ranges)

    if not mapped(entry_rva, 1):
        raise InspectionError("entry point is outside mapped sections")
    if not mapped(relocation_rva, relocation_size):
        raise InspectionError("relocation directory is outside mapped sections")

    return {
        "schema": 1,
        "path": str(path),
        "bytes": len(data),
        "sha256": digest,
        "machine": "x86-64",
        "pe_format": "PE32+",
        "subsystem": "EFI boot service driver",
        "entry_rva": entry_rva,
        "imports_empty": True,
        "relocation_rva": relocation_rva,
        "relocation_bytes": relocation_size,
        "authenticode_present": certificate_size != 0,
        "certificate_bytes": certificate_size,
        "sections": sections,
        "runtime_proofs_required": [
            "exact 10ec:8125 revision-05 binding",
            "standard UNDI/SNP publication",
            "unrelated controller non-binding",
            "DMA teardown and repeated physical packet exchange",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("driver", type=Path)
    parser.add_argument("--expect-sha256")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = inspect(args.driver, args.expect_sha256)
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (InspectionError, OSError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
