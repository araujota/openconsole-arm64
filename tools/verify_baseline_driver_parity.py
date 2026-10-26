#!/usr/bin/env python3
from __future__ import annotations
import argparse,json,pathlib,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from verify_efi import inspect,VerificationError

DIRECTORIES={"input":"Input","network":"Network","audio":"Audio","crypto":"Crypto"}

def verify(root:pathlib.Path,arch:str,contract:dict)->dict:
    found={}
    for role,directory in DIRECTORIES.items():
        base=root/"EFI/OpenConsole/Drivers"/directory
        names=sorted(path.name for path in base.glob("*.efi")) if base.is_dir() else []
        expected=sorted(contract["roles"][role])
        if names!=expected: raise ValueError(f"{arch} {role} drivers {names} != {expected}")
        found[role]=[]
        for name in names: found[role].append(inspect(base/name,arch,"driver"))
    return {"architecture":arch,"roles":found}

def main()->int:
    p=argparse.ArgumentParser();p.add_argument("--x64",type=pathlib.Path);p.add_argument("--arm64",type=pathlib.Path)
    p.add_argument("--contract",type=pathlib.Path,default=ROOT/"config/baseline-driver-roles.json")
    a=p.parse_args(); contract=json.loads(a.contract.read_text()); reports=[]
    if a.x64: reports.append(verify(a.x64,"x64",contract))
    if a.arm64: reports.append(verify(a.arm64,"arm64",contract))
    if not reports: p.error("at least one architecture tree is required")
    print(json.dumps({"schema":"openconsole.driver-parity-report.v1","reports":reports},sort_keys=True));return 0
if __name__=="__main__":
    try: raise SystemExit(main())
    except (OSError,ValueError,KeyError,VerificationError) as error:
        print(f"verify-driver-parity: {error}",file=sys.stderr);raise SystemExit(2)
