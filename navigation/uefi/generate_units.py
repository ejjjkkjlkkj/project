#!/usr/bin/env python3
from __future__ import annotations
import hashlib, importlib.util, struct, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'voice'/'uefi_units'/'build_native_units.py'
LETTER_UNITS={
 # Clear fallback spelling for arbitrary firmware labels. The previous map
 # treated each grapheme as a raw phoneme, which made unknown HII labels sound
 # like fused pseudo-words. Here each grapheme expands to a short French letter
 # name; the EFI runtime already inserts a gap between graphemes and can
 # interrupt the DMA stream immediately when focus moves.
 'a':('a',),
 'b':('b','e'),
 'c':('s','e'),
 'd':('d','e'),
 'e':('e',),
 'f':('e','f'),
 'g':('zh','e'),
 'h':('a','sh'),
 'i':('i',),
 'j':('zh','i'),
 'k':('k','a'),
 'l':('e','l'),
 'm':('e','m'),
 'n':('e','n'),
 'o':('o',),
 'p':('p','e'),
 'q':('k','u'),
 'r':('e','r'),
 's':('e','s'),
 't':('t','e'),
 'u':('u',),
 'v':('v','e'),
 'w':('d','u','b','l','e','v','e'),
 'x':('i','k','s'),
 'y':('i','g','r','e','k'),
 'z':('z','e','d'),
}

# BIOS labels contain semantic digits everywhere: IPv4/IPv6, USB 3, TPM 2.0,
# Boot Option #1, BIOS versions and timeout values. Preserve them instead of
# dropping them during prompt normalization.
DIGIT_UNITS={
 '0':('z','e','r','o'),
 '1':('eu','n'),
 '2':('d','eu'),
 '3':('t','r','w','a'),
 '4':('k','a','t','r'),
 '5':('s','e','n','k'),
 '6':('s','i','s'),
 '7':('s','e','p','t'),
 '8':('w','i','t'),
 '9':('n','eu','f'),
}

# Compact semantic pronunciation lexicon for recurring firmware words. These
# entries reuse the same first-party allophone bank, so they add negligible
# runtime footprint compared with pre-rendered whole-word PCM. Unknown words
# still fall back to deterministic French letter-name spelling.
WORD_UNITS={
 'advanced':('a','d','v','a','n','s','t'),
 'access':('a','k','s','e','s'),
 'action':('a','k','sh','on'),
 'asus':('a','s','u','s'),
 'back':('b','a','k'),
 'bios':('b','i','o','s'),
 'button':('b','a','t','on'),
 'change':('sh','e','n','zh'),
 'checked':('sh','e','k','t'),
 'choice':('sh','o','i','s'),
 'conditional':('k','on','d','i','sh','on','a','l'),
 'cpu':('s','e','p','e','u'),
 'details':('d','i','t','e','l','s'),
 'down':('d','a','w','n'),
 'editable':('e','d','i','t','a','b','l'),
 'edits':('e','d','i','t','s'),
 'enter':('e','n','t','e','r'),
 'escape':('e','s','k','e','p'),
 'firmware':('f','e','r','m','w','e','r'),
 'flash':('f','l','a','sh'),
 'for':('f','o','r'),
 'help':('e','l','p'),
 'left':('l','e','f','t'),
 'main':('m','e','n'),
 'move':('m','u','v'),
 'no':('n','o'),
 'not':('n','o','t'),
 'nvme':('e','n','v','e','e','m','e'),
 'only':('o','n','l','i'),
 'pending':('p','e','n','d','i','n','g'),
 'position':('p','o','z','i','s','i','on'),
 'press':('p','r','e','s'),
 'preview':('p','r','i','v','i','u'),
 'ready':('r','e','d','i'),
 'right':('r','i','t'),
 'sata':('s','a','t','a'),
 'setup':('s','e','t','u','p'),
 'smart':('s','m','a','r','t'),
 'stack':('s','t','a','k'),
 'tpm':('t','e','p','e','e','m'),
 'up':('a','p'),
 'usb':('u','e','s','b','e'),
 'value':('v','a','l','u'),
 'administrator':('a','d','m','i','n','i','s','t','r','a','t','o','r'),
 'boot':('b','u','t'),
 'changes':('sh','e','n','zh','e','s'),
 'configuration':('k','on','f','i','g','u','r','a','s','i','on'),
 'control':('k','on','t','r','o','l'),
 'default':('d','e','f','o','l','t'),
 'delete':('d','i','l','i','t'),
 'device':('d','i','v','a','i','s'),
 'disabled':('d','i','s','e','b','l','d'),
 'discard':('d','i','s','k','a','r','d'),
 'enabled':('e','n','e','b','l','d'),
 'exit':('e','k','s','i','t'),
 'fast':('f','a','s','t'),
 'information':('i','n','f','o','r','m','a','s','i','on'),
 'interface':('i','n','t','e','r','f','e','s'),
 'internal':('i','n','t','e','r','n','a','l'),
 'management':('m','a','n','a','zh','m','e','n','t'),
 'memory':('m','e','m','o','r','i'),
 'mode':('m','o','d'),
 'network':('n','e','t','w','o','r','k'),
 'open':('o','p','e','n'),
 'option':('o','p','s','i','on'),
 'password':('p','a','s','w','o','r','d'),
 'priority':('p','r','i','o','r','i','t','i'),
 'processor':('p','r','o','s','e','s','o','r'),
 'recovery':('r','i','k','a','v','e','r','i'),
 'restore':('r','e','s','t','o','r'),
 'save':('s','e','v'),
 'secure':('s','e','k','u','r'),
 'security':('s','i','k','u','r','i','t','i'),
 'settings':('s','e','t','i','n','g','s'),
 'storage':('s','t','o','r','a','zh'),
 'support':('s','u','p','o','r','t'),
 'system':('s','i','s','t','e','m'),
 'trusted':('t','r','u','s','t','e','d'),
 'user':('u','z','e','r'),
 'utility':('u','t','i','l','i','t','i'),
 'version':('v','e','r','zh','on'),
 'wake':('w','e','k'),
}

