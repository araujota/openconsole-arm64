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
)
for patch in "${patches[@]}"; do
    if git -C "$source_dir/OpenCorePkg" apply --reverse --check "$patch" 2>/dev/null; then
        : # already applied in this reusable source checkout
    else
        git -C "$source_dir/OpenCorePkg" apply --check "$patch"
        git -C "$source_dir/OpenCorePkg" apply "$patch"
    fi
done

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
mkdir -p "$source_dir/Conf"
cd "$source_dir"
# shellcheck disable=SC1091
set +u
source edksetup.sh
set -u
export SOURCE_DATE_EPOCH
SOURCE_DATE_EPOCH=$(git -C OpenCorePkg show -s --format=%ct "$opencore_commit")
build -a X64 -t GCC -b RELEASE -p OpenCorePkg/OpenCorePkg.dsc \
    -m OpenCorePkg/Staging/AudioDxe/AudioDxe.inf -n "${OPENCONSOLE_JOBS:-2}"

mkdir -p "$output_dir"
install -m 0644 Build/OpenCorePkg/RELEASE_GCC/X64/AudioDxe.efi "$output_dir/AudioDxe.efi"
(
    cd "$output_dir"
    sha256sum AudioDxe.efi > SHA256SUMS
)
printf '%s\n' "$audk_commit" > "$output_dir/AUDK_COMMIT"
printf '%s\n' "$opencore_commit" > "$output_dir/OPENCORE_COMMIT"
echo "Packaged pinned OpenCore AudioDxe in $output_dir"
