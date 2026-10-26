#!/usr/bin/env python3
import copy
import importlib.util
import json
from pathlib import Path

from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("ethernet_driver_manifest", ROOT / "tools" / "ethernet_driver_manifest.py")
assert spec and spec.loader
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def reject(code, callback):
    try:
        callback()
    except module.ManifestError as error:
        assert error.code == code, (error.code, code)
    else:
        raise AssertionError(f"expected {code}")


device = {"vendor": "10ec", "device": "8125", "subsystemVendor": "1462", "subsystemDevice": "7e12",
          "revisionMin": "05", "revisionMax": "05", "classCode": "020000"}
payload = {
    "manifestId": "realtek-rtl8125-ms7e12-x64", "driverVersion": "2.081", "architecture": "x64",
    "acquisition": "user-acquired", "license": "vendor-license-review-required",
    "provenance": {"source": "https://www.realtek.com/Download/List?cate_id=584", "upstreamVersion": "2.081", "upstreamDigest": None},
    "artifact": {"path": "EFI/OpenConsole/Drivers/Network/RealtekUndi.efi", "bytes": 123456,
                 "sha256": "ab" * 32, "peMachine": "0x8664", "subsystem": 11},
    "protocols": ["nii", "undi"], "priority": 100,
    "compatibility": {"platformAbiMajor": 1, "platformAbiMinorMin": 0, "platformAbiMinorMax": 0,
                      "firmwarePrerequisites": ["efi-pci-io"]},
    "devices": [device], "exclusions": [], "revoked": False,
}
key = Ed448PrivateKey.from_private_bytes(bytes(range(1, 58)))
first = module.build(payload, key)
second = module.build(copy.deepcopy(payload), key)
assert first == second
assert module.verify(first, key.public_key()) == payload

tampered = copy.deepcopy(first)
tampered["payload"]["priority"] = 99
reject("OCED_BAD_SIGNATURE", lambda: module.verify(tampered, key.public_key()))

wrong_digest = copy.deepcopy(payload)
wrong_digest["artifact"]["sha256"] = "not-a-sha256"
reject("OCED_BAD_ARTIFACT", lambda: module.build(wrong_digest, key))

digest_tampered = copy.deepcopy(first)
digest_tampered["payload"]["artifact"]["sha256"] = "cd" * 32
reject("OCED_BAD_SIGNATURE", lambda: module.verify(digest_tampered, key.public_key()))

wrong_machine = copy.deepcopy(payload)
wrong_machine["artifact"]["peMachine"] = "0xaa64"
reject("OCED_BAD_ARTIFACT", lambda: module.build(wrong_machine, key))

wildcard = copy.deepcopy(payload)
wildcard["devices"][0]["subsystemDevice"] = "*"
reject("OCED_BAD_DEVICE", lambda: module.build(wildcard, key))

wrong_protocol = copy.deepcopy(payload)
wrong_protocol["protocols"] = ["http"]
reject("OCED_BAD_PROTOCOL", lambda: module.build(wrong_protocol, key))

revoked = copy.deepcopy(payload)
revoked["revoked"] = True
document = module.build(revoked, key)
reject("OCED_REVOKED", lambda: module.verify(document, key.public_key()))

print("PASS: deterministic signed exact-device Ethernet driver manifests")
