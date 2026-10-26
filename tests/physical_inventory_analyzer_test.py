#!/usr/bin/env python3
import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYZER = ROOT / "scripts" / "analyze-physical-inventory.py"
TARGETS = (
    ("10EC", "8125", "05", "02", "00", "00"),
    ("1022", "15E3", "00", "04", "03", "00"),
    ("1002", "1640", "00", "04", "03", "00"),
    ("10DE", "22E9", "00", "04", "03", "00"),
)


def inventory(post: bool) -> bytes:
    lines = ["OPENCONSOLE_IO_INVENTORY_BEGIN"]
    for vendor, device, revision, base, subclass, interface in TARGETS:
        lines.append(
            f"OPENCONSOLE_IO_PCI_{vendor}_{device}_{base}_{subclass}_{interface}_DEVICE_PATH")
        snp = 1 if vendor == "10EC" else 0
        audio = 1 if post and base == "04" else 0
        controller = 1 if post and base == "04" else 0
        codec = 2 if post and base == "04" else 0
        lines.append(
            f"OPENCONSOLE_IO_PCI_PROTOCOLS_{vendor}_{device}_{revision}_"
            f"SNP_{snp:02X}_UNDI_00_UNDI31_00_AUDIO_IO_{audio:02X}_"
            f"HDA_CONTROLLER_INFO_{controller:02X}_HDA_CODEC_INFO_{codec:02X}")
    lines.append("OPENCONSOLE_IO_USB_AUDIO_17E9_6006_02_00_01_01_20")
    lines.append("OPENCONSOLE_IO_INVENTORY_END")
    return ("\n".join(lines) + "\n").encode("ascii")


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        before = directory / "before.log"
        after = directory / "after.log"
        output = directory / "evidence.json"
        before.write_bytes(inventory(False))
        after.write_bytes(inventory(True))
        subprocess.run(
            [str(ANALYZER), str(before), "--post-driver-log", str(after),
             "--output", str(output)], check=True, capture_output=True, text=True)
        report = json.loads(output.read_text(encoding="utf-8"))
        assert report["pass"] is True
        assert report["decisions"]["rtl8125_path"] == "firmware-snp-undi"
        assert report["decisions"]["firmware_audioio_present"] is False
        assert report["decisions"]["packaged_audioio_present"] is True
        assert report["decisions"]["usb_audio_17e9_6006_discovered"] is True
        assert report["after_packaged_drivers"]["targets"]["1022:15e3"][
            "protocols"]["hda_codec_info"] == 2

        truncated = directory / "truncated.log"
        truncated.write_text("OPENCONSOLE_IO_INVENTORY_BEGIN\n", encoding="ascii")
        failed = subprocess.run([str(ANALYZER), str(truncated)],
                                capture_output=True, text=True)
        assert failed.returncode != 0
        assert "incomplete or was not flushed" in failed.stderr
    print("physical inventory analyzer tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
