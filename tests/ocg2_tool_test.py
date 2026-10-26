#!/usr/bin/env python3
import importlib.util
import json
import pathlib
import sys
import tempfile

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("ocg2", ROOT / "tools" / "ocg2.py")
assert SPEC and SPEC.loader
ocg2 = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ocg2
SPEC.loader.exec_module(ocg2)

MANIFEST = b'''format_version = "2.0"
package_format = "ocg2"
id = "golden"
title = "Golden"
version = "1.0.0"
entry = "bin/game.efi"
architecture = "x64"
required_capabilities = ["openconsole.gles2@0.1"]
optional_capabilities = []
graphics_api = "gles2"
accel = "optional"
min_graphics_abi = 1
'''


def expect(code, callback):
    try:
        callback()
    except ocg2.Ocg2Error as exc:
        assert exc.code == code, (exc.code, code)
    else:
        raise AssertionError(f"expected {code}")


def main():
    key = Ed448PrivateKey.from_private_bytes(bytes(range(1, 58)))
    key_id = ocg2.hashlib.sha256(ocg2.public_bytes(key.public_key())).digest()
    x64_efi = (ROOT / "build" / "INPUT-PROBE.EFI").read_bytes()
    index, payload, _ = ocg2.make_index("golden", "1.0.0", key_id, [
        ("game.toml", ocg2.ROLES["manifest"], MANIFEST),
        ("bin/game.efi", ocg2.ROLES["entry-x64"], x64_efi),
    ])
    bundle_id = ocg2.hashlib.sha256(index).digest()
    total = ocg2.ENVELOPE.size + 114 + len(index) + len(payload)
    header = ocg2.ENVELOPE.pack(b"OCG2", 2, 0, 128, 0, len(index), len(payload), 2,
                                1, 114, total, bundle_id, key_id, bytes(16))
    blob = header + key.sign(ocg2.SIGN_DOMAIN + header + index) + index + payload
    parsed = ocg2.decode(blob, key.public_key())
    assert parsed.game_id == "golden" and parsed.version == "1.0.0"
    assert parsed.bundle_id == bundle_id and len(parsed.entries) == 2
    assert ocg2.decode(blob) == parsed
    damaged = bytearray(blob)
    damaged[-1] ^= 1
    expect("OCG2_BAD_DIGEST", lambda: ocg2.decode(bytes(damaged), key.public_key()))
    bad_signature = bytearray(blob)
    bad_signature[ocg2.ENVELOPE.size] ^= 1
    expect("OCG2_BAD_SIGNATURE", lambda: ocg2.decode(bytes(bad_signature), key.public_key()))
    expect("OCG2_LENGTH_MISMATCH", lambda: ocg2.decode(blob[:-1], key.public_key()))
    expect("OCG2_OVERFLOW", lambda: ocg2.checked_add(ocg2.MAX_BUNDLE, 1))
    expect("OCG2_BAD_PATH", lambda: ocg2.canonical_path("../game.efi"))
    expect("OCG2_PATH_COLLISION", lambda: ocg2.make_index("golden", "1.0.0", key_id, [
        ("game.toml", 1, MANIFEST), ("A/game.efi", 2, b"MZ"), ("a/GAME.efi", 5, b"x")]))
    migrated = ocg2.migrate_v1(b'''id = "old"
title = "Old"
entry = "game.efi"
architecture = "x64"
required_protocols = ["openconsole.net"]
graphics_api = "gop"
accel = false
min_graphics_abi = 0
''', "0.1.0")
    migrated_fields = ocg2.parse_manifest(migrated)
    assert migrated_fields["format_version"] == "2.0"
    assert migrated_fields["required_capabilities"] == ["openconsole.net@0.1"]
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        (root / "game.toml").write_bytes(MANIFEST)
        (root / "game.efi").write_bytes(x64_efi)
        raw_private = key.private_bytes(serialization.Encoding.Raw,
                                        serialization.PrivateFormat.Raw,
                                        serialization.NoEncryption())
        (root / "private.key").write_bytes(raw_private)
        packed_a = ocg2.pack(root / "game.toml", {"entry-x64": root / "game.efi"}, key)
        packed_b = ocg2.pack(root / "game.toml", {"entry-x64": root / "game.efi"}, key)
        assert packed_a == packed_b == blob
    fixture = ROOT / "tests" / "fixtures" / "ocg2"
    fixture.mkdir(parents=True, exist_ok=True)
    (fixture / "golden-minimal.ocg").write_bytes(blob)
    (fixture / "golden-minimal.json").write_text(json.dumps(ocg2.report(parsed, True),
                                                             sort_keys=True, indent=2) + "\n")
    print("PASS: canonical OCG2 pack/decode/signature, errors, and deterministic golden fixture")


if __name__ == "__main__":
    main()
