#!/usr/bin/env python3
"""Validate physical AudioIo lifecycle evidence and optional captured output."""

import argparse
import hashlib
import json
import math
import struct
import wave
from pathlib import Path


def runs(text: str) -> list[list[str]]:
    collected: list[list[str]] = []
    current: list[str] | None = None
    for line in text.replace("\x00", "").splitlines():
        if line == "OPENCONSOLE_PHYSICAL_AUDIO_RUN_BEGIN":
            current = [line]
        elif current is not None:
            current.append(line)
            if line == "OPENCONSOLE_PHYSICAL_AUDIO_RUN_COMPLETE":
                collected.append(current)
                current = None
    return collected


def value(lines: list[str], prefix: str) -> str | None:
    return next((line[len(prefix):] for line in lines if line.startswith(prefix)), None)


def rms(values) -> float:
    return math.sqrt(sum(value * value for value in values) / max(1, len(values)))


def capture_window(channel, rate: int, onset: int, start: float, end: float):
    first = max(0, onset + round(start * rate))
    last = min(len(channel), onset + round(end * rate))
    return channel[first:last]


def analyze_capture(path: Path) -> tuple[dict, list[str]]:
    failures: list[str] = []
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    with wave.open(str(path), "rb") as stream:
        channels = stream.getnchannels()
        width = stream.getsampwidth()
        rate = stream.getframerate()
        frames = stream.getnframes()
        raw = stream.readframes(frames)
    metrics = {
        "path": str(path), "sha256": digest, "channels": channels,
        "sample_bits": width * 8, "sample_rate_hz": rate,
        "captured_frames": frames,
        "captured_duration_seconds": frames / rate if rate else 0.0,
    }
    if channels != 2 or width != 2 or rate != 48000:
        failures.append("capture must be 48-kHz signed 16-bit stereo PCM WAV")
        return metrics, failures
    samples = struct.unpack(f"<{len(raw) // 2}h", raw)
    left, right = samples[0::2], samples[1::2]
    bin_frames = rate // 100
    bins = [(start, max(rms(left[start:start + bin_frames]),
                        rms(right[start:start + bin_frames])))
            for start in range(0, frames - bin_frames + 1, bin_frames)]
    maximum = max((level for _, level in bins), default=0.0)
    threshold = max(100.0, maximum * 0.10)
    active = [start for start, level in bins if level >= threshold]
    if not active:
        failures.append("capture contains no reference signal")
        return metrics, failures
    onset = active[0]
    windows = {
        "leading_silence": (-0.20, -0.05),
        "centered_music": (0.08, 0.28),
        "left_effect_overlap": (0.38, 0.60),
        "middle_silence": (1.05, 1.20),
        "recovered_music": (1.30, 1.48),
    }
    measured = {}
    for name, (start, end) in windows.items():
        l_values = capture_window(left, rate, onset, start, end)
        r_values = capture_window(right, rate, onset, start, end)
        measured[name] = {"left_rms": rms(l_values), "right_rms": rms(r_values),
                          "frames": min(len(l_values), len(r_values))}
    metrics["detected_music_onset_seconds"] = onset / rate
    metrics["windows"] = measured
    music_l = measured["centered_music"]["left_rms"]
    music_r = measured["centered_music"]["right_rms"]
    overlap_l = measured["left_effect_overlap"]["left_rms"]
    overlap_r = measured["left_effect_overlap"]["right_rms"]
    silence = max(measured["leading_silence"]["left_rms"],
                  measured["leading_silence"]["right_rms"],
                  measured["middle_silence"]["left_rms"],
                  measured["middle_silence"]["right_rms"])
    recovered = min(measured["recovered_music"]["left_rms"],
                    measured["recovered_music"]["right_rms"])
    if measured["leading_silence"]["frames"] < round(0.14 * rate):
        failures.append("capture must begin before the reference signal")
    if measured["recovered_music"]["frames"] < round(0.16 * rate):
        failures.append("capture ends before underrun recovery")
    if min(music_l, music_r) < max(200.0, silence * 6.0):
        failures.append("centered music is not sufficiently above captured silence/noise")
    if min(music_l, music_r) == 0 or max(music_l, music_r) / min(music_l, music_r) > 2.0:
        failures.append("centered music channel balance exceeds 6 dB")
    if overlap_l < overlap_r * 1.20 or overlap_l < music_l * 1.10:
        failures.append("left-panned effect overlap was not captured")
    if not 0.65 <= overlap_r / max(1.0, music_r) <= 1.45:
        failures.append("right music channel changed unexpectedly during overlap")
    if recovered < max(200.0, silence * 6.0):
        failures.append("music did not recover after the middle silence")
    peak = max((abs(value) for value in samples), default=0)
    metrics["peak_absolute"] = peak
    if peak >= 32767:
        failures.append("capture clips at full scale")
    crossings = sum(1 for a, b in zip(
        capture_window(right, rate, onset, 0.08, 0.28),
        capture_window(right, rate, onset, 0.08, 0.28)[1:]) if a <= 0 < b)
    music_frequency = crossings / 0.20
    metrics["music_frequency_hz"] = music_frequency
    if not 400.0 <= music_frequency <= 480.0:
        failures.append(f"captured music frequency is {music_frequency:.1f} Hz, expected about 440 Hz")
    metrics["passed"] = not failures
    return metrics, failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--connector", required=True,
                        help="physical connector used, e.g. motherboard rear line-out")
    parser.add_argument("--observation", required=True,
                        help="independent listening/capture observation")
    parser.add_argument("--capture-wav", type=Path,
                        help="external 48-kHz stereo PCM recording of a probe run")
    parser.add_argument("--inventory-report", type=Path, required=True,
                        help="JSON emitted by analyze-physical-inventory.py")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    parsed = runs(args.evidence.read_bytes().decode("ascii", "ignore"))
    failures: list[str] = []
    selected_runs = [run for run in parsed
                     if value(run, "OPENCONSOLE_PHYSICAL_AUDIO_SELECTED_") is not None]
    if len(selected_runs) < 2:
        failures.append("at least two complete explicitly selected child runs are required")
    selected: list[str] = []
    all_outputs: set[str] = set()
    for run in parsed:
        outputs = [line.split("_", 5)[-1] for line in run
                   if line.startswith("OPENCONSOLE_PHYSICAL_AUDIO_OUTPUT_")
                   and not line.startswith("OPENCONSOLE_PHYSICAL_AUDIO_OUTPUT_COUNT_")]
        all_outputs.update(outputs)
    for index, run in enumerate(selected_runs):
        choice = value(run, "OPENCONSOLE_PHYSICAL_AUDIO_SELECTED_")
        if choice is not None:
            selected.append(choice.split("_", 1)[-1])
            analog_device = any(f" {device} " in choice
                                for device in ("line", "speaker", "headphones"))
            if "1022:15E3" not in choice or not analog_device:
                failures.append(f"run {index + 1} did not select AMD 1022:15E3 analog")
        frames = value(run, "OPENCONSOLE_PHYSICAL_AUDIO_FRAMES_CONSUMED_")
        if frames is None or int(frames) < 86016:
            failures.append(f"run {index + 1} did not drain the reference mix")
    if selected and len(set(selected)) != 1:
        failures.append("repeated runs selected different outputs")
    hdmi = sorted(name for name in all_outputs if " hdmi " in name)
    usb = sorted(name for name in all_outputs if name.startswith("USB Audio "))
    analog = sorted(name for name in all_outputs
                    if any(f" {device} " in name
                           for device in ("line", "speaker", "headphones")))
    if not hdmi:
        failures.append("no separately enumerated HDMI output was recorded")
    inventory = json.loads(args.inventory_report.read_text(encoding="utf-8"))
    usb_discovered = bool(inventory.get("decisions", {}).get(
        "usb_audio_17e9_6006_discovered"))
    if not usb_discovered:
        failures.append("USB Audio 17e9:6006 was not separately discovered in UEFI inventory")
    if not args.connector.strip() or not args.observation.strip():
        failures.append("connector and independent observation must be non-empty")
    capture = None
    if args.capture_wav:
        try:
            capture, capture_failures = analyze_capture(args.capture_wav)
            failures.extend(capture_failures)
        except (OSError, EOFError, wave.Error, struct.error) as error:
            failures.append(f"capture WAV could not be analyzed: {error}")

    report = {
        "schema": 1,
        "scope": "physical UEFI AudioIo child lifecycle",
        "passed": not failures,
        "complete_runs": len(parsed),
        "explicitly_selected_runs": len(selected_runs),
        "selected_outputs": selected,
        "analog_outputs": analog,
        "hdmi_outputs": hdmi,
        "usb_audio_outputs": usb,
        "usb_audio_17e9_6006_discovered": usb_discovered,
        "usb_audio_playable": bool(usb),
        "connector": args.connector,
        "independent_observation": args.observation,
        "observation_kind": "objective-capture" if capture is not None else "subjective-listening",
        "external_capture": capture,
        "failures": failures,
    }
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    if failures:
        print("FAIL: physical audio evidence is incomplete")
        return 1
    print("PASS: repeated AMD analog playback with separate HDMI discovery" +
          (" and analyzed external capture" if capture is not None else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
