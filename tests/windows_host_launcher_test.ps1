$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$fixture = Join-Path ([IO.Path]::GetTempPath()) ("openconsole-host-" + [guid]::NewGuid())
try {
  New-Item -ItemType Directory -Force (Join-Path $fixture 'OpenConsole\Host'), (Join-Path $fixture 'EFI\BOOT'), (Join-Path $fixture 'EFI\OpenConsole') | Out-Null
  Copy-Item (Join-Path $root 'host-tools\manifest.json') (Join-Path $fixture 'OpenConsole\Host\manifest.json')
  Set-Content -NoNewline (Join-Path $fixture 'EFI\BOOT\BOOTX64.EFI') 'efi'
  Set-Content -NoNewline (Join-Path $fixture 'EFI\OpenConsole\platform.ocp') 'ocp'
  Set-Content -NoNewline (Join-Path $fixture 'machine.txt') 'x64'
  $identity = @{ BootPartitionGuid='aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee'; InstallationId='12345678-1234-4234-9234-123456789abc'; Device='fixture-usb'; Serial='fixture'; Volume='X:' }
  $identity | ConvertTo-Json -Compress | Set-Content -NoNewline (Join-Path $fixture 'identity.json')
  $entry = '{11111111-2222-4333-8444-555555555555}'
  @"
Firmware Application
--------------------
identifier              $entry
device                  partition=X:
path                    \EFI\BOOT\BOOTX64.EFI
description             Irrelevant localized text
"@ | Set-Content (Join-Path $fixture 'bcd.txt')
  $script = Join-Path $root 'host-tools\Open OpenConsole on next reboot.ps1'
  $plan = (& $script -Plan -FixtureDirectory $fixture) | ConvertFrom-Json
  if (-not $plan.directBootAvailable -or $plan.entry -ne $entry -or $plan.persistentBootOrderMutation) { throw 'exact fixture did not produce safe direct plan' }
  Add-Content (Join-Path $fixture 'bcd.txt') "`nidentifier $entry`ndevice partition=X:`npath \EFI\BOOT\BOOTX64.EFI`n"
  $ambiguous = (& $script -DryRun -FixtureDirectory $fixture) | ConvertFrom-Json
  if ($ambiguous.directBootAvailable -or $ambiguous.action -ne 'firmware-settings') { throw 'ambiguous fixture was not rejected' }
  if (Get-Process shutdown -ErrorAction SilentlyContinue) { throw 'dry run unexpectedly invoked shutdown' }
  Write-Host 'PASS: Windows BCD fixture matching and zero-mutation dry run'
} finally {
  Remove-Item -Recurse -Force $fixture -ErrorAction SilentlyContinue
}
