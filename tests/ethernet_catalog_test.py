#!/usr/bin/env python3
import importlib.util, json, pathlib, struct, tempfile

ROOT=pathlib.Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location("ethernet_catalog",ROOT/"tools/ethernet_catalog.py")
MODULE=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(MODULE)
source=json.loads((ROOT/"tests/fixtures/ethernet-catalog-x64.json").read_text())
blob=MODULE.build(source)
assert blob[:4]==b"OEC1" and len(blob)==224
assert struct.unpack_from("<I",blob,16)[0]==1
assert struct.unpack_from("<HHHH",blob,40)==(0x10ec,0x8125,0x1462,0x7e12)
assert MODULE.build(json.loads((ROOT/"config/ethernet-catalog-arm64.json").read_text()))[:4]==b"OEC1"
bad=json.loads(json.dumps(source));bad["entries"][0]["pci"]["device"]=0
try: MODULE.build(bad)
except ValueError: pass
else: raise AssertionError("zero PCI device accepted")
print("ethernet catalog tool tests passed")
