#!/usr/bin/env python3
"""Build and verify signed exact-device Ethernet driver manifests."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey, Ed448PublicKey

DOMAIN = b"OPENCONSOLE-ETHERNET-DRIVER-MANIFEST-V1\0"
IDENTIFIER = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")
HEX4 = re.compile(r"^[0-9a-f]{4}$")
HEX2 = re.compile(r"^[0-9a-f]{2}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")
MACHINES = {"x64": "0x8664", "arm64": "0xaa64"}
ACQUISITION = {"source-build", "redistributable", "user-acquired"}
PROTOCOLS = {"nii", "undi", "snp"}


class ManifestError(ValueError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def canonical(payload: dict) -> bytes:
    return json.dumps(payload, ensure_ascii=True, separators=(",", ":"), sort_keys=True).encode("ascii")


def private_key(path: Path) -> Ed448PrivateKey:
    raw = path.read_bytes()
    key = Ed448PrivateKey.from_private_bytes(raw) if len(raw) == 57 else serialization.load_pem_private_key(raw, None)
    if not isinstance(key, Ed448PrivateKey):
        raise ManifestError("OCED_BAD_KEY", "private key is not Ed448")
    return key


def public_key(path: Path) -> Ed448PublicKey:
    raw = path.read_bytes()
    key = Ed448PublicKey.from_public_bytes(raw) if len(raw) == 57 else serialization.load_pem_public_key(raw)
    if not isinstance(key, Ed448PublicKey):
        raise ManifestError("OCED_BAD_KEY", "public key is not Ed448")
    return key


def public_bytes(key: Ed448PublicKey) -> bytes:
    return key.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)


def exact_device(device: dict, where: str) -> None:
    required = ("vendor", "device", "subsystemVendor", "subsystemDevice", "revisionMin", "revisionMax", "classCode")
    if set(device) != set(required):
        raise ManifestError("OCED_BAD_DEVICE", f"{where} has missing or unknown fields")
    for field in ("vendor", "device", "subsystemVendor", "subsystemDevice"):
        if not HEX4.fullmatch(device[field]):
            raise ManifestError("OCED_BAD_DEVICE", f"{where}.{field} is not exact lowercase hex")
    for field in ("revisionMin", "revisionMax"):
        if not HEX2.fullmatch(device[field]):
            raise ManifestError("OCED_BAD_DEVICE", f"{where}.{field} is not exact lowercase hex")
    if int(device["revisionMin"], 16) > int(device["revisionMax"], 16):
        raise ManifestError("OCED_BAD_DEVICE", f"{where} revision range is reversed")
    if device["classCode"] != "020000":
        raise ManifestError("OCED_BAD_DEVICE", f"{where} is not an Ethernet controller")


def validate_payload(payload: dict) -> dict:
    required = {"manifestId", "driverVersion", "architecture", "acquisition", "license",
                "provenance", "artifact", "protocols", "priority", "compatibility",
                "devices", "exclusions", "revoked"}
    if set(payload) != required:
        raise ManifestError("OCED_BAD_FIELDS", "payload has missing or unknown fields")
    if not IDENTIFIER.fullmatch(payload["manifestId"]) or not IDENTIFIER.fullmatch(payload["driverVersion"]):
        raise ManifestError("OCED_BAD_ID", "invalid manifest id or version")
    architecture = payload["architecture"]
    if architecture not in MACHINES or payload["acquisition"] not in ACQUISITION:
        raise ManifestError("OCED_BAD_POLICY", "invalid architecture or acquisition")
    if not isinstance(payload["license"], str) or not 1 <= len(payload["license"]) <= 128:
        raise ManifestError("OCED_BAD_POLICY", "invalid license decision")
    provenance = payload["provenance"]
    if set(provenance) != {"source", "upstreamVersion", "upstreamDigest"} or not provenance["source"].startswith("https://"):
        raise ManifestError("OCED_BAD_PROVENANCE", "invalid provenance")
    if provenance["upstreamDigest"] is not None and not SHA256.fullmatch(provenance["upstreamDigest"]):
        raise ManifestError("OCED_BAD_PROVENANCE", "invalid upstream digest")
    artifact = payload["artifact"]
    if set(artifact) != {"path", "bytes", "sha256", "peMachine", "subsystem"}:
        raise ManifestError("OCED_BAD_ARTIFACT", "invalid artifact fields")
    if not re.fullmatch(r"EFI/OpenConsole/Drivers/Network/[A-Za-z0-9._-]+\.efi", artifact["path"]):
        raise ManifestError("OCED_BAD_ARTIFACT", "artifact path is outside network driver directory")
    if not isinstance(artifact["bytes"], int) or not 1 <= artifact["bytes"] <= 4 * 1024 * 1024:
        raise ManifestError("OCED_BAD_ARTIFACT", "artifact size is outside bounds")
    if not SHA256.fullmatch(artifact["sha256"]) or artifact["peMachine"] != MACHINES[architecture] or artifact["subsystem"] != 11:
        raise ManifestError("OCED_BAD_ARTIFACT", "artifact digest, machine, or EFI driver subsystem is invalid")
    protocols = payload["protocols"]
    if not isinstance(protocols, list) or protocols != sorted(set(protocols)) or not set(protocols).issubset(PROTOCOLS) or not protocols:
        raise ManifestError("OCED_BAD_PROTOCOL", "protocol list must be sorted, unique, and known")
    if not isinstance(payload["priority"], int) or not 0 <= payload["priority"] <= 65535:
        raise ManifestError("OCED_BAD_POLICY", "priority is outside bounds")
    compatibility = payload["compatibility"]
    if set(compatibility) != {"platformAbiMajor", "platformAbiMinorMin", "platformAbiMinorMax", "firmwarePrerequisites"}:
        raise ManifestError("OCED_BAD_COMPAT", "invalid compatibility fields")
    if compatibility["platformAbiMajor"] != 1 or not 0 <= compatibility["platformAbiMinorMin"] <= compatibility["platformAbiMinorMax"] <= 65535:
        raise ManifestError("OCED_BAD_COMPAT", "invalid ABI range")
    prerequisites = compatibility["firmwarePrerequisites"]
    if not isinstance(prerequisites, list) or prerequisites != sorted(set(prerequisites)):
        raise ManifestError("OCED_BAD_COMPAT", "firmware prerequisites must be sorted and unique")
    devices = payload["devices"]
    exclusions = payload["exclusions"]
    if not isinstance(devices, list) or not devices or len(devices) > 256 or not isinstance(exclusions, list) or len(exclusions) > 256:
        raise ManifestError("OCED_BAD_DEVICE", "invalid device/exclusion count")
    for index, device in enumerate(devices):
        exact_device(device, f"devices[{index}]")
    for index, device in enumerate(exclusions):
        exact_device(device, f"exclusions[{index}]")
    keys = lambda entry: tuple(entry[field] for field in ("vendor", "device", "subsystemVendor", "subsystemDevice", "revisionMin", "revisionMax", "classCode"))
    if [keys(item) for item in devices] != sorted(set(keys(item) for item in devices)):
        raise ManifestError("OCED_BAD_ORDER", "devices must be sorted and unique")
    if [keys(item) for item in exclusions] != sorted(set(keys(item) for item in exclusions)):
        raise ManifestError("OCED_BAD_ORDER", "exclusions must be sorted and unique")
    if set(keys(item) for item in devices) & set(keys(item) for item in exclusions):
        raise ManifestError("OCED_BAD_DEVICE", "device is simultaneously admitted and excluded")
    if not isinstance(payload["revoked"], bool):
        raise ManifestError("OCED_BAD_POLICY", "revoked must be boolean")
    return payload


def build(payload: dict, key: Ed448PrivateKey) -> dict:
    validate_payload(payload)
    body = canonical(payload)
    public = public_bytes(key.public_key())
    return {"schema": "openconsole.ethernet-driver-manifest.v1", "payload": payload,
            "signature": {"algorithm": "ed448", "keyId": hashlib.sha256(public).hexdigest(),
                          "value": key.sign(DOMAIN + body).hex()}}


def verify(document: dict, key: Ed448PublicKey) -> dict:
    if set(document) != {"schema", "payload", "signature"} or document.get("schema") != "openconsole.ethernet-driver-manifest.v1":
        raise ManifestError("OCED_BAD_ENVELOPE", "invalid manifest envelope")
    payload = validate_payload(document["payload"])
    signature = document["signature"]
    expected_key_id = hashlib.sha256(public_bytes(key)).hexdigest()
    if set(signature) != {"algorithm", "keyId", "value"} or signature["algorithm"] != "ed448" or signature["keyId"] != expected_key_id:
        raise ManifestError("OCED_UNTRUSTED_KEY", "manifest signer is not trusted")
    try:
        raw_signature = bytes.fromhex(signature["value"])
    except ValueError as exc:
        raise ManifestError("OCED_BAD_SIGNATURE", "signature is not hexadecimal") from exc
    if len(raw_signature) != 114:
        raise ManifestError("OCED_BAD_SIGNATURE", "signature has the wrong size")
    try:
        key.verify(raw_signature, DOMAIN + canonical(payload))
    except InvalidSignature as exc:
        raise ManifestError("OCED_BAD_SIGNATURE", "signature verification failed") from exc
    if payload["revoked"]:
        raise ManifestError("OCED_REVOKED", "driver manifest is revoked")
    return payload


def main() -> int:
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    sign = commands.add_parser("sign")
    sign.add_argument("payload", type=Path); sign.add_argument("private_key", type=Path); sign.add_argument("output", type=Path)
    check = commands.add_parser("verify")
    check.add_argument("manifest", type=Path); check.add_argument("public_key", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "sign":
            document = build(json.loads(args.payload.read_text(encoding="utf-8")), private_key(args.private_key))
            args.output.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            payload = document["payload"]
        else:
            payload = verify(json.loads(args.manifest.read_text(encoding="utf-8")), public_key(args.public_key))
        print(json.dumps({"architecture": payload["architecture"], "manifestId": payload["manifestId"],
                          "sha256": payload["artifact"]["sha256"]}, sort_keys=True))
        return 0
    except (OSError, ValueError, ManifestError) as exc:
        print(json.dumps({"code": getattr(exc, "code", "OCED_IO"), "message": str(exc)}, sort_keys=True), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
