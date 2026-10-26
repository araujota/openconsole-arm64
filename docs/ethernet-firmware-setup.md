# Ethernet firmware setup hints

These paths help operators expose a firmware SNP/NII provider before starting
OpenConsole. They are navigation hints, not compatibility evidence. Record the
exact board revision, firmware version, setting values, and the launcher's
firmware-only inventory in the coverage matrix after every run. Do not enable
legacy/CSM boot; OpenConsole is a UEFI application.

Use a connected Ethernet cable, enable the onboard LAN controller, enable the
UEFI **Network Stack**, then enable **IPv4 PXE Support** when that separate
switch exists. IPv6 and HTTP Boot are not required by OpenConsole. Save changes,
cold boot the USB target, and retain both the pre-driver and post-driver reports.

| Firmware family | Typical setup path | Setting names to try | Evidence status |
| --- | --- | --- | --- |
| ASUS UEFI | `F7` Advanced Mode → Advanced → Network Stack Configuration | Network Stack; IPv4 PXE Support | Vendor-documented hint; validate exact model/version |
| MSI Click BIOS | `Del` → `F7` Advanced → Settings → Advanced → Network Stack Configuration | Network Stack; IPv4 PXE Support | Vendor-documented hint; X670E physical state not yet captured |
| Gigabyte UEFI | Advanced Mode → Settings or Advanced → Network Stack | Network Stack; IPv4 PXE Support | Vendor-documented family hint; validate exact model/version |
| ASRock AMI UEFI | Advanced → Network Stack Configuration | Network Stack; IPv4 PXE Support | Vendor-documented family hint; validate exact model/version |
| Dell UEFI | Connection or System Configuration → Network Controller Configuration | Integrated NIC Enabled/Enabled with PXE; Enable UEFI Network Stack | Vendor-documented family hint; validate exact model/version |
| HP Computer Setup | Advanced or Boot Options → Network/PXE options | UEFI Network Boot; IPv4 PXE | Unverified navigation hint; capture labels before use |
| Lenovo UEFI | Config → Network, or Startup → Network Boot | UEFI IPv4 Network Stack/PXE | Unverified navigation hint; use Lenovo BIOS Simulator for exact product |

If the names are absent, do not infer that Ethernet is unsupported. Capture the
firmware version and an inventory run with settings unchanged. Conversely, a PXE
entry appearing in the boot menu proves neither usable SNP nor packet I/O.

Primary references: [ASUS BIOS manual](https://dlcdnets.asus.com/pub/ASUS/mb/13MANUAL/E25207_Intel_800_Series_BIOS_manual_EM_WEB.pdf),
[MSI AM5 BIOS manual](https://download.msi.com/archive/mnu_exe/mb/AMDAM5BIOS.pdf),
[ASRock BIOS manual](https://download.asrock.com/Manual/W680D4U-2L2TG5.pdf),
[Gigabyte BIOS manual](https://download.gigabyte.com/FileList/Manual/server_manual_r161-r12_e_10.pdf),
[Dell UEFI network settings](https://www.dell.com/support/manuals/en-lb/optiplex-7000-sff/optiplex7000_sff_sm/connection?guid=guid-70880f82-ded5-464c-bde8-9421181e1deb&lang=en-us), and
[Lenovo UEFI navigation](https://download.lenovo.com/manual/thinkpad_l14g5_l16g1/ug/html_en/en/Navigate_the_UEFI_BIOS_menu.html).
