# G1 launcher lifecycle (historical prototype)

This records the pre-OCG2 loose `games/` milestone and is not the current
installation contract. See `game-package-lifecycle.md` and
`boot-volume-format-v1.md` for the enforced transactional layout.

The current OVMF implementation performs this bounded path:

1. Resolve `EFI_LOADED_IMAGE_PROTOCOL` from the parent image handle.
2. Resolve `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL` from that image's device handle and
   open its root volume. No disk index is assumed.
3. Enumerate immediate directories beneath `games/`, read each `game.toml` with
   a 64 KiB limit, and pass it to the shared allocation-free parser.
4. Optionally read `jacket.ocj` with a 1 MiB limit.
5. CPU-compose the home screen into a BGRA buffer and present it using GOP
   `Blt` with `EfiBltBufferToVideo`.
6. Wait on the firmware console keyboard event. Arrow scan codes change the
   selection and Enter launches it.
7. Read the selected child image with a 16 MiB limit, call `LoadImage` from the
   source buffer, then `StartImage`.
8. The sample child fills GOP, waits on the same firmware console keyboard, and
   returns normally. The parent redraws its home screen and reports success.

The automated test controls QEMU over QMP. It waits for firmware debug markers,
captures the GOP framebuffer, injects Right and Enter into the parent and `x`
into the child, then requires proof that selection changed, the child received
the key, and the parent resumed. This demonstrates keyboard handoff in OVMF; it is not yet evidence for
pointer input or physical firmware behavior.

Current limitations include a maximum of eight accepted games, ASCII manifest
entry paths, one conventional `jacket.ocj` filename, no scrolling layout, no
pointer input, and no restoration of a child-selected GOP mode.
