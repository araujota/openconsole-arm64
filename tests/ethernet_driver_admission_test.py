#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys
import tempfile

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import admit_ethernet_driver as admission
import ethernet_driver_manifest as manifests


def expect_rejection(callback) -> None:
    try:
        callback()
    except (admission.AdmissionError, manifests.ManifestError):
        return
    raise AssertionError("invalid offline driver admission was accepted")


def main() -> None:
    artifact = ROOT / "build/edk2-network-stack/DpcDxe.efi"
    raw = artifact.read_bytes()
    key = Ed448PrivateKey.from_private_bytes(bytes(range(1, 58)))
    payload = {
        "manifestId": "offline-dpc-fixture", "driverVersion": "fixture-1",
        "architecture": "x64", "acquisition": "user-acquired",
        "license": "test-fixture-only",
        "provenance": {"source": "https://github.com/tianocore/edk2",
                       "upstreamVersion": "fixture", "upstreamDigest": None},
        "artifact": {"path": "EFI/OpenConsole/Drivers/Network/UserDpc.efi",
                     "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
                     "peMachine": "0x8664", "subsystem": 11},
        "protocols": ["snp"], "priority": 100,
        "compatibility": {"platformAbiMajor": 1, "platformAbiMinorMin": 0,
                          "platformAbiMinorMax": 0,
                          "firmwarePrerequisites": []},
        "devices": [{"vendor": "10ec", "device": "8125",
                     "subsystemVendor": "1462", "subsystemDevice": "7e12",
                     "revisionMin": "05", "revisionMax": "05",
                     "classCode": "020000"}],
        "exclusions": [], "revoked": False,
    }
    document = manifests.build(payload, key)
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        manifest_path = work / "driver.json"
        key_path = work / "driver.pub"
        root = work / "suitcase"
        manifest_path.write_text(json.dumps(document), encoding="utf-8")
        key_path.write_bytes(key.public_key().public_bytes(
            serialization.Encoding.Raw, serialization.PublicFormat.Raw))
        result = admission.admit(manifest_path, artifact, key_path, root)
        installed = root / payload["artifact"]["path"]
        assert installed.read_bytes() == raw
        assert installed.with_suffix(".efi.oced.json").is_file()
        assert result["sha256"] == payload["artifact"]["sha256"]
        expect_rejection(lambda: admission.admit(manifest_path, artifact, key_path, root))

        tampered = work / "tampered.efi"
        tampered.write_bytes(raw + b"x")
        expect_rejection(lambda: admission.admit(manifest_path, tampered, key_path, root))

        source_payload = dict(payload)
        source_payload["acquisition"] = "source-build"
        manifest_path.write_text(json.dumps(manifests.build(source_payload, key)),
                                 encoding="utf-8")
        expect_rejection(lambda: admission.admit(manifest_path, artifact, key_path, root))
    print("PASS: verified offline user-acquired Ethernet driver admission")


if __name__ == "__main__":
    main()
