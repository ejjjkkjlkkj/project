#!/usr/bin/env python3
from __future__ import annotations

from generate_units import (
    DIGIT_UNITS, LETTER_UNITS, WORD_NAME_STRIDE, WORD_UNIT_STRIDE,
    WORD_UNITS, compact_voice_clip, convert, load_source, load_voicecore,
)

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
        | {unit for seq in WORD_UNITS.values() for unit in seq}
    )
    missing = sorted(required - set(source_units))
    assert not missing, f"missing native speech units: {missing}"

    assert set(LETTER_UNITS) == set("abcdefghijklmnopqrstuvwxyz")
    assert set(DIGIT_UNITS) == set("0123456789")
    assert max(map(len, LETTER_UNITS.values())) <= 8
    assert max(map(len, DIGIT_UNITS.values())) <= 8
    assert len(WORD_UNITS) >= 35
    assert {"boot","security","configuration","password","network","save","exit"} <= set(WORD_UNITS)
    assert len(WORD_UNITS) == len(set(WORD_UNITS))
    assert max(map(len, WORD_UNITS)) < WORD_NAME_STRIDE
    assert max(map(len, WORD_UNITS.values())) <= WORD_UNIT_STRIDE
    assert all(word.isascii() and word.islower() for word in WORD_UNITS)

    converted = {name: convert(source_units[name], speech.SAMPLE_RATE) for name in sorted(required)}
    assert all(data for data in converted.values())
    assert all(len(data) % 4 == 0 for data in converted.values()), "PCM must be stereo s16le"
    bank_bytes = sum(map(len, converted.values()))
    assert bank_bytes <= MAX_BANK_BYTES, (bank_bytes, MAX_BANK_BYTES)

    silence = converted["sil"]
    assert silence == bytes(len(silence)), "converted silence must remain digital zero"

    voicecore = load_voicecore()
    word_clips = {
        word: compact_voice_clip(voicecore.synthesize(word, "screen"))
        for word in sorted(WORD_UNITS)
    }
    assert all(word_clips.values())
    assert all(len(set(clip)) > 16 for clip in word_clips.values()), "word clips must contain speech, not tones/silence"
    word_bank_bytes = sum(map(len, word_clips.values()))
    assert word_bank_bytes <= 8 * 1024 * 1024
    assert len(word_clips["ready"]) > 1000

    letter_clips = {
        ch: compact_voice_clip(voicecore.synthesize(voicecore.LETTER_NAMES[ch], "screen"))
        for ch in "abcdefghijklmnopqrstuvwxyz"
    }
    digit_clips = {
        ch: compact_voice_clip(voicecore.synthesize(voicecore.DIGITS[ch], "screen"))
        for ch in "0123456789"
    }
    assert all(letter_clips.values()) and all(digit_clips.values())
    assert all(len(set(clip)) > 16 for clip in letter_clips.values())
    assert all(len(set(clip)) > 16 for clip in digit_clips.values())
    letter_bank_bytes = sum(map(len, letter_clips.values()))
    digit_bank_bytes = sum(map(len, digit_clips.values()))

    print(f"source-rate={speech.SAMPLE_RATE}")
    print(f"unit-count={len(converted)}")
    print(f"bank-bytes={bank_bytes}")
    print(f"word-pcm-bank-bytes={word_bank_bytes}")
    print(f"letter-pcm-bank-bytes={letter_bank_bytes}")
    print(f"digit-pcm-bank-bytes={digit_bank_bytes}")
    print(f"word-lexicon-count={len(WORD_UNITS)}")
    print("VOICE_NAVIGATION_CONTRACT=PASS")


if __name__ == "__main__":
    main()
