#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "$0")/ovmf-common.sh"

for tool in qemu-system-x86_64 virt-fw-vars; do
    command -v "$tool" >/dev/null || { echo "missing required tool: $tool" >&2; exit 2; }
done
shell_efi=/usr/share/efi-shell-x64/shellx64.efi
[[ -r "$shell_efi" ]] || { echo "missing EDK II shell: $shell_efi" >&2; exit 2; }

temporary=$(mktemp -d -t openconsole-bootnext-XXXXXX)
trap 'rm -rf -- "$temporary"' EXIT
shell_root="$temporary/shell"
openconsole_root="$temporary/openconsole"
normal_root="$temporary/normal"
mkdir -p "$shell_root/EFI/BOOT" "$openconsole_root/EFI/BOOT" "$normal_root/EFI/BOOT"
cp "$shell_efi" "$shell_root/EFI/BOOT/BOOTX64.EFI"
cp "$project_root/tests/fixtures/bootnext/startup.nsh" "$shell_root/startup.nsh"
cp "$project_root/build/BOOTNEXT-ARMER.EFI" "$shell_root/arm-next.efi"
cp "$project_root/build/BOOTX64-BOOTNEXT-TEST.EFI" "$openconsole_root/EFI/BOOT/BOOTX64.EFI"
cp "$project_root/build/NORMAL-BOOT-SENTINEL.EFI" "$normal_root/EFI/BOOT/BOOTX64.EFI"
vars="$temporary/OVMF_VARS.fd"
cp "$ovmf_vars_template" "$vars"

qemu=(qemu-system-x86_64 -machine q35,accel=tcg -m 256 -nodefaults
      -device qemu-xhci -device usb-kbd -device virtio-gpu-pci
      -drive "if=pflash,format=raw,readonly=on,file=$ovmf_code"
      -drive "if=pflash,format=raw,file=$vars"
      -drive "format=raw,file=fat:rw:$shell_root"
      -drive "format=raw,file=fat:rw:$openconsole_root"
      -drive "format=raw,file=fat:rw:$normal_root"
      -global isa-debugcon.iobase=0x402
      -device isa-debug-exit,iobase=0xf4,iosize=0x04 -display none)

set +e
timeout 40 "${qemu[@]}" -debugcon "file:$temporary/first.log" -serial "file:$temporary/first.serial"
first_status=$?
set -e
[[ $first_status -eq 33 ]] || { echo "first OVMF cycle exited $first_status" >&2; exit 1; }
grep -q 'BN' "$temporary/first.log"
[[ $(grep -c 'OPENCONSOLE_BOOTNEXT_PARENT_REACHED' "$temporary/first.log") -eq 1 ]]
! grep -q 'OPENCONSOLE_BOOTNEXT_NORMAL_REACHED' "$temporary/first.log"
grep -q 'Boot0006 "OpenConsole acceptance"' "$temporary/first.serial"
first_order=$(virt-fw-vars -i "$vars" -p 2>&1 | grep '^BootOrder')
[[ $first_order == *'0005, 0006'* ]]
! virt-fw-vars -i "$vars" -p 2>&1 | grep -q '^BootNext'

set +e
timeout 20 "${qemu[@]}" -debugcon "file:$temporary/second.log" -serial "file:$temporary/second.serial"
second_status=$?
set -e
[[ $second_status -eq 35 ]] || { echo "second OVMF cycle exited $second_status" >&2; exit 1; }
[[ $(grep -c 'OPENCONSOLE_BOOTNEXT_NORMAL_REACHED' "$temporary/second.log") -eq 1 ]]
! grep -q 'OPENCONSOLE_BOOTNEXT_PARENT_REACHED' "$temporary/second.log"
grep -q 'Boot0005 "Normal host"' "$temporary/second.serial"
second_order=$(virt-fw-vars -i "$vars" -p 2>&1 | grep '^BootOrder')
[[ "$first_order" == "$second_order" ]]
! virt-fw-vars -i "$vars" -p 2>&1 | grep -q '^BootNext'

mkdir -p "$project_root/build"
cp "$temporary/first.log" "$project_root/build/ovmf-bootnext-openconsole.log"
cp "$temporary/second.log" "$project_root/build/ovmf-bootnext-normal.log"
printf '%s\n' "$second_order" > "$project_root/build/ovmf-bootnext-order.txt"
echo "PASS: OVMF consumed BootNext exactly once and preserved $second_order"
