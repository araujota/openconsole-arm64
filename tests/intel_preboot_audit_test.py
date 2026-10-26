#!/usr/bin/env python3
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
audit = json.loads((ROOT / "config/intel-preboot-31.3-audit.json").read_text())
assert audit["schema"] == "openconsole.vendor-ethernet-package-audit.v1"
assert audit["archive"]["publishedSha512Matched"]
assert len(audit["archive"]["sha256"]) == 64 and len(audit["archive"]["sha512"]) == 128
assert not audit["payload"]["looseEfiImages"]
assert {item["architecture"] for item in audit["imageInventory"]} == {"x64", "arm64"}
assert not any(item["consumerBaselineRelevant"] and item["architecture"] == "arm64"
               for item in audit["imageInventory"])
assert audit["redistributionDecision"] == "user-acquired-only"
assert not audit["openConsoleDecision"]["bundled"]
assert not audit["exactPciTableExtracted"]
print("PASS: Intel Preboot provenance, architecture, LOM, and redistribution gate")
