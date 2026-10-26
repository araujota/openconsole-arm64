#!/usr/bin/env python3
"""Tests for the fail-closed external UEFI driver inspector."""

import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "uefi_driver_inspector", ROOT / "scripts/inspect-uefi-driver.py")
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def fixture() -> bytearray:
    data = bytearray(0x220)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    pe = 0x80
    data[pe:pe + 4] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, pe + 4, 0x8664, 1, 0, 0, 0, 0xF0, 0x2022)
    optional = pe + 24
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<I", data, optional + 16, 0x1000)
    struct.pack_into("<I", data, optional + 32, 0x1000)
    struct.pack_into("<I", data, optional + 36, 0x200)
    struct.pack_into("<I", data, optional + 56, 0x2000)
    struct.pack_into("<I", data, optional + 60, 0x200)
    struct.pack_into("<H", data, optional + 68, 11)
    struct.pack_into("<I", data, optional + 108, 16)
    struct.pack_into("<II", data, optional + 112 + 5 * 8, 0x1010, 8)
    section = optional + 0xF0
    data[section:section + 8] = b".text\0\0\0"
    struct.pack_into("<IIII", data, section + 8, 0x20, 0x1000, 0x20, 0x200)
    data[0x200:0x220] = bytes(range(0x20))
    return data


def rejected(path: Path, phrase: str) -> None:
    try:
        MODULE.inspect(path)
    except MODULE.InspectionError as error:
        assert phrase in str(error), (phrase, str(error))
    else:
        raise AssertionError(f"fixture unexpectedly accepted; wanted {phrase}")


with tempfile.TemporaryDirectory(prefix="openconsole-uefi-driver-") as directory:
    root = Path(directory)
    valid = root / "valid.efi"
    valid.write_bytes(fixture())
    report = MODULE.inspect(valid)
    assert report["machine"] == "x86-64"
    assert report["subsystem"] == "EFI boot service driver"
    assert report["imports_empty"] is True
    assert report["authenticode_present"] is False
    assert report["sha256"] == hashlib.sha256(valid.read_bytes()).hexdigest()
    try:
        MODULE.inspect(valid, "00" * 32)
    except MODULE.InspectionError as error:
        assert "SHA-256" in str(error)
    else:
        raise AssertionError("incorrect reviewed digest accepted")

    application = bytearray(fixture())
    struct.pack_into("<H", application, 0x80 + 24 + 68, 10)
    path = root / "application.efi"
    path.write_bytes(application)
    rejected(path, "boot-service driver")

    arm = bytearray(fixture())
    struct.pack_into("<H", arm, 0x80 + 4, 0xAA64)
    path = root / "arm.efi"
    path.write_bytes(arm)
    rejected(path, "not x86-64")

    imported = bytearray(fixture())
    struct.pack_into("<II", imported, 0x80 + 24 + 112 + 8, 0x1000, 8)
    path = root / "imported.efi"
    path.write_bytes(imported)
    rejected(path, "import directory")

    path = root / "truncated.efi"
    path.write_bytes(fixture()[:0x205])
    rejected(path, "raw data is out of range")

print("UEFI driver inspector tests passed")
