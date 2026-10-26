#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
esp="$project_root/build/esp"
image="$project_root/build/openconsole-physical-test.img"
receipt="$project_root/build/physical-image-preflight.json"
loop_device=""
mount_dir=""

cleanup() {
    if [[ -n "$mount_dir" ]] && mountpoint -q "$mount_dir"; then
        umount "$mount_dir"
    fi
    if [[ -n "$loop_device" ]]; then
        losetup -d "$loop_device" 2>/dev/null || true
    fi
    if [[ -n "$mount_dir" && -d "$mount_dir" ]]; then
        rmdir "$mount_dir" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

[[ $EUID -eq 0 ]] || {
    echo "FAIL: disk-image construction requires root for a private loop mount" >&2
    exit 1
}
[[ -f "$esp/EFI/BOOT/BOOTX64.EFI" ]] || {
    echo "FAIL: physical ESP is absent; run make physical-test-package first" >&2
    exit 1
}
for tool in sgdisk losetup mkfs.vfat mount umount mountpoint; do
    command -v "$tool" >/dev/null || {
        echo "FAIL: required tool is absent: $tool" >&2
        exit 1
    }
done

# Own one fixed regular-file path. Never accept a caller-supplied device and
# never replace a block device or symlink.
if [[ -e "$image" && ! -f "$image" ]] || [[ -L "$image" ]]; then
    echo "FAIL: refusing non-regular output path: $image" >&2
    exit 1
fi
rm -f -- "$image"
truncate -s 64M "$image"
sgdisk --clear \
    --new=1:2048:129023 \
    --typecode=1:ef00 \
    --change-name=1:OPENCONSOLE \
    "$image" >/dev/null

loop_device="$(losetup --find --show --partscan "$image")"
partition="${loop_device}p1"
for _ in {1..50}; do
    [[ -b "$partition" ]] && break
    sleep 0.1
done
[[ -b "$partition" ]] || {
    echo "FAIL: GPT partition node did not appear: $partition" >&2
    exit 1
}

mkfs.vfat -F 32 -n OPENCONSOLE "$partition" >/dev/null
mount_dir="$(mktemp -d -t openconsole-physical-image.XXXXXX)"
mount "$partition" "$mount_dir"
cp -a "$esp/." "$mount_dir/"
sync -f "$mount_dir"
umount "$mount_dir"

# Validate the actual FAT contents, not merely the source tree.
mount -o ro "$partition" "$mount_dir"
"$project_root/scripts/verify-physical-test-media.py" "$mount_dir" \
    --output "$receipt" >/dev/null
umount "$mount_dir"
losetup -d "$loop_device"
loop_device=""
rmdir "$mount_dir"
mount_dir=""

echo "Bootable physical-test disk image created at $image"
sha256sum "$image"
echo "Verified-media receipt: $receipt"
