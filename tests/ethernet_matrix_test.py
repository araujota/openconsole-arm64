#!/usr/bin/env python3
import copy
import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.validate_ethernet_matrix import validate

SOURCE = ROOT / "config" / "ethernet-coverage-matrix.json"


def rejected(data: dict) -> None:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "matrix.json"
        path.write_text(json.dumps(data), encoding="utf-8")
        try:
            validate(path)
        except SystemExit:
            return
        raise AssertionError("invalid matrix was accepted")


data = validate(SOURCE)
assert data["arm64Baseline"]["status"] == "matrix-ready-no-physical-motherboard-qualified"
assert all(row["support"] != "supported" for row in data["motherboards"])
assert any(row["evidence"] == "host-observed" for row in data["motherboards"])
assert data["exactIdentities"][0]["architectures"]["x64"]["physical"] == "inventory-only"
assert data["exactIdentities"][0]["architectures"]["arm64"]["physical"] == "not-tested"
assert {item["id"] for item in data["connectionTypes"]} == {
    "firmware-snp", "pci-lom-1g", "pci-lom-2.5g", "pci-lom-5g", "pci-10g", "soc-mac"}
assert sum(row["architecture"] == "arm64" for row in data["motherboards"]) >= 3

bad = copy.deepcopy(data)
bad["motherboards"][1]["support"] = "supported"
rejected(bad)

bad = copy.deepcopy(data)
bad["architectures"] = ["x64"]
rejected(bad)

bad = copy.deepcopy(data)
bad["motherboards"] = [row for row in bad["motherboards"] if row["architecture"] != "arm64"]
rejected(bad)

bad = copy.deepcopy(data)
bad["motherboards"][0]["ethernet"]["pci"]["device"] = "812"
rejected(bad)

bad = copy.deepcopy(data)
del bad["exactIdentities"][0]["architectures"]["arm64"]
rejected(bad)

bad = copy.deepcopy(data)
bad["exactIdentities"][0]["architectures"]["x64"]["support"] = "supported"
rejected(bad)

print("PASS: architecture-indexed Ethernet matrix and evidence gates")
