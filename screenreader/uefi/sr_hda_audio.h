#ifndef SR_HDA_AUDIO_H
#define SR_HDA_AUDIO_H

#include "sr_voice_stream.h"

typedef struct {
    void *system_table;
    volatile sr_u8 *mmio;
    sr_u64 dma_base;
    sr_u32 staged_bytes;
    sr_u32 codec_vendor_id;
    sr_u32 underruns;
    sr_u32 starts;
    sr_u32 stops;
    sr_u8 cad;
    sr_u8 afg;
    sr_u8 pin;
    sr_u8 dac;
    sr_u8 preferred_controller;
    sr_u8 internal_speaker;
    sr_u8 stream_initialized;
    sr_u8 ready;
    sr_u8 playing;
} sr_hda_audio;

int sr_hda_audio_static_selftest(void);
int sr_hda_audio_init(sr_hda_audio *audio, void *system_table);
sr_audio_sink sr_hda_audio_as_sink(sr_hda_audio *audio);
void sr_hda_audio_stop(sr_hda_audio *audio);

#endif
