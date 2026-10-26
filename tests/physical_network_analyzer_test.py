#!/usr/bin/env python3
import datetime
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYZER = ROOT / "scripts" / "analyze-physical-network-evidence.py"
TOKEN = "A1B2C3D4"
UDP = f"openconsole-physical-udp:{TOKEN}"


def run_block() -> str:
    return f"""OPENCONSOLE_PHYSICAL_NET_RUN_BEGIN
OPENCONSOLE_PHYSICAL_NET_FIXTURE_{UDP}
OPENCONSOLE_PHYSICAL_NET_DHCP_OK_USEC_00001000
OPENCONSOLE_PHYSICAL_NET_ADDRESS_0A01A8C0
OPENCONSOLE_PHYSICAL_NET_DNS_OK_USEC_00000100
OPENCONSOLE_PHYSICAL_NET_DNS_ADDRESS_0100007F
OPENCONSOLE_PHYSICAL_NET_UDP_OK_USEC_00000020
OPENCONSOLE_PHYSICAL_NET_HTTPS_OK_USEC_00002000
OPENCONSOLE_PHYSICAL_NET_RUN_COMPLETE
"""


def observation(kind: str, payload: str, when: datetime.datetime, port: int) -> dict:
    return {
        "schema": 1, "fixture_token": TOKEN, "kind": kind,
        "observed_utc": when.isoformat(), "peer": "192.168.1.77",
        "peer_port": port, "payload": payload,
        "sha256": hashlib.sha256(payload.encode("ascii")).hexdigest(),
    }


def invoke(usb, observed, fixture, inventory, output):
    return subprocess.run([
        str(ANALYZER), str(usb), str(observed), str(fixture),
        "--inventory-report", str(inventory), "--output", str(output)],
        text=True, capture_output=True)


def main():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        created = datetime.datetime(2026, 1, 1, tzinfo=datetime.timezone.utc)
        fixture_data = {
            "schema": 1, "endpoint_ipv4": "192.168.1.50", "token": TOKEN,
            "created_utc": created.isoformat(),
            "expires_utc": (created + datetime.timedelta(days=2)).isoformat(),
            "udp_request": UDP,
        }
        inventory_data = {
            "decisions": {"rtl8125_path": "firmware-snp-undi"},
            "firmware": {"targets": {"10ec:8125": {
                "identity_verified": True,
                "protocols": {"revision": "05", "snp": 1, "undi": 0, "undi31": 0},
            }}},
        }
        usb, observed = root / "usb.log", root / "observed.jsonl"
        fixture, inventory, output = root / "fixture.json", root / "inventory.json", root / "report.json"
        usb.write_bytes((run_block() * 2).encode("ascii") + b"\0" * 32)
        fixture.write_text(json.dumps(fixture_data), encoding="utf-8")
        inventory.write_text(json.dumps(inventory_data), encoding="utf-8")
        records = []
        for offset, port in ((10, 40001), (30, 40002)):
            start = created + datetime.timedelta(seconds=offset)
            records += [observation("udp", UDP, start, port),
                        observation("https", f"GET /probe?token={TOKEN} HTTP/1.1\r\nHost: fixture\r\n\r\n",
                                    start + datetime.timedelta(seconds=1), port + 100)]
        observed.write_text("".join(json.dumps(record) + "\n" for record in records),
                            encoding="utf-8")
        result = invoke(usb, observed, fixture, inventory, output)
        assert result.returncode == 0, result.stdout + result.stderr
        report = json.loads(output.read_text())
        assert report["pass"] and len(report["correlated_external_pairs"]) == 2
        assert report["rtl8125_firmware_inventory_pass"]

        duplicated = records[:2] * 2
        observed.write_text("".join(json.dumps(record) + "\n" for record in duplicated),
                            encoding="utf-8")
        result = invoke(usb, observed, fixture, inventory, output)
        assert result.returncode != 0
        assert "failed schema/hash/token/time/peer validation" in result.stdout

        observed.write_text("".join(json.dumps(record) + "\n" for record in records),
                            encoding="utf-8")
        inventory_data["firmware"]["targets"]["10ec:8125"]["protocols"]["snp"] = 0
        inventory.write_text(json.dumps(inventory_data), encoding="utf-8")
        result = invoke(usb, observed, fixture, inventory, output)
        assert result.returncode != 0
        assert "does not prove firmware SNP/UNDI" in result.stdout
    print("PASS: physical network evidence correlation and replay rejection")


if __name__ == "__main__":
    main()
