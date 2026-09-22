#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

SRC = Path(__file__).with_name("hii_graph_prompt_speech_uefi.c")
text = SRC.read_text(encoding="utf-8")

required = (
    'HII_GRAPH_NAV_REALTIME_MODE=INTERRUPTIBLE_DMA',
    'HII_GRAPH_NAV_DIRECTIONAL_ALIASES=PASS',
    'HII_GRAPH_NAV_TAB_FORWARD=PASS',
    'HII_GRAPH_NAV_CONTEXT_HELP=PASS',
    'HII_GRAPH_NAV_CONTEXT_HELP_SPEECH=PASS',
    'HII_GRAPH_NAV_SPEECH_INTERRUPT=PASS',
    'HII_GRAPH_NAV_STRUCTURAL_KEYS=PASS',
    'HII_GRAPH_NAV_FORM_KEYS=PASS',
    'HII_GRAPH_NAV_FORM_ID_TRACKING=PASS',
    'HII_GRAPH_NAV_QUESTION_ID_TRACKING=PASS',
    'HII_GRAPH_NAV_WHERE_AM_I_KEY=PASS',
    'HII_GRAPH_NAV_WHERE_AM_I=PASS',
    "key.unicode_char == (u16)'w'",
    "key.unicode_char == (u16)'f'",
    "key.unicode_char == (u16)'b'",
    "key.unicode_char == (u16)'x'",
    "key.unicode_char == (u16)'c'",
    "key.unicode_char == (u16)'e'",
    'NAV_GROUP_BUTTON',
    'NAV_GROUP_CHECKBOX',
    'NAV_GROUP_CHOICE',
    'NAV_GROUP_EDITABLE',
    'nav_find_group',
    'nav_find_form',
    'MAX_HII_NAV_PROMPTS 240',
    'g_nav_prompt_overflow',
    'HII_GRAPH_NAV_PROFILE=M1603QA_BIOS_308',
    'HII_GRAPH_NAV_PACKAGE_GUID_MATCH=PASS',
    'HII_GRAPH_NAV_ROOT_FORM_2710=PASS',
    'HII_GRAPH_NAV_M1603QA_ROOT_FILTER=PASS',
    'HII_GRAPH_NAV_FORM_AWARE=PASS',
    'HII_GRAPH_NAV_FORM_LOAD=PASS',
    'HII_GRAPH_NAV_FORM_ENTER=PASS',
    'HII_GRAPH_NAV_FORM_BACK=PASS',
    'HII_GRAPH_NAV_FORM_TITLE_SPEECH=PASS',
    'HII_GRAPH_NAV_READ_ONLY_ACTION=BLOCKED',
    'HII_GRAPH_NAV_SELF_REF_ACTION=BLOCKED',
    'HII_GRAPH_NAV_VARSTORE_PARSE=PASS',
    'HII_GRAPH_NAV_GET_VARIABLE=PASS',
    'HII_GRAPH_NAV_VALUE_READ=PASS',
    'nav_collect_varstores',
    'nav_control_value',
    'nav_one_of_value',
    'g_nav_values',
    'g_m1603qa_308_setup_package_list_guid',
    'guid_bytes_equal',
    'nav_package_is_setup',
    'nav_get_form_title',
    'nav_load_form',
    'g_nav_ref_form_ids',
    'g_nav_form_history',
    'key.unicode_char == 0x000du',
    "key.unicode_char == (u16)'h'",
    'u16 help_token = oplen >= 6u ? rd16(q + 4) : 0u',
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
