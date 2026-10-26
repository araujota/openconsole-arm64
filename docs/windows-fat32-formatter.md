# Windows large-FAT32 formatter provenance

`tools/oc-fat32-format.c` is an OpenConsole-owned, Apache-2.0 host utility. It
exists because Windows' built-in formatting UI and tools decline to create the
large FAT32 data volume required by split-layout suitcases. It does not parse or
modify an existing filesystem: it initializes an already-created, empty
partition with a FAT32 BPB, redundant FATs, FSInfo, volume label, and root
directory.

Release builds compile it as `oc-fat32-format.exe`, record its SHA-256 in the
installer payload manifest and SBOM, and pass its output through an independent
FAT checker. The PowerShell installer invokes it only after disk identity
confirmation, system-disk refusal, GPT initialization, and creation of the
specific data partition. The source—not an opaque downloaded executable—is the
redistribution authority.
