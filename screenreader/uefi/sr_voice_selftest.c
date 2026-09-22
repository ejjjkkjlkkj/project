#include <stdio.h>
#include <string.h>
#include "sr_voice_stream.h"

typedef struct {
    unsigned calls;
    unsigned frames;
    unsigned flushes;
    unsigned stops;
    sr_pcm16 samples[128];
} fake_audio;

static int audio_write(void *ctx, const sr_pcm16 *pcm, sr_u32 frames, sr_u32 sample_rate) {
    fake_audio *a = (fake_audio *)ctx;
    sr_u32 i;
    if (sample_rate != 48000u) return 0;
    a->calls++;
    for (i = 0; i < frames && a->frames < 128u; ++i)
        a->samples[a->frames++] = pcm[i];
    return 1;
}
static void audio_flush(void *ctx) { ((fake_audio *)ctx)->flushes++; }
static void audio_stop(void *ctx) { ((fake_audio *)ctx)->stops++; }

static int ok(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        return 0;
    }
    return 1;
}

int main(void) {
    static const sr_pcm16 bonjour_pcm[] = {10,11,12,13,14,15};
    static const sr_pcm16 a_pcm[] = {21,22,23};
    static const sr_pcm16 b_pcm[] = {31,32,33};
    static const sr_voice_unit units[] = {
        {"bonjour", bonjour_pcm, 6},
        {"a", a_pcm, 3},
        {"b", b_pcm, 3}
    };
    static const sr_voice_bank bank = {units, 3, 48000};
    fake_audio audio = {0};
    sr_audio_sink out = {&audio, audio_write, audio_flush, audio_stop};
    sr_voice_stream voice;
    sr_speech_sink sink;
    const char *text = "Bonjour ab";

    sr_voice_stream_init(&voice, &bank, out);
    sink = sr_voice_stream_as_sink(&voice);

    if (!ok(sink.begin(sink.ctx, 7, SR_SPEECH_FOCUS), "begin")) return 1;
    if (!ok(sink.write_utf8(sink.ctx, text, (sr_u32)strlen(text)), "write")) return 1;
    if (!ok(sink.commit(sink.ctx), "commit")) return 1;
    if (!ok(voice.active, "voice active after commit")) return 1;

    while (sr_voice_stream_pump(&voice, 2)) {}
    if (!ok(audio.frames == 12u, "all expected frames streamed")) return 1;
    if (!ok(audio.samples[0] == 10 && audio.samples[5] == 15, "whole-word unit preferred")) return 1;
    if (!ok(audio.samples[6] == 21 && audio.samples[9] == 31, "fallback letters streamed")) return 1;
    if (!ok(voice.fallback_units == 2u, "fallback counter")) return 1;
    if (!ok(audio.flushes == 1u, "flush on completion")) return 1;

    if (!ok(sink.begin(sink.ctx, 8, SR_SPEECH_FOCUS), "second begin")) return 1;
    if (!ok(sink.write_utf8(sink.ctx, text, (sr_u32)strlen(text)), "second write")) return 1;
    if (!ok(sink.commit(sink.ctx), "second commit")) return 1;
    if (!ok(sr_voice_stream_pump(&voice, 2), "partial pump remains active")) return 1;
    sink.cancel(sink.ctx);
    if (!ok(!voice.active, "cancel immediate")) return 1;
    if (!ok(audio.stops == 1u, "hardware stop called")) return 1;

    printf("UEFI_VOICE_STREAM=PASS\n");
    return 0;
}
