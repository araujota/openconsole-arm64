#!/usr/bin/env bash
set -euo pipefail

edk_commit=0a503bc2f5bd37c7a5d39d0ba31008f446f0298d
repo_root=$(cd "$(dirname "$0")/.." && pwd)
source_dir=${OPENCONSOLE_EDK2_SOURCE:-"$repo_root/build/vendor-src/edk2-$edk_commit"}
output_dir=${OPENCONSOLE_OVMF_POINTER_OUTPUT:-"$repo_root/build/ovmf-pointer"}
patch_file="$repo_root/patches/edk2/0002-ovmf-usb-mouse-test.patch"

if ! command -v iasl >/dev/null 2>&1; then
    echo "iasl is required (Debian/Ubuntu package: acpica-tools)." >&2
    exit 2
fi
if [[ ! -d "$source_dir/.git" ]]; then
    echo "Pinned EDK II source is missing; run make edk2-network-stack first." >&2
    exit 2
fi
test "$(git -C "$source_dir" rev-parse HEAD)" = "$edk_commit"
git -C "$source_dir" sparse-checkout add \
    OvmfPkg UefiCpuPkg PcAtChipsetPkg FatPkg ShellPkg EmbeddedPkg \
    StandaloneMmPkg SourceLevelDebugPkg
git -C "$source_dir" submodule update --init --depth 1 -- \
    SecurityPkg/DeviceSecurity/SpdmLib/libspdm

if git -C "$source_dir" apply --ignore-space-change --reverse --check "$patch_file" 2>/dev/null; then
    : # already applied
else
    git -C "$source_dir" apply --ignore-space-change --check "$patch_file"
    git -C "$source_dir" apply --ignore-space-change "$patch_file"
fi

make -C "$source_dir/BaseTools" -j"${OPENCONSOLE_JOBS:-2}"
mkdir -p "$source_dir/Conf"
cd "$source_dir"
# shellcheck disable=SC1091
set +u
source edksetup.sh
set -u
# GCC 14 diagnoses control-flow patterns accepted by this pinned EDK II. Keep
# warnings visible while allowing the test firmware to remain reproducible.
if ! grep -q -- '-Wno-error=maybe-uninitialized' Conf/tools_def.txt; then
    sed -i '/DEFINE GCCNOLTO_X64_CC_FLAGS/s/ -fno-omit-frame-pointer/ -Wno-error=maybe-uninitialized -fno-omit-frame-pointer/' Conf/tools_def.txt
fi
export SOURCE_DATE_EPOCH
SOURCE_DATE_EPOCH=$(git show -s --format=%ct "$edk_commit")
build -a X64 -t GCCNOLTO -b RELEASE -p OvmfPkg/OvmfPkgX64.dsc \
    -D FD_SIZE_4MB -n "${OPENCONSOLE_JOBS:-2}"

mkdir -p "$output_dir"
install -m 0644 Build/OvmfX64/RELEASE_GCCNOLTO/FV/OVMF_CODE.fd "$output_dir/OVMF_CODE.fd"
install -m 0644 Build/OvmfX64/RELEASE_GCCNOLTO/FV/OVMF_VARS.fd "$output_dir/OVMF_VARS.fd"
printf '%s\n' "$edk_commit" > "$output_dir/EDK2_COMMIT"
sha256sum "$output_dir/OVMF_CODE.fd" "$output_dir/OVMF_VARS.fd" > "$output_dir/SHA256SUMS"
echo "Packaged USB-pointer test OVMF in $output_dir"
