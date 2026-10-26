#!/usr/bin/env python3
"""Correlate complete USB runs with token-bound, external LAN observations."""

import argparse
import datetime
import hashlib
import ipaddress
import json
from pathlib import Path


PREFIXES = {
    "fixture": "OPENCONSOLE_PHYSICAL_NET_FIXTURE_",
    "dhcp": "OPENCONSOLE_PHYSICAL_NET_DHCP_OK_USEC_",
    "address": "OPENCONSOLE_PHYSICAL_NET_ADDRESS_",
    "dns": "OPENCONSOLE_PHYSICAL_NET_DNS_OK_USEC_",
    "dns_address": "OPENCONSOLE_PHYSICAL_NET_DNS_ADDRESS_",
    "udp": "OPENCONSOLE_PHYSICAL_NET_UDP_OK_USEC_",
    "https": "OPENCONSOLE_PHYSICAL_NET_HTTPS_OK_USEC_",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_time(value: str) -> datetime.datetime:
    parsed = datetime.datetime.fromisoformat(value)
    if parsed.tzinfo is None:
        raise ValueError("timestamp lacks timezone")
    return parsed.astimezone(datetime.timezone.utc)


def usb_runs(raw: bytes) -> list[list[str]]:
    text = raw.split(b"\0", 1)[0].decode("ascii")
    complete, current = [], None
    for line in text.splitlines():
        if line == "OPENCONSOLE_PHYSICAL_NET_RUN_BEGIN":
            current = []
        elif current is not None:
            if line.startswith("OPENCONSOLE_PHYSICAL_NET_RUN_FAILED_STATUS_"):
                current = None
            elif line == "OPENCONSOLE_PHYSICAL_NET_RUN_COMPLETE":
                complete.append(current)
                current = None
            else:
                current.append(line)
    return complete


def valid_usb_run(lines: list[str], expected_fixture: str) -> bool:
    for name, prefix in PREFIXES.items():
        matches = [line[len(prefix):] for line in lines if line.startswith(prefix)]
        if len(matches) != 1:
            return False
        if name == "fixture" and matches[0] != expected_fixture:
            return False
        if name in ("address", "dns_address") and matches[0] == "00000000":
            return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Correlate USB and external physical-network evidence")
    parser.add_argument("usb_log", type=Path)
    parser.add_argument("observer_jsonl", type=Path)
    parser.add_argument("fixture_json", type=Path)
    parser.add_argument("--inventory-report", type=Path, required=True,
                        help="physical inventory report proving exact RTL8125 SNP/UNDI")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--minimum-runs", type=int, default=2)
    args = parser.parse_args()
    failures = []
    fixture = json.loads(args.fixture_json.read_text(encoding="utf-8"))
    inventory = json.loads(args.inventory_report.read_text(encoding="utf-8"))
    observations = [json.loads(line) for line in args.observer_jsonl.read_text(
        encoding="utf-8").splitlines() if line.strip()]
    token = fixture.get("token", "")
    expected_udp = fixture.get("udp_request", "")
    expected_https = f"GET /probe?token={token} HTTP/1.1"
    try:
        created = parse_time(fixture["created_utc"])
        expires = parse_time(fixture["expires_utc"])
        endpoint = str(ipaddress.ip_address(fixture["endpoint_ipv4"]))
    except (KeyError, TypeError, ValueError) as error:
        raise SystemExit(f"FAIL: invalid fixture identity/window: {error}") from error
    if not token or expected_udp != f"openconsole-physical-udp:{token}":
        failures.append("fixture token and UDP request are inconsistent")
    if expires <= created:
        failures.append("fixture validity window is empty")

    runs = usb_runs(args.usb_log.read_bytes())
    valid_runs = [run for run in runs if valid_usb_run(run, expected_udp)]
    if len(valid_runs) < args.minimum_runs:
        failures.append("insufficient complete token-bound USB child runs")

    rtl = inventory.get("firmware", {}).get("targets", {}).get("10ec:8125", {})
    protocols = rtl.get("protocols") or {}
    rtl_inventory_pass = (
        inventory.get("decisions", {}).get("rtl8125_path") == "firmware-snp-undi"
        and rtl.get("identity_verified") is True
        and protocols.get("revision") == "05"
        and any(protocols.get(name, 0) > 0 for name in ("snp", "undi", "undi31")))
    if not rtl_inventory_pass:
        failures.append("inventory does not prove firmware SNP/UNDI on RTL8125 10ec:8125 rev 05")

    accepted = []
    signatures = set()
    for index, entry in enumerate(observations, 1):
        try:
            payload = entry["payload"].encode("ascii")
            observed = parse_time(entry["observed_utc"])
            peer = str(ipaddress.ip_address(entry["peer"]))
            signature = (entry["observed_utc"], entry["kind"], peer,
                         entry["peer_port"], entry["sha256"])
            valid = (entry.get("schema") == 1 and entry.get("fixture_token") == token
                     and hashlib.sha256(payload).hexdigest() == entry["sha256"]
                     and created <= observed <= expires and signature not in signatures)
        except (KeyError, TypeError, UnicodeEncodeError, ValueError):
            valid = False
        if valid:
            signatures.add(signature)
            accepted.append({**entry, "peer": peer, "time": observed})
        else:
            failures.append(f"observer record {index} failed schema/hash/token/time/peer validation")

    udp = sorted((entry for entry in accepted if entry["kind"] == "udp"
                  and entry["payload"] == expected_udp), key=lambda entry: entry["time"])
    https = sorted((entry for entry in accepted if entry["kind"] == "https"
                    and entry["payload"].split("\r\n", 1)[0] == expected_https),
                   key=lambda entry: entry["time"])
    pairs, used_https = [], set()
    for datagram in udp:
        match = next((index for index, request in enumerate(https)
                      if index not in used_https and request["peer"] == datagram["peer"]
                      and datagram["time"] <= request["time"]
                      <= datagram["time"] + datetime.timedelta(seconds=120)), None)
        if match is not None:
            used_https.add(match)
            pairs.append({"peer": datagram["peer"],
                          "udp_observed_utc": datagram["observed_utc"],
                          "https_observed_utc": https[match]["observed_utc"]})
    if len(pairs) < args.minimum_runs:
        failures.append("insufficient same-peer token-bound UDP/HTTPS observation pairs")

    report = {
        "schema": 2,
        "scope": "physical RTL8125 child plus independently observed LAN fixture",
        "minimum_runs": args.minimum_runs,
        "complete_usb_runs": len(runs),
        "valid_token_bound_usb_runs": len(valid_runs),
        "valid_external_udp_observations": len(udp),
        "valid_external_https_observations": len(https),
        "correlated_external_pairs": pairs,
        "external_peer_addresses": sorted({pair["peer"] for pair in pairs}),
        "fixture_endpoint_ipv4": endpoint,
        "fixture_token": token,
        "fixture_sha256": sha256(args.fixture_json),
        "usb_log_sha256": sha256(args.usb_log),
        "observer_log_sha256": sha256(args.observer_jsonl),
        "inventory_report_sha256": sha256(args.inventory_report),
        "rtl8125_firmware_inventory_pass": rtl_inventory_pass,
        "failures": failures,
        "pass": not failures,
    }
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    if failures:
        raise SystemExit("FAIL: physical network evidence is incomplete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
