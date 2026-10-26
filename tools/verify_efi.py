#!/usr/bin/env python3
"""Fail closed when an EFI PE/COFF image does not match its declared target."""

from __future__ import annotations

import argparse
import json
import pathlib
import struct
import sys


MACHINES = {"x64": 0x8664, "arm64": 0xAA64}
SUBSYSTEMS = {"application": {10}, "driver": {11, 12}}


class VerificationError(ValueError):
    pass


def u16(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise VerificationError("truncated 16-bit PE field")
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise VerificationError("truncated 32-bit PE field")
    return struct.unpack_from("<I", data, offset)[0]


def inspect(path: pathlib.Path, architecture: str, role: str) -> dict[str, object]:
    data = path.read_bytes()
    if len(data) < 64 or data[:2] != b"MZ":
        raise VerificationError("missing DOS/PE header")
    pe = u32(data, 0x3C)
    if pe + 24 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise VerificationError("missing PE signature")
    machine = u16(data, pe + 4)
    expected = MACHINES[architecture]
    if machine != expected:
        raise VerificationError(
            f"machine 0x{machine:04x} does not match {architecture} (0x{expected:04x})")
    timestamp = u32(data, pe + 8)
    optional_size = u16(data, pe + 20)
    optional = pe + 24
    if optional_size < 112 or optional + optional_size > len(data) or u16(data, optional) != 0x20B:
        raise VerificationError("image is not a bounded PE32+ executable")
    subsystem = u16(data, optional + 68)
    if subsystem not in SUBSYSTEMS[role]:
        allowed = ", ".join(str(item) for item in sorted(SUBSYSTEMS[role]))
        raise VerificationError(f"subsystem {subsystem} is not valid for {role} ({allowed})")
    directory_count = u32(data, optional + 108)
    import_rva = import_size = 0
    if directory_count > 1 and optional_size >= 128:
        import_rva = u32(data, optional + 120)
        import_size = u32(data, optional + 124)
    if import_rva or import_size:
        raise VerificationError("EFI image imports hosted symbols")
    return {
        "path": str(path),
        "architecture": architecture,
        "machine": f"0x{machine:04x}",
        "role": role,
        "subsystem": subsystem,
        "timestamp": timestamp,
        "bytes": len(data),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=pathlib.Path)
    parser.add_argument("--arch", required=True, choices=sorted(MACHINES))
    parser.add_argument("--role", default="application", choices=sorted(SUBSYSTEMS))
    parser.add_argument("--require-zero-timestamp", action="store_true")
    args = parser.parse_args()
    try:
        report = inspect(args.path, args.arch, args.role)
        if args.require_zero_timestamp and report["timestamp"] != 0:
            raise VerificationError("PE timestamp is not deterministic")
        print(json.dumps(report, sort_keys=True))
        return 0
    except (OSError, VerificationError) as error:
        print(f"verify-efi: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
