#include "sr_uefi_runtime.h"

int sr_uefi_runtime_init(sr_uefi_runtime *runtime,
                         void *system_table,
                         const sr_node *nodes,
                         sr_u32 node_count,
                         const sr_voice_bank *voice_bank,
                         sr_audio_sink audio,
                         sr_activate_fn activate,
                         void *activate_ctx) {
    sr_speech_sink speech;
    if (!runtime || !system_table || !nodes || !node_count ||
        !voice_bank || !audio.write_pcm) return 0;

    sr_voice_stream_init(&runtime->voice, voice_bank, audio);
    speech = sr_voice_stream_as_sink(&runtime->voice);
    sr_init(&runtime->screenreader, nodes, node_count, speech);

    if (!sr_uefi_keyboard_init(&runtime->keyboard, system_table)) return 0;
    sr_chooser_init(&runtime->chooser);
    runtime->config = 0;

    runtime->activate = activate;
    runtime->activate_ctx = activate_ctx;
    runtime->audio_frame_budget = voice_bank->sample_rate / 100u;
    if (!runtime->audio_frame_budget) runtime->audio_frame_budget = 1u;
    runtime->ticks = 0;
    runtime->keys_seen = 0;
    runtime->commands_seen = 0;

    return sr_focus_first(&runtime->screenreader);
}

void sr_uefi_runtime_bind_config(sr_uefi_runtime *runtime,
                                 sr_uefi_config *config) {
    if (!runtime) return;
    runtime->config = config;
}

int sr_uefi_runtime_tick(sr_uefi_runtime *runtime) {
    sr_key key;
    sr_command command;
    const sr_node *node;
    int did_work = 0;
    if (!runtime) return 0;

    runtime->ticks++;

    if (runtime->voice.active) {
        (void)sr_voice_stream_pump(
            &runtime->voice, runtime->audio_frame_budget);
        did_work = 1;
    }

    if (!sr_uefi_keyboard_poll(&runtime->keyboard, &key))
        return did_work;

    runtime->keys_seen++;

    if (runtime->chooser.active) {
        (void)sr_chooser_handle_key(
            &runtime->chooser, &runtime->screenreader, key);
        return 1;
    }

    command = sr_key_to_command(key);
    if (command == SR_CMD_NONE) return 1;
    runtime->commands_seen++;

    if (command == SR_CMD_ITEM_CHOOSER) {
        (void)sr_chooser_open(&runtime->chooser, &runtime->screenreader);
        return 1;
    }

    if (command == SR_CMD_VALUE_PREVIOUS || command == SR_CMD_VALUE_NEXT) {
        if (runtime->config && runtime->screenreader.has_focus) {
            int direction = command == SR_CMD_VALUE_NEXT ? 1 : -1;
            if (sr_uefi_config_adjust(
                    runtime->config,
                    runtime->screenreader.focus_index,
                    direction)) {
                (void)sr_announce_focus(&runtime->screenreader);
            } else {
                (void)sr_say(
                    &runtime->screenreader,
                    "Modification impossible",
                    SR_SPEECH_CRITICAL);
            }
        }
        return 1;
    }

    if (command == SR_CMD_ACTIVATE &&
        runtime->config &&
        runtime->screenreader.has_focus &&
        sr_current(&runtime->screenreader) &&
        sr_current(&runtime->screenreader)->role == SR_ROLE_CHECKBOX) {
        if (sr_uefi_config_adjust(
                runtime->config, runtime->screenreader.focus_index, 1)) {
            (void)sr_announce_focus(&runtime->screenreader);
        } else {
            (void)sr_say(
                &runtime->screenreader,
                "Modification impossible",
                SR_SPEECH_CRITICAL);
        }
        return 1;
    }

    if (command == SR_CMD_ACTIVATE && runtime->activate) {
        node = sr_current(&runtime->screenreader);
        if (!node) return 1;
        sr_stop_speech(&runtime->screenreader);
        if (!runtime->activate(runtime->activate_ctx, node)) {
            sr_speech_sink sink = runtime->screenreader.speech;
            if (sink.begin && sink.write_utf8 && sink.commit) {
                static const char msg[] = "Activation impossible";
                sr_u32 token = ++runtime->screenreader.speech_token;
                if (!token) token = ++runtime->screenreader.speech_token;
                if (sink.begin(sink.ctx, token, SR_SPEECH_CRITICAL) &&
                    sink.write_utf8(sink.ctx, msg, (sr_u32)(sizeof(msg) - 1u)) &&
                    sink.commit(sink.ctx)) {
                    runtime->screenreader.speech_active = 1;
                }
            }
        } else {
            (void)sr_announce_focus(&runtime->screenreader);
        }
        return 1;
    }

    (void)sr_handle(&runtime->screenreader, command);
    return 1;
}
