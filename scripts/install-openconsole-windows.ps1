# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
  [switch]$List,
  [int]$DiskNumber = -1,
  [ValidateSet('x64','arm64')][string]$Architecture = 'x64',
  [string]$Payload,
  [switch]$Erase,
  [string]$Confirm,
  [string]$InstallationId = [guid]::NewGuid().ToString().ToLowerInvariant(),
  [string]$Fat32Formatter = "$PSScriptRoot\..\build\host-tools\oc-fat32-format.exe",
  [string]$Report = ".\openconsole-install-report.json",
  [switch]$TestVirtualDisk,
  [ValidateSet('None','AfterPartition','CorruptAfterCopy')][string]$TestFault = 'None'
)
$ErrorActionPreference = 'Stop'
$EspBytes = 1GB
$SplitThreshold = 8GB
$EspType = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$DataType = '{0fc63daf-8483-4772-8e79-3d69d8477de4}'

function Get-IdentityToken($Disk) {
  $stable = [ordered]@{number=$Disk.Number; uniqueId=$Disk.UniqueId; serial=$Disk.SerialNumber;
    friendlyName=$Disk.FriendlyName; busType=[string]$Disk.BusType; bytes=[uint64]$Disk.Size;
    logicalSectorBytes=[uint32]$Disk.LogicalSectorSize; system=[bool]($Disk.IsSystem -or $Disk.IsBoot)}
  $json = $stable | ConvertTo-Json -Compress
  $sha = [Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($json))
  return (-join ($sha[0..11] | ForEach-Object { $_.ToString('x2') }))
}
function Get-DeviceRecord($Disk) {
  [ordered]@{number=$Disk.Number; uniqueId=$Disk.UniqueId; serial=$Disk.SerialNumber;
    model=$Disk.FriendlyName; transport=[string]$Disk.BusType; bytes=[uint64]$Disk.Size;
    logicalSectorBytes=[uint32]$Disk.LogicalSectorSize; systemDisk=[bool]($Disk.IsSystem -or $Disk.IsBoot);
    identityToken=(Get-IdentityToken $Disk)}
}
function Get-TreeDigest([string]$Root) {
  $sha = [Security.Cryptography.SHA256]::Create()
  $stream = [IO.MemoryStream]::new()
  Get-ChildItem -LiteralPath $Root -File -Recurse | Sort-Object FullName | ForEach-Object {
    $relative = [IO.Path]::GetRelativePath($Root,$_.FullName).Replace('\','/')
    $name = [Text.Encoding]::UTF8.GetBytes($relative)
    $stream.Write([BitConverter]::GetBytes([uint32]$name.Length)); $stream.Write($name)
    $bytes = [IO.File]::ReadAllBytes($_.FullName)
    $stream.Write([BitConverter]::GetBytes([uint64]$bytes.Length)); $stream.Write($bytes)
  }
  return -join ($sha.ComputeHash($stream.ToArray()) | ForEach-Object {$_.ToString('x2')})
}
if ($List) {
  @(Get-Disk | ForEach-Object { Get-DeviceRecord $_ }) | ConvertTo-Json -Depth 5
  exit 0
}
if ($DiskNumber -lt 0) { throw 'DiskNumber is required unless -List is used' }
$disk = Get-Disk -Number $DiskNumber
$device = Get-DeviceRecord $disk
if ($device.systemDisk) { throw 'Refusing the active Windows system/boot disk' }
if (-not $TestVirtualDisk -and $disk.BusType -ne 'USB') { throw 'Target is not a USB disk' }
if ($TestVirtualDisk -and $disk.BusType -notin @('File Backed Virtual','Virtual')) { throw 'TestVirtualDisk requires a VHD/VHDX disk' }
if ($disk.IsReadOnly) { throw 'Target disk is read-only' }
if ((Get-Partition -DiskNumber $DiskNumber -ErrorAction SilentlyContinue | Get-Volume -ErrorAction SilentlyContinue | Where-Object DriveLetter)) {
  throw 'Refusing a disk with mounted volumes; remove drive letters first'
}
$split = $disk.Size -gt $SplitThreshold
$sector = [uint64]$disk.LogicalSectorSize
$first = [uint64]1MB
$usableEnd = ([math]::Floor(([uint64]$disk.Size - 33*$sector) / 1MB) * 1MB) - $sector
if ($split) {
  $dataStart = $first + [uint64]$EspBytes
  $partitions = @(
    [ordered]@{number=1;role='boot';start_lba=$first/$sector;end_lba=($dataStart-$sector)/$sector;bytes=[uint64]$EspBytes;type_guid=$EspType.Trim('{}');label='OC_BOOT'},
    [ordered]@{number=2;role='data';start_lba=$dataStart/$sector;end_lba=$usableEnd/$sector;bytes=$usableEnd-$dataStart+$sector;type_guid=$DataType.Trim('{}');label='OC_DATA'})
} else {
  $partitions = @([ordered]@{number=1;role='boot-data';start_lba=$first/$sector;end_lba=$usableEnd/$sector;bytes=$usableEnd-$first+$sector;type_guid=$EspType.Trim('{}');label='OC_BOOT'})
}
$layout = [ordered]@{schema='openconsole.media-layout.v1'; mode=($(if($split){'split'}else{'single'}));
  disk_bytes=[uint64]$disk.Size; sector_bytes=[uint32]$disk.LogicalSectorSize; alignment_bytes=1MB;
  partitions=$partitions;expected_unallocated_bytes=([uint64]$disk.Size-[uint64](($partitions|Measure-Object -Property bytes -Sum).Sum));
  fat32_max_file_bytes=4294967295}
$preview = [ordered]@{device=$device; layout=$layout; requiredConfirmation=$device.identityToken}
if (-not $Erase) { $preview | ConvertTo-Json -Depth 8; exit 0 }
if ($Confirm -ne $device.identityToken) { throw 'Confirm must equal the preview identity token' }
if (-not (Test-Path -LiteralPath $Payload -PathType Container)) { throw 'Payload must be an extracted OpenConsole tree' }
$payloadFiles = @(Get-ChildItem -LiteralPath $Payload -File -Recurse)
if ($payloadFiles | Where-Object Length -gt 4294967295) { throw 'Payload contains a file exceeding the FAT32 individual-file limit' }
$payloadBytes = [uint64](($payloadFiles | Measure-Object Length -Sum).Sum)
if ($payloadBytes -gt ([uint64]$partitions[0].bytes - 8MB)) { throw 'Payload cannot fit in the boot filesystem' }
$sourceDigest = Get-TreeDigest $Payload
$bootName = if($Architecture -eq 'x64'){'BOOTX64.EFI'}else{'BOOTAA64.EFI'}
if (-not (Test-Path -LiteralPath (Join-Path $Payload "EFI\BOOT\$bootName") -PathType Leaf)) { throw "Payload lacks $bootName" }
if ($split -and -not (Test-Path -LiteralPath $Fat32Formatter -PathType Leaf)) { throw 'Pinned oc-fat32-format.exe is required for a large data volume' }
$current = Get-Disk -Number $DiskNumber
if ((Get-IdentityToken $current) -ne $device.identityToken) { throw 'Disk identity changed after confirmation' }
if ($TestFault -ne 'None' -and -not $TestVirtualDisk) { throw 'TestFault is restricted to TestVirtualDisk fixtures' }

Clear-Disk -Number $DiskNumber -RemoveData -RemoveOEM -Confirm:$false
Initialize-Disk -Number $DiskNumber -PartitionStyle GPT
if ($split) {
  $boot = New-Partition -DiskNumber $DiskNumber -Offset $first -Size $partitions[0].bytes -GptType $EspType -AssignDriveLetter
  $data = New-Partition -DiskNumber $DiskNumber -Offset $dataStart -Size $partitions[1].bytes -GptType $DataType -AssignDriveLetter
} else {
  $boot = New-Partition -DiskNumber $DiskNumber -Offset $first -Size $partitions[0].bytes -GptType $EspType -AssignDriveLetter
  $data = $boot
}
if ($TestFault -eq 'AfterPartition') { throw 'Injected interruption after partition creation' }
Format-Volume -Partition $boot -FileSystem FAT32 -NewFileSystemLabel 'OC_BOOT' -Confirm:$false -Force | Out-Null
if ($split) {
  $raw = "\\.\$($data.DriveLetter):"
  & $Fat32Formatter $raw 'OC_DATA' $disk.LogicalSectorSize
  if ($LASTEXITCODE -ne 0) { throw 'Large FAT32 formatter failed' }
  Update-HostStorageCache
}
$bootRoot = "$($boot.DriveLetter):\"
$dataRoot = "$($data.DriveLetter):\"
Get-ChildItem -LiteralPath $Payload -Force | Copy-Item -Destination $bootRoot -Recurse -Force
if ($TestFault -eq 'CorruptAfterCopy') {
  [IO.File]::AppendAllText((Join-Path $bootRoot "EFI\BOOT\$bootName"),'corrupt',[Text.Encoding]::ASCII)
}
$markerDirectory = Join-Path $dataRoot 'EFI\OpenConsole'
New-Item -ItemType Directory -Force -Path $markerDirectory | Out-Null
$marker = '{"installationId":"' + ([guid]$InstallationId).ToString().ToLowerInvariant() + '","schema":"openconsole.data-volume.v1"}' + "`n"
[IO.File]::WriteAllText((Join-Path $markerDirectory 'data-volume.json'),$marker,[Text.Encoding]::ASCII)
@('State\Games','State\Launch','State\Recovery','Logs','Notices') | ForEach-Object {
  New-Item -ItemType Directory -Force -Path (Join-Path $markerDirectory $_) | Out-Null
}
if (-not (Test-Path -LiteralPath (Join-Path $bootRoot "EFI\BOOT\$bootName"))) { throw 'Default boot file verification failed' }
foreach ($source in $payloadFiles) {
  $relative = [IO.Path]::GetRelativePath($Payload,$source.FullName)
  $copied = Join-Path $bootRoot $relative
  if (-not (Test-Path -LiteralPath $copied -PathType Leaf) -or
      (Get-FileHash -LiteralPath $copied -Algorithm SHA256).Hash -ne
      (Get-FileHash -LiteralPath $source.FullName -Algorithm SHA256).Hash) {
    throw "Payload byte verification failed: $relative"
  }
}
$verified = [IO.File]::ReadAllText((Join-Path $markerDirectory 'data-volume.json'),[Text.Encoding]::ASCII)
if ($verified -ne $marker) { throw 'Data marker verification failed' }
$result = [ordered]@{schema='openconsole.usb-install-report.v1';status='verified';architecture=$Architecture;
  sourcePayloadSha256=$sourceDigest;device=$device;installationId=([guid]$InstallationId).ToString().ToLowerInvariant();layout=$layout;
  bootFilename=$bootName;bootPartitionGuid=$boot.Guid;dataPartitionGuid=$data.Guid}
$result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $Report -Encoding utf8NoBOM
Write-Output (Resolve-Path $Report)
