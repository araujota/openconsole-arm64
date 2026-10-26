#!/usr/bin/env python3
import argparse
import json
import pathlib
import re
import subprocess


def require(text, needle, label):
    if needle not in text:
        raise RuntimeError(f"missing AudioDxe cleanup invariant: {label}")
    return label


def ordered(text, first, second, label):
    if text.find(first) < 0 or text.find(second) < 0 or text.find(first) >= text.find(second):
        raise RuntimeError(f"invalid AudioDxe cleanup ordering: {label}")
    return label


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--patch", action="append", required=True)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    root = pathlib.Path(args.source)
    patches = [pathlib.Path(value).resolve() for value in args.patch]
    binary = pathlib.Path(args.binary)
    if not binary.is_file() or binary.stat().st_size == 0:
        raise RuntimeError("packaged AudioDxe binary is absent or empty")
    head = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    for patch in patches:
        reverse = subprocess.run(
            ["git", "-C", str(root), "apply", "--reverse", "--check", str(patch)],
            capture_output=True, text=True)
        if reverse.returncode != 0:
            raise RuntimeError(f"reviewed AudioDxe patch is not applied: {patch}")

    mem = (root / "Staging/AudioDxe/HdaController/HdaControllerMem.c").read_text()
    io = (root / "Staging/AudioDxe/HdaController/HdaControllerHdaIo.c").read_text()
    controller = (root / "Staging/AudioDxe/HdaController/HdaController.c").read_text()
    codec = (root / "Staging/AudioDxe/HdaCodec/HdaCodec.c").read_text()
    codec_io = (root / "Staging/AudioDxe/HdaCodec/HdaCodecAudioIo.c").read_text()
    checks = []
    for size in ("HDA_BDL_SIZE", "HDA_STREAM_BUF_SIZE", "HdaDev->DmaPositionsSize"):
        checks.append(require(mem, f"if (PciLengthActual != {size})", f"reject short mapping for {size}"))
    checks += [
        require(mem, "gBS->CloseEvent (HdaStream->PollTimer)", "close each stream timer"),
        require(mem, "PciIo->Unmap (PciIo, HdaStream->BufferListMapping)", "unmap BDL"),
        require(mem, "PciIo->Unmap (PciIo, HdaStream->BufferDataMapping)", "unmap PCM buffer"),
        require(mem, "PciIo->Unmap (PciIo, HdaDev->DmaPositionsMapping)", "unmap DMA positions"),
        require(mem, "HdaControllerSetStreamState (HdaStream, FALSE)", "stop streams during cleanup"),
        require(controller, "HdaControllerReset (HdaControllerDev, FALSE)", "reset controller during cleanup"),
        require(controller, "UninstallProtocolInterface", "uninstall controller protocols"),
        require(codec, "clean Audio I/O", "uninstall AudioIo during codec cleanup"),
        ordered(codec, "if (EFI_ERROR (Status)) {\n    goto FREE_POOLS;",
                "HdaCodecDev->AudioIoData      = AudioIoData;",
                "publish codec pointers only after protocol install"),
        ordered(controller, "if (EFI_ERROR (Status)) {\n    FreePool (HdaControllerInfoData);",
                "HdaControllerDev->HdaControllerInfoData = HdaControllerInfoData;",
                "publish controller pointer only after protocol install"),
        ordered(io, "Status = gBS->SetTimer (HdaStream->PollTimer, TimerCancel, 0);",
                "HdaControllerSetStreamState (HdaStream, FALSE)",
                "attempt DMA stop even when timer cancellation fails"),
        require(io, "HdaStream->BufferActive         = FALSE;", "clear active stream state"),
        require(codec_io, "AudioIoPrivateData = AUDIO_IO_PRIVATE_DATA_FROM_THIS (This);",
                "resolve playback setup from the selected AudioIo instance"),
    ]
    for unsafe_cache in ("mOutputIndexMask", "mGain", "mFreq", "mBits", "mChannels"):
        if re.search(rf"\b{re.escape(unsafe_cache)}\b", codec_io):
            raise RuntimeError(f"cross-codec playback setup cache remains: {unsafe_cache}")
    checks.append("no process-global playback setup cache across AudioIo instances")
    failure_cleanup_calls = controller.count("goto FREE_CONTROLLER;") + codec.count("goto FREE_CODEC;")
    if failure_cleanup_calls < 10:
        raise RuntimeError("driver start failure paths no longer converge on cleanup")
    report = {
        "schema": 1,
        "scope": "pinned OpenCore AudioDxe source failure-path audit; not physical output evidence",
        "opencore_commit": head,
        "patches": [str(patch) for patch in patches],
        "binary": str(binary.resolve()),
        "binary_bytes": binary.stat().st_size,
        "failure_paths_converging_on_cleanup": failure_cleanup_calls,
        "verified_invariants": checks,
    }
    output = pathlib.Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"PASS: audited {len(checks)} AudioDxe teardown invariants across {failure_cleanup_calls} failure paths")


if __name__ == "__main__":
    main()
