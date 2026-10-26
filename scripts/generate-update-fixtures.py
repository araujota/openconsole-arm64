#!/usr/bin/env python3
"""Generate deterministic catalog, signed metadata, and EFI artifact fixtures."""

import hashlib
import pathlib
import struct
import sys
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ed448


SEED = bytes(range(1, 10)) + bytes(48)
KEY_ID = bytes([0x11]) * 16
ARTIFACT_URL = b"https://10.0.2.2:9443/artifact"
METADATA_URL = b"https://10.0.2.2:9443/metadata"


def update_metadata(artifact):
    digest = hashlib.sha256(artifact).digest()
    fixed = bytearray(287)
    fixed[0:4] = b"OCU1"
    struct.pack_into("<HHIHHQQQQ", fixed, 4, 1, 287,
                     287 + len(ARTIFACT_URL), 0, 0, 8, 2_000_000_000,
                     12, len(artifact))
    fixed[48:64] = KEY_ID
    fixed[137:169] = digest
    struct.pack_into("<H", fixed, 283, len(ARTIFACT_URL))
    signed = (b"OCU1-SIGNED" + struct.pack("<HHQQQQ", 1, 0, 8,
              2_000_000_000, 12, len(artifact)) + KEY_ID + bytes(16) +
              bytes(57) + digest + struct.pack("<H", len(ARTIFACT_URL)) +
              ARTIFACT_URL)
    key = ed448.Ed448PrivateKey.from_private_bytes(SEED)
    fixed[169:283] = key.sign(signed)
    return bytes(fixed) + ARTIFACT_URL, digest, key


def catalog(artifact, digest):
    entry_id = b"openconsole"
    name = b"OpenConsole Stable"
    total = 34 + 56 + len(entry_id) + len(name) + len(METADATA_URL)
    out = bytearray(total)
    out[0:4] = b"OCC1"
    struct.pack_into("<HHIIQQH", out, 4, 1, 32, total, 0, 4,
                     2_000_000_000, 1)
    offset = 34
    struct.pack_into("<HHHHQQ", out, offset, len(entry_id), len(name),
                     len(METADATA_URL), 0, 12, len(artifact))
    out[offset + 24:offset + 56] = digest
    offset += 56
    out[offset:offset + len(entry_id)] = entry_id; offset += len(entry_id)
    out[offset:offset + len(name)] = name; offset += len(name)
    out[offset:offset + len(METADATA_URL)] = METADATA_URL
    return bytes(out)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: generate-update-fixtures.py ARTIFACT OUTPUT_DIR")
    artifact = pathlib.Path(sys.argv[1]).read_bytes()
    output = pathlib.Path(sys.argv[2])
    output.mkdir(parents=True, exist_ok=True)
    metadata, digest, key = update_metadata(artifact)
    public_key = key.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    (output / "artifact.efi").write_bytes(artifact)
    (output / "metadata.ocum").write_bytes(metadata)
    (output / "catalog.occ").write_bytes(catalog(artifact, digest))
    (output / "public-key.bin").write_bytes(public_key)
    print("PASS: deterministic signed update fixtures generated")


if __name__ == "__main__":
    main()
