#include "sr_voice_stream.h"

static sr_u32 v_strlen(const char *s) {
    sr_u32 n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static int v_is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
           ch == ',' || ch == ';' || ch == ':' || ch == '.' ||
           ch == '!' || ch == '?' || ch == '(' || ch == ')' ||
           ch == '[' || ch == ']' || ch == '{' || ch == '}';
}

static char v_ascii_lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') return (char)(ch + ('a' - 'A'));
    return ch;
}

static int v_match_prefix_ci(const char *text, const char *key, sr_u32 *key_len) {
    sr_u32 i = 0;
    if (!text || !key || !key[0]) return 0;
    while (key[i]) {
        if (!text[i]) return 0;
        if (v_ascii_lower(text[i]) != v_ascii_lower(key[i])) return 0;
        ++i;
    }
    if (key_len) *key_len = i;
    return 1;
}

static sr_u32 v_utf8_advance(const char *s) {
    sr_u8 c;
    if (!s || !s[0]) return 0;
    c = (sr_u8)s[0];
    if ((c & 0x80u) == 0) return 1;
    if ((c & 0xe0u) == 0xc0u && s[1]) return 2;
    if ((c & 0xf0u) == 0xe0u && s[1] && s[2]) return 3;
    if ((c & 0xf8u) == 0xf0u && s[1] && s[2] && s[3]) return 4;
    return 1;
}

static char v_fold_french(const char *s, sr_u32 *advance) {
    sr_u8 a;
    sr_u8 b;
    if (!s || !s[0]) {
        if (advance) *advance = 0;
        return 0;
    }
    a = (sr_u8)s[0];
    if (a < 0x80u) {
        if (advance) *advance = 1;
        return v_ascii_lower((char)a);
    }
    b = (sr_u8)s[1];
    if (a == 0xc3u) {
        if (advance) *advance = 2;
        switch (b) {
            case 0x80u: case 0x81u: case 0x82u: case 0x84u:
            case 0xa0u: case 0xa1u: case 0xa2u: case 0xa4u: return 'a';
            case 0x87u: case 0xa7u: return 'c';
            case 0x88u: case 0x89u: case 0x8au: case 0x8bu:
            case 0xa8u: case 0xa9u: case 0xaau: case 0xabu: return 'e';
            case 0x8eu: case 0x8fu: case 0xaeu: case 0xafu: return 'i';
            case 0x94u: case 0x96u: case 0xb4u: case 0xb6u: return 'o';
            case 0x99u: case 0x9bu: case 0x9cu:
            case 0xb9u: case 0xbbu: case 0xbcu: return 'u';
            default: break;
        }
    }
    if (advance) *advance = v_utf8_advance(s);
    return 0;
}

static const sr_voice_unit *v_find_exact(const sr_voice_bank *bank,
                                         const char *key,
                                         sr_u32 key_len) {
    sr_u32 i;
    if (!bank || !key || !key_len) return 0;
    for (i = 0; i < bank->unit_count; ++i) {
        const char *candidate = bank->units[i].key_utf8;
        sr_u32 n = v_strlen(candidate);
        sr_u32 j;
        if (n != key_len) continue;
        for (j = 0; j < n; ++j) {
            if (v_ascii_lower(candidate[j]) != v_ascii_lower(key[j])) break;
        }
        if (j == n) return &bank->units[i];
    }
    return 0;
}

static const sr_voice_unit *v_longest_prefix(const sr_voice_stream *voice,
                                              const char *text,
                                              sr_u32 *consumed) {
    const sr_voice_unit *best = 0;
    sr_u32 best_len = 0;
    sr_u32 i;
    if (!voice || !voice->bank || !text) return 0;

    for (i = 0; i < voice->bank->unit_count; ++i) {
        const char *key = voice->bank->units[i].key_utf8;
        sr_u32 n = 0;
        if (!key || !key[0]) continue;
        if (!v_match_prefix_ci(text, key, &n)) continue;
        if (n <= best_len) continue;

        /*
         * Whole word/phrase units must stop on a separator. One-character
         * units are permitted inside unknown words for guaranteed spelling.
         */
        if (n > 1u && text[n] && !v_is_space(text[n])) continue;
        best = &voice->bank->units[i];
        best_len = n;
    }

    if (best && consumed) *consumed = best_len;
    return best;
}

