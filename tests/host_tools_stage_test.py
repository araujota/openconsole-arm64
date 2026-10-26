#!/usr/bin/env python3
import pathlib,subprocess,sys,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="oc-stage-") as td:
    root=pathlib.Path(td);subprocess.run([sys.executable,"scripts/stage-host-tools.py",str(root)],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
    host=root/"OpenConsole/Host";assert (host/"manifest.json").is_file();assert (host/"Open OpenConsole on next reboot.cmd").is_file()
    assert (host/"Open OpenConsole on next reboot.sh").stat().st_mode & 0o111
print("PASS: host launchers stage outside the EFI execution tree")
