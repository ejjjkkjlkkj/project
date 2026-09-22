#!/usr/bin/env python3
from __future__ import annotations

import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
BASELINE = HERE / "profiles" / "asus_m1603qa_308.json"
SRC = HERE / "hii_graph_prompt_speech_uefi.c"

data = json.loads(BASELINE.read_text(encoding="utf-8"))
source = SRC.read_text(encoding="utf-8")

assert data["schema"] == "m1603qa-308-ifr-baseline-v1"
assert data["source_zip_sha256"] == "cf9f3058709626b42f51aff11eecb4d482d431906fe5dc76e1c31b017ad00eed"
assert data["bios_sha256"] == "12a932605d3a1ca35dd5023b14a0c7fdc2f8713465655265795f0f9799aaa0de"
assert data["bios_bytes"] == 16779264
assert data["formset_guid"] == "7b59104a-c00d-4158-87ff-f04d6396a915"
assert data["package_list_guid"] == "899407d7-99fe-43d8-9a21-79ec328cac21"
assert data["string_count"] == 1336
assert data["varstore_count"] == 48
assert data["form_count"] == 63
assert data["control_count"] == 791
assert data["max_controls_per_form"] == 106
assert len(data["forms"]) == data["form_count"]
assert sum(f["control_count"] for f in data["forms"]) == data["control_count"]

forms = {(f["form_id"], f["title"]): f["control_count"] for f in data["forms"]}
for key, expected in {
    (10000, "Setup"): 11,
    (10007, "Advanced"): 13,
    (10008, "Boot"): 9,
    (10009, "Security"): 12,
    (10030, "Secure Boot"): 11,
    (10031, "Key Management"): 11,
    (10124, "SATA Configuration"): 41,
    (10012, "Save & Exit"): 106,
}.items():
    assert forms[key] == expected, (key, forms.get(key), expected)

required = data["required_controls"]
for prompt in (
    "SVM Mode",
    "ASUS EZ Flash 3 Utility",
    "Trusted Computing",
    "NVMe Configuration",
    "Fast Boot",
    "Administrator Password",
    "Secure Boot",
    "Key Management",
):
    assert prompt in required and required[prompt], prompt

m = re.search(r"#define\s+MAX_HII_NAV_PROMPTS\s+(\d+)", source)
assert m, "MAX_HII_NAV_PROMPTS missing"
assert int(m.group(1)) >= data["max_controls_per_form"], (
    int(m.group(1)),
    data["max_controls_per_form"],
)

for marker in (
    "HII_GRAPH_NAV_PROFILE=M1603QA_BIOS_308",
    "HII_GRAPH_NAV_PACKAGE_GUID_MATCH=PASS",
    "HII_GRAPH_NAV_ROOT_FORM_2710=PASS",
    "HII_GRAPH_NAV_FORM_AWARE=PASS",
    "HII_GRAPH_NAV_SETUP_FORMSET=PASS",
    "HII_GRAPH_NAV_FORM_LOAD=PASS",
    "HII_GRAPH_NAV_FORM_ENTER=PASS",
    "HII_GRAPH_NAV_FORM_BACK=PASS",
    "HII_GRAPH_NAV_READ_ONLY_ACTION=BLOCKED",
):
    assert marker in source, marker

for contract in (
    "g_m1603qa_308_setup_package_list_guid",
    "guid_bytes_equal",
    "nav_package_is_setup",
    "nav_load_form",
    "g_nav_ref_form_ids",
    "g_nav_form_history",
    "key.unicode_char == 0x000du",
):
    assert contract in source, contract

print("M1603QA_308_IFR_BASELINE=PASS")
print("M1603QA_308_FORM_CAPACITY=PASS")
print("M1603QA_308_READ_ONLY_FORM_NAVIGATION=PASS")
