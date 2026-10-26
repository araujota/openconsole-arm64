#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import random
import struct
import sys
import time

from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import ocg2  # noqa: E402
import openconsole_analyzer as analyzer  # noqa: E402


MANIFEST = b'''format_version = "2.0"
package_format = "ocg2"
id = "analyzer-fixture"
title = "Analyzer Fixture"
version = "1.0.0"
entry = "bin/game.efi"
architecture = "x64"
required_capabilities = ["uefi.input.keyboard@1.0"]
optional_capabilities = []
graphics_api = "gop"
accel = "forbidden"
min_graphics_abi = 0
'''


def pe(code: bytes, data: bytes = b"", *, writable_code: bool = False,
       overlap: bool = False) -> bytes:
    assert len(code) <= 0x200 and len(data) <= 0x200
    image = bytearray(0x600 if data else 0x400)
    image[:2] = b"MZ"
    struct.pack_into("<I", image, 0x3C, 0x80)
    image[0x80:0x84] = b"PE\0\0"
    count = 2 if data else 1
    struct.pack_into("<HHIIIHH", image, 0x84, 0x8664, count, 0, 0, 0, 0xF0, 0x2022)
    optional = 0x98
    struct.pack_into("<H", image, optional, 0x20B)
    struct.pack_into("<I", image, optional + 16, 0x1000)
    struct.pack_into("<I", image, optional + 20, 0x1000)
    struct.pack_into("<Q", image, optional + 24, 0)
    struct.pack_into("<II", image, optional + 32, 0x1000, 0x200)
    struct.pack_into("<I", image, optional + 56, 0x3000 if data else 0x2000)
    struct.pack_into("<I", image, optional + 60, 0x200)
    struct.pack_into("<H", image, optional + 68, 10)
    struct.pack_into("<I", image, optional + 108, 16)
    section = optional + 0xF0
    image[section:section + 8] = b".text\0\0\0"
    struct.pack_into("<IIII", image, section + 8, 0x200, 0x1000, 0x200, 0x200)
    struct.pack_into("<I", image, section + 36, 0xE0000020 if writable_code else 0x60000020)
    image[0x200:0x200 + len(code)] = code
    image[0x200 + len(code):0x400] = b"\x90" * (0x200 - len(code))
    if data:
        second = section + 40
        image[second:second + 8] = b".rdata\0\0"
        struct.pack_into("<IIII", image, second + 8, 0x200,
                         0x1000 if overlap else 0x2000, 0x200, 0x400)
        struct.pack_into("<I", image, second + 36, 0x40000040)
        image[0x400:0x400 + len(data)] = data
    return bytes(image)


def bundle(entry: bytes, key: Ed448PrivateKey, manifest: bytes = MANIFEST,
           assets: dict[str, bytes] | None = None) -> bytes:
    import tempfile
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        (root / "game.toml").write_bytes(manifest)
        (root / "game.efi").write_bytes(entry)
        sources = {"entry-x64": root / "game.efi"}
        for role, contents in (assets or {}).items():
            filename = {"jacket-ocj1": "jacket.ocj", "controls-v1": "controls.cfg"}[role]
            (root / filename).write_bytes(contents)
            sources[role] = root / filename
        return ocg2.pack(root / "game.toml", sources, key)


