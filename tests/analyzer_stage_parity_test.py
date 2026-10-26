#!/usr/bin/env python3
"""Verify host and Convex-worker analyzers agree on pre-execution stages."""
import json,pathlib,subprocess,sys,tempfile
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/"tools"),str(ROOT/"tests")]
import ocg2,openconsole_analyzer as host
from analyzer_test import MANIFEST,bundle,pe

STAGES={"container","signature","manifest-assets","installed-tree","compatibility"}
RULES={"OCSA-CONTAINER-INVALID","OCSA-SIGNATURE-UNTRUSTED","OCSA-SIGNATURE-INVALID",
       "OCSA-ASSET-INVALID","OCSA-INSTALLED-TREE-INVALID",
       "OCSA-CAPABILITY-REQUIRED-UNSUPPORTED","OCSA-CAPABILITY-OPTIONAL-UNSUPPORTED"}

def service(path,key_path):
    run=subprocess.run(["node","--experimental-strip-types",str(ROOT/"web/scripts/analyze.ts"),
                        str(path),str(key_path)],text=True,capture_output=True)
    assert run.returncode in (0,2),run.stderr
    return json.loads(run.stdout)

def normalized(report):
    return (report["verdict"],[(s["name"],s["status"],s.get("code")) for s in report["stages"]
            if s["name"] in STAGES],sorted(f["ruleId"] for f in report["findings"] if f["ruleId"] in RULES))

def main():
    key=Ed448PrivateKey.from_private_bytes(bytes(range(1,58)))
    raw=key.public_key().public_bytes(serialization.Encoding.Raw,serialization.PublicFormat.Raw)
    trust={ocg2.hashlib.sha256(raw).digest():key.public_key()}
    long=("a/"*80+"game.efi").encode()
    bad_asset_manifest=MANIFEST.replace(b'entry = "bin/game.efi"\n',b'entry = "bin/game.efi"\njacket = "jacket.ocj"\n')
    cases={
      "good":bundle(pe(b"\x31\xc0\xc3"),key),
      "installed":bundle(pe(b"\xc3"),key,MANIFEST.replace(b"bin/game.efi",long)),
      "asset":bundle(pe(b"\xc3"),key,bad_asset_manifest,{"jacket-ocj1":b"bad"}),
      "compatibility":bundle(pe(b"\xc3"),key,MANIFEST.replace(b"uefi.input.keyboard@1.0",b"vendor.future.device@9.9")),
    }
    with tempfile.TemporaryDirectory() as td:
      root=pathlib.Path(td);pub=root/"publisher.pub";pub.write_bytes(raw)
      for name,data in cases.items():
        path=root/f"{name}.ocg";path.write_bytes(data)
        left=normalized(host.analyze(data,trust));right=normalized(service(path,pub))
        assert left==right,(name,left,right)
    print("PASS: host and Convex analyzers have container/signature/manifest/asset/installed-tree/compatibility parity")
if __name__=="__main__":main()
