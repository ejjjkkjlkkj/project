#!/usr/bin/env python3
from __future__ import annotations

import json
import math
import subprocess
import sys
import tempfile
import wave
from pathlib import Path


def make_wav(path: Path, rate: int = 48_000, channels: int = 1) -> None:
    frames = 960
    samples = bytearray()
    for i in range(frames):
        v = int(5000 * math.sin(2 * math.pi * 220 * i / rate))
        for _ in range(channels):
            samples += int(v).to_bytes(2, "little", signed=True)
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(bytes(samples))


def main() -> None:
    here = Path(__file__).resolve().parent
    packer = here / "pack_voice_bank.py"
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        make_wav(root / "bonjour.wav")
        manifest = root / "voice.json"
        manifest.write_text(
            json.dumps({"units": [{"key": "bonjour", "wav": "bonjour.wav"}]}),
            encoding="utf-8",
        )
        out = root / "bank.c"
        p = subprocess.run(
            [sys.executable, str(packer), str(manifest), str(out)],
            text=True,
            capture_output=True,
            check=False,
        )
        assert p.returncode == 0, p.stderr
        text = out.read_text(encoding="utf-8")
        assert "sr_quality_voice_bank" in text
        assert '{"bonjour",' in text
        assert "960u" in text

        make_wav(root / "bad.wav", rate=44_100)
        manifest.write_text(
            json.dumps({"units": [{"key": "bad", "wav": "bad.wav"}]}),
            encoding="utf-8",
        )
        p = subprocess.run(
            [sys.executable, str(packer), str(manifest), str(out)],
            text=True,
            capture_output=True,
            check=False,
        )
        assert p.returncode != 0
        assert "expected 48000 Hz" in (p.stdout + p.stderr)

    print("UEFI_QUALITY_VOICE_PACKER_TEST=PASS")


if __name__ == "__main__":
    main()