static const sr_voice_unit *v_next_unit(sr_voice_stream *voice) {
    const sr_voice_unit *unit;
    sr_u32 consumed = 0;

    while (voice->text_pos < voice->text_len &&
           v_is_space(voice->text[voice->text_pos])) {
        voice->text_pos++;
    }
    if (voice->text_pos >= voice->text_len) return 0;

    unit = v_longest_prefix(voice, &voice->text[voice->text_pos], &consumed);
    if (unit) {
        voice->text_pos += consumed;
        voice->started_units++;
        return unit;
    }

    {
        sr_u32 advance = 0;
        char folded = v_fold_french(&voice->text[voice->text_pos], &advance);
        if (!advance) advance = 1;
        voice->text_pos += advance;
        if (folded) {
            unit = v_find_exact(voice->bank, &folded, 1u);
            if (unit) {
                voice->started_units++;
                voice->fallback_units++;
                return unit;
            }
        }
    }

    voice->fallback_units++;
    return v_next_unit(voice);
}

static int v_begin(void *ctx, sr_u32 token, sr_speech_priority priority) {
    sr_voice_stream *voice = (sr_voice_stream *)ctx;
    if (!voice) return 0;
    sr_voice_stream_cancel(voice);
    voice->token = token;
    voice->priority = priority;
    voice->text_len = 0;
    voice->text_pos = 0;
    voice->text[0] = 0;
    return 1;
}

static int v_write(void *ctx, const char *text, sr_u32 bytes) {
    sr_voice_stream *voice = (sr_voice_stream *)ctx;
    sr_u32 room;
    sr_u32 i;
    if (!voice || !text) return 0;
    room = (sr_u32)sizeof(voice->text) - 1u - voice->text_len;
    if (bytes > room) return 0;
    for (i = 0; i < bytes; ++i)
        voice->text[voice->text_len + i] = text[i];
    voice->text_len += bytes;
    voice->text[voice->text_len] = 0;
    return 1;
}

static int v_commit(void *ctx) {
    sr_voice_stream *voice = (sr_voice_stream *)ctx;
    if (!voice || !voice->bank || !voice->audio.write_pcm) return 0;
    voice->text_pos = 0;
    voice->current = 0;
    voice->current_frame = 0;
    voice->active = voice->text_len ? 1u : 0u;
    return 1;
}

static void v_cancel(void *ctx) {
    sr_voice_stream_cancel((sr_voice_stream *)ctx);
}

void sr_voice_stream_init(sr_voice_stream *voice,
                          const sr_voice_bank *bank,
                          sr_audio_sink audio) {
    if (!voice) return;
    voice->bank = bank;
    voice->audio = audio;
    voice->text[0] = 0;
    voice->text_len = 0;
    voice->text_pos = 0;
    voice->token = 0;
    voice->priority = SR_SPEECH_INFO;
    voice->current = 0;
    voice->current_frame = 0;
    voice->started_units = 0;
    voice->fallback_units = 0;
    voice->active = 0;
}

sr_speech_sink sr_voice_stream_as_sink(sr_voice_stream *voice) {
    sr_speech_sink sink;
    sink.ctx = voice;
    sink.begin = v_begin;
    sink.write_utf8 = v_write;
    sink.commit = v_commit;
    sink.cancel = v_cancel;
    return sink;
}

int sr_voice_stream_pump(sr_voice_stream *voice, sr_u32 frame_budget) {
    sr_u32 remaining = frame_budget;
    if (!voice || !voice->active || !voice->bank || !voice->audio.write_pcm)
        return 0;
    if (!remaining) remaining = 1;

    while (remaining && voice->active) {
        sr_u32 left;
        sr_u32 chunk;
        if (!voice->current) {
            voice->current = v_next_unit(voice);
            voice->current_frame = 0;
            if (!voice->current) {
                voice->active = 0;
                if (voice->audio.flush) voice->audio.flush(voice->audio.ctx);
                break;
            }
        }

        left = voice->current->frames - voice->current_frame;
        if (!left) {
            voice->current = 0;
            continue;
        }
        chunk = left < remaining ? left : remaining;
        if (!voice->audio.write_pcm(
                voice->audio.ctx,
                voice->current->pcm + voice->current_frame,
                chunk,
                voice->bank->sample_rate)) {
            sr_voice_stream_cancel(voice);
            return 0;
        }
        voice->current_frame += chunk;
        remaining -= chunk;
        if (voice->current_frame >= voice->current->frames)
            voice->current = 0;
    }

    return voice->active ? 1 : 0;
}

void sr_voice_stream_cancel(sr_voice_stream *voice) {
    if (!voice) return;
    if (voice->active && voice->audio.stop)
        voice->audio.stop(voice->audio.ctx);
    voice->active = 0;
    voice->current = 0;
    voice->current_frame = 0;
    voice->text_pos = 0;
}
