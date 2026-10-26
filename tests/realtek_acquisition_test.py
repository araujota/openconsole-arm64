#!/usr/bin/env python3
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
record = json.loads((ROOT / "config/realtek-undi-2.081-acquisition.json").read_text())
assert record["downloadId"] == 3413 and record["artifact"]["bytes"] == 319890
assert len(record["artifact"]["sha256"]) == 64
assert record["artifact"]["licenseFiles"] == [] and not record["artifact"]["redistributionGrant"]
assert set(record["images"]) >= {"x64", "arm64"}
assert record["authenticode"]["present"] and record["protocols"] == ["NII", "UNDI"]
assert set(record["supportedPci"]["deviceIdsFromX64SupportedRoutine"]) == {"8125", "8126", "8127", "8136", "8161", "8162", "8168", "9151"}
assert record["acquisition"]["artifactObtained"]
assert record["admission"] == "user-acquired-not-redistributable"
assert not (ROOT / "EFI/OpenConsole/Drivers/Network/RtkUndiDxe.efi").exists()
print("PASS: official Realtek artifact is audited and remains user-acquired")
