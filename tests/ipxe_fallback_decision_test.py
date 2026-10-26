#!/usr/bin/env python3
import json
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
decision = json.loads((ROOT / "config/ipxe-ethernet-fallback-decision.json").read_text())
assert decision["schema"] == "openconsole.ipxe-fallback-decision.v1"
assert decision["decision"] == "rejected-for-baseline"
assert decision["scope"] == "wired-ethernet-only"
assert not decision["licensing"]["approvedForPermissiveCoreLinking"]
assert not decision["licensing"]["approvedForBaselineRedistribution"]
assert decision["result"] == {
    "sourceImported": False,
    "binaryBundled": False,
    "separateOpenSpecAuthorized": False,
}
tracked = subprocess.run(["git", "ls-files"], cwd=ROOT, check=True,
                         text=True, capture_output=True).stdout.splitlines()
assert not any(path.startswith(("vendor/ipxe/", "src/ipxe/")) for path in tracked)
print("PASS: iPXE fallback remains separately gated and absent from baseline")
