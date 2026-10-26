#!/usr/bin/env python3
import math
import sys
import wave

RATE = 48000
SEGMENT = 4800


def rms(samples):
    return math.sqrt(sum(value * value for value in samples) / len(samples))


def frequency(samples):
    crossings = sum(1 for a, b in zip(samples, samples[1:]) if a <= 0 < b)
    return crossings * RATE / len(samples)


def main():
    if len(sys.argv) != 2:
        return 2
    with wave.open(sys.argv[1], "rb") as stream:
        assert stream.getnchannels() == 2
        assert stream.getsampwidth() == 2
        assert stream.getframerate() == RATE
        assert stream.getnframes() == SEGMENT * 3 + 2400
        raw = stream.readframes(stream.getnframes())
    values = [int.from_bytes(raw[i:i + 2], "little", signed=True)
              for i in range(0, len(raw), 2)]
    left = values[0::2]
    right = values[1::2]
    segments = [(left[i:i + SEGMENT], right[i:i + SEGMENT])
                for i in range(0, SEGMENT * 3, SEGMENT)]
    assert 990 <= frequency(segments[0][0]) <= 1010
    assert rms(segments[0][0]) > 8000 and rms(segments[0][1]) == 0
    assert 990 <= frequency(segments[1][0]) <= 1010
    assert 490 <= frequency(segments[1][1]) <= 510
    assert rms(segments[1][0]) > 8000 and rms(segments[1][1]) > 6000
    assert rms(segments[2][0]) == 0 and 490 <= frequency(segments[2][1]) <= 510
    assert rms(left[SEGMENT * 3:]) == 0 and rms(right[SEGMENT * 3:]) == 0
    assert max(abs(value) for value in values) <= 32767
    print("PASS: golden WAV duration, pitch, stereo placement, overlap, and silence")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
