#!/usr/bin/env python3
from __future__ import annotations

from generate_units import DIGIT_UNITS, LETTER_UNITS, convert, load_source

MAX_BANK_BYTES = 128 * 4096 - 0x1000


def main() -> None:
    speech = load_source()
    assert speech.SAMPLE_RATE > 0
    assert 48000 % speech.SAMPLE_RATE == 0

    source_units = speech.make_units()
    required = (
        {"sil"}
        | {unit for seq in LETTER_UNITS.values() for unit in seq}
        | {unit for seq in DIGIT_UNITS.values() for unit in seq}
    )
    missing = sorted(required - set(source_units))
    assert not missing, f"missing native speech units: {missing}"

    assert set(LETTER_UNITS) == set("abcdefghijklmnopqrstuvwxyz")
    assert set(DIGIT_UNITS) == set("0123456789")
    assert max(map(len, LETTER_UNITS.values())) <= 8
    assert max(map(len, DIGIT_UNITS.values())) <= 8

    converted = {name: convert(source_units[name], speech.SAMPLE_RATE) for name in sorted(required)}
    assert all(data for data in converted.values())
    assert all(len(data) % 4 == 0 for data in converted.values()), "PCM must be stereo s16le"
    bank_bytes = sum(map(len, converted.values()))
    assert bank_bytes <= MAX_BANK_BYTES, (bank_bytes, MAX_BANK_BYTES)

    silence = converted["sil"]
    assert silence == bytes(len(silence)), "converted silence must remain digital zero"

    print(f"source-rate={speech.SAMPLE_RATE}")
    print(f"unit-count={len(converted)}")
    print(f"bank-bytes={bank_bytes}")
    print("VOICE_NAVIGATION_CONTRACT=PASS")


if __name__ == "__main__":
    main()
