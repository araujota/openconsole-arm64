#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare persistent FAT states corresponding to interrupted game mutations."""
from __future__ import annotations

import argparse
import hashlib
import pathlib
import shutil
import struct
import sys
import zlib

from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ocg2


def selector(bundle: ocg2.Bundle, bundle_bytes: bytes, descriptor: bytes,
             *, generation: int = 2, slot: int = 1, tombstone: bool = False) -> bytes:
    out = bytearray(196)
    out[:4] = b"OCGS"
    out[4] = 1
    out[5] = slot
    out[6] = int(tombstone)
    struct.pack_into("<Q", out, 8, generation)
    out[16:48] = bundle.bundle_id
    out[48:80] = hashlib.sha256(descriptor).digest()
    struct.pack_into("<Q", out, 80, 0 if tombstone else len(bundle_bytes))
    game_id = bundle.game_id.encode("ascii")
    version = bundle.version.encode("ascii")
    out[88:90] = bytes((len(game_id), len(version)))
    out[96:96 + len(game_id)] = game_id
    out[160:160 + len(version)] = version
    struct.pack_into("<I", out, 192, zlib.crc32(out[:192]) & 0xffffffff)
    return bytes(out)


def stage_slot(game: pathlib.Path, bundle_bytes: bytes, bundle: ocg2.Bundle,
               descriptor: bytes, stage: str) -> None:
    slot = game / "slot-b"
    slot.mkdir(parents=True, exist_ok=True)
    marker = b"OCGT\x01\x01\0\0" + struct.pack("<Q", 2) + hashlib.sha256(bundle_bytes).digest()
    (slot / "staging.ocgt").write_bytes(marker)
    if stage == "staging-write":
        return
    (slot / "bundle.ocg").write_bytes(bundle_bytes)
    if stage == "bundle-write":
        return
    (slot / "descriptor.ocgi").write_bytes(descriptor)
    if stage == "descriptor-write":
        return
    package = slot / "package"
    package.mkdir()
    for index, entry in enumerate(bundle.entries):
        target = package / entry.path
        target.parent.mkdir(parents=True, exist_ok=True)
        data = entry.data
        if stage == "package-short-write" and index == len(bundle.entries) - 1:
            data = data[:-1]
        target.write_bytes(data)
    if stage in ("package-short-write", "package-write"):
        return
    record = selector(bundle, bundle_bytes, descriptor)
    if stage == "selector-short-write":
        record = record[:80]
    elif stage == "selector-verify-corrupt":
        record = record[:-1] + bytes((record[-1] ^ 1,))
    (game / "select-b.ocgs").write_bytes(record)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("base", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    args.output = args.output.resolve()
    try:
        relative_output = args.output.relative_to((ROOT / "build").resolve())
    except ValueError as error:
        raise SystemExit("matrix output must be beneath the project build directory") from error
    if not relative_output.parts:
        raise SystemExit("matrix output cannot be the build directory itself")
    if args.output.exists():
        shutil.rmtree(args.output)
    args.output.mkdir(parents=True)
    manifest = b'''format_version = "2.0"
package_format = "ocg2"
id = "authenticated"
title = "Authenticated fixture"
version = "1.0.1"
entry = "bin/game.efi"
architecture = "x64"
required_capabilities = ["uefi.input.keyboard@1.0"]
optional_capabilities = []
graphics_api = "gop"
accel = "forbidden"
min_graphics_abi = 0
'''
    fixture = ROOT / "build/storage-recovery-fixture"
    fixture.mkdir(parents=True, exist_ok=True)
    (fixture / "game.toml").write_bytes(manifest)
    key = Ed448PrivateKey.from_private_bytes(bytes(range(1, 58)))
    bundle_bytes = ocg2.pack(fixture / "game.toml",
                             {"entry-x64": ROOT / "build/INPUT-PROBE.EFI"}, key)
    bundle = ocg2.decode(bundle_bytes)
    index_size = struct.unpack_from("<Q", bundle_bytes, 16)[0]
    descriptor = bundle_bytes[:242 + index_size]
    interrupted = ("staging-write", "bundle-write", "descriptor-write",
                   "package-short-write", "package-write", "selector-short-write",
                   "selector-verify-corrupt")
    for name in interrupted:
        target = args.output / name
        shutil.copytree(args.base, target)
        stage_slot(target / "EFI/OpenConsole/State/Games/authenticated",
                   bundle_bytes, bundle, descriptor, name)
    target = args.output / "selector-commit"
    shutil.copytree(args.base, target)
    game = target / "EFI/OpenConsole/State/Games/authenticated"
    stage_slot(game, bundle_bytes, bundle, descriptor, "selector-commit")
    (game / "slot-b/staging.ocgt").unlink()
    target = args.output / "delete-tombstone"
    shutil.copytree(args.base, target)
    game = target / "EFI/OpenConsole/State/Games/authenticated"
    old_bundle = ocg2.decode((game / "slot-a/bundle.ocg").read_bytes())
    old_descriptor = (game / "slot-a/descriptor.ocgi").read_bytes()
    (game / "select-b.ocgs").write_bytes(selector(
        old_bundle, (game / "slot-a/bundle.ocg").read_bytes(), old_descriptor,
        generation=2, slot=1, tombstone=True))
    print(f"prepared {len(interrupted) + 2} OVMF storage recovery states")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
