#!/usr/bin/env python3
"""Build the bounded OEC1 Ethernet catalog consumed by the parent EFI."""

from __future__ import annotations
import argparse, hashlib, json, pathlib, struct

HEADER = struct.Struct("<4sHHIIIIQ")
ENTRY = struct.Struct("<HHBBHHHHHBBHIQ32s96s32s")
ARCH = {"x64": 1, "arm64": 2}
PROTOCOL = {"nii": 1, "undi": 2, "snp": 4}
PREFIX = "EFI/OpenConsole/Drivers/Network/"


def _integer(value, bits, field):
    if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value < 1 << bits:
        raise ValueError(f"{field} must be an unsigned {bits}-bit integer")
    return value


def build(document: dict) -> bytes:
    if document.get("schema") != "openconsole.ethernet-catalog.v1":
        raise ValueError("unsupported schema")
    records = []
    for item in document.get("entries", []):
        path = item["path"].encode("ascii"); ident = item["id"].encode("ascii")
        if not item["path"].startswith(PREFIX) or not item["path"].endswith(".efi") or len(path) > 96:
            raise ValueError("driver path must be a bounded Network .efi path")
        if not 0 < len(ident) < 32 or any(chr(c) not in "abcdefghijklmnopqrstuvwxyz0123456789._-" for c in ident):
            raise ValueError("invalid catalog id")
        protocols = 0
        for protocol in item["protocols"]: protocols |= PROTOCOL[protocol]
        digest = bytes.fromhex(item["sha256"])
        if len(digest) != 32: raise ValueError("sha256 must contain 32 bytes")
        pci = item["pci"]
        fields = [_integer(pci[name], 16, name) for name in
                  ("vendor", "device", "subsystemVendor", "subsystemDevice")]
        if not fields[0] or not fields[1]: raise ValueError("vendor and device must be nonzero")
        rev_min = _integer(pci["revisionMin"], 8, "revisionMin")
        rev_max = _integer(pci["revisionMax"], 8, "revisionMax")
        if rev_min > rev_max: raise ValueError("invalid revision interval")
        priority = _integer(item["priority"], 32, "priority")
        size = _integer(item["bytes"], 64, "bytes")
        if not 0 < size <= 4 * 1024 * 1024: raise ValueError("driver size outside policy")
        records.append((priority, ident, ENTRY.pack(len(path), len(ident), ARCH[item["architecture"]],
            0, protocols, *fields, rev_min, rev_max, 0, priority, size, digest,
            path.ljust(96, b"\0"), ident.ljust(32, b"\0"))))
    if len(records) > 32: raise ValueError("catalog has more than 32 entries")
    records.sort(key=lambda value: (value[0], value[1]))
    body = b"".join(value[2] for value in records)
    return HEADER.pack(b"OEC1", 1, 0, HEADER.size, ENTRY.size, len(records), 0,
                       HEADER.size + len(body)) + body


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    blob = build(json.loads(args.input.read_text(encoding="utf-8")))
    args.output.parent.mkdir(parents=True, exist_ok=True); args.output.write_bytes(blob)
    print(json.dumps({"output": str(args.output), "bytes": len(blob),
                      "sha256": hashlib.sha256(blob).hexdigest()}, sort_keys=True))
    return 0


if __name__ == "__main__": raise SystemExit(main())
