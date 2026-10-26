#!/usr/bin/env python3
import pathlib
ROOT=pathlib.Path(__file__).resolve().parents[1]
text=(ROOT/"host-tools/Open OpenConsole on next reboot.ps1").read_text()
required=("System.Windows.Forms","-Verb RunAs","{fwbootmgr}","bootsequence","/deletevalue",
          "shutdown.exe /r /fw /t 0","persistentBootOrderMutation=$false","$FixtureDirectory",
          "mediaToken","OpenConsole — firmware settings fallback")
for token in required: assert token in text,token
assert "displayorder" not in text.lower()
assert text.index("$state = Get-LauncherState") < text.index("Set-OneTimeBoot $state")
cmd=(ROOT/"host-tools/Open OpenConsole on next reboot.cmd").read_text()
assert "-ExecutionPolicy Bypass" in cmd and "%~dp0" in cmd
print("PASS: Windows native panel and one-time boot contract")
