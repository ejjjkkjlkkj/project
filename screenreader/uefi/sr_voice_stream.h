#ifndef SR_VOICE_STREAM_H
#define SR_VOICE_STREAM_H

#include "sr_core.h"

typedef short sr_pcm16;

typedef struct {
    const char *key_utf8;
    const sr_pcm16 *pcm;
    sr_u32 frames;
} sr_voice_unit;

typedef struct {
    const sr_voice_unit *units;
    sr_u32 unit_count;
    sr_u32 sample_rate;
} sr_voice_bank;

typedef struct {
    void *ctx;
    int (*write_pcm)(void *ctx,
                     const sr_pcm16 *mono,
                     sr_u32 frames,
                     sr_u32 sample_rate);
    void (*flush)(void *ctx);
    void (*stop)(void *ctx);
} sr_audio_sink;

typedef struct {
    const sr_voice_bank *bank;
    sr_audio_sink audio;
    char text[768];
    sr_u32 text_len;
    sr_u32 text_pos;
    sr_u32 token;
    sr_speech_priority priority;
    const sr_voice_unit *current;
    sr_u32 current_frame;
    sr_u32 started_units;
    sr_u32 fallback_units;
    sr_u8 active;
} sr_voice_stream;

void sr_voice_stream_init(sr_voice_stream *voice,
                          const sr_voice_bank *bank,
                          sr_audio_sink audio);

sr_speech_sink sr_voice_stream_as_sink(sr_voice_stream *voice);

/*
 * Pump at most frame_budget mono frames. Call this from the firmware event
 * loop between input polls. Returns 1 while speech remains active, 0 when idle.
 */
int sr_voice_stream_pump(sr_voice_stream *voice, sr_u32 frame_budget);
void sr_voice_stream_cancel(sr_voice_stream *voice);

#endif
