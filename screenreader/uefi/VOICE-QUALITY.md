# UEFI quality voice bank

The screen reader accepts native 48 kHz, mono, signed PCM16 units. No
resampling, neural inference, network request, OS speech API or codec decoder is
required in firmware.

The pack_voice_bank.py tool converts a JSON manifest and WAV masters into a
freestanding C bank consumed by sr_voice_stream.c. The packer rejects wrong
sample rates, stereo files, clipping, excessive DC offset and oversized units.

Recommended bank layers, in priority order:

1. Complete high-frequency UEFI phrases and state announcements.
2. Roles and common firmware terms.
3. Numbers, hexadecimal, units and punctuation.
4. French letter names a-z for deterministic spelling fallback.

The runtime always prefers the longest lexical unit on token boundaries, then
falls back to character units. New focus cancels the current unit at the audio
sink, so a long phrase can never trap the user.

Quality acceptance is external to firmware: blind A/B tests, intelligibility at
high speech rates, no clipping, stable loudness, and measured first-audio and
cancellation latency. A claim of matching or exceeding another screen reader
requires those measurements; it is not inferred from architecture.
