#!/usr/bin/env python3
"""Materialize the canonical two-slot tree for the signed OVMF game fixture."""

import hashlib, pathlib, shutil, struct, sys, zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ocg2


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
    struct.pack_into("<I", out, 192, zlib.crc32(out[:192]) & 0xffffffff)
    return bytes(out)


def main() -> int:
    fixture = ROOT / "build/authenticated-game-fixture"
    target = ROOT / "build/authenticated-installed-games"
    bundle_bytes = (fixture / "game.ocg").read_bytes()
    bundle = ocg2.decode(bundle_bytes, ocg2.load_public_key(fixture / "publisher.pub"))
    index_size = struct.unpack_from("<Q", bundle_bytes, 16)[0]
    descriptor = bundle_bytes[:242 + index_size]
    if target.exists(): shutil.rmtree(target)
    slot = target / bundle.game_id / "slot-a"
    package = slot / "package"
    package.mkdir(parents=True)
    (slot / "bundle.ocg").write_bytes(bundle_bytes)
    (slot / "descriptor.ocgi").write_bytes(descriptor)
    for entry in bundle.entries:
        path = package / entry.path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(entry.data)
    (target / bundle.game_id / "select-a.ocgs").write_bytes(
        selection(bundle, bundle_bytes, descriptor))
    print(f"prepared {bundle.game_id} {bundle.version} in {target}")
    return 0


if __name__ == "__main__": raise SystemExit(main())
