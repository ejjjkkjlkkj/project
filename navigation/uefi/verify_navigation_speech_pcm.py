#!/usr/bin/env python3
"""Verify exact PCM for completed UEFI navigation speech.

This proves emitted content, not human intelligibility. It pairs the F1, Down
and Up runtime speech strings from the serial log with the actual QEMU WAV,
reproduces the firmware's <=32-character word-boundary chunking, and requires
every captured chunk to be bit-identical and ordered after the startup phrase.

Consecutive DMA chunks contain deliberate leading/trailing zero PCM. Those
silence regions can overlap as byte patterns in a continuous capture, so search
ordering advances to the end of each chunk's non-silent core rather than to the
end of its trailing silence. The complete expected chunk must still match.
"""
from __future__ import annotations

import hashlib
import json
import sys
import wave
from pathlib import Path

import verify_speech_pcm as speech_pcm

KEY_MARKERS = {
    "HII_GRAPH_NAV_KEY=F1": "F1",
    "HII_GRAPH_NAV_KEY=DOWN": "DOWN",
    "HII_GRAPH_NAV_KEY=UP": "UP",
}
EXPECTED_KEYS = ("F1", "DOWN", "UP")
MAX_PHRASE = 64
MAX_CHUNK = 32
PCM_FRAME_BYTES = 4
ZERO_FRAME = bytes(PCM_FRAME_BYTES)


def split_runtime_phrase(text: str) -> list[str]:
    if not text or len(text) > MAX_PHRASE:
        raise ValueError("runtime navigation phrase must be 1..64 characters")

    chunks: list[str] = []
    offset = 0
    length = len(text)
    while offset < length:
        start = offset
        while start < length and text[start] == " ":
            start += 1
        if start >= length:
            break

        remaining = length - start
        take = min(remaining, MAX_CHUNK)
        if remaining > MAX_CHUNK:
            split = take
            while split > 0 and text[start + split] != " ":
                split -= 1
            if split >= 8:
                take = split

        while take and text[start + take - 1] == " ":
            take -= 1
        if not take:
            raise ValueError("runtime chunker produced an empty chunk")

        chunks.append(text[start : start + take])
        offset = start + take
        while offset < length and text[offset] == " ":
            offset += 1

    if not chunks:
        raise ValueError("runtime chunker produced no chunks")
    return chunks


def parse_navigation_speech(serial_path: Path) -> list[dict[str, str]]:
    pending: str | None = None
    found: dict[str, str] = {}
    for raw_line in serial_path.read_text(
        encoding="utf-8", errors="ignore"
    ).splitlines():
        line = raw_line.strip()
        key = KEY_MARKERS.get(line)
        if key is not None:
            pending = key
            continue
        prefix = "HII_GRAPH_NAV_SPEECH_TEXT="
        if pending and line.startswith(prefix):
            if pending not in found:
                found[pending] = line[len(prefix) :]
            pending = None

    missing = [key for key in EXPECTED_KEYS if key not in found]
    if missing:
        raise ValueError(f"missing runtime speech for keys: {','.join(missing)}")
    return [{"key": key, "text": found[key]} for key in EXPECTED_KEYS]


def wav_bytes(path: Path) -> tuple[dict[str, int | str], bytes]:
    with wave.open(str(path), "rb") as wav:
        params: dict[str, int | str] = {
            "channels": wav.getnchannels(),
            "sample_width_bytes": wav.getsampwidth(),
            "sample_rate_hz": wav.getframerate(),
            "compression": wav.getcomptype(),
            "frames": wav.getnframes(),
        }
        raw = wav.readframes(wav.getnframes())
    return params, raw


def outer_silence_bytes(pcm: bytes) -> tuple[int, int]:
    if len(pcm) % PCM_FRAME_BYTES:
        raise ValueError("expected PCM is not frame-aligned")
    start = 0
    end = len(pcm)
    while start < end and pcm[start : start + PCM_FRAME_BYTES] == ZERO_FRAME:
        start += PCM_FRAME_BYTES
    while end > start and pcm[end - PCM_FRAME_BYTES : end] == ZERO_FRAME:
        end -= PCM_FRAME_BYTES
    if start == end:
        raise ValueError("speech chunk contains no non-silent PCM")
    return start, len(pcm) - end


