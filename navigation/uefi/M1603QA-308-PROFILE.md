# ASUS VivoBook M1603QA BIOS 308 — HII navigation profile

This profile is derived from the supplied `M1603QAAS.308` firmware image.

- Firmware image SHA-256: `12a932605d3a1ca35dd5023b14a0c7fdc2f8713465655265795f0f9799aaa0de`
- Setup FFS/package GUID: `899407d7-99fe-43d8-9a21-79ec328cac21`
- Main FormSet GUID: `7b59104a-c00d-4158-87ff-f04d6396a915`
- Extracted forms: 63
- Extracted VarStores: 48
- Extracted English strings: 1336

## Primary ASUS notebook forms

| FormId | Title | Confirmed controls |
|---|---|---|
| `0x2716` | Main | BIOS information, vendor/version, VBIOS/EC, processor, memory, system information, serial, MAC/asset when exposed, date, time, access level |
| `0x2717` | Advanced | Internal Pointing Device, Fn Lock, Wake On Lid Open, SVM Mode, MyASUS auto-install mechanism, ASUS EZ Flash 3, SMART, Network Stack, USB, Trusted Computing, SATA, NVMe |
| `0x2718` | Boot | Fast Boot, Boot Option Priorities, Boot Option #N, Add New Boot Option, Delete Boot Option |
| `0x2719` | Security | Administrator/User password state and entry, I/O Interface Security, Secure Boot |
| `0x271A` | Save & Exit | Save/discard, defaults, Boot Override, EFI Shell |
| `0x271B` | Save & Exit | Save/discard/reset variants, user defaults, Boot Override |

## Confirmed referenced forms

- `0x2743` SMART Settings
- `0x2727` Network Stack Configuration
- `0x2728` USB Configuration
- Trusted Computing form
- SATA Configuration form
- `0x2760` NVMe Configuration
- I/O Interface Security form
- Secure Boot form

## Important runtime rule

The firmware uses `SuppressIf`, `GrayOutIf`, and `DisableIf` extensively. The screen reader must therefore retain IFR scope metadata and must not treat the static package order as equivalent to the currently visible firmware UI.

The native navigation engine tracks:

- FormId
- QuestionId
- conditional scope flags
- semantic IFR role
- prompt/help strings
- structural navigation across controls and forms

The next correctness gate is evaluating conditional expressions against live VarStores so hidden/disabled controls can be filtered according to the current firmware state.
