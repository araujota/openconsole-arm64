#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "$0")/ovmf-common.sh"

vars="$project_root/build/OVMF_VARS.test.fd"
gpu_mode="${OPENCONSOLE_TEST_GPU:-ati-probe}"
scene="${OPENCONSOLE_TEST_SCENE:-lifecycle}"
esp_name="${OPENCONSOLE_TEST_ESP:-test-esp}"
if [[ "$scene" == "gles" ]]; then
    log="$project_root/build/ovmf-test-gles-software.log"
    screen="$project_root/build/openconsole-gles-software.ppm"
elif [[ "$scene" == "io" ]]; then
    log="$project_root/build/ovmf-test-io.log"
    screen="$project_root/build/openconsole-io.ppm"
elif [[ "$scene" == "dashboard" ]]; then
    log="$project_root/build/ovmf-test-dashboard.log"
    screen="$project_root/build/openconsole-dashboard.ppm"
elif [[ "$scene" == "pointer-pagination" ]]; then
    log="$project_root/build/ovmf-test-pointer-pagination.log"
    screen="$project_root/build/openconsole-pointer-page-one.ppm"
elif [[ "$scene" == "game-details" ]]; then
    log="$project_root/build/ovmf-test-game-details.log"
    screen="$project_root/build/openconsole-details-library.ppm"
elif [[ "$scene" == "recovery" ]]; then
    log="$project_root/build/ovmf-test-recovery.log"
    screen="$project_root/build/openconsole-recovery.ppm"
elif [[ "$scene" == "storage-recovery" ]]; then
    case_name="${OPENCONSOLE_STORAGE_CASE:?OPENCONSOLE_STORAGE_CASE is required}"
    log="$project_root/build/ovmf-storage-$case_name.log"
    screen="$project_root/build/openconsole-storage-$case_name.ppm"
elif [[ "$scene" == "mutation-after-discovery" || "$scene" == "unsupported-watchdog" || "$scene" == "corrupt-driver" || "$scene" == "hang-reset" ]]; then
    log="$project_root/build/ovmf-test-$scene.log"
    screen="$project_root/build/openconsole-$scene.ppm"
elif [[ "$scene" == "catalog-acceptance" ]]; then
    log="$project_root/build/ovmf-test-catalog-acceptance.log"
    screen="$project_root/build/openconsole-catalog-acceptance.ppm"
elif [[ "$scene" == "audio" ]]; then
    log="$project_root/build/ovmf-test-audio.log"
    screen="$project_root/build/openconsole-audio.ppm"
elif [[ "$scene" == "audio-no-output" ]]; then
    log="$project_root/build/ovmf-test-audio-no-output.log"
    screen="$project_root/build/openconsole-audio-no-output.ppm"
elif [[ "$scene" == "physical-inventory" ]]; then
    log="$project_root/build/ovmf-test-physical-inventory.log"
    screen="$project_root/build/openconsole-physical-inventory.ppm"
elif [[ "$scene" == "integrated" ]]; then
    log="$project_root/build/ovmf-test-integrated.log"
    screen="$project_root/build/openconsole-integrated.ppm"
elif [[ "$scene" == "slice" ]]; then
    log="$project_root/build/ovmf-test-slice.log"
    screen="$project_root/build/openconsole-slice-before-download.ppm"
elif [[ "$scene" == "crypto" || "$scene" == "update-storage" || "$scene" == "update-e2e" ]]; then
    log="$project_root/build/ovmf-test-$scene.log"
    screen="$project_root/build/openconsole-$scene.ppm"
elif [[ "$scene" == "network" || "$scene" == "network-fixture" || "$scene" == "physical-network" || "$scene" == "https" || "$scene" == "offline" ]]; then
    log="$project_root/build/ovmf-test-$scene.log"
    screen="$project_root/build/openconsole-$scene.ppm"
elif [[ "$gpu_mode" == "virtio-gl" ]]; then
    log="$project_root/build/ovmf-test-virtio-gl.log"
    screen="$project_root/build/openconsole-home-virtio-gl.ppm"
else
    log="$project_root/build/ovmf-test.log"
    screen="$project_root/build/openconsole-home.ppm"
fi
prepare_vars "$vars"

exec python3 "$project_root/scripts/ovmf_interaction_test.py" \
    qemu-system-x86_64 "$ovmf_code" "$vars" \
    "$project_root/build/$esp_name" "$log" "$screen"
