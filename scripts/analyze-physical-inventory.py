#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path

TARGETS = {
    ("10EC", "8125"): {"kind": "ethernet", "revision": "05"},
    ("1022", "15E3"): {"kind": "analog-hda"},
    ("1002", "1640"): {"kind": "amd-hdmi-hda"},
    ("10DE", "22E9"): {"kind": "nvidia-hdmi-hda"},
}

PCI = re.compile(
    r"^OPENCONSOLE_IO_PCI_([0-9A-F]{4})_([0-9A-F]{4})_"
    r"([0-9A-F]{2})_([0-9A-F]{2})_([0-9A-F]{2})_(DEVICE|NO_DEVICE)_PATH$")
BOUND = re.compile(
    r"^OPENCONSOLE_IO_PCI_PROTOCOLS_([0-9A-F]{4})_([0-9A-F]{4})_([0-9A-F]{2})_"
    r"SNP_([0-9A-F]{2})_UNDI_([0-9A-F]{2})_UNDI31_([0-9A-F]{2})_"
    r"AUDIO_IO_([0-9A-F]{2})_HDA_CONTROLLER_INFO_([0-9A-F]{2})_"
    r"HDA_CODEC_INFO_([0-9A-F]{2})$")
USB_AUDIO = re.compile(
    r"^OPENCONSOLE_IO_USB_AUDIO_([0-9A-F]{4})_([0-9A-F]{4})_"
    r"([0-9A-F]{2})_([0-9A-F]{2})_01_([0-9A-F]{2})_([0-9A-F]{2})$")


def parse_inventory(raw: bytes, stage: str) -> dict:
    end = raw.find(b"OPENCONSOLE_IO_INVENTORY_END\n")
    if end < 0:
        raise ValueError(f"{stage} inventory is incomplete or was not flushed")
    text = raw[:end + len(b"OPENCONSOLE_IO_INVENTORY_END\n")].decode("ascii")
    devices, bindings, usb_audio = {}, {}, []
    for line in text.splitlines():
        match = PCI.match(line)
        if match:
            vendor, device, base, sub, interface, path = match.groups()
            devices[(vendor, device)] = {
                "base_class": base, "subclass": sub, "interface": interface,
                "device_path": path == "DEVICE",
            }
        match = BOUND.match(line)
        if match:
            vendor, device, revision, *counts = match.groups()
            names = ("snp", "undi", "undi31", "audio_io",
                     "hda_controller_info", "hda_codec_info")
            bindings[(vendor, device)] = {"revision": revision, **{
                name: int(value, 16) for name, value in zip(names, counts)}}
        match = USB_AUDIO.match(line)
        if match:
            vendor, product, interface, alternate, subclass, protocol = match.groups()
            usb_audio.append({"vendor_id": vendor.lower(), "product_id": product.lower(),
                              "interface": int(interface, 16),
                              "alternate_setting": int(alternate, 16),
                              "subclass": int(subclass, 16),
                              "protocol": int(protocol, 16)})
    report = {"stage": stage, "targets": {}, "usb_audio_interfaces": usb_audio,
              "identity_pass": True}
    for identity, expected in TARGETS.items():
        key = f"{identity[0].lower()}:{identity[1].lower()}"
        entry = {**expected, "pci": devices.get(identity),
                 "protocols": bindings.get(identity)}
        present = entry["pci"] is not None and entry["protocols"] is not None
        if expected.get("revision") and present:
            present = entry["protocols"]["revision"] == expected["revision"]
        entry["identity_verified"] = present
        report["targets"][key] = entry
        report["identity_pass"] = report["identity_pass"] and present
    return report


def has_network_device_protocol(target: dict) -> bool:
    protocols = target.get("protocols") or {}
    return any(protocols.get(name, 0) > 0 for name in ("snp", "undi", "undi31"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate physical OpenConsole UEFI inventory evidence")
    parser.add_argument("log", type=Path, help="firmware-only inventory log")
    parser.add_argument("--post-driver-log", type=Path,
                        help="optional inventory captured after packaged drivers load")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        firmware = parse_inventory(args.log.read_bytes(), "firmware-before-packaged-drivers")
        post = (parse_inventory(args.post_driver_log.read_bytes(),
                                "after-packaged-drivers")
                if args.post_driver_log else None)
    except (OSError, UnicodeDecodeError, ValueError) as error:
        raise SystemExit(f"FAIL: {error}") from error
    rtl = firmware["targets"]["10ec:8125"]
    report = {
        "scope": "physical-firmware-and-packaged-driver-inventory",
        "firmware": firmware,
        "after_packaged_drivers": post,
        "decisions": {
            "rtl8125_path": ("firmware-snp-undi" if has_network_device_protocol(rtl)
                              else "exact-device-driver-required"),
            "firmware_audioio_present": any(
                (entry.get("protocols") or {}).get("audio_io", 0) > 0
                for key, entry in firmware["targets"].items() if key != "10ec:8125"),
            "packaged_audioio_present": (any(
                (entry.get("protocols") or {}).get("audio_io", 0) > 0
                for key, entry in post["targets"].items() if key != "10ec:8125")
                if post else None),
            "usb_audio_17e9_6006_discovered": any(
                entry["vendor_id"] == "17e9" and entry["product_id"] == "6006"
                for entry in firmware["usb_audio_interfaces"]),
        },
        "pass": firmware["identity_pass"] and (post is None or post["identity_pass"]),
    }
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    if not report["pass"]:
        raise SystemExit("FAIL: one or more exact physical targets are absent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
