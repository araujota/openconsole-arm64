# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
  [switch]$Plan,
  [switch]$DryRun,
  [switch]$Arm,
  [string]$ExpectedToken,
  [string]$Entry,
  [string]$FixtureDirectory
)
$ErrorActionPreference = 'Stop'
$PlanSchema = 'openconsole.host-reboot-plan.v1'

function Fail([string]$Message) { throw "OpenConsole launcher: $Message" }
function Read-BoundedJson([string]$Path, [int]$Maximum) {
  $file = Get-Item -LiteralPath $Path -ErrorAction Stop
  if ($file.Length -lt 1 -or $file.Length -gt $Maximum) { Fail "invalid or oversized metadata: $Path" }
  return Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
}
function Get-BcdBlocks([string]$Text) {
  if ([Text.Encoding]::UTF8.GetByteCount($Text) -gt 1048576 -or $Text.Contains([char]0)) { Fail 'BCD output is oversized or binary' }
  $blocks = @(); $current = @{}
  foreach ($line in ($Text -split "`r?`n")) {
    if ($line.Length -gt 4096) { Fail 'BCD output line exceeds parser bound' }
    if ($line -match '^\s*identifier\s+(\{[0-9a-fA-F-]+\})\s*$') {
      if ($current.Count) { $blocks += [pscustomobject]$current }; $current = @{ Identifier = $Matches[1].ToLowerInvariant() }
    } elseif ($line -match '^\s*device\s+(.+?)\s*$') { $current.Device = $Matches[1].Trim() }
    elseif ($line -match '^\s*path\s+(.+?)\s*$') { $current.Path = $Matches[1].Trim() }
  }
  if ($current.Count) { $blocks += [pscustomobject]$current }
  return $blocks
}
function Get-LauncherState {
  $hostDirectory = $PSScriptRoot
  if ($FixtureDirectory) { $hostDirectory = Join-Path $FixtureDirectory 'OpenConsole\Host' }
  $hostItem = Get-Item -LiteralPath $hostDirectory
  if ($hostItem.Name -ne 'Host' -or $hostItem.Parent.Name -ne 'OpenConsole') { Fail 'launcher was copied away from OpenConsole\Host' }
  $bootRoot = $hostItem.Parent.Parent.FullName
  $manifest = Read-BoundedJson (Join-Path $hostDirectory 'manifest.json') 8192
  if ($manifest.schema -ne 'openconsole.host-launcher.v1' -or $manifest.version -ne 1) { Fail 'unsupported host launcher manifest' }
  $machine = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToLowerInvariant()
  if ($FixtureDirectory) { $machine = (Get-Content -Raw (Join-Path $FixtureDirectory 'machine.txt')).Trim().ToLowerInvariant() }
  if ($machine -eq 'x64') { $arch = 'x64'; $bootPath = '\EFI\BOOT\BOOTX64.EFI'; $other = 'EFI\BOOT\BOOTAA64.EFI' }
  elseif ($machine -eq 'arm64') { $arch = 'arm64'; $bootPath = '\EFI\BOOT\BOOTAA64.EFI'; $other = 'EFI\BOOT\BOOTX64.EFI' }
  else { Fail "unsupported host architecture: $machine" }
  if (-not (Test-Path -LiteralPath (Join-Path $bootRoot $bootPath.TrimStart('\'))) -or
      -not (Test-Path -LiteralPath (Join-Path $bootRoot 'EFI\OpenConsole\platform.ocp'))) { Fail 'matching EFI or platform inventory is missing' }
  if (Test-Path -LiteralPath (Join-Path $bootRoot $other)) { Fail 'media has ambiguous architecture boot files' }
  if ($FixtureDirectory) {
    $identity = Read-BoundedJson (Join-Path $FixtureDirectory 'identity.json') 8192
  } else {
    $drive = (Get-Item -LiteralPath $bootRoot).PSDrive.Name
    $partition = Get-Partition -DriveLetter $drive
    $disk = Get-Disk -Number $partition.DiskNumber
    if ($disk.BusType.ToString() -ne 'USB') { Fail 'launcher media is not USB storage' }
    $identity = [pscustomobject]@{ BootPartitionGuid = $partition.Guid.ToString(); InstallationId = $null; Device = $disk.Path; Serial = $disk.SerialNumber; Volume = "$drive`:" }
    $markers = @()
    foreach ($candidate in (Get-Partition -DiskNumber $partition.DiskNumber)) {
      foreach ($access in $candidate.AccessPaths) {
        if ($access -match '^[A-Za-z]:\\$') {
          $marker = Join-Path $access 'EFI\OpenConsole\data-volume.json'
          if (Test-Path -LiteralPath $marker) { $markers += $marker }
        }
      }
    }
    if ($markers.Count -ne 1) { Fail 'expected exactly one OpenConsole marker on this USB' }
    $markerValue = Read-BoundedJson $markers[0] 512
    if ($markerValue.schema -ne 'openconsole.data-volume.v1' -or $markerValue.installationId -notmatch '^[0-9a-f-]{36}$') { Fail 'invalid OpenConsole marker' }
    $identity.InstallationId = $markerValue.installationId.ToLowerInvariant()
  }
  $tokenText = "$($identity.InstallationId)`0$($identity.BootPartitionGuid.ToLowerInvariant())`0$($identity.Device)`0$($identity.Serial)"
  $hash = [Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($tokenText))
  $token = -join ($hash | ForEach-Object { $_.ToString('x2') })
  $bcd = if ($FixtureDirectory) { Get-Content -Raw (Join-Path $FixtureDirectory 'bcd.txt') } else { (& bcdedit.exe /enum firmware /v | Out-String) }
  $volume = $identity.Volume.ToLowerInvariant().TrimEnd('\')
  $matches = @(Get-BcdBlocks $bcd | Where-Object {
    $_.Path -and $_.Device -and $_.Path.Replace('/','\').ToLowerInvariant() -eq $bootPath.ToLowerInvariant() -and
    ($_.Device.ToLowerInvariant() -eq "partition=$volume" -or $_.Device.ToLowerInvariant().Contains($identity.BootPartitionGuid.ToLowerInvariant()))
  })
  $direct = $matches.Count -eq 1
  return [ordered]@{ schema=$PlanSchema; version=1; action=$(if($direct){'boot-next'}else{'firmware-settings'});
    directBootAvailable=$direct; entry=$(if($direct){$matches[0].Identifier}else{$null}); architecture=$arch;
    bootPath=$bootPath; bootPartitionGuid=$identity.BootPartitionGuid.ToLowerInvariant(); installationId=$identity.InstallationId;
    mediaToken=$token; device=$identity.Device; persistentBootOrderMutation=$false; armed=$false }
}
function Set-OneTimeBoot($state) {
  if (-not $state.directBootAvailable) { Fail 'no unique matching firmware entry is available' }
  $before = if ($FixtureDirectory) { '' } else { (& bcdedit.exe /enum '{fwbootmgr}' /v | Out-String) }
  & bcdedit.exe /set '{fwbootmgr}' bootsequence $state.entry | Out-Null
  if ($LASTEXITCODE -ne 0) { Fail 'firmware bootsequence command failed' }
  $after = (& bcdedit.exe /enum '{fwbootmgr}' /v | Out-String)
  if ($after.ToLowerInvariant() -notmatch [regex]::Escape($state.entry.ToLowerInvariant())) {
    & bcdedit.exe /deletevalue '{fwbootmgr}' bootsequence | Out-Null
    Fail 'firmware bootsequence readback failed; temporary state was removed'
  }
  $state.armed = $true
}

try {
  $state = Get-LauncherState
  if ($Plan -or $DryRun) { $state | ConvertTo-Json -Compress; exit 0 }
  if ($Arm) {
    $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { Fail 'administrator privileges are required' }
    if ($state.mediaToken -ne $ExpectedToken -or $state.entry -ne $Entry) { Fail 'USB identity changed after confirmation' }
    Set-OneTimeBoot $state; $state | ConvertTo-Json -Compress; exit 0
  }
  Add-Type -AssemblyName System.Windows.Forms
  $message = if ($state.directBootAvailable) { "OpenConsole was found on $($state.device). Prepare it for the next restart only?`n`nYour normal boot order will not change. Save your work first." } else { 'No exact OpenConsole firmware entry was found. Open firmware settings so you can select the USB manually? No boot settings will be changed.' }
  $caption = if ($state.directBootAvailable) { 'OpenConsole — next restart' } else { 'OpenConsole — firmware settings fallback' }
  if ([Windows.Forms.MessageBox]::Show($message,$caption,'YesNo','Information') -ne 'Yes') { exit 0 }
  if ($state.directBootAvailable) {
    $arguments = "-NoLogo -NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Arm -ExpectedToken $($state.mediaToken) -Entry $($state.entry)"
    $process = Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -Wait -PassThru
    if ($process.ExitCode -ne 0) { Fail 'administrator confirmation was cancelled or arming failed' }
  } else {
    if ([Windows.Forms.MessageBox]::Show('Restart into firmware settings now? Unsaved work may be lost.','OpenConsole — confirm restart','YesNo','Warning') -eq 'Yes') { shutdown.exe /r /fw /t 0 }
    exit 0
  }
  if ([Windows.Forms.MessageBox]::Show('OpenConsole is ready. Restart now? Unsaved work may be lost.','OpenConsole is ready','YesNo','Warning') -eq 'Yes') { shutdown.exe /r /t 0 }
} catch {
  if (-not ($Plan -or $DryRun -or $FixtureDirectory)) {
    Add-Type -AssemblyName System.Windows.Forms
    [Windows.Forms.MessageBox]::Show($_.Exception.Message,'OpenConsole launcher','OK','Error') | Out-Null
  } else { Write-Error $_ }
  exit 2
}