def find_frame_aligned(raw: bytes, expected: bytes, start: int) -> int:
    pos = raw.find(expected, start)
    while pos >= 0 and pos % PCM_FRAME_BYTES:
        pos = raw.find(expected, pos + 1)
    return pos


def main() -> int:
    if len(sys.argv) not in (3, 4):
        print(
            "usage: verify_navigation_speech_pcm.py CAPTURE.wav SERIAL.log "
            "[REPORT.json]",
            file=sys.stderr,
        )
        return 2

    wav_path = Path(sys.argv[1])
    serial_path = Path(sys.argv[2])
    report_path = Path(sys.argv[3]) if len(sys.argv) == 4 else None

    events = parse_navigation_speech(serial_path)
    params, raw = wav_bytes(wav_path)
    format_ok = (
        params["channels"] == 2
        and params["sample_width_bytes"] == 2
        and params["sample_rate_hz"] == 48000
        and params["compression"] == "NONE"
    )

    discovery = speech_pcm.render_runtime_pcm(speech_pcm.DISCOVERY_TEXT)
    discovery_exact = raw.startswith(discovery)
    cursor = len(discovery) if discovery_exact else 0
    chunk_results: list[dict[str, object]] = []
    all_exact = format_ok and discovery_exact

    for event in events:
        chunks = split_runtime_phrase(event["text"])
        for chunk_index, chunk in enumerate(chunks):
            expected = speech_pcm.render_runtime_pcm(chunk)
            leading_silence, trailing_silence = outer_silence_bytes(expected)
            core_bytes = len(expected) - leading_silence - trailing_silence
            offset = (
                find_frame_aligned(raw, expected, cursor)
                if all_exact
                else -1
            )
            exact = offset >= 0
            core_end = (
                offset + leading_silence + core_bytes
                if exact
                else -1
            )
            chunk_results.append(
                {
                    "key": event["key"],
                    "phrase": event["text"],
                    "chunk_index": chunk_index,
                    "chunk": chunk,
                    "expected_bytes": len(expected),
                    "expected_sha256": hashlib.sha256(expected).hexdigest(),
                    "leading_silence_bytes": leading_silence,
                    "non_silent_core_bytes": core_bytes,
                    "trailing_silence_bytes": trailing_silence,
                    "capture_offset_bytes": offset,
                    "ordered_core_end_bytes": core_end,
                    "bit_identical": exact,
                }
            )
            if not exact:
                all_exact = False
                break

            # Do not skip a legitimate following chunk whose leading silence
            # begins inside this chunk's trailing all-zero region.
            cursor = core_end
        if not all_exact:
            break

    report = {
        "scope": "COMPLETED_F1_DOWN_UP_NAVIGATION_SPEECH",
        "ordering_rule": "FULL_CHUNK_EXACT_MATCH_ADVANCE_BY_NON_SILENT_CORE",
        "wav": params,
        "discovery_prefix_exact": discovery_exact,
        "events": events,
        "chunks": chunk_results,
        "format_ok": format_ok,
        "full_navigation_speech_content": "PASS" if all_exact else "FAIL",
        "intelligibility": "NOT_TESTED",
    }
    if report_path:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    print(f"NAVIGATION_SPEECH_PCM_FORMAT={'PASS' if format_ok else 'FAIL'}")
    print(
        "NAVIGATION_DISCOVERY_PREFIX="
        + ("PASS" if discovery_exact else "FAIL")
    )
    print("NAVIGATION_SPEECH_KEYS=F1,DOWN,UP")
    print(
        "FULL_NAVIGATION_SPEECH_CONTENT="
        + ("PASS" if all_exact else "FAIL")
    )
    print("INTELLIGIBILITY=NOT_TESTED")
    return 0 if all_exact else 1


if __name__ == "__main__":
    raise SystemExit(main())
