@echo -off
map -r
bcfg boot add 0 fs2:\EFI\BOOT\BOOTX64.EFI "Normal host"
bcfg boot add 1 fs1:\EFI\BOOT\BOOTX64.EFI "OpenConsole acceptance"
fs0:\arm-next.efi
