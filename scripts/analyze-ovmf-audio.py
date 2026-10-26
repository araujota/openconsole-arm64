#!/usr/bin/env python3
import json
import math
import struct
import sys
import wave


def rms(values):
    return math.sqrt(sum(value * value for value in values) / max(1, len(values)))


def main():
    if len(sys.argv) not in (3, 4):
        print("usage: analyze-ovmf-audio.py CAPTURE.wav METRICS.json [audio|integrated]", file=sys.stderr)
        return 2
    wav_path, metrics_path = sys.argv[1:3]
    mode = sys.argv[3] if len(sys.argv) == 4 else "audio"
    with wave.open(wav_path, "rb") as stream:
        channels = stream.getnchannels()
        width = stream.getsampwidth()
        rate = stream.getframerate()
        frames = stream.getnframes()
        raw = stream.readframes(frames)
    if (channels != 2 or width != 2 or rate != 48000):
        raise RuntimeError(f"unexpected WAV format: {channels}ch/{width * 8}bit/{rate}Hz")
    samples = struct.unpack(f"<{len(raw) // 2}h", raw)
    left = samples[0::2]
    right = samples[1::2]
    duration = frames / rate
    if not 0.25 <= duration <= (10.0 if mode == "integrated" else 1.25):
        raise RuntimeError(f"unexpected captured duration {duration:.3f}s")

    peak_left = max(abs(value) for value in left)
    peak_right = max(abs(value) for value in right)
    if mode == "integrated":
        if not 2500 <= peak_left <= 3500 or peak_right < 9000:
            raise RuntimeError(f"integrated capture lacks centered music/right effect: L={peak_left}, R={peak_right}")
        active_left = sum(1 for value in left if abs(value) >= 2500)
        active_right = sum(1 for value in right if abs(value) >= 2500)
        if active_left < rate // 10 or active_right < rate // 10:
            raise RuntimeError("integrated capture has insufficient sustained activity")
        metrics = {
            "schema": 1,
            "scope": "integrated QEMU graphics/input/network/audio child",
            "channels": channels,
            "sample_bits": width * 8,
            "sample_rate_hz": rate,
            "captured_frames": frames,
            "captured_duration_seconds": duration,
            "peak_left": peak_left,
            "peak_right": peak_right,
            "active_left_samples": active_left,
            "active_right_samples": active_right,
            "note": "QEMU WAV is host-paced; duration is VM evidence, not physical latency evidence.",
        }
        with open(metrics_path, "w", encoding="utf-8") as stream:
            json.dump(metrics, stream, indent=2, sort_keys=True)
            stream.write("\n")
        print(f"PASS: integrated WAV {duration:.3f}s, peaks L/R {peak_left}/{peak_right}")
        return 0

    bin_frames = rate // 50
    bins = []
    for start in range(0, frames, bin_frames):
        end = min(frames, start + bin_frames)
        bins.append((start / rate, rms(left[start:end]), rms(right[start:end])))
    active = [entry for entry in bins if max(entry[1], entry[2]) > 2000]
    centered = [entry for entry in active if abs(entry[1] - entry[2]) < 500]
    overlap = [entry for entry in active if entry[1] > entry[2] * 1.5 and entry[2] > 2500]
    silent = [entry for entry in bins if entry[1] < 100 and entry[2] < 100]
    if len(centered) < 4 or not overlap or len(silent) < 3:
        raise RuntimeError("capture lacks centered music, left-panned overlap, or silence")
    if not any(entry[0] < 0.12 for entry in silent):
        raise RuntimeError("capture lacks the leading underrun/silence interval")
    if not any(entry[0] > duration * 0.55 for entry in silent) or not any(
            entry[0] > duration * 0.70 for entry in centered):
        raise RuntimeError("capture lacks silence and later underrun recovery")

    rising = []
    for index in range(1, len(right)):
        if right[index - 1] <= 0 < right[index] and abs(right[index]) >= 3000:
            rising.append(index)
    periods = [second - first for first, second in zip(rising, rising[1:])
               if 50 <= second - first <= 200]
    frequency = rate / (sum(periods) / len(periods)) if periods else 0.0
    if not 400.0 <= frequency <= 480.0:
        raise RuntimeError(f"music frequency outside tolerance: {frequency:.2f}Hz")
    if peak_left < 9000 or not 3500 <= peak_right <= 4500:
        raise RuntimeError(f"unexpected channel peaks: L={peak_left}, R={peak_right}")

    metrics = {
        "schema": 1,
        "scope": "QEMU ich9-intel-hda + hda-output + wav backend",
        "channels": channels,
        "sample_bits": width * 8,
        "sample_rate_hz": rate,
        "captured_frames": frames,
        "captured_duration_seconds": duration,
        "music_frequency_hz": frequency,
        "peak_left": peak_left,
        "peak_right": peak_right,
        "centered_bins": len(centered),
        "overlap_bins": len(overlap),
        "silent_bins": len(silent),
        "note": "QEMU WAV is host-paced; duration is recorded as VM evidence, not physical latency evidence.",
    }
    with open(metrics_path, "w", encoding="utf-8") as stream:
        json.dump(metrics, stream, indent=2, sort_keys=True)
        stream.write("\n")
    print(f"PASS: WAV {duration:.3f}s, {frequency:.1f}Hz, peaks L/R {peak_left}/{peak_right}, silence and recovery")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
