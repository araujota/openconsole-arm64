#!/usr/bin/env python3
"""Stage the versioned, non-firmware host launchers into a payload."""
from __future__ import annotations
import argparse, hashlib, json, pathlib, shutil
ROOT = pathlib.Path(__file__).resolve().parents[1]
FILES = ("manifest.json", "openconsole_host.py", "Open OpenConsole on next reboot.sh",
         "Open OpenConsole on next reboot.ps1", "Open OpenConsole on next reboot.cmd", "README.txt")
def stage(destination: pathlib.Path) -> dict:
    source=ROOT/"host-tools"; target=destination/"OpenConsole"/"Host"; target.mkdir(parents=True,exist_ok=True)
    inventory=[]
    for name in FILES:
        data=(source/name).read_bytes(); out=target/name; out.write_bytes(data)
        if data.startswith(b"#!"): out.chmod(0o755)
        inventory.append({"path":name,"bytes":len(data),"sha256":hashlib.sha256(data).hexdigest()})
    return {"schema":"openconsole.host-tools-stage.v1","files":inventory}
if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("destination",type=pathlib.Path);a=p.parse_args();print(json.dumps(stage(a.destination),sort_keys=True))
