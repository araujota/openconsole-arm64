#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare signed hang-child state and a deliberately corrupt driver payload."""
from __future__ import annotations

import argparse
import hashlib
import pathlib
import shutil
import struct
import sys
import zlib

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ocg2
import publisher_keys


def selection(bundle: ocg2.Bundle, bundle_bytes: bytes, descriptor: bytes) -> bytes:
    out = bytearray(196)
    out[:4] = b"OCGS"
    out[4] = 1
    struct.pack_into("<Q", out, 8, 1)
    out[16:48] = bundle.bundle_id
    out[48:80] = hashlib.sha256(descriptor).digest()
    struct.pack_into("<Q", out, 80, len(bundle_bytes))
    game_id = bundle.game_id.encode("ascii")
    version = bundle.version.encode("ascii")
    out[88:90] = bytes((len(game_id), len(version)))
    out[96:96 + len(game_id)] = game_id
    out[160:160 + len(version)] = version
    struct.pack_into("<I", out, 192, zlib.crc32(out[:192]) & 0xFFFFFFFF)
    return bytes(out)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    output = args.output.resolve()
    try:
        relative = output.relative_to((ROOT / "build").resolve())
    except ValueError as error:
        raise SystemExit("output must be beneath the project build directory") from error
    if not relative.parts:
        raise SystemExit("output cannot be the build directory")
    if output.exists():
        shutil.rmtree(output)
    fixture = output / "fixture"
    installed = output / "installed"
    fixture.mkdir(parents=True)
    manifest = b'''format_version = "2.0"
package_format = "ocg2"
id = "hang_probe"
title = "Hang and reset probe"
version = "1.0.0"
entry = "bin/game.efi"
architecture = "x64"
required_capabilities = ["uefi.input.keyboard@1.0"]
optional_capabilities = []
graphics_api = "gop"
accel = "forbidden"
min_graphics_abi = 0
'''
    key = Ed448PrivateKey.from_private_bytes(bytes(range(1, 58)))
    public = key.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    (fixture / "publisher.pub").write_bytes(public)
    (fixture / "game.toml").write_bytes(manifest)
    bundle_bytes = ocg2.pack(
        fixture / "game.toml", {"entry-x64": ROOT / "build/HANG-PROBE.EFI"}, key)
    (fixture / "game.ocg").write_bytes(bundle_bytes)
    (fixture / "publishers.ocpk").write_bytes(publisher_keys.build([
        ("openconsole", "OpenConsole test publisher", fixture / "publisher.pub", 1)]))
    bundle = ocg2.decode(bundle_bytes, key.public_key())
    index_size = struct.unpack_from("<Q", bundle_bytes, 16)[0]
    descriptor = bundle_bytes[:242 + index_size]
    slot = installed / bundle.game_id / "slot-a"
    package = slot / "package"
    package.mkdir(parents=True)
    (slot / "bundle.ocg").write_bytes(bundle_bytes)
    (slot / "descriptor.ocgi").write_bytes(descriptor)
    for entry in bundle.entries:
        target = package / entry.path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(entry.data)
    (installed / bundle.game_id / "select-a.ocgs").write_bytes(
        selection(bundle, bundle_bytes, descriptor))
    print(f"prepared signed hang fixture in {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
