#!/usr/bin/env python3
import importlib.util
import pathlib
import sys
import tempfile

from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("ocg2_arm", ROOT / "tools/ocg2.py")
assert SPEC and SPEC.loader
ocg2 = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ocg2
SPEC.loader.exec_module(ocg2)

ARM_MANIFEST = b'''format_version = "2.0"
package_format = "ocg2"
id = "arm-input-probe"
title = "ARM Input Probe"
version = "0.1.0"
entry = "bin/input-probe.efi"
architecture = "arm64"
required_capabilities = ["uefi.input.keyboard@1.0"]
optional_capabilities = ["uefi.input.pointer@1.0"]
graphics_api = "gop"
accel = "forbidden"
min_graphics_abi = 0
'''


def expect(code, callback):
    try:
        callback()
    except ocg2.Ocg2Error as error:
        assert error.code == code, (error.code, code)
    else:
        raise AssertionError(f"expected {code}")


def main():
    key = Ed448PrivateKey.from_private_bytes(bytes(range(1, 58)))
    arm_efi = (ROOT / "build/arm64/INPUT-PROBE-AA64.EFI").read_bytes()
    x64_efi = (ROOT / "build/INPUT-PROBE.EFI").read_bytes()
    with tempfile.TemporaryDirectory(prefix="openconsole-ocg2-arm-") as directory:
        root = pathlib.Path(directory)
        manifest = root / "game.toml"
        executable = root / "input.efi"
        manifest.write_bytes(ARM_MANIFEST)
        executable.write_bytes(arm_efi)
        first = ocg2.pack(manifest, {"entry-arm64": executable}, key)
        second = ocg2.pack(manifest, {"entry-arm64": executable}, key)
        assert first == second
        decoded = ocg2.decode(first, key.public_key())
        roles = [item.role for item in decoded.entries]
        assert roles.count(ocg2.ROLES["entry-arm64"]) == 1
        assert ocg2.ROLES["entry-x64"] not in roles
        key_id = ocg2.hashlib.sha256(ocg2.public_bytes(key.public_key())).digest()
        expect("OCG2_BAD_ROLE", lambda: ocg2.make_index(
            "arm-input-probe", "0.1.0", key_id, [
                ("game.toml", ocg2.ROLES["manifest"], ARM_MANIFEST),
                ("bin/arm.efi", ocg2.ROLES["entry-arm64"], arm_efi),
                ("bin/x64.efi", ocg2.ROLES["entry-x64"], x64_efi),
            ]))
        executable.write_bytes(x64_efi)
        expect("OCG2_MANIFEST_MISMATCH",
               lambda: ocg2.pack(manifest, {"entry-arm64": executable}, key))
        expect("OCG2_MANIFEST_MISMATCH",
               lambda: ocg2.pack(manifest, {"entry-x64": executable}, key))
        executable.write_bytes(b"MZ malformed")
        expect("OCG2_MANIFEST_MISMATCH",
               lambda: ocg2.pack(manifest, {"entry-arm64": executable}, key))
        unpacked = root / "unpacked"
        unpacked.mkdir()
        decoded = ocg2.decode(first, key.public_key())
        for item in decoded.entries:
            target = unpacked.joinpath(*item.path.split("/"))
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(item.data)
        assert (unpacked / "bin/input-probe.efi").read_bytes() == arm_efi
    fixture = ROOT / "tests/fixtures/ocg2/arm64-minimal.ocg"
    fixture.parent.mkdir(parents=True, exist_ok=True)
    fixture.write_bytes(first)
    (fixture.with_suffix(".json")).write_text(
        ocg2.json.dumps(ocg2.report(ocg2.decode(first, key.public_key()), True),
                        indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("PASS: deterministic ARM64 OCG2 role and PE architecture enforcement")


if __name__ == "__main__":
    main()