def main() -> None:
    if analyzer.capstone is None:
        raise SystemExit("capstone is required; install requirements-analyzer.txt")
    key = Ed448PrivateKey.from_private_bytes(bytes(range(1, 58)))
    key_id = ocg2.hashlib.sha256(ocg2.public_bytes(key.public_key())).digest()
    trust = {key_id: key.public_key()}
    good = bundle(pe(b"\x31\xc0\xc3"), key)
    first = analyzer.analyze(good, trust)
    second = analyzer.analyze(good, trust)
    assert first == second and first["verdict"] == "accepted"
    assert len(first["reportDigest"]) == 64 and analyzer.to_text(first).endswith("\n")
    sarif = analyzer.to_sarif(first)
    assert sarif["version"] == "2.1.0"
    untrusted = analyzer.analyze(good)
    assert untrusted["verdict"] == "inconclusive"
    assert untrusted["findings"][0]["ruleId"] == "OCSA-SIGNATURE-UNTRUSTED"
    direct_io = analyzer.analyze(bundle(pe(b"\xee\xc3"), key), trust)
    assert direct_io["verdict"] == "rejected"
    assert any(item["ruleId"] == "OCSA-X64-DIRECT-IO" for item in direct_io["findings"])
    malformed = analyzer.analyze(b"not-an-ocg2", trust)
    assert malformed["verdict"] == "rejected"
    assert malformed["stages"][0]["name"] == "container"
    bad_signature_blob = bytearray(good)
    bad_signature_blob[ocg2.ENVELOPE.size] ^= 1
    bad_signature = analyzer.analyze(bytes(bad_signature_blob), trust)
    assert bad_signature["verdict"] == "rejected"
    assert any(item["ruleId"] == "OCSA-SIGNATURE-INVALID" for item in bad_signature["findings"])

    jacket_manifest = MANIFEST.replace(
        b'entry = "bin/game.efi"\n', b'entry = "bin/game.efi"\njacket = "jacket.ocj"\n')
    bad_asset = analyzer.analyze(bundle(pe(b"\xc3"), key, jacket_manifest,
                                        {"jacket-ocj1": b"not-ocj1"}), trust)
    assert bad_asset["verdict"] == "rejected"
    assert any(item["ruleId"] == "OCSA-ASSET-INVALID" for item in bad_asset["findings"])

    unsupported_manifest = MANIFEST.replace(
        b'"uefi.input.keyboard@1.0"', b'"vendor.future.device@9.9"')
    unsupported = analyzer.analyze(bundle(pe(b"\xc3"), key, unsupported_manifest), trust)
    assert unsupported["verdict"] == "rejected"
    assert any(item["ruleId"] == "OCSA-CAPABILITY-REQUIRED-UNSUPPORTED"
               for item in unsupported["findings"])
    long_entry = ("a/" * 80 + "game.efi").encode("ascii")
    long_manifest = MANIFEST.replace(b"bin/game.efi", long_entry)
    installed_tree = analyzer.analyze(bundle(pe(b"\xc3"), key, long_manifest), trust)
    assert installed_tree["verdict"] == "rejected"
    assert any(item["ruleId"] == "OCSA-INSTALLED-TREE-INVALID"
               for item in installed_tree["findings"])
    assert any(stage["name"] == "installed-tree" and stage["status"] == "failed"
               for stage in installed_tree["stages"])
    timed_out = analyzer.analyze(good, trust, deadline_seconds=0)
    assert timed_out["verdict"] == "inconclusive"

    blocking = {
        "OCSA-X64-PRIVILEGED": pe(b"\xfa\xc3"),
        "OCSA-X64-CONTROL-REGISTER": pe(b"\x0f\x20\xc0\xc3"),
        "OCSA-UEFI-VARIABLE-WRITE": pe(b"\xc3", b"SetVariable"),
        "OCSA-UEFI-RESET": pe(b"\xc3", b"ResetSystem"),
        "OCSA-FILESYSTEM-WRITE": pe(b"\xc3", b"WriteFile"),
        "OCSA-DRIVER-BINDING": pe(b"\xc3", b"EFI_DRIVER_BINDING_PROTOCOL"),
        "OCSA-NETWORK-UNDECLARED": pe(b"\xc3", b"https://example.invalid"),
        "OCSA-LIFECYCLE-EXIT-BOOT-SERVICES": pe(b"\xc3", b"ExitBootServices"),
        "OCSA-LIFECYCLE-WATCHDOG": pe(b"\xc3", b"SetWatchdogTimer"),
        "OCSA-PE-WRITABLE-EXECUTABLE": pe(b"\xc3", writable_code=True),
    }
    for rule_id, entry in blocking.items():
        report = analyzer.analyze(bundle(entry, key), trust)
        assert report["verdict"] == "rejected", (rule_id, report)
        assert any(item["ruleId"] == rule_id for item in report["findings"]), rule_id

    suspicious_guid = analyzer.analyze(
        bundle(pe(b"\xc3", bytes.fromhex("61dfe48bca93d211aa0d00e098032b8c")), key), trust)
    assert any(item["ruleId"] == "OCSA-GUID-SENSITIVE" for item in suspicious_guid["findings"])
    assert suspicious_guid["verdict"] == "accepted"  # review finding, not automatic rejection

    overlapping = analyzer.analyze(bundle(pe(b"\xc3", b"data", overlap=True), key), trust)
    assert overlapping["verdict"] == "rejected"
    assert overlapping["stages"][-1]["code"] == "PE_OVERLAPPING_VIRTUAL_SECTIONS"
    capped = analyzer.analyze(bundle(pe(b"\xee" * 300 + b"\xc3"), key), trust)
    assert capped["verdict"] == "inconclusive"
    assert any(item["ruleId"] == "OCSA-ANALYSIS-BUDGET" for item in capped["findings"])

    # Deliberately encoded service names demonstrate a documented static-analysis
    # false negative. The corpus records this limitation rather than calling it safe.
    encoded = bytes(byte ^ 0x5A for byte in b"SetVariable ResetSystem WriteFile")
    evasive = analyzer.analyze(bundle(pe(b"\xc3", encoded), key), trust)
    assert evasive["verdict"] == "accepted" and not evasive["findings"]

    rng = random.Random(0x4F434753)
    for size in (0, 1, 127, 128, 1024, 4096):
        for _ in range(20):
            fuzzed = bytes(rng.randrange(256) for _ in range(size))
            fuzz_report = analyzer.analyze(fuzzed, trust)
            assert fuzz_report["verdict"] in ("rejected", "inconclusive")
    for _ in range(40):
        random_code = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 128)))
        decoder_report = analyzer.analyze(bundle(pe(random_code), key), trust)
        assert decoder_report["verdict"] in ("accepted", "rejected", "inconclusive")
    started = time.monotonic()
    for _ in range(20):
        assert analyzer.analyze(good, trust)["reportDigest"] == first["reportDigest"]
    assert time.monotonic() - started < 5.0
    print("PASS: deterministic analyzer corpus, trust, PE mappings, blocking rules, fuzz seeds, and budgets")


if __name__ == "__main__":
    main()
