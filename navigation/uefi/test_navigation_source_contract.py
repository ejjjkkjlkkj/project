#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

SRC = Path(__file__).with_name("hii_graph_prompt_speech_uefi.c")
text = SRC.read_text(encoding="utf-8")

required = (
    'HII_GRAPH_NAV_REALTIME_MODE=INTERRUPTIBLE_DMA',
    'HII_GRAPH_NAV_DIRECTIONAL_ALIASES=PASS',
    'HII_GRAPH_NAV_TAB_FORWARD=PASS',
    'HII_GRAPH_NAV_SPEECH_INTERRUPT=PASS',
    'key.scan_code == 0x0001u',
    'key.scan_code == 0x0002u',
    'key.scan_code == 0x0003u',
    'key.scan_code == 0x0004u',
    'key.scan_code == 0x0005u',
    'key.scan_code == 0x0006u',
    'key.scan_code == 0x0009u',
    'key.scan_code == 0x000au',
    'key.unicode_char == 0x0009u',
)
missing = [needle for needle in required if needle not in text]
assert not missing, f"missing navigation contract markers: {missing}"

mask_start = text.index("#define NAV_REQUIRED_MASK")
mask_end = text.index("#endif", mask_start)
mask = text[mask_start:mask_end]
for required_name in (
    "NAV_SEEN_UP",
    "NAV_SEEN_DOWN",
    "NAV_SEEN_R",
    "NAV_SEEN_HOME",
    "NAV_SEEN_END",
    "NAV_SEEN_PAGE_UP",
    "NAV_SEEN_PAGE_DOWN",
):
    assert required_name in mask, required_name

# New ergonomic aliases must not weaken or silently replace the established
# physical-proof key set.
assert "LEFT" not in mask
assert "RIGHT" not in mask
assert "TAB" not in mask

# This repository evolves only voice/navigation. Guard the source contract from
# accidentally taking ownership of the preserved boot payload.
assert "BOOTX64.EFI" not in text
assert "KERNEL.BIN" not in text

print("SCREEN_READER_NAVIGATION_SOURCE_CONTRACT=PASS")
