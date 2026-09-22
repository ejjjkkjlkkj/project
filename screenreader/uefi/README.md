# UEFI Screen Reader — zero-base architecture

This directory is a new screen-reader runtime designed for firmware first. It
does not depend on Windows, Linux, UIA, AT-SPI, a network service, libc, heap
allocation, or the legacy monolithic navigation runtime.

## Runtime pipeline

1. **HII/IFR semantic model** — firmware controls become bounded semantic
   nodes with role, label, value, help and state.
2. **Navigation core** — focus movement, structural navigation, Where Am I,
   repeat, help, activation feedback and verbosity are deterministic.
3. **Interruptible speech ABI** — every focus event receives a generation
   token; a new focus cancels stale speech immediately.
4. **UEFI voice streamer** — prepared 48 kHz mono PCM units are streamed in
   small frame budgets. Whole-word/phrase units are preferred; unknown text
   falls back to letter units so a blind user is never left with silence.
5. **Hardware adapter** — the audio backend owns HDA/USB-audio details and
   exposes only write/flush/stop to the speech engine.

## Blind-first interaction contract

- Arrow keys: previous/next focus.
- Tab / Shift+Tab: next/previous focus.
- Home / End: first/last focus.
- Page Up / Page Down: move by page.
- Enter / Space: activate.
- R: repeat current announcement.
- H: contextual help.
- W: Where Am I.
- S or Escape: stop speech.
- B / Shift+B: next/previous control.
- E / Shift+E: next/previous edit.
- C / Shift+C: next/previous checkbox.
- X / Shift+X: next/previous choice.

## Voice quality direction

The firmware runtime deliberately does not run a multi-billion-parameter model.
Quality is obtained by preparing a first-party voice bank outside firmware and
playing high-quality phrase/word units unchanged in UEFI. The runtime keeps a
spelling fallback for arbitrary HII text. This gives predictable memory,
deterministic latency, no network dependency and immediate interruption.

The release target is not stated as “better than VoiceOver” until blind A/B
tests demonstrate it. The engineering gates are:

- first audible focus feedback target <= 50 ms after an already-initialized
  audio device receives a focus event;
- cancellation target <= one audio period;
- no clipped final phoneme;
- stable loudness and no buffer underruns;
- intelligible French UI terms, paths, acronyms, numbers and firmware values;
- complete keyboard operation without sight;
- recovery from unsupported controls or missing voice units without silence.

## Current implementation boundary

The new semantic core, keymap, HII IFR parser and interruptible PCM-unit
streamer are implemented here. The next hardware gate is a native UEFI HII
collector plus audio adapter using the existing proven machine-specific HDA
knowledge only as validation data, not as screen-reader architecture.
