#!/usr/bin/env bash
set -euo pipefail

audk_commit=dfd5516702b4911f1fc1919a429d637b456df913
audk_url=https://github.com/acidanthera/audk.git
opencore_commit=78d5ff824992d5b227f842e34d8221eb8f31baf1
opencore_url=https://github.com/acidanthera/OpenCorePkg.git
repo_root=$(cd "$(dirname "$0")/.." && pwd)
source_dir=${OPENCONSOLE_AUDK_SOURCE:-"$repo_root/build/vendor-src/audk-$audk_commit"}
output_dir=${OPENCONSOLE_AUDIODXE_OUTPUT:-"$repo_root/build/opencore-audiodxe"}
if [[ "$source_dir" != /* ]]; then source_dir="$repo_root/$source_dir"; fi
if [[ "$output_dir" != /* ]]; then output_dir="$repo_root/$output_dir"; fi

if [[ ! -d "$source_dir/.git" ]]; then
    mkdir -p "$(dirname "$source_dir")"
    git clone --filter=blob:none --no-checkout "$audk_url" "$source_dir"
fi
git -C "$source_dir" fetch --depth 1 origin "$audk_commit"
git -C "$source_dir" checkout --detach "$audk_commit"
test "$(git -C "$source_dir" rev-parse HEAD)" = "$audk_commit"

# OpenCore CI builds against audk, replacing audk's OpenCorePkg gitlink with
# the requested OpenCore revision. A full tree is needed because BaseTools
# validates every component named by OpenCorePkg.dsc.
if [[ ! -d "$source_dir/OpenCorePkg/.git" ]]; then
    rmdir "$source_dir/OpenCorePkg" 2>/dev/null || true
    git clone --filter=blob:none --no-checkout "$opencore_url" "$source_dir/OpenCorePkg"
fi
git -C "$source_dir/OpenCorePkg" fetch --depth 1 origin "$opencore_commit"
git -C "$source_dir/OpenCorePkg" checkout --detach "$opencore_commit"
test "$(git -C "$source_dir/OpenCorePkg" rev-parse HEAD)" = "$opencore_commit"

patches=(
    "$repo_root/patches/opencore-audiodxe/0001-failure-cleanup.patch"
    "$repo_root/patches/opencore-audiodxe/0002-instance-safe-playback-setup.patch"
    "$repo_root/patches/opencore-audiodxe/0003-aarch64-build-profile.patch"
    "$repo_root/patches/opencore-audiodxe/0004-aarch64-audio-load-options-stub.patch"
)
for patch in "${patches[@]}"; do
    if git -C "$source_dir/OpenCorePkg" apply --reverse --check "$patch" 2>/dev/null; then
        : # already applied in this reusable source checkout
    else
        git -C "$source_dir/OpenCorePkg" apply --check "$patch"
        git -C "$source_dir/OpenCorePkg" apply "$patch"
    fi
done

mkdir -p "$source_dir/OpenConsolePkg/OcAudioLoadOptionsStub"
cp "$repo_root/vendor/opencore/OcAudioLoadOptionsStub/"* \
   "$source_dir/OpenConsolePkg/OcAudioLoadOptionsStub/"

# Package DEC files name include paths from these submodules even though the
# AudioDxe link does not consume every library.
submodules=(
    BaseTools/Source/C/BrotliCompress/brotli
    CryptoPkg/Library/MbedTlsLib/mbedtls
    CryptoPkg/Library/OpensslLib/openssl
    MdeModulePkg/Library/BrotliCustomDecompressLib/brotli
    MdeModulePkg/Universal/RegularExpressionDxe/oniguruma
    MdePkg/Library/BaseFdtLib/libfdt
    MdePkg/Library/MipiSysTLib/mipisyst
    RedfishPkg/Library/JsonLib/jansson
    SecurityPkg/DeviceSecurity/SpdmLib/libspdm
    UnitTestFrameworkPkg/Library/CmockaLib/cmocka
    UnitTestFrameworkPkg/Library/GoogleTestLib/googletest
    UnitTestFrameworkPkg/Library/SubhookLib/subhook
)
git -C "$source_dir" submodule update --init --depth 1 -- "${submodules[@]}"

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
# shellcheck disable=SC1091
set +u
source edksetup.sh
set -u
# AUDK's legacy CLANGDWARF/AARCH64 profile incorrectly combines LLVM LTO
# objects with its GCC pass-through archive. The link then silently emits an
# ELF containing only debug sections. Disable those two template macros for
# this build; this is the same supported no-LTO path exposed by tools_def.
sed -i \
    -e 's/^DEFINE LTO .*$/DEFINE LTO         =/' \
    -e 's/^DEFINE AARCH64_LTO .*$/DEFINE AARCH64_LTO =/' \
    -e '/^RELEASE_CLANGDWARF_AARCH64_CC_FLAGS/ s/ -flto//' \
    -e '/^RELEASE_CLANGDWARF_AARCH64_DLINK_FLAGS/c\RELEASE_CLANGDWARF_AARCH64_DLINK_FLAGS = DEF(CLANGDWARF_AARCH64_DLINK_FLAGS)' \
    Conf/tools_def.txt
if grep -Eq '^DEFINE (LTO|AARCH64_LTO) += .+|^RELEASE_CLANGDWARF_AARCH64_.*flto' Conf/tools_def.txt; then
    echo "Failed to disable AUDK AArch64 LTO macros" >&2
    exit 1
fi
# Newer LLVM emits an allocated GNU AArch64 feature note which AUDK's pinned
# ELF-to-PE converter does not represent. UEFI images do not consume it.
if ! grep -q '\*(\.note.gnu.property)' BaseTools/Scripts/Base.lds; then
    sed -i '/\*(\.note.GNU-stack)/a\    *(.note.gnu.property)' BaseTools/Scripts/Base.lds
fi
export SOURCE_DATE_EPOCH
SOURCE_DATE_EPOCH=$(git -C OpenCorePkg show -s --format=%ct "$opencore_commit")
build -a X64 -t GCC -b RELEASE -p OpenCorePkg/OpenCorePkg.dsc \
    -m OpenCorePkg/Staging/AudioDxe/AudioDxe.inf -n "${OPENCONSOLE_JOBS:-2}"
CLANG_BIN="$output_dir/.llvm-bin/" build -a AARCH64 -t CLANGDWARF -b RELEASE \
    -p OpenCorePkg/OpenCorePkg.dsc \
    -m OpenCorePkg/Staging/AudioDxe/AudioDxe.inf -n "${OPENCONSOLE_JOBS:-2}"

install -m 0644 Build/OpenCorePkg/RELEASE_GCC/X64/AudioDxe.efi "$output_dir/AudioDxe.efi"
install -m 0644 Build/OpenCorePkg/RELEASE_CLANGDWARF/AARCH64/AudioDxe.efi \
    "$output_dir/AARCH64/AudioDxe.efi"
python3 "$repo_root/tools/verify_efi.py" "$output_dir/AudioDxe.efi" \
    --arch x64 --role driver --require-zero-timestamp >/dev/null
python3 "$repo_root/tools/verify_efi.py" "$output_dir/AARCH64/AudioDxe.efi" \
    --arch arm64 --role driver --require-zero-timestamp >/dev/null
(
    cd "$output_dir"
    find . -type f -name '*.efi' -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
)
printf '%s\n' "$audk_commit" > "$output_dir/AUDK_COMMIT"
printf '%s\n' "$opencore_commit" > "$output_dir/OPENCORE_COMMIT"
echo "Packaged pinned OpenCore AudioDxe in $output_dir"
