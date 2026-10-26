@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Open OpenConsole on next reboot.ps1"
if errorlevel 1 pause