WORD_NAME_STRIDE=16
WORD_UNIT_STRIDE=24

def load_source():
    spec=importlib.util.spec_from_file_location('qevarynx_native_speech_source',SOURCE)
    if spec is None or spec.loader is None:
        raise SystemExit('cannot load native speech source')
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def convert(raw: bytes, source_rate: int) -> bytes:
    if source_rate <= 0 or 48000 % source_rate:
        raise SystemExit(f'unsupported source sample rate: {source_rate}')
    factor=48000//source_rate
    out=bytearray()
    for i,sample in enumerate(raw):
        a=max(-32768,min(32767,(sample-128)*180))
        nxt=raw[i+1] if i+1 < len(raw) else sample
        b=max(-32768,min(32767,(nxt-128)*180))
        for phase in range(factor):
            signed=a + ((b-a)*phase)//factor
            out += struct.pack('<hh',signed,signed)
    return bytes(out)

def arr_u8(name, values, cols=16):
    lines=[]
    for i in range(0,len(values),cols):
        lines.append('    '+', '.join(str(x) for x in values[i:i+cols])+',')
    return f'const unsigned char {name}[] = {{\n'+'\n'.join(lines)+'\n};\n'

def arr_u32(name, values, cols=8):
    lines=[]
    for i in range(0,len(values),cols):
        lines.append('    '+', '.join(str(x)+'u' for x in values[i:i+cols])+',')
    return f'const unsigned int {name}[] = {{\n'+'\n'.join(lines)+'\n};\n'

