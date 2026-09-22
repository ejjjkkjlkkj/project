#ifndef SR_UEFI_RUNTIME_H
#define SR_UEFI_RUNTIME_H

#include "sr_core.h"
#include "sr_keymap.h"
#include "sr_voice_stream.h"
#include "sr_uefi_input.h"
#include "sr_chooser.h"
#include "sr_uefi_config.h"

typedef int (*sr_activate_fn)(void *ctx, const sr_node *node);

typedef struct {
    sr_runtime screenreader;
    sr_voice_stream voice;
    sr_uefi_keyboard keyboard;
    sr_chooser chooser;
    sr_uefi_config *config;
    sr_activate_fn activate;
    void *activate_ctx;
    sr_u32 audio_frame_budget;
    sr_u64 ticks;
    sr_u64 keys_seen;
    sr_u64 commands_seen;
} sr_uefi_runtime;

int sr_uefi_runtime_init(sr_uefi_runtime *runtime,
                         void *system_table,
                         const sr_node *nodes,
                         sr_u32 node_count,
                         const sr_voice_bank *voice_bank,
                         sr_audio_sink audio,
                         sr_activate_fn activate,
                         void *activate_ctx);

void sr_uefi_runtime_bind_config(sr_uefi_runtime *runtime,
                                 sr_uefi_config *config);

int sr_uefi_runtime_tick(sr_uefi_runtime *runtime);

#endif
