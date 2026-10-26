#!/usr/bin/env bash
set -euo pipefail

edk_commit=0a503bc2f5bd37c7a5d39d0ba31008f446f0298d
repo_root=$(cd "$(dirname "$0")/.." && pwd)
source_dir=${OPENCONSOLE_EDK2_SOURCE:-"$repo_root/build/vendor-src/edk2-$edk_commit"}
output_dir=${OPENCONSOLE_INPUT_OUTPUT:-"$repo_root/build/edk2-input-driver"}
if [[ ! -d "$source_dir/.git" ]]; then
    echo "Pinned EDK II source is missing; run make edk2-network-stack first." >&2
    exit 2
fi
test "$(git -C "$source_dir" rev-parse HEAD)" = "$edk_commit"
git -C "$source_dir" sparse-checkout add MdeModulePkg

make -C "$source_dir/BaseTools" -j"${OPENCONSOLE_JOBS:-2}"
mkdir -p "$source_dir/Conf" "$output_dir/AARCH64" "$output_dir/.llvm-bin"
find_tool() {
    local name=$1 candidate suffix
    if candidate=$(command -v "$name" 2>/dev/null); then printf '%s\n' "$candidate"; return; fi
    for suffix in 20 19 18 17 16; do
        if candidate=$(command -v "$name-$suffix" 2>/dev/null); then printf '%s\n' "$candidate"; return; fi
    done
    echo "Missing LLVM tool: $name" >&2; return 1
}
ln -sfn "$(find_tool clang)" "$output_dir/.llvm-bin/clang"
ln -sfn "$(find_tool llvm-ar)" "$output_dir/.llvm-bin/llvm-ar"
ln -sfn "$(find_tool llvm-objcopy)" "$output_dir/.llvm-bin/llvm-objcopy"

cd "$source_dir"
set +u
# shellcheck disable=SC1091
source edksetup.sh
set -u
export SOURCE_DATE_EPOCH
SOURCE_DATE_EPOCH=$(git show -s --format=%ct "$edk_commit")
module=MdeModulePkg/Bus/Usb/UsbMouseDxe/UsbMouseDxe.inf
build -a X64 -t GCC -b RELEASE -p MdeModulePkg/MdeModulePkg.dsc -m "$module" -n "${OPENCONSOLE_JOBS:-2}"
CLANGDWARF_BIN="$output_dir/.llvm-bin/" build -a AARCH64 -t CLANGDWARF -b RELEASE \
    -p MdeModulePkg/MdeModulePkg.dsc -m "$module" -n "${OPENCONSOLE_JOBS:-2}"
install -m 0644 Build/MdeModule/RELEASE_GCC/X64/UsbMouseDxe.efi "$output_dir/UsbMouseDxe.efi"
install -m 0644 Build/MdeModule/RELEASE_CLANGDWARF/AARCH64/UsbMouseDxe.efi \
    "$output_dir/AARCH64/UsbMouseDxe.efi"
python3 "$repo_root/tools/verify_efi.py" "$output_dir/UsbMouseDxe.efi" \
    --arch x64 --role driver --require-zero-timestamp >/dev/null
python3 "$repo_root/tools/verify_efi.py" "$output_dir/AARCH64/UsbMouseDxe.efi" \
    --arch arm64 --role driver --require-zero-timestamp >/dev/null
printf '%s\n' "$edk_commit" > "$output_dir/EDK2_COMMIT"
(cd "$output_dir" && find . -type f -name '*.efi' -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS)
echo "Packaged pinned x64 and ARM64 UsbMouseDxe in $output_dir"