def main():
    if len(sys.argv)!=3:
        raise SystemExit('usage: generate_units.py OUTPUT_C METADATA')
    out=Path(sys.argv[1]); meta=Path(sys.argv[2])
    speech=load_source()
    if speech.SAMPLE_RATE < 16000:
        raise SystemExit(f'UEFI speech source rate too low for intelligibility: {speech.SAMPLE_RATE}')
    names=sorted(
        {'sil'}
        | {u for seq in LETTER_UNITS.values() for u in seq}
        | {u for seq in DIGIT_UNITS.values() for u in seq}
        | {u for seq in WORD_UNITS.values() for u in seq}
    )
    source_units=speech.make_units()
    converted={n:convert(source_units[n], speech.SAMPLE_RATE) for n in names}
    offsets=[]; lengths=[]; bank=bytearray()
    for n in names:
        offsets.append(len(bank)); lengths.append(len(converted[n])); bank += converted[n]
    if len(bank) > 128*4096-0x1000:
        raise SystemExit(f'unit bank too large: {len(bank)}')
    index={n:i for i,n in enumerate(names)}
    counts=[]; flat=[]
    for ch in 'abcdefghijklmnopqrstuvwxyz':
        seq=LETTER_UNITS[ch]
        if len(seq)>8: raise SystemExit('letter unit fanout too large')
        counts.append(len(seq))
        row=[index[u] for u in seq] + [0]*(8-len(seq))
        flat.extend(row)

    digit_counts=[]; digit_flat=[]
    for ch in '0123456789':
        seq=DIGIT_UNITS[ch]
        if len(seq)>8: raise SystemExit('digit unit fanout too large')
        digit_counts.append(len(seq))
        row=[index[u] for u in seq] + [0]*(8-len(seq))
        digit_flat.extend(row)

    word_names=sorted(WORD_UNITS)
    word_name_lens=[]; word_name_flat=[]
    word_unit_counts=[]; word_unit_flat=[]
    for word in word_names:
        encoded=word.encode('ascii')
        seq=WORD_UNITS[word]
        if len(encoded)>=WORD_NAME_STRIDE:
            raise SystemExit(f'word name too long: {word}')
        if len(seq)>WORD_UNIT_STRIDE:
            raise SystemExit(f'word unit fanout too large: {word}')
        word_name_lens.append(len(encoded))
        word_name_flat.extend(encoded)
        word_name_flat.extend([0]*(WORD_NAME_STRIDE-len(encoded)))
        word_unit_counts.append(len(seq))
        word_unit_flat.extend(index[u] for u in seq)
        word_unit_flat.extend([0]*(WORD_UNIT_STRIDE-len(seq)))
    lines=[
        '/* Generated deterministically from first-party native speech units. */',
        arr_u8('qev_unit_bank',list(bank)),
        f'const unsigned int qev_unit_bank_len = {len(bank)}u;\n',
        arr_u32('qev_unit_off',offsets),
        arr_u32('qev_unit_len',lengths),
        f'const unsigned int qev_unit_count = {len(names)}u;\n',
        f'const unsigned int qev_sil_unit_index = {index["sil"]}u;\n',
        arr_u8('qev_letter_unit_count',counts),
        arr_u8('qev_letter_units',flat),
        arr_u8('qev_digit_unit_count',digit_counts),
        arr_u8('qev_digit_units',digit_flat),
        f'const unsigned int qev_word_count = {len(word_names)}u;\n',
        f'const unsigned int qev_word_name_stride = {WORD_NAME_STRIDE}u;\n',
        f'const unsigned int qev_word_unit_stride = {WORD_UNIT_STRIDE}u;\n',
        arr_u8('qev_word_name_len',word_name_lens),
        arr_u8('qev_word_names',word_name_flat),
        arr_u8('qev_word_unit_count',word_unit_counts),
        arr_u8('qev_word_units',word_unit_flat),
    ]
    out.write_text('\n'.join(lines))
    meta.write_text(
        'OS-UEFI-HII-GRAPH-PROMPT-SPEECH-UNITS-V1\n'
        'source=voice/uefi_units/build_native_units.py\n'
        f'source-sha256={hashlib.sha256(SOURCE.read_bytes()).hexdigest()}\n'
        f'unit-names={",".join(names)}\n'
        f'unit-count={len(names)}\n'
        f'bank-bytes={len(bank)}\n'
        f'bank-sha256={hashlib.sha256(bank).hexdigest()}\n'
        'letter-map=a-z-french-letter-names\n'
        'digit-map=0-9-french-number-names\n'
        'max-input-graphemes=64\n'
        'max-units-per-letter=8\n'
        f'word-lexicon-count={len(word_names)}\n'
        f'word-name-stride={WORD_NAME_STRIDE}\n'
        f'word-unit-stride={WORD_UNIT_STRIDE}\n'
        'source-sample-rate-hz='+str(speech.SAMPLE_RATE)+'\n'
        'inter-letter-silence-ms=18-runtime-gap\n'
        'intra-word-phoneme-silence-ms=0\n'
        'word-silence-ms=70\n'
        'speech-mode=hybrid-word-formant-fr-v5\n'
        'full-utterance-asset=false\n'
    )
    print('HII_GRAPH_PROMPT_UNIT_GENERATION=PASS')
    print('UNIT_COUNT='+str(len(names)))
    print('BANK_BYTES='+str(len(bank)))
    print('WORD_LEXICON_COUNT='+str(len(word_names)))

if __name__=='__main__':
    main()
