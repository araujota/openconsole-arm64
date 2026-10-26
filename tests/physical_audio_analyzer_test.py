#!/usr/bin/env python3
import json
import math
import struct
import subprocess
import tempfile
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ANALYZER = ROOT / "scripts" / "analyze-physical-audio-evidence.py"

RUN = """OPENCONSOLE_PHYSICAL_AUDIO_RUN_BEGIN
OPENCONSOLE_PHYSICAL_AUDIO_OUTPUT_COUNT_2
OPENCONSOLE_PHYSICAL_AUDIO_OUTPUT_0_HDA 1022:15E3 rev 01 speaker port 00 loc 01 surf 00
OPENCONSOLE_PHYSICAL_AUDIO_OUTPUT_1_HDA 10DE:1AEB rev A1 hdmi port 00 loc 00 surf 00
OPENCONSOLE_PHYSICAL_AUDIO_SELECTED_0_HDA 1022:15E3 rev 01 speaker port 00 loc 01 surf 00
OPENCONSOLE_PHYSICAL_AUDIO_FRAMES_CONSUMED_86016
OPENCONSOLE_PHYSICAL_AUDIO_UNDERRUNS_0
OPENCONSOLE_PHYSICAL_AUDIO_RUN_COMPLETE
"""


def invoke(path: Path, output: Path, inventory: Path, capture=None) -> subprocess.CompletedProcess[str]:
    command = [str(ANALYZER), str(path), "--connector", "rear line-out",
                           "--observation", "captured stereo tone",
                           "--inventory-report", str(inventory), "--output", str(output)]
    if capture is not None:
        command += ["--capture-wav", str(capture)]
    return subprocess.run(command, text=True, capture_output=True)


def write_capture(path: Path, valid=True):
    rate = 48000
    samples = []
    for frame in range(rate * 11 // 5):
        time = frame / rate
        probe = time - 0.25
        music_on = (0 <= probe < 1.0) or (1.25 <= probe < 1.542)
        effect_on = 0.35 <= probe < 0.65
        music = 3000 * math.sin(2 * math.pi * 440 * probe) if music_on and valid else 0
        effect = 5000 * math.sin(2 * math.pi * 880 * probe) if effect_on and valid else 0
        samples.extend((round(music + effect), round(music)))
    with wave.open(str(path), "wb") as stream:
        stream.setnchannels(2); stream.setsampwidth(2); stream.setframerate(rate)
        stream.writeframes(struct.pack(f"<{len(samples)}h", *samples))


with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    evidence = temp / "audio.log"
    report = temp / "report.json"
    inventory = temp / "inventory.json"
    capture = temp / "capture.wav"
    write_capture(capture)
    inventory.write_text(json.dumps({"decisions": {"usb_audio_17e9_6006_discovered": True}}))
    evidence.write_bytes((RUN + RUN).encode() + b"\0" * 32)
    result = invoke(evidence, report, inventory, capture)
    assert result.returncode == 0, result.stdout + result.stderr
    parsed = json.loads(report.read_text())
    assert parsed["passed"] and parsed["complete_runs"] == 2
    assert len(parsed["hdmi_outputs"]) == 1
    assert parsed["usb_audio_outputs"] == []
    assert parsed["usb_audio_17e9_6006_discovered"] is True
    assert parsed["usb_audio_playable"] is False
    assert parsed["observation_kind"] == "objective-capture"
    assert parsed["external_capture"]["passed"]
    assert len(parsed["external_capture"]["sha256"]) == 64

    spdif = (RUN + RUN).replace(" speaker ", " spdif ")
    evidence.write_text(spdif)
    result = invoke(evidence, report, inventory, capture)
    assert result.returncode != 0
    assert "did not select AMD 1022:15E3 analog" in result.stdout

    write_capture(capture, valid=False)
    evidence.write_bytes((RUN + RUN).encode() + b"\0" * 32)
    result = invoke(evidence, report, inventory, capture)
    assert result.returncode != 0
    assert "contains no reference signal" in result.stdout

    evidence.write_text(RUN)
    result = invoke(evidence, report, inventory)
    assert result.returncode != 0
    assert "at least two complete explicitly selected" in result.stdout

print("physical audio analyzer tests passed")
