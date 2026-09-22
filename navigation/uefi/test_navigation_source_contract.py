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
    'HII_GRAPH_NAV_VARSTORE_METADATA=PASS',
    'HII_GRAPH_NAV_VARSTORE_CATALOG=PASS',
    'HII_GRAPH_NAV_ONEOF_OPTIONS=PASS',
    'HII_GRAPH_NAV_LIVE_OPTION_MATCH=PASS',
    'HII_GRAPH_NAV_LIVE_VALUE_KEY=PASS',
    'HII_GRAPH_NAV_LIVE_VALUE_READ=PASS',
    'HII_GRAPH_NAV_CONDITION_TRACKING=PASS',
    'HII_GRAPH_NAV_FORM_ID=0x',
    'HII_GRAPH_NAV_QUESTION_ID=0x',
    'HII_GRAPH_NAV_VARSTORE_ID=0x',
    'HII_GRAPH_NAV_VAR_INFO=0x',
    'HII_GRAPH_NAV_QUESTION_FLAGS=0x',
    'HII_GRAPH_NAV_CONDITION_FLAGS=0x',
    'HII_GRAPH_SPEECH_DIGITS=PASS',
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
    'NAV_COND_SUPPRESS',
    'NAV_COND_GRAY',
    'NAV_COND_DISABLE',
    'ifr_condition_flag',
    'scope_condition_stack',
    'qev_digit_unit_count',
    'qev_digit_units',
    'g_nav_prompt_varstore_ids',
    'g_nav_prompt_var_infos',
    'g_nav_prompt_question_flags',
    "(ch >= (u16)'0' && ch <= (u16)'9')",
    "key.unicode_char == (u16)'h'",
    "key.unicode_char == (u16)'v'",
    'nav_read_scalar_value',
    'nav_format_u64',
    'nav_option_add',
    'nav_option_for_value',
    'nav_option_value',
    'op == 0x09u',
    'nav_varstore_add',
    'op == 0x24u',
    'op == 0x26u',
    'get_variable_fn',
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
