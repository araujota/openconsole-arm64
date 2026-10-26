#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "$0")/ovmf-common.sh"

vars="$project_root/build/OVMF_VARS.run.fd"
prepare_vars "$vars"

exec qemu-system-x86_64 \
    -machine q35,accel="${OPENCONSOLE_QEMU_ACCEL:-kvm:tcg}" \
    -cpu max \
    -m 512M \
    -no-reboot \
    -drive "if=pflash,format=raw,unit=0,readonly=on,file=$ovmf_code" \
    -drive "if=pflash,format=raw,unit=1,file=$vars" \
    -drive "format=raw,file=fat:rw:$project_root/build/esp" \
    -debugcon "file:$project_root/build/ovmf-run.log" \
    -global isa-debugcon.iobase=0x402

