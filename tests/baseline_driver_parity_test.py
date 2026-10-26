#!/usr/bin/env python3
from __future__ import annotations
import json,pathlib,shutil,subprocess,sys,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
contract=json.loads((ROOT/"config/baseline-driver-roles.json").read_text())
def populate(root,arch):
    sources={
      "input":ROOT/"build/edk2-input-driver"/("AARCH64" if arch=="arm64" else "") ,
      "network":ROOT/"build/edk2-network-stack"/("AARCH64" if arch=="arm64" else ""),
      "audio":ROOT/"build/opencore-audiodxe"/("AARCH64" if arch=="arm64" else ""),
      "crypto":ROOT/"build/update-crypto-dxe"/("AARCH64" if arch=="arm64" else "")}
    dirs={"input":"Input","network":"Network","audio":"Audio","crypto":"Crypto"}
    for role,names in contract["roles"].items():
      dest=root/"EFI/OpenConsole/Drivers"/dirs[role];dest.mkdir(parents=True)
      for name in names: shutil.copyfile(sources[role]/name,dest/name)
with tempfile.TemporaryDirectory(prefix="oc-parity-") as d:
  base=pathlib.Path(d);x=base/"x64";a=base/"arm64";populate(x,"x64");populate(a,"arm64")
  subprocess.run([sys.executable,"tools/verify_baseline_driver_parity.py","--x64",str(x),"--arm64",str(a)],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
  shutil.copyfile(x/"EFI/OpenConsole/Drivers/Input/UsbMouseDxe.efi",a/"EFI/OpenConsole/Drivers/Input/UsbMouseDxe.efi")
  assert subprocess.run([sys.executable,"tools/verify_baseline_driver_parity.py","--arm64",str(a)],cwd=ROOT,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==2
print("PASS: role parity accepts native sets and rejects x64 substitution on ARM64")
