BUILD_DIR := build
TARGET ?= x64
EDK2_NETWORK_PIN := 0a503bc2f5bd37c7a5d39d0ba31008f446f0298d
AUDK_AUDIO_PIN := dfd5516702b4911f1fc1919a429d637b456df913
OPENCONSOLE_EDK2_SOURCE ?= $(abspath $(BUILD_DIR)/vendor-src/edk2-$(EDK2_NETWORK_PIN))
SRC := src/boot/main.c
OBJDUMP := objdump
INCLUDES := -Iinclude
CFLAGS_COMMON := -std=c11 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
	-fshort-wchar -fno-stack-protector -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -O2 $(INCLUDES)
ifeq ($(TARGET),x64)
TARGET_TRIPLE := x86_64-unknown-windows
TARGET_PE_MACHINE := 0x8664
TARGET_BOOT_FILE := BOOTX64.EFI
TARGET_OCG2_ROLE := entry-x64
TARGET_QEMU := qemu-system-x86_64
TARGET_QEMU_MACHINE := q35
CFLAGS := --target=$(TARGET_TRIPLE) $(CFLAGS_COMMON) -mno-red-zone -mno-stack-arg-probe
LDFLAGS := -mi386pep --subsystem 10 --entry efi_main --enable-reloc-section \
	--image-base 0 --dynamicbase --no-insert-timestamp
LD := ld
else ifeq ($(TARGET),arm64)
TARGET_TRIPLE := aarch64-unknown-windows
TARGET_PE_MACHINE := 0xaa64
TARGET_BOOT_FILE := BOOTAA64.EFI
TARGET_OCG2_ROLE := entry-arm64
TARGET_QEMU := qemu-system-aarch64
TARGET_QEMU_MACHINE := virt
CFLAGS := --target=$(TARGET_TRIPLE) $(CFLAGS_COMMON) -mno-stack-arg-probe
LDFLAGS := --target=$(TARGET_TRIPLE) -fuse-ld=lld -nostdlib \
	-Wl,/subsystem:efi_application,/entry:efi_main,/machine:arm64,/dynamicbase,/fixed:no,/timestamp:0
LD := clang
else
$(error Unsupported TARGET '$(TARGET)'; expected x64 or arm64)
endif

QEMU_TEST_DEFINES := -DOPENCONSOLE_QEMU_TEST=1 -DOPENCONSOLE_DATA_VOLUME_TEST=1
ifeq ($(TARGET),arm64)
QEMU_TEST_DEFINES += -DOPENCONSOLE_REAL_NET_TEST=1
endif

.PHONY: test-game-storage-ovmf-matrix test-runtime-integrity-ovmf test-split-volume-firmware test-bootnext-ovmf arm64 arm64-parent arm64-child arm64-image test-targets test-arm64-host test-arm64-aavmf test-usb-installers test-linux-installer-loop stage-host-tools test-host-launchers
.PHONY: all clean FORCE_PLATFORM_TRUST esp esp-network-stack esp-audio-driver esp-crypto-driver development-image physical-test-esp physical-test-package physical-test-image physical-network-test-esp audio-test-esp integrated-io-test-esp slice-test-esp crypto-test-esp update-storage-test-esp update-e2e-test-esp https-test-esp edk2-network-stack edk2-input-driver update-crypto-dxe ovmf-snp-only ovmf-pointer audit-edk2-network-stack opencore-audiodxe audit-opencore-audiodxe test-esp pointer-pagination-test-esp game-details-test-esp gles-test-esp io-test-esp real-net-test-esp network-fixture-test-esp packaged-network-fixture-test-esp test test-host test-analyzer test-ovmf test-dashboard-ovmf test-pointer-pagination-ovmf test-game-details-ovmf test-gles-ovmf test-io-ovmf test-real-net-ovmf test-network-fixture-ovmf test-packaged-network-fixture-ovmf test-physical-inventory-ovmf test-physical-network-ovmf test-audio-ovmf test-audio-no-output-ovmf test-integrated-io-ovmf test-slice-ovmf test-crypto-ovmf test-update-storage-ovmf test-update-e2e-ovmf test-https-ovmf test-offline-ovmf test-virtio-gpu check-efi-core check-examples run

all: $(BUILD_DIR)/BOOTX64.EFI esp

arm64:
	$(MAKE) TARGET=arm64 BUILD_DIR=build/arm64 arm64-parent arm64-child

arm64-parent: $(BUILD_DIR)/BOOTAA64.EFI

arm64-child: $(BUILD_DIR)/INPUT-PROBE-AA64.EFI

arm64-image: edk2-network-stack edk2-input-driver update-crypto-dxe opencore-audiodxe
	$(MAKE) TARGET=arm64 BUILD_DIR=build/arm64 build/arm64/BOOTAA64-TEST.EFI arm64-child
	$(MAKE) build/jacket.ocj
	python3 scripts/build-arm64-image.py

test-arm64-aavmf: arm64-image
	python3 tests/arm64_image_test.py
	python3 scripts/test-arm64-aavmf.py

test-targets: edk2-network-stack
	python3 tests/target_matrix_test.py
	python3 tests/ethernet_size_budget_test.py

test-usb-installers:
	python3 tests/media_layout_test.py
	python3 tests/media_layout_verifier_test.py
	python3 tests/linux_installer_test.py
	python3 tests/windows_installer_contract_test.py
	python3 tests/fat32_formatter_test.py
	python3 tests/usb_installer_package_test.py

test-host-launchers:
	python3 tests/host_launcher_test.py
	python3 tests/windows_host_launcher_contract_test.py
	python3 tests/host_tools_stage_test.py

test-bootnext-ovmf: $(BUILD_DIR)/BOOTX64-BOOTNEXT-TEST.EFI $(BUILD_DIR)/NORMAL-BOOT-SENTINEL.EFI $(BUILD_DIR)/BOOTNEXT-ARMER.EFI
	./scripts/test-bootnext-ovmf.sh

stage-host-tools:
	python3 scripts/stage-host-tools.py $(BUILD_DIR)/esp

test-linux-installer-loop:
	python3 tests/linux_installer_loop_test.py

test-split-volume-firmware:
	./scripts/test-split-volume-firmware.sh

test-arm64-host: $(BUILD_DIR)/INPUT-PROBE.EFI arm64 test-targets
	python3 tests/ocg2_arm64_test.py

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/ethernet-catalog-x64.oec: tests/fixtures/ethernet-catalog-x64.json tools/ethernet_catalog.py | $(BUILD_DIR)
	python3 tools/ethernet_catalog.py $< $@

$(BUILD_DIR)/ethernet-catalog-test: tests/ethernet_catalog_test.c src/net/ethernet_catalog.c include/openconsole/ethernet_catalog.h $(BUILD_DIR)/ethernet-catalog-x64.oec | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -Iinclude tests/ethernet_catalog_test.c src/net/ethernet_catalog.c -o $@

