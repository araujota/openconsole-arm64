#!/usr/bin/env python3
import json
import re
import sys
from pathlib import Path

HEX4 = re.compile(r"^[0-9a-f]{4}$")
VALID_EVIDENCE = {"declared", "host-observed", "firmware-inventoried", "vm-validated", "physical-validated"}
VALID_SUPPORT = {"unverified", "supported", "unsupported"}
VALID_ADMISSION = {"firmware", "source-build", "redistributable", "user-acquired", "candidate", "unsupported"}
COVERAGE_STATES = {
    "build": {"built", "not-built", "not-applicable"},
    "vm": {"validated", "not-validated", "not-applicable"},
    "catalogMatch": {"exact", "family-only", "none"},
    "physical": {"validated", "inventory-only", "not-tested"},
    "delivery": {"bundled", "downloadable", "user-acquired", "unavailable"},
    "support": {"supported", "unverified", "unsupported"},
}


def fail(message: str) -> None:
    raise SystemExit(f"ethernet-matrix: {message}")


def validate(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema") != "openconsole.ethernet-coverage-matrix.v1":
        fail("unsupported schema")
    if data.get("scope") != "wired-ethernet-only":
        fail("scope must remain wired-ethernet-only")
    if data.get("architectures") != ["x64", "arm64"]:
        fail("architectures must be exactly x64 and arm64")
    if {name: set(values) for name, values in data.get("coverageStates", {}).items()} != COVERAGE_STATES:
        fail("coverage state vocabulary is incomplete or changed")
    connection_types = data.get("connectionTypes", [])
    required_types = {"firmware-snp", "pci-lom-1g", "pci-lom-2.5g", "pci-lom-5g", "pci-10g", "soc-mac"}
    if {item.get("id") for item in connection_types} != required_types:
        fail("baseline connection-type matrix is incomplete")
    for item in connection_types:
        if not item.get("x64") or not item.get("arm64") or not isinstance(item.get("speedsGbps"), list):
            fail(f"connection type lacks architecture/speed status: {item.get('id')}")
    encoded = json.dumps(data).lower()
    for forbidden in ("wifi driver", "wi-fi driver", "wireless driver", "ssid", "wpa2", "wpa3"):
        if forbidden in encoded:
            fail(f"out-of-scope wireless content: {forbidden}")

    upstream_ids = set()
    for item in data.get("upstreams", []):
        upstream_id = item.get("id")
        if not upstream_id or upstream_id in upstream_ids:
            fail("upstream ids must be present and unique")
        upstream_ids.add(upstream_id)
        if item.get("admission") not in VALID_ADMISSION:
            fail(f"invalid admission for {upstream_id}")
        if not set(item.get("architectures", [])).issubset({"ia32", "x64", "arm", "arm64", "loongarch64"}):
            fail(f"invalid architecture for {upstream_id}")
        if item.get("bundleDecision", "").startswith("blocked") and item.get("digest"):
            fail(f"blocked artifact {upstream_id} must not assert an unverified digest")

    boards = data.get("motherboards", [])
    if len(boards) < 10:
        fail("seed sample must contain at least ten motherboard/system rows")
    if sum(row.get("architecture") == "arm64" for row in boards) < 3:
        fail("seed sample must contain at least three ARM64 platform rows")
    makers = {row.get("manufacturer") for row in boards}
    required = {"ASUS", "MSI", "Gigabyte", "ASRock", "Dell", "HP", "Lenovo"}
    if not required.issubset(makers):
        fail(f"missing manufacturers: {sorted(required - makers)}")
    keys = set()
    for row in boards:
        key = (row.get("manufacturer"), row.get("model"), row.get("boardRevision"),
               row.get("firmware", {}).get("version"))
        if key in keys:
            fail(f"duplicate board/firmware row: {key}")
        keys.add(key)
        if row.get("architecture") not in {"x64", "arm64"}:
            fail(f"invalid board architecture: {key}")
        if row.get("evidence") not in VALID_EVIDENCE or row.get("support") not in VALID_SUPPORT:
            fail(f"invalid evidence/support state: {key}")
        if row.get("support") == "supported" and row.get("evidence") != "physical-validated":
            fail(f"support requires physical evidence: {key}")
        if not row.get("source"):
            fail(f"board row lacks source: {key}")
        pci = row.get("ethernet", {}).get("pci")
        if pci:
            for name in ("vendor", "device", "subsystemVendor", "subsystemDevice"):
                if not HEX4.fullmatch(pci.get(name, "")):
                    fail(f"invalid PCI {name}: {key}")
    exact_keys = set()
    for identity in data.get("exactIdentities", []):
        pci = identity.get("pci", {})
        for name in ("vendor", "device", "subsystemVendor", "subsystemDevice"):
            if not HEX4.fullmatch(pci.get(name, "")):
                fail(f"invalid exact-identity PCI {name}")
        if not re.fullmatch(r"^[0-9a-f]{2}$", pci.get("revision", "")):
            fail("invalid exact-identity PCI revision")
        key = tuple(pci.get(name) for name in
                    ("vendor", "device", "subsystemVendor", "subsystemDevice", "revision"))
        if key in exact_keys:
            fail(f"duplicate exact identity: {key}")
        exact_keys.add(key)
        architectures = identity.get("architectures", {})
        if set(architectures) != {"x64", "arm64"}:
            fail(f"exact identity lacks architecture parity: {key}")
        for architecture, coverage in architectures.items():
            if set(coverage) != set(COVERAGE_STATES) | {"provider"}:
                fail(f"incomplete coverage axes for {architecture}: {key}")
            for axis, allowed in COVERAGE_STATES.items():
                if coverage[axis] not in allowed:
                    fail(f"invalid {axis} state for {architecture}: {key}")
            if coverage["support"] == "supported" and coverage["physical"] != "validated":
                fail(f"support requires physical validation for {architecture}: {key}")
            if coverage["delivery"] == "bundled" and not coverage["provider"]:
                fail(f"bundled coverage requires a provider for {architecture}: {key}")
    observed_exact = {
        (pci["vendor"], pci["device"], pci["subsystemVendor"], pci["subsystemDevice"], pci["revision"])
        for row in boards if (pci := row.get("ethernet", {}).get("pci"))
    }
    if not observed_exact.issubset(exact_keys):
        fail("every exact motherboard identity needs architecture-indexed coverage")
    return data


if __name__ == "__main__":
    matrix = Path(sys.argv[1] if len(sys.argv) > 1 else "config/ethernet-coverage-matrix.json")
    data = validate(matrix)
    print(json.dumps({"architectures": data["architectures"], "boards": len(data["motherboards"]),
                      "schema": data["schema"], "upstreams": len(data["upstreams"])}, sort_keys=True))
