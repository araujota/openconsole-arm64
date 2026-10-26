# SPDX-License-Identifier: Apache-2.0
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not (Test-Path (Join-Path $Root 'scripts\install-openconsole-windows.ps1'))) { $Root = Split-Path -Parent $PSScriptRoot }
$Installer = Join-Path $Root 'scripts\install-openconsole-windows.ps1'
$Formatter = Join-Path $Root 'oc-fat32-format.exe'
$Temp = Join-Path ([IO.Path]::GetTempPath()) ('openconsole-vhd-' + [guid]::NewGuid())
New-Item -ItemType Directory $Temp | Out-Null

function New-Payload {
  $payload = Join-Path $Temp 'payload'
  New-Item -ItemType Directory -Force (Join-Path $payload 'EFI\BOOT') | Out-Null
  New-Item -ItemType Directory -Force (Join-Path $payload 'EFI\OpenConsole') | Out-Null
  [IO.File]::WriteAllBytes((Join-Path $payload 'EFI\BOOT\BOOTX64.EFI'),[byte[]](1,2,3,4))
  [IO.File]::WriteAllBytes((Join-Path $payload 'EFI\OpenConsole\platform.ocp'),[byte[]](5,6,7,8))
  return $payload
}
function New-Fixture([string]$Name,[uint64]$Bytes) {
  $path = Join-Path $Temp "$Name.vhdx"
  New-VHD -Path $path -Dynamic -SizeBytes $Bytes | Out-Null
  Mount-VHD -Path $path -NoDriveLetter
  $disk = Get-DiskImage -ImagePath $path | Get-Disk
  if (@($disk).Count -ne 1) { throw "Could not uniquely resolve $path" }
  return [pscustomobject]@{Path=$path;Disk=$disk}
}
function Invoke-Install($Fixture,[string]$Report,[string]$Fault='None') {
  $preview = & $Installer -DiskNumber $Fixture.Disk.Number -TestVirtualDisk | ConvertFrom-Json
  & $Installer -DiskNumber $Fixture.Disk.Number -TestVirtualDisk -Erase `
    -Confirm $preview.requiredConfirmation -Payload $script:Payload -Architecture x64 `
    -Fat32Formatter $Formatter -Report $Report -TestFault $Fault
}

$mounted = @()
try {
  $script:Payload = New-Payload
  foreach ($case in @(@('small',256MB),@('large-2tb',2TB))) {
    $fixture = New-Fixture $case[0] $case[1]; $mounted += $fixture
    $report = Join-Path $Temp ($case[0] + '.json')
    Invoke-Install $fixture $report
    $result = Get-Content -Raw $report | ConvertFrom-Json
    if ($result.status -ne 'verified' -or [uint64]$result.layout.disk_bytes -ne [uint64]$case[1]) { throw 'VHD layout/report mismatch' }
    if ($case[0] -eq 'large-2tb' -and $result.layout.mode -ne 'split') { throw '2 TB fixture did not use split layout' }
  }

  $fixture = New-Fixture 'interruption' 256MB; $mounted += $fixture
  $report = Join-Path $Temp 'interruption.json'
  try { Invoke-Install $fixture $report 'AfterPartition'; throw 'Injected interruption unexpectedly succeeded' } catch {
    if (Test-Path $report) { throw 'Interrupted install emitted a verified report' }
  }

  $fixture = New-Fixture 'verification' 256MB; $mounted += $fixture
  $report = Join-Path $Temp 'verification.json'
  try { Invoke-Install $fixture $report 'CorruptAfterCopy'; throw 'Corrupt install unexpectedly succeeded' } catch {
    if (Test-Path $report) { throw 'Failed verification emitted a verified report' }
  }

  $fixture = New-Fixture 'safety' 256MB; $mounted += $fixture
  $preview = & $Installer -DiskNumber $fixture.Disk.Number -TestVirtualDisk | ConvertFrom-Json
  try {
    & $Installer -DiskNumber $fixture.Disk.Number -TestVirtualDisk -Erase -Confirm stale `
      -Payload $script:Payload -Fat32Formatter $Formatter -Report (Join-Path $Temp 'stale.json')
    throw 'Stale identity confirmation unexpectedly succeeded'
  } catch {
    if ($_.Exception.Message -notmatch 'Confirm') { throw }
  }
  Write-Output 'PASS: Windows VHD/VHDX layout, safety, 2 TB, interruption, and verification matrix'
} finally {
  foreach ($fixture in $mounted) { Dismount-VHD -Path $fixture.Path -ErrorAction SilentlyContinue }
  Remove-Item -LiteralPath $Temp -Recurse -Force -ErrorAction SilentlyContinue
}