# main.c owns the concrete provider/backend objects, so any public or private
# OpenConsole header may alter its compiled layout. Keep every launcher variant
# from linking a freshly rebuilt backend into a stale parent object.
BOOT_HEADERS := $(wildcard include/openconsole/*.h)
DASHBOARD_OBJ := $(BUILD_DIR)/dashboard-efi.obj
TIME_FORMAT_OBJ := $(BUILD_DIR)/time-format-efi.obj
PACKAGE_MEASURE_OBJ := $(BUILD_DIR)/package-measure-efi.obj
DETAILS_INPUT_OBJ := $(BUILD_DIR)/details-input-efi.obj
DELETE_POLICY_OBJ := $(BUILD_DIR)/delete-policy-efi.obj
SHA256_OBJ := $(BUILD_DIR)/sha256-efi.obj
CAPABILITIES_OBJ := $(BUILD_DIR)/capabilities-efi.obj

$(SHA256_OBJ): src/core/sha256.c include/openconsole/sha256.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(CAPABILITIES_OBJ): src/core/capabilities.c include/openconsole/capabilities.h include/openconsole/manifest.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ethernet-catalog-efi.obj: src/net/ethernet_catalog.c include/openconsole/ethernet_catalog.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ethernet-driver-selector-efi.obj: src/net/ethernet_driver_selector.c include/openconsole/ethernet_driver_selector.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(TIME_FORMAT_OBJ): src/core/time_format.c include/openconsole/time_format.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(PACKAGE_MEASURE_OBJ): src/core/package_measure.c include/openconsole/package_measure.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(DETAILS_INPUT_OBJ): src/ui/details_input.c include/openconsole/details_input.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(DELETE_POLICY_OBJ): src/core/delete_policy.c include/openconsole/delete_policy.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(DASHBOARD_OBJ): src/ui/dashboard.c include/openconsole/dashboard.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/BOOTX64.EFI \
$(BUILD_DIR)/BOOTX64-PHYSICAL-TEST.EFI \
$(BUILD_DIR)/BOOTX64-TEST.EFI \
$(BUILD_DIR)/BOOTX64-REAL-NET-TEST.EFI \
$(BUILD_DIR)/BOOTX64-AUDIO-TEST.EFI \
$(BUILD_DIR)/BOOTX64-INTEGRATED-IO-TEST.EFI \
$(BUILD_DIR)/BOOTX64-SLICE-TEST.EFI \
$(BUILD_DIR)/BOOTX64-NETWORK-FIXTURE-TEST.EFI \
$(BUILD_DIR)/BOOTX64-HTTPS-TEST.EFI: $(DASHBOARD_OBJ) $(TIME_FORMAT_OBJ) $(PACKAGE_MEASURE_OBJ) $(DETAILS_INPUT_OBJ) $(DELETE_POLICY_OBJ) $(CAPABILITIES_OBJ)

$(BUILD_DIR)/BOOTX64-DEVELOPMENT.EFI: $(DASHBOARD_OBJ) $(TIME_FORMAT_OBJ) $(PACKAGE_MEASURE_OBJ) $(DETAILS_INPUT_OBJ) $(DELETE_POLICY_OBJ) $(CAPABILITIES_OBJ)

$(BUILD_DIR)/BOOTX64-DEVELOPMENT-MUTATION-TEST.EFI \
$(BUILD_DIR)/BOOTX64-DEVELOPMENT-WATCHDOG-TEST.EFI: $(DASHBOARD_OBJ) $(TIME_FORMAT_OBJ) $(PACKAGE_MEASURE_OBJ) $(DETAILS_INPUT_OBJ) $(DELETE_POLICY_OBJ) $(CAPABILITIES_OBJ)

$(BUILD_DIR)/BOOTX64-BOOTNEXT-TEST.EFI: $(DASHBOARD_OBJ) $(TIME_FORMAT_OBJ) $(PACKAGE_MEASURE_OBJ) $(DETAILS_INPUT_OBJ) $(DELETE_POLICY_OBJ) $(CAPABILITIES_OBJ)

$(BUILD_DIR)/boot.obj: $(SRC) $(BOOT_HEADERS) | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gpu-probe.obj: src/gpu/pci_probe.c include/openconsole/gpu_probe.h include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/io-inventory-efi.obj: src/io/inventory.c include/openconsole/io_inventory.h include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gles-provider-efi.obj: src/gles/provider.c include/openconsole/gles_provider.h include/openconsole/gles2.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/BOOTX64.EFI: $(BUILD_DIR)/boot.obj $(BUILD_DIR)/manifest-efi.obj $(BUILD_DIR)/gpu-probe.obj $(BUILD_DIR)/gles-provider-efi.obj $(BUILD_DIR)/io-inventory-efi.obj $(BUILD_DIR)/net-provider-efi.obj $(BUILD_DIR)/uefi-net-backend-efi.obj $(BUILD_DIR)/net-stack-loader-efi.obj $(BUILD_DIR)/tls-ca-efi.obj $(BUILD_DIR)/audio-provider-efi.obj $(BUILD_DIR)/audioio-backend-efi.obj
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/BOOTAA64.EFI: $(BUILD_DIR)/boot.obj $(BUILD_DIR)/manifest-efi.obj $(BUILD_DIR)/gpu-probe.obj $(BUILD_DIR)/gles-provider-efi.obj $(BUILD_DIR)/io-inventory-efi.obj $(BUILD_DIR)/net-provider-efi.obj $(BUILD_DIR)/uefi-net-backend-efi.obj $(BUILD_DIR)/net-stack-loader-efi.obj $(BUILD_DIR)/tls-ca-efi.obj $(BUILD_DIR)/audio-provider-efi.obj $(BUILD_DIR)/audioio-backend-efi.obj $(DASHBOARD_OBJ) $(TIME_FORMAT_OBJ) $(PACKAGE_MEASURE_OBJ) $(DETAILS_INPUT_OBJ) $(DELETE_POLICY_OBJ) $(CAPABILITIES_OBJ)
	@test "$(TARGET)" = arm64 || { echo "BOOTAA64.EFI requires TARGET=arm64" >&2; exit 2; }
	$(LD) $(LDFLAGS) $^ -o $@
	python3 tools/verify_efi.py --arch arm64 --role application --require-zero-timestamp $@

$(BUILD_DIR)/BOOTAA64-TEST.EFI: $(BUILD_DIR)/boot-test.obj $(BUILD_DIR)/manifest-efi.obj $(BUILD_DIR)/gpu-probe.obj $(BUILD_DIR)/gles-provider-efi.obj $(BUILD_DIR)/io-inventory-efi.obj $(BUILD_DIR)/io-mock-provider-efi.obj $(BUILD_DIR)/net-provider-efi.obj $(BUILD_DIR)/uefi-net-backend-efi.obj $(BUILD_DIR)/net-stack-loader-efi.obj $(BUILD_DIR)/tls-ca-efi.obj $(BUILD_DIR)/audio-provider-efi.obj $(BUILD_DIR)/audioio-backend-efi.obj $(DASHBOARD_OBJ) $(TIME_FORMAT_OBJ) $(PACKAGE_MEASURE_OBJ) $(DETAILS_INPUT_OBJ) $(DELETE_POLICY_OBJ) $(CAPABILITIES_OBJ)
	@test "$(TARGET)" = arm64 || { echo "BOOTAA64-TEST.EFI requires TARGET=arm64" >&2; exit 2; }
	$(LD) $(LDFLAGS) $^ -o $@
	python3 tools/verify_efi.py --arch arm64 --role application --require-zero-timestamp $@

$(BUILD_DIR)/boot-physical-test.obj: $(SRC) $(BOOT_HEADERS) | $(BUILD_DIR)
	clang $(CFLAGS) -DOPENCONSOLE_PHYSICAL_TEST=1 -c $< -o $@

FORCE_PLATFORM_TRUST:

$(BUILD_DIR)/generated/openconsole_platform_trust.h: FORCE_PLATFORM_TRUST $(BUILD_DIR)/update-crypto-dxe/OpenConsoleCryptoDxe.efi scripts/generate-platform-trust-header.py
	@test -n "$(PLATFORM_KEY)" || { echo "PLATFORM_KEY must name an Ed448 private key" >&2; exit 2; }
	./scripts/generate-platform-trust-header.py --platform-key "$(PLATFORM_KEY)" \
		--crypto-driver $(BUILD_DIR)/update-crypto-dxe/OpenConsoleCryptoDxe.efi --output $@

$(BUILD_DIR)/boot-development.obj: $(SRC) $(BOOT_HEADERS) $(BUILD_DIR)/generated/openconsole_platform_trust.h | $(BUILD_DIR)
	clang $(CFLAGS) -I$(BUILD_DIR)/generated -DOPENCONSOLE_PINNED_PLATFORM_TRUST=1 -c $< -o $@

$(BUILD_DIR)/BOOTX64-DEVELOPMENT.EFI: $(BUILD_DIR)/boot-development.obj $(BUILD_DIR)/manifest-efi.obj $(BUILD_DIR)/gpu-probe.obj $(BUILD_DIR)/gles-provider-efi.obj $(BUILD_DIR)/io-inventory-efi.obj $(BUILD_DIR)/net-provider-efi.obj $(BUILD_DIR)/uefi-net-backend-efi.obj $(BUILD_DIR)/net-stack-loader-efi.obj $(BUILD_DIR)/tls-ca-efi.obj $(BUILD_DIR)/audio-provider-efi.obj $(BUILD_DIR)/audioio-backend-efi.obj $(SHA256_OBJ) $(BUILD_DIR)/pe-image-efi.obj $(BUILD_DIR)/platform-inventory-efi.obj $(BUILD_DIR)/publisher-keys-efi.obj $(BUILD_DIR)/ocg2-efi.obj $(BUILD_DIR)/authenticated-load-efi.obj $(BUILD_DIR)/game-storage-efi.obj $(BUILD_DIR)/uefi-game-storage-efi.obj $(BUILD_DIR)/launch-recovery-efi.obj $(BUILD_DIR)/freestanding-memory-efi.obj $(BUILD_DIR)/ethernet-catalog-efi.obj $(BUILD_DIR)/ethernet-driver-selector-efi.obj
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/boot-development-mutation-test.obj: $(SRC) $(BOOT_HEADERS) $(BUILD_DIR)/generated/openconsole_platform_trust.h | $(BUILD_DIR)
	clang $(CFLAGS) -I$(BUILD_DIR)/generated -DOPENCONSOLE_PINNED_PLATFORM_TRUST=1 \
		-DOPENCONSOLE_TEST_MUTATE_GAME_BEFORE_LOAD=1 -c $< -o $@

$(BUILD_DIR)/boot-development-watchdog-test.obj: $(SRC) $(BOOT_HEADERS) $(BUILD_DIR)/generated/openconsole_platform_trust.h | $(BUILD_DIR)
	clang $(CFLAGS) -I$(BUILD_DIR)/generated -DOPENCONSOLE_PINNED_PLATFORM_TRUST=1 \
		-DOPENCONSOLE_TEST_WATCHDOG_UNSUPPORTED=1 -c $< -o $@

DEVELOPMENT_LINK_OBJECTS := $(BUILD_DIR)/manifest-efi.obj $(BUILD_DIR)/gpu-probe.obj $(BUILD_DIR)/gles-provider-efi.obj $(BUILD_DIR)/io-inventory-efi.obj $(BUILD_DIR)/net-provider-efi.obj $(BUILD_DIR)/uefi-net-backend-efi.obj $(BUILD_DIR)/net-stack-loader-efi.obj $(BUILD_DIR)/tls-ca-efi.obj $(BUILD_DIR)/audio-provider-efi.obj $(BUILD_DIR)/audioio-backend-efi.obj $(SHA256_OBJ) $(BUILD_DIR)/pe-image-efi.obj $(BUILD_DIR)/platform-inventory-efi.obj $(BUILD_DIR)/publisher-keys-efi.obj $(BUILD_DIR)/ocg2-efi.obj $(BUILD_DIR)/authenticated-load-efi.obj $(BUILD_DIR)/game-storage-efi.obj $(BUILD_DIR)/uefi-game-storage-efi.obj $(BUILD_DIR)/launch-recovery-efi.obj $(BUILD_DIR)/freestanding-memory-efi.obj $(BUILD_DIR)/ethernet-catalog-efi.obj $(BUILD_DIR)/ethernet-driver-selector-efi.obj

$(BUILD_DIR)/BOOTX64-DEVELOPMENT-MUTATION-TEST.EFI: $(BUILD_DIR)/boot-development-mutation-test.obj $(DEVELOPMENT_LINK_OBJECTS)
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/BOOTX64-DEVELOPMENT-WATCHDOG-TEST.EFI: $(BUILD_DIR)/boot-development-watchdog-test.obj $(DEVELOPMENT_LINK_OBJECTS)
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/hang-probe.obj: src/games/hang_probe.c include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/HANG-PROBE.EFI: $(BUILD_DIR)/hang-probe.obj
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/BOOTX64-PHYSICAL-TEST.EFI: $(BUILD_DIR)/boot-physical-test.obj $(BUILD_DIR)/manifest-efi.obj $(BUILD_DIR)/gpu-probe.obj $(BUILD_DIR)/gles-provider-efi.obj $(BUILD_DIR)/io-inventory-efi.obj $(BUILD_DIR)/net-provider-efi.obj $(BUILD_DIR)/uefi-net-backend-efi.obj $(BUILD_DIR)/net-stack-loader-efi.obj $(BUILD_DIR)/tls-ca-efi.obj $(BUILD_DIR)/audio-provider-efi.obj $(BUILD_DIR)/audioio-backend-efi.obj
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/boot-test.obj: $(SRC) $(BOOT_HEADERS) | $(BUILD_DIR)
	clang $(CFLAGS) $(QEMU_TEST_DEFINES) -c $< -o $@

$(BUILD_DIR)/io-mock-provider-efi.obj: src/io/mock_provider.c include/openconsole/io_mock_provider.h include/openconsole/net_protocol.h include/openconsole/audio_protocol.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/BOOTX64-TEST.EFI: $(BUILD_DIR)/boot-test.obj $(BUILD_DIR)/manifest-efi.obj $(BUILD_DIR)/gpu-probe.obj $(BUILD_DIR)/gles-provider-efi.obj $(BUILD_DIR)/io-inventory-efi.obj $(BUILD_DIR)/io-mock-provider-efi.obj $(BUILD_DIR)/net-provider-efi.obj $(BUILD_DIR)/uefi-net-backend-efi.obj $(BUILD_DIR)/tls-ca-efi.obj $(BUILD_DIR)/audio-provider-efi.obj $(BUILD_DIR)/audioio-backend-efi.obj
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/boot-bootnext-test.obj: $(SRC) $(BOOT_HEADERS) | $(BUILD_DIR)
	clang $(CFLAGS) -DOPENCONSOLE_QEMU_TEST=1 -DOPENCONSOLE_BOOTNEXT_TEST=1 -c $< -o $@

$(BUILD_DIR)/BOOTX64-BOOTNEXT-TEST.EFI: $(BUILD_DIR)/boot-bootnext-test.obj $(BUILD_DIR)/manifest-efi.obj $(BUILD_DIR)/gpu-probe.obj $(BUILD_DIR)/gles-provider-efi.obj $(BUILD_DIR)/io-inventory-efi.obj $(BUILD_DIR)/io-mock-provider-efi.obj $(BUILD_DIR)/net-provider-efi.obj $(BUILD_DIR)/uefi-net-backend-efi.obj $(BUILD_DIR)/tls-ca-efi.obj $(BUILD_DIR)/audio-provider-efi.obj $(BUILD_DIR)/audioio-backend-efi.obj
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/normal-boot-sentinel.obj: src/games/normal_boot_sentinel.c include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/NORMAL-BOOT-SENTINEL.EFI: $(BUILD_DIR)/normal-boot-sentinel.obj
	$(LD) $(LDFLAGS) $< -o $@

$(BUILD_DIR)/bootnext-armer.obj: src/games/bootnext_armer.c include/openconsole/uefi.h include/openconsole/platform_debug.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/BOOTNEXT-ARMER.EFI: $(BUILD_DIR)/bootnext-armer.obj
	$(LD) $(LDFLAGS) $< -o $@

$(BUILD_DIR)/boot-real-net-test.obj: $(SRC) $(BOOT_HEADERS) | $(BUILD_DIR)
	clang $(CFLAGS) -DOPENCONSOLE_QEMU_TEST=1 -DOPENCONSOLE_REAL_NET_TEST=1 -c $< -o $@

$(BUILD_DIR)/BOOTX64-REAL-NET-TEST.EFI: $(BUILD_DIR)/boot-real-net-test.obj $(BUILD_DIR)/manifest-efi.obj $(BUILD_DIR)/gpu-probe.obj $(BUILD_DIR)/gles-provider-efi.obj $(BUILD_DIR)/io-inventory-efi.obj $(BUILD_DIR)/net-provider-efi.obj $(BUILD_DIR)/uefi-net-backend-efi.obj $(BUILD_DIR)/net-stack-loader-efi.obj $(BUILD_DIR)/tls-ca-efi.obj $(BUILD_DIR)/audio-provider-efi.obj $(BUILD_DIR)/audioio-backend-efi.obj
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/boot-network-fixture-test.obj: $(SRC) $(BOOT_HEADERS) | $(BUILD_DIR)
	clang $(CFLAGS) -DOPENCONSOLE_QEMU_TEST=1 -DOPENCONSOLE_DATA_VOLUME_TEST=1 -DOPENCONSOLE_REAL_NET_TEST=1 -DOPENCONSOLE_QEMU_REPEAT_TEST=1 -c $< -o $@

$(BUILD_DIR)/boot-audio-test.obj: $(SRC) $(BOOT_HEADERS) | $(BUILD_DIR)
	clang $(CFLAGS) -DOPENCONSOLE_QEMU_TEST=1 -DOPENCONSOLE_REAL_AUDIO_TEST=1 -c $< -o $@

$(BUILD_DIR)/BOOTX64-AUDIO-TEST.EFI: $(BUILD_DIR)/boot-audio-test.obj $(BUILD_DIR)/manifest-efi.obj $(BUILD_DIR)/gpu-probe.obj $(BUILD_DIR)/gles-provider-efi.obj $(BUILD_DIR)/io-inventory-efi.obj $(BUILD_DIR)/net-provider-efi.obj $(BUILD_DIR)/uefi-net-backend-efi.obj $(BUILD_DIR)/net-stack-loader-efi.obj $(BUILD_DIR)/tls-ca-efi.obj $(BUILD_DIR)/audio-provider-efi.obj $(BUILD_DIR)/audioio-backend-efi.obj
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/boot-integrated-io-test.obj: $(SRC) $(BOOT_HEADERS) | $(BUILD_DIR)
	clang $(CFLAGS) -DOPENCONSOLE_QEMU_TEST=1 -DOPENCONSOLE_INTEGRATED_IO_TEST=1 -DOPENCONSOLE_QEMU_REPEAT_TEST=1 -c $< -o $@

$(BUILD_DIR)/BOOTX64-INTEGRATED-IO-TEST.EFI: $(BUILD_DIR)/boot-integrated-io-test.obj $(BUILD_DIR)/manifest-efi.obj $(BUILD_DIR)/gpu-probe.obj $(BUILD_DIR)/gles-provider-efi.obj $(BUILD_DIR)/io-inventory-efi.obj $(BUILD_DIR)/net-provider-efi.obj $(BUILD_DIR)/uefi-net-backend-efi.obj $(BUILD_DIR)/net-stack-loader-efi.obj $(BUILD_DIR)/tls-ca-efi.obj $(BUILD_DIR)/audio-provider-efi.obj $(BUILD_DIR)/audioio-backend-efi.obj
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/boot-slice-test.obj: $(SRC) $(BOOT_HEADERS) | $(BUILD_DIR)
	clang $(CFLAGS) -DOPENCONSOLE_QEMU_TEST=1 -DOPENCONSOLE_INTEGRATED_IO_TEST=1 -DOPENCONSOLE_SLICE_TEST=1 -c $< -o $@

$(BUILD_DIR)/BOOTX64-SLICE-TEST.EFI: $(BUILD_DIR)/boot-slice-test.obj $(BUILD_DIR)/manifest-efi.obj $(BUILD_DIR)/gpu-probe.obj $(BUILD_DIR)/gles-provider-efi.obj $(BUILD_DIR)/io-inventory-efi.obj $(BUILD_DIR)/net-provider-efi.obj $(BUILD_DIR)/uefi-net-backend-efi.obj $(BUILD_DIR)/net-stack-loader-efi.obj $(BUILD_DIR)/tls-ca-efi.obj $(BUILD_DIR)/audio-provider-efi.obj $(BUILD_DIR)/audioio-backend-efi.obj
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/BOOTX64-NETWORK-FIXTURE-TEST.EFI: $(BUILD_DIR)/boot-network-fixture-test.obj $(BUILD_DIR)/manifest-efi.obj $(BUILD_DIR)/gpu-probe.obj $(BUILD_DIR)/gles-provider-efi.obj $(BUILD_DIR)/io-inventory-efi.obj $(BUILD_DIR)/net-provider-efi.obj $(BUILD_DIR)/uefi-net-backend-efi.obj $(BUILD_DIR)/net-stack-loader-efi.obj $(BUILD_DIR)/tls-ca-efi.obj $(BUILD_DIR)/audio-provider-efi.obj $(BUILD_DIR)/audioio-backend-efi.obj
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/boot-https-test.obj: $(SRC) $(BOOT_HEADERS) | $(BUILD_DIR)
	clang $(CFLAGS) -DOPENCONSOLE_QEMU_TEST=1 -DOPENCONSOLE_REAL_NET_TEST=1 -c $< -o $@

$(BUILD_DIR)/BOOTX64-HTTPS-TEST.EFI: $(BUILD_DIR)/boot-https-test.obj $(BUILD_DIR)/manifest-efi.obj $(BUILD_DIR)/gpu-probe.obj $(BUILD_DIR)/gles-provider-efi.obj $(BUILD_DIR)/io-inventory-efi.obj $(BUILD_DIR)/net-provider-efi.obj $(BUILD_DIR)/uefi-net-backend-efi.obj $(BUILD_DIR)/net-stack-loader-efi.obj $(BUILD_DIR)/tls-ca-efi.obj $(BUILD_DIR)/audio-provider-efi.obj $(BUILD_DIR)/audioio-backend-efi.obj
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/input-probe.obj: src/games/input_probe.c include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/INPUT-PROBE.EFI: $(BUILD_DIR)/input-probe.obj
	$(LD) $(LDFLAGS) $< -o $@

$(BUILD_DIR)/INPUT-PROBE-AA64.EFI: $(BUILD_DIR)/input-probe.obj
	@test "$(TARGET)" = arm64 || { echo "INPUT-PROBE-AA64.EFI requires TARGET=arm64" >&2; exit 2; }
	$(LD) $(LDFLAGS) $< -o $@
	python3 tools/verify_efi.py --arch arm64 --role application --require-zero-timestamp $@

$(BUILD_DIR)/io-probe.obj: src/games/io_probe.c include/openconsole/net_protocol.h include/openconsole/audio_protocol.h include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/IO-PROBE.EFI: $(BUILD_DIR)/io-probe.obj
	$(LD) $(LDFLAGS) $< -o $@

$(BUILD_DIR)/network-probe.obj: src/games/network_probe.c include/openconsole/net_protocol.h include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/NETWORK-PROBE.EFI: $(BUILD_DIR)/network-probe.obj
	$(LD) $(LDFLAGS) $< -o $@

$(BUILD_DIR)/physical-network-probe.obj: src/games/physical_network_probe.c include/openconsole/net_protocol.h include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/PHYSICAL-NETWORK-PROBE.EFI: $(BUILD_DIR)/physical-network-probe.obj
	$(LD) $(LDFLAGS) $< -o $@

$(BUILD_DIR)/https-probe.obj: src/games/network_probe.c include/openconsole/net_protocol.h include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -DOPENCONSOLE_HTTPS_TEST=1 -c $< -o $@

$(BUILD_DIR)/HTTPS-PROBE.EFI: $(BUILD_DIR)/https-probe.obj
	$(LD) $(LDFLAGS) $< -o $@

$(BUILD_DIR)/gles-triangle.obj: src/games/gles_triangle.c include/openconsole/gles2_protocol.h include/openconsole/gles2.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/GLES-TRIANGLE.EFI: $(BUILD_DIR)/gles-triangle.obj
	$(LD) $(LDFLAGS) $< -o $@

$(BUILD_DIR)/audio-probe.obj: src/games/audio_probe.c include/openconsole/audio_protocol.h include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/AUDIO-PROBE.EFI: $(BUILD_DIR)/audio-probe.obj
	$(LD) $(LDFLAGS) $< -o $@

$(BUILD_DIR)/crypto-probe.obj: src/games/crypto_probe.c include/openconsole/crypto_protocol.h include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/CRYPTO-PROBE.EFI: $(BUILD_DIR)/crypto-probe.obj $(BUILD_DIR)/update-metadata-efi.obj $(BUILD_DIR)/update-verify-efi.obj $(BUILD_DIR)/uefi-update-crypto-efi.obj
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/update-storage-probe.obj: src/games/update_storage_probe.c include/openconsole/uefi_update_storage.h include/openconsole/update_storage.h include/openconsole/crypto_protocol.h include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/UPDATE-STORAGE-PROBE.EFI: $(BUILD_DIR)/update-storage-probe.obj $(BUILD_DIR)/update-storage-efi.obj $(BUILD_DIR)/uefi-update-storage-efi.obj
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/update-e2e-probe.obj: src/games/update_e2e_probe.c include/openconsole/net_protocol.h include/openconsole/uefi_update_crypto.h include/openconsole/uefi_update_storage.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/UPDATE-E2E-PROBE.EFI: $(BUILD_DIR)/update-e2e-probe.obj $(BUILD_DIR)/update-metadata-efi.obj $(BUILD_DIR)/update-verify-efi.obj $(BUILD_DIR)/uefi-update-crypto-efi.obj $(BUILD_DIR)/update-storage-efi.obj $(BUILD_DIR)/uefi-update-storage-efi.obj
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/integrated-io-game.obj: src/games/integrated_io_game.c include/openconsole/audio_protocol.h include/openconsole/gles2_protocol.h include/openconsole/gles2.h include/openconsole/net_protocol.h include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/INTEGRATED-IO-GAME.EFI: $(BUILD_DIR)/integrated-io-game.obj
	$(LD) $(LDFLAGS) $< -o $@

$(BUILD_DIR)/slice-installer.obj: src/games/slice_installer.c include/openconsole/net_protocol.h include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/SLICE-INSTALLER.EFI: $(BUILD_DIR)/slice-installer.obj
	$(LD) $(LDFLAGS) $< -o $@

$(BUILD_DIR)/slice-fixtures/slice.ocg: $(BUILD_DIR)/INTEGRATED-IO-GAME.EFI $(BUILD_DIR)/jacket.ocj examples/games/integrated/game.toml examples/games/integrated/controls.cfg scripts/build-slice-bundle.py
	python3 scripts/build-slice-bundle.py examples/games/integrated/game.toml \
		examples/games/integrated/controls.cfg $(BUILD_DIR)/jacket.ocj \
		$(BUILD_DIR)/INTEGRATED-IO-GAME.EFI $@

$(BUILD_DIR)/jacket-gen: tools/jacket-gen.c | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic $< -o $@

$(BUILD_DIR)/jacket.ocj: $(BUILD_DIR)/jacket-gen
	$< $@

esp: $(BUILD_DIR)/BOOTX64.EFI $(BUILD_DIR)/INPUT-PROBE.EFI $(BUILD_DIR)/GLES-TRIANGLE.EFI $(BUILD_DIR)/jacket.ocj edk2-input-driver
	mkdir -p $(BUILD_DIR)/esp/EFI/BOOT $(BUILD_DIR)/esp/EFI/OpenConsole/Drivers/Input
	cp $< $(BUILD_DIR)/esp/EFI/BOOT/BOOTX64.EFI
	cp $(BUILD_DIR)/edk2-input-driver/UsbMouseDxe.efi $(BUILD_DIR)/esp/EFI/OpenConsole/Drivers/Input/UsbMouseDxe.efi
	mkdir -p $(BUILD_DIR)/esp/games/hello
	cp examples/games/hello/game.toml $(BUILD_DIR)/esp/games/hello/game.toml
	cp $(BUILD_DIR)/INPUT-PROBE.EFI $(BUILD_DIR)/esp/games/hello/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/esp/games/hello/jacket.ocj
	mkdir -p $(BUILD_DIR)/esp/games/second
	cp examples/games/second/game.toml $(BUILD_DIR)/esp/games/second/game.toml
	cp $(BUILD_DIR)/INPUT-PROBE.EFI $(BUILD_DIR)/esp/games/second/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/esp/games/second/jacket.ocj
	mkdir -p $(BUILD_DIR)/esp/games/gles
	cp examples/games/gles/game.toml $(BUILD_DIR)/esp/games/gles/game.toml
	cp $(BUILD_DIR)/GLES-TRIANGLE.EFI $(BUILD_DIR)/esp/games/gles/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/esp/games/gles/jacket.ocj
	python3 scripts/stage-host-tools.py $(BUILD_DIR)/esp

edk2-network-stack:
	./scripts/build-edk2-network-stack.sh

edk2-input-driver: edk2-network-stack
	./scripts/build-edk2-input-driver.sh

update-crypto-dxe: edk2-network-stack
	./scripts/build-update-crypto-dxe.sh

ovmf-snp-only: edk2-network-stack
	./scripts/build-ovmf-snp-only.sh

ovmf-pointer: edk2-network-stack
	./scripts/build-ovmf-pointer.sh

opencore-audiodxe:
	./scripts/build-opencore-audiodxe.sh

audit-opencore-audiodxe: opencore-audiodxe
	./scripts/audit-opencore-audiodxe.py \
		--source "$(abspath $(BUILD_DIR)/vendor-src/audk-$(AUDK_AUDIO_PIN)/OpenCorePkg)" \
		--patch "$(abspath patches/opencore-audiodxe/0001-failure-cleanup.patch)" \
		--patch "$(abspath patches/opencore-audiodxe/0002-instance-safe-playback-setup.patch)" \
		--patch "$(abspath patches/opencore-audiodxe/0003-aarch64-build-profile.patch)" \
		--patch "$(abspath patches/opencore-audiodxe/0004-aarch64-audio-load-options-stub.patch)" \
		--binary "$(abspath $(BUILD_DIR)/opencore-audiodxe/AudioDxe.efi)" \
		--output "$(abspath $(BUILD_DIR)/opencore-audiodxe/failure-audit.json)"

audit-edk2-network-stack: test-packaged-network-fixture-ovmf
	./scripts/audit-edk2-network-stack.py \
		--source "$(OPENCONSOLE_EDK2_SOURCE)" \
		--package $(BUILD_DIR)/edk2-network-stack \
		--runtime $(BUILD_DIR)/network-fixture-metrics.json \
		--runtime-log $(BUILD_DIR)/ovmf-test-network-fixture.log \
		--firmware $(BUILD_DIR)/ovmf-snp-only/OVMF_CODE.fd \
		--output $(BUILD_DIR)/edk2-network-stack-audit.json

esp-network-stack: esp edk2-network-stack
	mkdir -p $(BUILD_DIR)/esp/EFI/OpenConsole/Drivers/Network
	cp $(BUILD_DIR)/edk2-network-stack/*.efi $(BUILD_DIR)/esp/EFI/OpenConsole/Drivers/Network/
	cp $(BUILD_DIR)/edk2-network-stack/SHA256SUMS $(BUILD_DIR)/esp/EFI/OpenConsole/Drivers/Network/

esp-audio-driver: esp opencore-audiodxe
	mkdir -p $(BUILD_DIR)/esp/EFI/OpenConsole/Drivers/Audio
	cp $(BUILD_DIR)/opencore-audiodxe/AudioDxe.efi $(BUILD_DIR)/esp/EFI/OpenConsole/Drivers/Audio/
	cp $(BUILD_DIR)/opencore-audiodxe/SHA256SUMS $(BUILD_DIR)/esp/EFI/OpenConsole/Drivers/Audio/

esp-crypto-driver: esp update-crypto-dxe
	mkdir -p $(BUILD_DIR)/esp/EFI/OpenConsole/Drivers/Crypto
	cp $(BUILD_DIR)/update-crypto-dxe/OpenConsoleCryptoDxe.efi $(BUILD_DIR)/esp/EFI/OpenConsole/Drivers/Crypto/
	cp $(BUILD_DIR)/update-crypto-dxe/SHA256SUMS $(BUILD_DIR)/esp/EFI/OpenConsole/Drivers/Crypto/

physical-test-esp: esp-network-stack esp-audio-driver esp-crypto-driver $(BUILD_DIR)/BOOTX64-PHYSICAL-TEST.EFI $(BUILD_DIR)/AUDIO-PROBE.EFI $(BUILD_DIR)/PHYSICAL-NETWORK-PROBE.EFI
	cp $(BUILD_DIR)/BOOTX64-PHYSICAL-TEST.EFI $(BUILD_DIR)/esp/EFI/BOOT/BOOTX64.EFI
	mkdir -p $(BUILD_DIR)/esp/games/audio
	cp examples/games/audio/game.toml $(BUILD_DIR)/esp/games/audio/game.toml
	cp $(BUILD_DIR)/AUDIO-PROBE.EFI $(BUILD_DIR)/esp/games/audio/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/esp/games/audio/jacket.ocj
	mkdir -p $(BUILD_DIR)/esp/games/physical-network
	cp examples/games/physical-network/game.toml $(BUILD_DIR)/esp/games/physical-network/game.toml
	cp $(BUILD_DIR)/PHYSICAL-NETWORK-PROBE.EFI $(BUILD_DIR)/esp/games/physical-network/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/esp/games/physical-network/jacket.ocj
	truncate -s 0 $(BUILD_DIR)/esp/OPENCONSOLE-INVENTORY.LOG
	truncate -s 16384 $(BUILD_DIR)/esp/OPENCONSOLE-INVENTORY.LOG
	truncate -s 0 $(BUILD_DIR)/esp/OPENCONSOLE-DRIVER-INVENTORY.LOG
	truncate -s 16384 $(BUILD_DIR)/esp/OPENCONSOLE-DRIVER-INVENTORY.LOG
	truncate -s 0 $(BUILD_DIR)/esp/OPENCONSOLE-NETWORK-EVIDENCE.LOG
	truncate -s 16384 $(BUILD_DIR)/esp/OPENCONSOLE-NETWORK-EVIDENCE.LOG
	truncate -s 0 $(BUILD_DIR)/esp/OPENCONSOLE-AUDIO-EVIDENCE.LOG
	truncate -s 16384 $(BUILD_DIR)/esp/OPENCONSOLE-AUDIO-EVIDENCE.LOG
	cp examples/physical/OPENCONSOLE-AUDIO.CFG.example $(BUILD_DIR)/esp/
	@echo "Physical test ESP prepared at $(BUILD_DIR)/esp"

physical-test-package: physical-test-esp
	./scripts/package-physical-test-esp.sh

physical-test-image: physical-test-package
	./scripts/build-physical-test-disk-image.sh

development-image: physical-test-esp $(BUILD_DIR)/BOOTX64-DEVELOPMENT.EFI
	@test -n "$(PLATFORM_KEY)" || { echo "PLATFORM_KEY must name an Ed448 private key" >&2; exit 2; }
	./scripts/build-development-image.py --platform-key "$(PLATFORM_KEY)" \
		--parent $(BUILD_DIR)/BOOTX64-DEVELOPMENT.EFI

physical-network-test-esp: $(BUILD_DIR)/BOOTX64-HTTPS-TEST.EFI $(BUILD_DIR)/PHYSICAL-NETWORK-PROBE.EFI $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/tls-fixtures/ca.der edk2-network-stack
	mkdir -p $(BUILD_DIR)/physical-network-test-esp/EFI/BOOT $(BUILD_DIR)/physical-network-test-esp/EFI/OpenConsole/Drivers/Network $(BUILD_DIR)/physical-network-test-esp/EFI/OpenConsole/Trust $(BUILD_DIR)/physical-network-test-esp/games/physical-network
	cp $(BUILD_DIR)/BOOTX64-HTTPS-TEST.EFI $(BUILD_DIR)/physical-network-test-esp/EFI/BOOT/BOOTX64.EFI
	cp $(BUILD_DIR)/edk2-network-stack/*.efi $(BUILD_DIR)/physical-network-test-esp/EFI/OpenConsole/Drivers/Network/
	cp $(BUILD_DIR)/tls-fixtures/ca.der $(BUILD_DIR)/physical-network-test-esp/EFI/OpenConsole/Trust/ca.der
	cp examples/fixtures/physical-network-qemu.cfg $(BUILD_DIR)/physical-network-test-esp/OPENCONSOLE-NETWORK.CFG
	cp examples/games/physical-network/game.toml $(BUILD_DIR)/physical-network-test-esp/games/physical-network/game.toml
	cp $(BUILD_DIR)/PHYSICAL-NETWORK-PROBE.EFI $(BUILD_DIR)/physical-network-test-esp/games/physical-network/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/physical-network-test-esp/games/physical-network/jacket.ocj
	truncate -s 0 $(BUILD_DIR)/physical-network-test-esp/OPENCONSOLE-NETWORK-EVIDENCE.LOG
	truncate -s 16384 $(BUILD_DIR)/physical-network-test-esp/OPENCONSOLE-NETWORK-EVIDENCE.LOG

audio-test-esp: $(BUILD_DIR)/BOOTX64-AUDIO-TEST.EFI $(BUILD_DIR)/AUDIO-PROBE.EFI $(BUILD_DIR)/jacket.ocj opencore-audiodxe
	mkdir -p $(BUILD_DIR)/audio-test-esp/EFI/BOOT $(BUILD_DIR)/audio-test-esp/EFI/OpenConsole/Drivers/Audio $(BUILD_DIR)/audio-test-esp/games/audio
	cp $(BUILD_DIR)/BOOTX64-AUDIO-TEST.EFI $(BUILD_DIR)/audio-test-esp/EFI/BOOT/BOOTX64.EFI
	cp $(BUILD_DIR)/opencore-audiodxe/AudioDxe.efi $(BUILD_DIR)/audio-test-esp/EFI/OpenConsole/Drivers/Audio/AudioDxe.efi
	cp examples/games/hello/game.toml $(BUILD_DIR)/audio-test-esp/games/audio/game.toml
	cp $(BUILD_DIR)/AUDIO-PROBE.EFI $(BUILD_DIR)/audio-test-esp/games/audio/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/audio-test-esp/games/audio/jacket.ocj

integrated-io-test-esp: $(BUILD_DIR)/BOOTX64-INTEGRATED-IO-TEST.EFI $(BUILD_DIR)/INTEGRATED-IO-GAME.EFI $(BUILD_DIR)/jacket.ocj opencore-audiodxe edk2-input-driver
	mkdir -p $(BUILD_DIR)/integrated-io-test-esp/EFI/BOOT $(BUILD_DIR)/integrated-io-test-esp/EFI/OpenConsole/Drivers/Audio $(BUILD_DIR)/integrated-io-test-esp/EFI/OpenConsole/Drivers/Input $(BUILD_DIR)/integrated-io-test-esp/games/integrated
	cp $(BUILD_DIR)/BOOTX64-INTEGRATED-IO-TEST.EFI $(BUILD_DIR)/integrated-io-test-esp/EFI/BOOT/BOOTX64.EFI
	cp $(BUILD_DIR)/opencore-audiodxe/AudioDxe.efi $(BUILD_DIR)/integrated-io-test-esp/EFI/OpenConsole/Drivers/Audio/AudioDxe.efi
	cp $(BUILD_DIR)/edk2-input-driver/UsbMouseDxe.efi $(BUILD_DIR)/integrated-io-test-esp/EFI/OpenConsole/Drivers/Input/UsbMouseDxe.efi
	cp examples/games/integrated/game.toml $(BUILD_DIR)/integrated-io-test-esp/games/integrated/game.toml
	cp examples/games/integrated/controls.cfg $(BUILD_DIR)/integrated-io-test-esp/games/integrated/controls.cfg
	cp $(BUILD_DIR)/INTEGRATED-IO-GAME.EFI $(BUILD_DIR)/integrated-io-test-esp/games/integrated/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/integrated-io-test-esp/games/integrated/jacket.ocj

slice-test-esp: $(BUILD_DIR)/BOOTX64-SLICE-TEST.EFI $(BUILD_DIR)/SLICE-INSTALLER.EFI $(BUILD_DIR)/slice-fixtures/slice.ocg $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/tls-fixtures/ca.der edk2-network-stack edk2-input-driver update-crypto-dxe opencore-audiodxe
	rm -rf -- $(BUILD_DIR)/slice-test-esp
	mkdir -p $(BUILD_DIR)/slice-test-esp/EFI/BOOT \
		$(BUILD_DIR)/slice-test-esp/EFI/OpenConsole/Drivers/Network \
		$(BUILD_DIR)/slice-test-esp/EFI/OpenConsole/Drivers/Crypto \
		$(BUILD_DIR)/slice-test-esp/EFI/OpenConsole/Drivers/Audio \
		$(BUILD_DIR)/slice-test-esp/EFI/OpenConsole/Drivers/Input \
		$(BUILD_DIR)/slice-test-esp/EFI/OpenConsole/Trust \
		$(BUILD_DIR)/slice-test-esp/games/slice-installer
	cp $(BUILD_DIR)/BOOTX64-SLICE-TEST.EFI $(BUILD_DIR)/slice-test-esp/EFI/BOOT/BOOTX64.EFI
	cp $(BUILD_DIR)/edk2-network-stack/*.efi $(BUILD_DIR)/slice-test-esp/EFI/OpenConsole/Drivers/Network/
	cp $(BUILD_DIR)/update-crypto-dxe/OpenConsoleCryptoDxe.efi $(BUILD_DIR)/slice-test-esp/EFI/OpenConsole/Drivers/Crypto/OpenConsoleCryptoDxe.efi
	cp $(BUILD_DIR)/opencore-audiodxe/AudioDxe.efi $(BUILD_DIR)/slice-test-esp/EFI/OpenConsole/Drivers/Audio/AudioDxe.efi
	cp $(BUILD_DIR)/edk2-input-driver/UsbMouseDxe.efi $(BUILD_DIR)/slice-test-esp/EFI/OpenConsole/Drivers/Input/UsbMouseDxe.efi
	cp $(BUILD_DIR)/tls-fixtures/ca.der $(BUILD_DIR)/slice-test-esp/EFI/OpenConsole/Trust/ca.der
	cp examples/games/slice-installer/game.toml $(BUILD_DIR)/slice-test-esp/games/slice-installer/game.toml
	cp $(BUILD_DIR)/SLICE-INSTALLER.EFI $(BUILD_DIR)/slice-test-esp/games/slice-installer/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/slice-test-esp/games/slice-installer/jacket.ocj

crypto-test-esp: $(BUILD_DIR)/BOOTX64-TEST.EFI $(BUILD_DIR)/CRYPTO-PROBE.EFI $(BUILD_DIR)/jacket.ocj update-crypto-dxe
	mkdir -p $(BUILD_DIR)/crypto-test-esp/EFI/BOOT $(BUILD_DIR)/crypto-test-esp/EFI/OpenConsole/Drivers/Crypto $(BUILD_DIR)/crypto-test-esp/games/crypto
	cp $(BUILD_DIR)/BOOTX64-TEST.EFI $(BUILD_DIR)/crypto-test-esp/EFI/BOOT/BOOTX64.EFI
	cp $(BUILD_DIR)/update-crypto-dxe/OpenConsoleCryptoDxe.efi $(BUILD_DIR)/crypto-test-esp/EFI/OpenConsole/Drivers/Crypto/OpenConsoleCryptoDxe.efi
	cp examples/games/hello/game.toml $(BUILD_DIR)/crypto-test-esp/games/crypto/game.toml
	cp $(BUILD_DIR)/CRYPTO-PROBE.EFI $(BUILD_DIR)/crypto-test-esp/games/crypto/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/crypto-test-esp/games/crypto/jacket.ocj

update-storage-test-esp: $(BUILD_DIR)/BOOTX64-TEST.EFI $(BUILD_DIR)/UPDATE-STORAGE-PROBE.EFI $(BUILD_DIR)/jacket.ocj update-crypto-dxe
	mkdir -p $(BUILD_DIR)/update-storage-test-esp/EFI/BOOT $(BUILD_DIR)/update-storage-test-esp/EFI/OpenConsole/Drivers/Crypto $(BUILD_DIR)/update-storage-test-esp/games/update-storage
	rm -f -- $(BUILD_DIR)/update-storage-test-esp/EFI/OpenConsole/Updates/slot-a.efi $(BUILD_DIR)/update-storage-test-esp/EFI/OpenConsole/Updates/slot-b.efi $(BUILD_DIR)/update-storage-test-esp/EFI/OpenConsole/Updates/select-a.ocu $(BUILD_DIR)/update-storage-test-esp/EFI/OpenConsole/Updates/select-b.ocu
	cp $(BUILD_DIR)/BOOTX64-TEST.EFI $(BUILD_DIR)/update-storage-test-esp/EFI/BOOT/BOOTX64.EFI
	cp $(BUILD_DIR)/update-crypto-dxe/OpenConsoleCryptoDxe.efi $(BUILD_DIR)/update-storage-test-esp/EFI/OpenConsole/Drivers/Crypto/OpenConsoleCryptoDxe.efi
	cp examples/games/hello/game.toml $(BUILD_DIR)/update-storage-test-esp/games/update-storage/game.toml
	cp $(BUILD_DIR)/UPDATE-STORAGE-PROBE.EFI $(BUILD_DIR)/update-storage-test-esp/games/update-storage/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/update-storage-test-esp/games/update-storage/jacket.ocj

$(BUILD_DIR)/update-fixtures/catalog.occ: $(BUILD_DIR)/BOOTX64-HTTPS-TEST.EFI scripts/generate-update-fixtures.py
	./scripts/generate-update-fixtures.py $(BUILD_DIR)/BOOTX64-HTTPS-TEST.EFI $(BUILD_DIR)/update-fixtures

update-e2e-test-esp: $(BUILD_DIR)/BOOTX64-HTTPS-TEST.EFI $(BUILD_DIR)/UPDATE-E2E-PROBE.EFI $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/tls-fixtures/ca.der $(BUILD_DIR)/update-fixtures/catalog.occ edk2-network-stack update-crypto-dxe
	mkdir -p $(BUILD_DIR)/update-e2e-test-esp/EFI/BOOT $(BUILD_DIR)/update-e2e-test-esp/EFI/OpenConsole/Drivers/Network $(BUILD_DIR)/update-e2e-test-esp/EFI/OpenConsole/Drivers/Crypto $(BUILD_DIR)/update-e2e-test-esp/EFI/OpenConsole/Trust $(BUILD_DIR)/update-e2e-test-esp/games/update
	rm -f -- $(BUILD_DIR)/update-e2e-test-esp/EFI/OpenConsole/Updates/slot-a.efi $(BUILD_DIR)/update-e2e-test-esp/EFI/OpenConsole/Updates/slot-b.efi $(BUILD_DIR)/update-e2e-test-esp/EFI/OpenConsole/Updates/select-a.ocu $(BUILD_DIR)/update-e2e-test-esp/EFI/OpenConsole/Updates/select-b.ocu
	cp $(BUILD_DIR)/BOOTX64-HTTPS-TEST.EFI $(BUILD_DIR)/update-e2e-test-esp/EFI/BOOT/BOOTX64.EFI
	cp $(BUILD_DIR)/edk2-network-stack/*.efi $(BUILD_DIR)/update-e2e-test-esp/EFI/OpenConsole/Drivers/Network/
	cp $(BUILD_DIR)/update-crypto-dxe/OpenConsoleCryptoDxe.efi $(BUILD_DIR)/update-e2e-test-esp/EFI/OpenConsole/Drivers/Crypto/OpenConsoleCryptoDxe.efi
	cp $(BUILD_DIR)/tls-fixtures/ca.der $(BUILD_DIR)/update-e2e-test-esp/EFI/OpenConsole/Trust/ca.der
	cp examples/games/hello/game.toml $(BUILD_DIR)/update-e2e-test-esp/games/update/game.toml
	cp $(BUILD_DIR)/UPDATE-E2E-PROBE.EFI $(BUILD_DIR)/update-e2e-test-esp/games/update/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/update-e2e-test-esp/games/update/jacket.ocj

test-esp: $(BUILD_DIR)/BOOTX64-TEST.EFI $(BUILD_DIR)/INPUT-PROBE.EFI $(BUILD_DIR)/jacket.ocj
	mkdir -p $(BUILD_DIR)/test-esp/EFI/BOOT
	cp $< $(BUILD_DIR)/test-esp/EFI/BOOT/BOOTX64.EFI
	mkdir -p $(BUILD_DIR)/test-esp/games/hello
	cp examples/games/hello/game.toml $(BUILD_DIR)/test-esp/games/hello/game.toml
	cp $(BUILD_DIR)/INPUT-PROBE.EFI $(BUILD_DIR)/test-esp/games/hello/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/test-esp/games/hello/jacket.ocj
	mkdir -p $(BUILD_DIR)/test-esp/games/second
	cp examples/games/second/game.toml $(BUILD_DIR)/test-esp/games/second/game.toml
	cp $(BUILD_DIR)/INPUT-PROBE.EFI $(BUILD_DIR)/test-esp/games/second/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/test-esp/games/second/jacket.ocj

pointer-pagination-test-esp: $(BUILD_DIR)/BOOTX64-TEST.EFI $(BUILD_DIR)/INPUT-PROBE.EFI $(BUILD_DIR)/jacket.ocj scripts/prepare-pointer-pagination-esp.py edk2-input-driver
	python3 scripts/prepare-pointer-pagination-esp.py $(BUILD_DIR)/BOOTX64-TEST.EFI \
		$(BUILD_DIR)/INPUT-PROBE.EFI $(BUILD_DIR)/jacket.ocj \
		$(BUILD_DIR)/pointer-pagination-test-esp
	mkdir -p $(BUILD_DIR)/pointer-pagination-test-esp/EFI/OpenConsole/Drivers/Input
	cp $(BUILD_DIR)/edk2-input-driver/UsbMouseDxe.efi $(BUILD_DIR)/pointer-pagination-test-esp/EFI/OpenConsole/Drivers/Input/UsbMouseDxe.efi

game-details-test-esp: $(BUILD_DIR)/BOOTX64-NETWORK-FIXTURE-TEST.EFI $(BUILD_DIR)/INPUT-PROBE.EFI $(BUILD_DIR)/jacket.ocj edk2-network-stack edk2-input-driver scripts/prepare-game-details-esp.py
	python3 scripts/prepare-game-details-esp.py $(BUILD_DIR)/BOOTX64-NETWORK-FIXTURE-TEST.EFI \
		$(BUILD_DIR)/INPUT-PROBE.EFI $(BUILD_DIR)/jacket.ocj \
		$(BUILD_DIR)/edk2-network-stack $(BUILD_DIR)/game-details-test-esp
	mkdir -p $(BUILD_DIR)/game-details-test-esp/EFI/OpenConsole/Drivers/Input
	cp $(BUILD_DIR)/edk2-input-driver/UsbMouseDxe.efi $(BUILD_DIR)/game-details-test-esp/EFI/OpenConsole/Drivers/Input/UsbMouseDxe.efi

gles-test-esp: $(BUILD_DIR)/BOOTX64-TEST.EFI $(BUILD_DIR)/GLES-TRIANGLE.EFI $(BUILD_DIR)/jacket.ocj
	mkdir -p $(BUILD_DIR)/gles-test-esp/EFI/BOOT $(BUILD_DIR)/gles-test-esp/games/gles
	cp $(BUILD_DIR)/BOOTX64-TEST.EFI $(BUILD_DIR)/gles-test-esp/EFI/BOOT/BOOTX64.EFI
	cp examples/games/gles/game.toml $(BUILD_DIR)/gles-test-esp/games/gles/game.toml
	cp $(BUILD_DIR)/GLES-TRIANGLE.EFI $(BUILD_DIR)/gles-test-esp/games/gles/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/gles-test-esp/games/gles/jacket.ocj

io-test-esp: $(BUILD_DIR)/BOOTX64-TEST.EFI $(BUILD_DIR)/IO-PROBE.EFI $(BUILD_DIR)/jacket.ocj
	mkdir -p $(BUILD_DIR)/io-test-esp/EFI/BOOT $(BUILD_DIR)/io-test-esp/games/io
	cp $(BUILD_DIR)/BOOTX64-TEST.EFI $(BUILD_DIR)/io-test-esp/EFI/BOOT/BOOTX64.EFI
	cp examples/games/hello/game.toml $(BUILD_DIR)/io-test-esp/games/io/game.toml
	cp $(BUILD_DIR)/IO-PROBE.EFI $(BUILD_DIR)/io-test-esp/games/io/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/io-test-esp/games/io/jacket.ocj

real-net-test-esp: $(BUILD_DIR)/BOOTX64-REAL-NET-TEST.EFI $(BUILD_DIR)/INPUT-PROBE.EFI $(BUILD_DIR)/jacket.ocj
	mkdir -p $(BUILD_DIR)/real-net-test-esp/EFI/BOOT $(BUILD_DIR)/real-net-test-esp/games/probe
	truncate -s 16384 $(BUILD_DIR)/real-net-test-esp/OPENCONSOLE-INVENTORY.LOG
	cp $(BUILD_DIR)/BOOTX64-REAL-NET-TEST.EFI $(BUILD_DIR)/real-net-test-esp/EFI/BOOT/BOOTX64.EFI
	cp examples/games/hello/game.toml $(BUILD_DIR)/real-net-test-esp/games/probe/game.toml
	cp $(BUILD_DIR)/INPUT-PROBE.EFI $(BUILD_DIR)/real-net-test-esp/games/probe/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/real-net-test-esp/games/probe/jacket.ocj

network-fixture-test-esp: $(BUILD_DIR)/BOOTX64-NETWORK-FIXTURE-TEST.EFI $(BUILD_DIR)/NETWORK-PROBE.EFI $(BUILD_DIR)/jacket.ocj
	mkdir -p $(BUILD_DIR)/network-fixture-test-esp/EFI/BOOT $(BUILD_DIR)/network-fixture-test-esp/games/probe
	cp $(BUILD_DIR)/BOOTX64-NETWORK-FIXTURE-TEST.EFI $(BUILD_DIR)/network-fixture-test-esp/EFI/BOOT/BOOTX64.EFI
	cp examples/games/hello/game.toml $(BUILD_DIR)/network-fixture-test-esp/games/probe/game.toml
	cp $(BUILD_DIR)/NETWORK-PROBE.EFI $(BUILD_DIR)/network-fixture-test-esp/games/probe/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/network-fixture-test-esp/games/probe/jacket.ocj

packaged-network-fixture-test-esp: network-fixture-test-esp edk2-network-stack
	mkdir -p $(BUILD_DIR)/network-fixture-test-esp/EFI/OpenConsole/Drivers/Network
	cp $(BUILD_DIR)/edk2-network-stack/*.efi $(BUILD_DIR)/network-fixture-test-esp/EFI/OpenConsole/Drivers/Network/
	cp $(BUILD_DIR)/edk2-network-stack/SHA256SUMS $(BUILD_DIR)/network-fixture-test-esp/EFI/OpenConsole/Drivers/Network/

$(BUILD_DIR)/tls-fixtures/ca.der: scripts/generate-tls-fixtures.py | $(BUILD_DIR)
	./scripts/generate-tls-fixtures.py $(BUILD_DIR)/tls-fixtures

https-test-esp: $(BUILD_DIR)/BOOTX64-HTTPS-TEST.EFI $(BUILD_DIR)/HTTPS-PROBE.EFI $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/tls-fixtures/ca.der edk2-network-stack
	mkdir -p $(BUILD_DIR)/https-test-esp/EFI/BOOT $(BUILD_DIR)/https-test-esp/EFI/OpenConsole/Drivers/Network $(BUILD_DIR)/https-test-esp/EFI/OpenConsole/Trust $(BUILD_DIR)/https-test-esp/games/https
	cp $(BUILD_DIR)/BOOTX64-HTTPS-TEST.EFI $(BUILD_DIR)/https-test-esp/EFI/BOOT/BOOTX64.EFI
	cp $(BUILD_DIR)/edk2-network-stack/*.efi $(BUILD_DIR)/https-test-esp/EFI/OpenConsole/Drivers/Network/
	cp $(BUILD_DIR)/tls-fixtures/ca.der $(BUILD_DIR)/https-test-esp/EFI/OpenConsole/Trust/ca.der
	cp examples/games/hello/game.toml $(BUILD_DIR)/https-test-esp/games/https/game.toml
	cp $(BUILD_DIR)/HTTPS-PROBE.EFI $(BUILD_DIR)/https-test-esp/games/https/game.efi
	cp $(BUILD_DIR)/jacket.ocj $(BUILD_DIR)/https-test-esp/games/https/jacket.ocj

$(BUILD_DIR)/manifest-test: tests/manifest_test.c src/core/manifest.c include/openconsole/manifest.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/manifest_test.c src/core/manifest.c -o $@

$(BUILD_DIR)/capabilities-test: tests/capabilities_test.c src/core/capabilities.c include/openconsole/capabilities.h include/openconsole/manifest.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/capabilities_test.c src/core/capabilities.c -o $@

$(BUILD_DIR)/gles-abi-test: tests/gles_abi_test.c include/openconsole/gles2_protocol.h include/openconsole/gles2.h include/openconsole/uefi.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -Iinclude $< -o $@

$(BUILD_DIR)/gles-provider-test: tests/gles_provider_test.c src/gles/provider.c include/openconsole/gles_provider.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/gles_provider_test.c src/gles/provider.c -o $@

$(BUILD_DIR)/io-inventory-test: tests/io_inventory_test.c src/io/inventory.c include/openconsole/io_inventory.h include/openconsole/uefi.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/io_inventory_test.c src/io/inventory.c -o $@

$(BUILD_DIR)/io-abi-test: tests/io_abi_test.c include/openconsole/net_protocol.h include/openconsole/audio_protocol.h include/openconsole/uefi.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -Iinclude $< -o $@

$(BUILD_DIR)/io-mock-provider-test: tests/io_mock_provider_test.c src/io/mock_provider.c include/openconsole/io_mock_provider.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/io_mock_provider_test.c src/io/mock_provider.c -o $@

$(BUILD_DIR)/net-provider-test: tests/net_provider_test.c src/net/provider.c include/openconsole/net_provider.h include/openconsole/net_protocol.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/net_provider_test.c src/net/provider.c -o $@

$(BUILD_DIR)/uefi-net-backend-test: tests/uefi_net_backend_test.c src/net/uefi_backend.c include/openconsole/uefi_net_backend.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/uefi_net_backend_test.c src/net/uefi_backend.c -o $@

$(BUILD_DIR)/net-failure-test: tests/net_failure_test.c src/net/provider.c include/openconsole/net_provider.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/net_failure_test.c src/net/provider.c -o $@

$(BUILD_DIR)/net-stack-loader-test: tests/net_stack_loader_test.c src/net/stack_loader.c include/openconsole/net_stack_loader.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/net_stack_loader_test.c src/net/stack_loader.c -o $@

$(BUILD_DIR)/net-snp-bridge-test: tests/net_snp_bridge_test.c src/net/stack_loader.c include/openconsole/net_snp_bridge.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/net_snp_bridge_test.c src/net/stack_loader.c -o $@

$(BUILD_DIR)/ethernet-driver-selector-test: tests/ethernet_driver_selector_test.c src/net/ethernet_driver_selector.c include/openconsole/ethernet_driver_selector.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/ethernet_driver_selector_test.c src/net/ethernet_driver_selector.c -o $@

$(BUILD_DIR)/update-metadata-test: tests/update_metadata_test.c src/net/update_metadata.c include/openconsole/update_metadata.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/update_metadata_test.c src/net/update_metadata.c -o $@

$(BUILD_DIR)/update-verify-test: tests/update_verify_test.c src/net/update_verify.c src/net/update_metadata.c include/openconsole/update_verify.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/update_verify_test.c src/net/update_verify.c src/net/update_metadata.c -lcrypto -o $@

$(BUILD_DIR)/update-storage-test: tests/update_storage_test.c src/net/update_storage.c include/openconsole/update_storage.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/update_storage_test.c src/net/update_storage.c -o $@

$(BUILD_DIR)/uefi-update-crypto-test: tests/uefi_update_crypto_test.c src/net/uefi_update_crypto.c include/openconsole/uefi_update_crypto.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/uefi_update_crypto_test.c src/net/uefi_update_crypto.c -o $@

$(BUILD_DIR)/tls-ca-test: tests/tls_ca_test.c src/net/tls_ca.c include/openconsole/tls_ca.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/tls_ca_test.c src/net/tls_ca.c -o $@

$(BUILD_DIR)/audio-provider-test: tests/audio_provider_test.c src/audio/provider.c include/openconsole/audio_provider.h include/openconsole/audio_protocol.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/audio_provider_test.c src/audio/provider.c -o $@

$(BUILD_DIR)/audio-golden-test: tests/audio_golden_test.c src/audio/provider.c include/openconsole/audio_provider.h include/openconsole/audio_protocol.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/audio_golden_test.c src/audio/provider.c -lm -o $@

$(BUILD_DIR)/audioio-backend-test: tests/audioio_backend_test.c src/audio/audioio_backend.c include/openconsole/audioio_backend.h include/openconsole/audio_provider.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/audioio_backend_test.c src/audio/audioio_backend.c -o $@

$(BUILD_DIR)/dashboard-test: tests/dashboard_test.c src/ui/dashboard.c include/openconsole/dashboard.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/dashboard_test.c src/ui/dashboard.c -o $@

$(BUILD_DIR)/time-format-test: tests/time_format_test.c src/core/time_format.c include/openconsole/time_format.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/time_format_test.c src/core/time_format.c -o $@

$(BUILD_DIR)/package-measure-test: tests/package_measure_test.c src/core/package_measure.c include/openconsole/package_measure.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/package_measure_test.c src/core/package_measure.c -o $@

$(BUILD_DIR)/game-storage-test: tests/game_storage_test.c src/core/game_storage.c include/openconsole/game_storage.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/game_storage_test.c src/core/game_storage.c -o $@

$(BUILD_DIR)/game-transaction-test: tests/game_transaction_test.c src/core/game_storage.c include/openconsole/game_storage.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/game_transaction_test.c src/core/game_storage.c -o $@

$(BUILD_DIR)/pe-image-test: tests/pe_image_test.c src/core/pe_image.c include/openconsole/pe_image.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/pe_image_test.c src/core/pe_image.c -o $@

$(BUILD_DIR)/details-input-test: tests/details_input_test.c src/ui/details_input.c include/openconsole/details_input.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/details_input_test.c src/ui/details_input.c -o $@

$(BUILD_DIR)/delete-policy-test: tests/delete_policy_test.c src/core/delete_policy.c include/openconsole/delete_policy.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/delete_policy_test.c src/core/delete_policy.c -o $@

$(BUILD_DIR)/sha256-test: tests/sha256_test.c src/core/sha256.c include/openconsole/sha256.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/sha256_test.c src/core/sha256.c -o $@

$(BUILD_DIR)/platform-inventory-c-fixture/platform.ocp: tests/make_platform_inventory_c_fixture.py tools/platform_inventory.py
	./tests/make_platform_inventory_c_fixture.py

$(BUILD_DIR)/platform-inventory-c-test: tests/platform_inventory_c_test.c src/core/platform_inventory.c src/core/sha256.c include/openconsole/platform_inventory.h $(BUILD_DIR)/platform-inventory-c-fixture/platform.ocp | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/platform_inventory_c_test.c src/core/platform_inventory.c src/core/sha256.c -lcrypto -o $@

$(BUILD_DIR)/publisher-keys-fixture.ocpk: tests/publisher_keys_test.py tools/publisher_keys.py
	./tests/publisher_keys_test.py

$(BUILD_DIR)/publisher-keys-c-test: tests/publisher_keys_c_test.c src/core/publisher_keys.c src/core/sha256.c include/openconsole/publisher_keys.h $(BUILD_DIR)/publisher-keys-fixture.ocpk | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/publisher_keys_c_test.c src/core/publisher_keys.c src/core/sha256.c -o $@

$(BUILD_DIR)/ocg2-c-test: tests/ocg2_c_test.c src/core/ocg2.c src/core/sha256.c include/openconsole/ocg2.h tests/fixtures/ocg2/golden-minimal.ocg $(BUILD_DIR)/publisher-keys-fixture.pub | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
		-Iinclude tests/ocg2_c_test.c src/core/ocg2.c src/core/sha256.c -lcrypto -o $@

$(BUILD_DIR)/authenticated-game-fixture/game.ocg: tests/make_authenticated_game_fixture.py tools/ocg2.py tools/publisher_keys.py $(BUILD_DIR)/INPUT-PROBE.EFI
	./tests/make_authenticated_game_fixture.py

$(BUILD_DIR)/authenticated-load-test: tests/authenticated_load_test.c src/core/authenticated_load.c src/core/ocg2.c src/core/publisher_keys.c src/core/platform_inventory.c src/core/sha256.c src/core/pe_image.c src/core/manifest.c $(BUILD_DIR)/authenticated-game-fixture/game.ocg | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -Iinclude \
		tests/authenticated_load_test.c src/core/authenticated_load.c src/core/ocg2.c src/core/publisher_keys.c src/core/platform_inventory.c src/core/sha256.c src/core/pe_image.c src/core/manifest.c -lcrypto -o $@

# Set ANALYZER_PYTHON to a Python environment populated from
# requirements-analyzer.txt. Kept separate so ordinary EFI builds stay
# offline and dependency-free.
ANALYZER_PYTHON ?= python3
test-analyzer:
	$(ANALYZER_PYTHON) tests/analyzer_test.py

# Cross-repository parity belongs to the integrated workspace: the public
# runtime checkout intentionally does not contain the catalog's TypeScript
# worker implementation.
test-analyzer-integration: test-analyzer
	$(ANALYZER_PYTHON) tests/analyzer_stage_parity_test.py

$(BUILD_DIR)/launch-recovery-test: tests/launch_recovery_test.c src/core/launch_recovery.c include/openconsole/launch_recovery.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -Iinclude $< src/core/launch_recovery.c -o $@

test-host: $(BUILD_DIR)/dashboard-test $(BUILD_DIR)/time-format-test $(BUILD_DIR)/package-measure-test $(BUILD_DIR)/game-storage-test $(BUILD_DIR)/game-transaction-test $(BUILD_DIR)/launch-recovery-test $(BUILD_DIR)/pe-image-test $(BUILD_DIR)/INPUT-PROBE.EFI $(BUILD_DIR)/details-input-test $(BUILD_DIR)/delete-policy-test $(BUILD_DIR)/sha256-test $(BUILD_DIR)/platform-inventory-c-test $(BUILD_DIR)/publisher-keys-c-test $(BUILD_DIR)/ocg2-c-test $(BUILD_DIR)/authenticated-load-test

test-host: $(BUILD_DIR)/manifest-test $(BUILD_DIR)/capabilities-test $(BUILD_DIR)/gles-abi-test $(BUILD_DIR)/gles-provider-test $(BUILD_DIR)/io-inventory-test $(BUILD_DIR)/io-abi-test $(BUILD_DIR)/io-mock-provider-test $(BUILD_DIR)/net-provider-test $(BUILD_DIR)/uefi-net-backend-test $(BUILD_DIR)/net-failure-test $(BUILD_DIR)/net-stack-loader-test $(BUILD_DIR)/net-snp-bridge-test $(BUILD_DIR)/ethernet-driver-selector-test $(BUILD_DIR)/ethernet-catalog-test $(BUILD_DIR)/update-metadata-test $(BUILD_DIR)/update-verify-test $(BUILD_DIR)/update-storage-test $(BUILD_DIR)/uefi-update-crypto-test $(BUILD_DIR)/tls-ca-test $(BUILD_DIR)/audio-provider-test $(BUILD_DIR)/audio-golden-test $(BUILD_DIR)/audioio-backend-test
	python3 tests/ethernet_catalog_test.py
	./$(BUILD_DIR)/ethernet-catalog-test $(BUILD_DIR)/ethernet-catalog-x64.oec
	./tests/ocg2_tool_test.py
	./tests/game_assets_test.py
	./tests/sdk_examples_test.py
	./tests/platform_inventory_test.py
	./tests/publisher_keys_test.py
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/manifest-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/capabilities-test
	./$(BUILD_DIR)/gles-abi-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/gles-provider-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/io-inventory-test
	./$(BUILD_DIR)/io-abi-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/io-mock-provider-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/net-provider-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/uefi-net-backend-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/net-failure-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/net-stack-loader-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/net-snp-bridge-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/ethernet-driver-selector-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/update-metadata-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/update-verify-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/update-storage-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/uefi-update-crypto-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/tls-ca-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/audio-provider-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/audio-golden-test $(BUILD_DIR)/audio-golden.wav
	./scripts/analyze-audio-wav.py $(BUILD_DIR)/audio-golden.wav
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/audioio-backend-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/dashboard-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/time-format-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/package-measure-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/game-storage-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/game-transaction-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/launch-recovery-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/pe-image-test $(BUILD_DIR)/INPUT-PROBE.EFI
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/details-input-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/delete-policy-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/sha256-test
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/platform-inventory-c-test $(BUILD_DIR)/platform-inventory-c-fixture/platform.ocp $(BUILD_DIR)/platform-inventory-c-fixture/platform.pub $(BUILD_DIR)/platform-inventory-c-fixture/root/EFI/BOOT/BOOTX64.EFI
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/publisher-keys-c-test $(BUILD_DIR)/publisher-keys-fixture.ocpk $(BUILD_DIR)/publisher-keys-fixture.pub
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/ocg2-c-test tests/fixtures/ocg2/golden-minimal.ocg $(BUILD_DIR)/publisher-keys-fixture.pub
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD_DIR)/authenticated-load-test $(BUILD_DIR)/authenticated-game-fixture/game.ocg $(BUILD_DIR)/authenticated-game-fixture/publishers.ocpk $(BUILD_DIR)/authenticated-game-fixture/empty.ocpk
	./tests/physical_inventory_analyzer_test.py
	./tests/physical_audio_analyzer_test.py
	./tests/physical_network_analyzer_test.py
	./tests/ethernet_matrix_test.py
	./tests/ethernet_driver_manifest_test.py
	./tests/ethernet_driver_admission_test.py
	./tests/ipxe_fallback_decision_test.py
	./tests/intel_preboot_audit_test.py
	./tests/realtek_acquisition_test.py
	./tests/ethernet_lifecycle_audit_test.py
	./tests/physical_media_verifier_test.py
	./tests/uefi_driver_inspector_test.py

$(BUILD_DIR)/net-provider-efi.obj: src/net/provider.c include/openconsole/net_provider.h include/openconsole/net_protocol.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/uefi-net-backend-efi.obj: src/net/uefi_backend.c include/openconsole/uefi_net_backend.h include/openconsole/net_provider.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/net-stack-loader-efi.obj: src/net/stack_loader.c include/openconsole/net_stack_loader.h include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/update-metadata-efi.obj: src/net/update_metadata.c include/openconsole/update_metadata.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/update-verify-efi.obj: src/net/update_verify.c include/openconsole/update_verify.h include/openconsole/update_metadata.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/uefi-update-crypto-efi.obj: src/net/uefi_update_crypto.c include/openconsole/uefi_update_crypto.h include/openconsole/crypto_protocol.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/update-storage-efi.obj: src/net/update_storage.c include/openconsole/update_storage.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/uefi-update-storage-efi.obj: src/net/uefi_update_storage.c include/openconsole/uefi_update_storage.h include/openconsole/update_storage.h include/openconsole/crypto_protocol.h include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tls-ca-efi.obj: src/net/tls_ca.c include/openconsole/tls_ca.h include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/audio-provider-efi.obj: src/audio/provider.c include/openconsole/audio_provider.h include/openconsole/audio_protocol.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/audioio-backend-efi.obj: src/audio/audioio_backend.c include/openconsole/audioio_backend.h include/openconsole/audio_provider.h include/openconsole/uefi.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/manifest-check: tools/manifest-check.c src/core/manifest.c include/openconsole/manifest.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -Iinclude \
		tools/manifest-check.c src/core/manifest.c -o $@

check-examples: $(BUILD_DIR)/manifest-check
	./$(BUILD_DIR)/manifest-check examples/games/hello/game.toml
	./$(BUILD_DIR)/manifest-check examples/games/second/game.toml
	./$(BUILD_DIR)/manifest-check examples/games/gles/game.toml
	./$(BUILD_DIR)/manifest-check examples/games/integrated/game.toml
	./$(BUILD_DIR)/manifest-check examples/games/audio/game.toml
	./$(BUILD_DIR)/manifest-check examples/games/physical-network/game.toml
	./$(BUILD_DIR)/manifest-check examples/games/slice-installer/game.toml

$(BUILD_DIR)/manifest-efi.obj: src/core/manifest.c include/openconsole/manifest.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/game-storage-efi.obj: src/core/game_storage.c include/openconsole/game_storage.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/freestanding-memory-efi.obj: src/core/freestanding_memory.c | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/launch-recovery-efi.obj: src/core/launch_recovery.c include/openconsole/launch_recovery.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pe-image-efi.obj: src/core/pe_image.c include/openconsole/pe_image.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/platform-inventory-efi.obj: src/core/platform_inventory.c include/openconsole/platform_inventory.h include/openconsole/sha256.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/publisher-keys-efi.obj: src/core/publisher_keys.c include/openconsole/publisher_keys.h include/openconsole/sha256.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ocg2-efi.obj: src/core/ocg2.c include/openconsole/ocg2.h include/openconsole/sha256.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/authenticated-load-efi.obj: src/core/authenticated_load.c include/openconsole/authenticated_load.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/uefi-game-storage-efi.obj: src/core/uefi_game_storage.c include/openconsole/uefi_game_storage.h | $(BUILD_DIR)
	clang $(CFLAGS) -c $< -o $@

check-efi-core: $(DASHBOARD_OBJ)

check-efi-core: $(BUILD_DIR)/manifest-efi.obj $(CAPABILITIES_OBJ) $(BUILD_DIR)/game-storage-efi.obj $(BUILD_DIR)/launch-recovery-efi.obj $(BUILD_DIR)/pe-image-efi.obj $(BUILD_DIR)/gles-provider-efi.obj $(BUILD_DIR)/io-inventory-efi.obj $(BUILD_DIR)/io-mock-provider-efi.obj $(BUILD_DIR)/io-probe.obj $(BUILD_DIR)/network-probe.obj $(BUILD_DIR)/physical-network-probe.obj $(BUILD_DIR)/crypto-probe.obj $(BUILD_DIR)/integrated-io-game.obj $(BUILD_DIR)/net-provider-efi.obj $(BUILD_DIR)/uefi-net-backend-efi.obj $(BUILD_DIR)/net-stack-loader-efi.obj $(BUILD_DIR)/update-metadata-efi.obj $(BUILD_DIR)/update-verify-efi.obj $(BUILD_DIR)/uefi-update-crypto-efi.obj $(BUILD_DIR)/update-storage-efi.obj $(BUILD_DIR)/uefi-update-storage-efi.obj $(BUILD_DIR)/tls-ca-efi.obj $(BUILD_DIR)/audio-provider-efi.obj $(BUILD_DIR)/audioio-backend-efi.obj $(BUILD_DIR)/publisher-keys-efi.obj $(BUILD_DIR)/ocg2-efi.obj $(BUILD_DIR)/authenticated-load-efi.obj $(BUILD_DIR)/uefi-game-storage-efi.obj
	@for object in $^; do \
		test -z "$$($(OBJDUMP) -t $$object | awk '$$4 == "*UND*" { print $$NF }')" || \
		{ echo "$$object has unresolved symbols" >&2; exit 1; }; \
	done

test-ovmf: test-esp
	./scripts/test-ovmf.sh

test-game-storage-ovmf-matrix:
	./scripts/ci-development-image-test.sh
	./scripts/test-game-storage-ovmf-matrix.sh

test-runtime-integrity-ovmf:
	./scripts/ci-development-image-test.sh
	$(MAKE) $(BUILD_DIR)/BOOTX64-DEVELOPMENT-MUTATION-TEST.EFI \
		$(BUILD_DIR)/BOOTX64-DEVELOPMENT-WATCHDOG-TEST.EFI \
		$(BUILD_DIR)/HANG-PROBE.EFI PLATFORM_KEY=$(BUILD_DIR)/ci-platform-key.pem
	./scripts/test-runtime-integrity-ovmf.sh

test-dashboard-ovmf: test-esp
	OPENCONSOLE_TEST_SCENE=dashboard OPENCONSOLE_TEST_ESP=test-esp ./scripts/test-ovmf.sh

test-pointer-pagination-ovmf: pointer-pagination-test-esp ovmf-pointer
	OPENCONSOLE_OVMF_CODE=$(abspath $(BUILD_DIR)/ovmf-pointer/OVMF_CODE.fd) \
		OPENCONSOLE_OVMF_VARS=$(abspath $(BUILD_DIR)/ovmf-pointer/OVMF_VARS.fd) \
		OPENCONSOLE_POINTER_DISPLAY=none OPENCONSOLE_TEST_SCENE=pointer-pagination \
		OPENCONSOLE_TEST_ESP=pointer-pagination-test-esp ./scripts/test-ovmf.sh

test-game-details-ovmf: game-details-test-esp ovmf-pointer
	OPENCONSOLE_OVMF_CODE=$(abspath $(BUILD_DIR)/ovmf-pointer/OVMF_CODE.fd) \
		OPENCONSOLE_OVMF_VARS=$(abspath $(BUILD_DIR)/ovmf-pointer/OVMF_VARS.fd) \
		OPENCONSOLE_POINTER_DISPLAY=none OPENCONSOLE_TEST_SCENE=game-details \
		OPENCONSOLE_TEST_PACKAGED_NETWORK=1 OPENCONSOLE_TEST_ESP=game-details-test-esp \
		./scripts/test-ovmf.sh

test-gles-ovmf: gles-test-esp
	OPENCONSOLE_TEST_SCENE=gles OPENCONSOLE_TEST_ESP=gles-test-esp ./scripts/test-ovmf.sh

test-io-ovmf: io-test-esp
	OPENCONSOLE_TEST_SCENE=io OPENCONSOLE_TEST_ESP=io-test-esp ./scripts/test-ovmf.sh

test-real-net-ovmf: real-net-test-esp
	OPENCONSOLE_TEST_SCENE=network OPENCONSOLE_TEST_ESP=real-net-test-esp ./scripts/test-ovmf.sh

test-network-fixture-ovmf: network-fixture-test-esp
	OPENCONSOLE_TEST_SCENE=network-fixture OPENCONSOLE_TEST_ESP=network-fixture-test-esp ./scripts/test-ovmf.sh

test-packaged-network-fixture-ovmf: packaged-network-fixture-test-esp ovmf-snp-only
	OPENCONSOLE_TEST_SCENE=network-fixture OPENCONSOLE_TEST_ESP=network-fixture-test-esp \
	OPENCONSOLE_TEST_PACKAGED_NETWORK=1 \
	OPENCONSOLE_OVMF_CODE=$(abspath $(BUILD_DIR)/ovmf-snp-only/OVMF_CODE.fd) \
	OPENCONSOLE_OVMF_VARS=$(abspath $(BUILD_DIR)/ovmf-snp-only/OVMF_VARS.fd) \
	./scripts/test-ovmf.sh

test-physical-network-ovmf: physical-network-test-esp ovmf-snp-only
	OPENCONSOLE_TEST_SCENE=physical-network OPENCONSOLE_TEST_ESP=physical-network-test-esp \
	OPENCONSOLE_TEST_PACKAGED_NETWORK=1 \
	OPENCONSOLE_OVMF_CODE=$(abspath $(BUILD_DIR)/ovmf-snp-only/OVMF_CODE.fd) \
	OPENCONSOLE_OVMF_VARS=$(abspath $(BUILD_DIR)/ovmf-snp-only/OVMF_VARS.fd) \
	./scripts/test-ovmf.sh

test-physical-inventory-ovmf: physical-test-package
	OPENCONSOLE_TEST_SCENE=physical-inventory OPENCONSOLE_TEST_ESP=esp \
	OPENCONSOLE_TEST_NETWORK=none ./scripts/test-ovmf.sh

test-audio-ovmf: audio-test-esp
	OPENCONSOLE_TEST_SCENE=audio OPENCONSOLE_TEST_ESP=audio-test-esp OPENCONSOLE_TEST_NETWORK=none ./scripts/test-ovmf.sh

test-audio-no-output-ovmf: audio-test-esp
	OPENCONSOLE_TEST_SCENE=audio-no-output OPENCONSOLE_TEST_ESP=audio-test-esp OPENCONSOLE_TEST_NETWORK=none ./scripts/test-ovmf.sh

test-integrated-io-ovmf: integrated-io-test-esp
	OPENCONSOLE_TEST_SCENE=integrated OPENCONSOLE_TEST_ESP=integrated-io-test-esp ./scripts/test-ovmf.sh

test-slice-ovmf: slice-test-esp
	OPENCONSOLE_TEST_SCENE=slice OPENCONSOLE_TEST_ESP=slice-test-esp ./scripts/test-ovmf.sh

test-crypto-ovmf: crypto-test-esp
	OPENCONSOLE_TEST_SCENE=crypto OPENCONSOLE_TEST_NETWORK=none OPENCONSOLE_TEST_ESP=crypto-test-esp ./scripts/test-ovmf.sh

test-update-storage-ovmf: update-storage-test-esp
	OPENCONSOLE_TEST_SCENE=update-storage OPENCONSOLE_TEST_NETWORK=none OPENCONSOLE_TEST_ESP=update-storage-test-esp ./scripts/test-ovmf.sh

test-update-e2e-ovmf: update-e2e-test-esp ovmf-snp-only
	OPENCONSOLE_TEST_SCENE=update-e2e OPENCONSOLE_TEST_ESP=update-e2e-test-esp \
	OPENCONSOLE_TEST_PACKAGED_NETWORK=1 \
	OPENCONSOLE_OVMF_CODE=$(abspath $(BUILD_DIR)/ovmf-snp-only/OVMF_CODE.fd) \
	OPENCONSOLE_OVMF_VARS=$(abspath $(BUILD_DIR)/ovmf-snp-only/OVMF_VARS.fd) \
	./scripts/test-ovmf.sh

test-https-ovmf: https-test-esp ovmf-snp-only
	OPENCONSOLE_TEST_SCENE=https OPENCONSOLE_TEST_ESP=https-test-esp \
	OPENCONSOLE_TEST_PACKAGED_NETWORK=1 \
	OPENCONSOLE_OVMF_CODE=$(abspath $(BUILD_DIR)/ovmf-snp-only/OVMF_CODE.fd) \
	OPENCONSOLE_OVMF_VARS=$(abspath $(BUILD_DIR)/ovmf-snp-only/OVMF_VARS.fd) \
	./scripts/test-ovmf.sh

test-offline-ovmf: real-net-test-esp
	OPENCONSOLE_TEST_SCENE=offline OPENCONSOLE_TEST_NETWORK=none OPENCONSOLE_TEST_ESP=real-net-test-esp ./scripts/test-ovmf.sh

test-virtio-gpu: test-esp
	OPENCONSOLE_TEST_GPU=virtio-gl ./scripts/test-ovmf.sh

test: test-host check-examples check-efi-core test-ovmf

run: esp
	./scripts/run-ovmf.sh

clean:
	rm -rf -- $(BUILD_DIR)
