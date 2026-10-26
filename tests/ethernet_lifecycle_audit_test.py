#!/usr/bin/env python3
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
audit = json.loads((root / "config/ethernet-lifecycle-audit.json").read_text())
static = json.loads((root / "config/realtek-undi-2.081-static-lifecycle.json").read_text())
assert audit["schema"] == "openconsole.ethernet-lifecycle-audit.v1"
assert audit["policy"]["unknownIsReleaseBlocking"] is True
providers = {p["id"]: p for p in audit["providers"]}
wrapper = providers["edk2-snpdxe-0a503bc"]
assert wrapper["persistentMutation"] == "pass-nvdata-write-rejected"
assert wrapper["busMastering"] == "delegated-to-undi-provider"
for name in ("realtek-undi-2.081-arm64", "intel-preboot-31.3"):
    provider = providers[name]
    assert provider["releaseDecision"].startswith("blocked")
    for field in ("dmaMapping", "failureCleanup",
                  "disconnectUnload", "busMastering"):
        assert provider[field] == "unknown"
    assert provider["persistentMutation"].startswith("unknown")
x64 = providers["realtek-undi-2.081-x64"]
assert x64["persistentMutation"] == "fail-static-config-ui-advertises-nvram-write"
assert x64["releaseDecision"] == "rejected-for-baseline"
for field in ("dmaMapping", "failureCleanup", "disconnectUnload", "busMastering"):
    assert x64[field] == "unknown"
images = {image["architecture"]: image for image in static["images"]}
assert images["x64"]["markers"]["Save RealManage Configuration to NvRam"]["utf16leOffset"] == 154201
assert all(offset is None for marker in images["arm64"]["markers"].values()
           for offset in marker.values())
assert static["interpretation"]["dma"].startswith("not-provable")

source = next((root / "build/vendor-src").glob("edk2-*/NetworkPkg/SnpDxe"))
callback = (source / "Callback.c").read_text()
shutdown = (source / "Shutdown.c").read_text()
snp = (source / "Snp.c").read_text()
nvdata = (source / "Nvdata.c").read_text()
assert "Snp->PciIo->Map" in callback and "Snp->PciIo->Unmap" in callback
assert "PxeShutdown (Snp);" in snp and "PxeStop (Snp);" in snp
assert "Snp->PciIo->FreeBuffer" in shutdown
assert "if (!ReadWrite)" in nvdata and "Status = EFI_UNSUPPORTED;" in nvdata
print("PASS: Ethernet lifecycle audit preserves vendor-specific safety gates")
