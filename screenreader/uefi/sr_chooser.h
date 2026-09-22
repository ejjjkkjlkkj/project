#ifndef SR_CHOOSER_H
#define SR_CHOOSER_H

#include "sr_core.h"
#include "sr_keymap.h"

#define SR_CHOOSER_MAX_MATCHES 96u
#define SR_CHOOSER_QUERY_CAP 32u

typedef struct {
    sr_u32 matches[SR_CHOOSER_MAX_MATCHES];
    sr_u32 match_count;
    sr_u32 selected_match;
    sr_u32 original_focus;
    char query[SR_CHOOSER_QUERY_CAP];
    sr_u32 query_len;
    sr_u8 active;
    sr_u8 had_original_focus;
} sr_chooser;

void sr_chooser_init(sr_chooser *chooser);
int sr_chooser_open(sr_chooser *chooser, sr_runtime *rt);
int sr_chooser_cancel(sr_chooser *chooser, sr_runtime *rt);
int sr_chooser_accept(sr_chooser *chooser, sr_runtime *rt);
int sr_chooser_next(sr_chooser *chooser, sr_runtime *rt, int direction);
int sr_chooser_type(sr_chooser *chooser, sr_runtime *rt, sr_u16 unicode_char);
int sr_chooser_backspace(sr_chooser *chooser, sr_runtime *rt);
int sr_chooser_handle_key(sr_chooser *chooser, sr_runtime *rt, sr_key key);

#endif
