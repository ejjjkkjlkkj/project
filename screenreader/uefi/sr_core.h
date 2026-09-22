#ifndef SR_CORE_H
#define SR_CORE_H

typedef unsigned char sr_u8;
typedef unsigned short sr_u16;
typedef unsigned int sr_u32;
typedef unsigned long long sr_u64;
typedef unsigned long long sr_size;

typedef enum {
    SR_ROLE_UNKNOWN = 0,
    SR_ROLE_WINDOW,
    SR_ROLE_GROUP,
    SR_ROLE_TEXT,
    SR_ROLE_BUTTON,
    SR_ROLE_CHECKBOX,
    SR_ROLE_RADIO,
    SR_ROLE_COMBO,
    SR_ROLE_EDIT,
    SR_ROLE_SLIDER,
    SR_ROLE_LIST,
    SR_ROLE_LIST_ITEM,
    SR_ROLE_MENU,
    SR_ROLE_MENU_ITEM,
    SR_ROLE_TAB,
    SR_ROLE_STATUS,
    SR_ROLE_DIALOG
} sr_role;

enum {
    SR_STATE_FOCUSABLE = 1u << 0,
    SR_STATE_ENABLED   = 1u << 1,
    SR_STATE_CHECKED   = 1u << 2,
    SR_STATE_SELECTED  = 1u << 3,
    SR_STATE_EXPANDED  = 1u << 4,
    SR_STATE_COLLAPSED = 1u << 5,
    SR_STATE_REQUIRED  = 1u << 6,
    SR_STATE_READONLY  = 1u << 7,
    SR_STATE_HIDDEN    = 1u << 8
};

typedef struct {
    sr_u32 id;
    sr_role role;
    sr_u32 state;
    const char *label;
    const char *value;
    const char *help;
} sr_node;

typedef enum {
    SR_SPEECH_INFO = 0,
    SR_SPEECH_FOCUS = 1,
    SR_SPEECH_CRITICAL = 2
} sr_speech_priority;

typedef struct {
    void *ctx;
    int  (*begin)(void *ctx, sr_u32 token, sr_speech_priority priority);
    int  (*write_utf8)(void *ctx, const char *text, sr_u32 bytes);
    int  (*commit)(void *ctx);
    void (*cancel)(void *ctx);
} sr_speech_sink;

typedef enum {
    SR_VERBOSITY_BRIEF = 0,
    SR_VERBOSITY_NORMAL = 1,
    SR_VERBOSITY_VERBOSE = 2
} sr_verbosity;

typedef enum {
    SR_CMD_NONE = 0,
    SR_CMD_PREVIOUS,
    SR_CMD_NEXT,
    SR_CMD_FIRST,
    SR_CMD_LAST,
    SR_CMD_PAGE_PREVIOUS,
    SR_CMD_PAGE_NEXT,
    SR_CMD_ACTIVATE,
    SR_CMD_HELP,
    SR_CMD_REPEAT,
    SR_CMD_WHERE_AM_I,
    SR_CMD_STOP_SPEECH,
    SR_CMD_NEXT_CONTROL,
    SR_CMD_PREVIOUS_CONTROL,
    SR_CMD_NEXT_EDIT,
    SR_CMD_PREVIOUS_EDIT,
    SR_CMD_NEXT_CHECKBOX,
    SR_CMD_PREVIOUS_CHECKBOX,
    SR_CMD_NEXT_CHOICE,
    SR_CMD_PREVIOUS_CHOICE
} sr_command;

typedef struct {
    const sr_node *nodes;
    sr_u32 node_count;
    sr_u32 focus_index;
    sr_u32 page_step;
    sr_u32 speech_token;
    sr_u32 focus_events;
    sr_u32 speech_interrupts;
    sr_u32 emitted_utterances;
    sr_u8 has_focus;
    sr_u8 speech_active;
    sr_verbosity verbosity;
    sr_speech_sink speech;
    char last_utterance[768];
} sr_runtime;

void sr_init(sr_runtime *rt,
             const sr_node *nodes,
             sr_u32 node_count,
             sr_speech_sink sink);

int sr_set_focus(sr_runtime *rt, sr_u32 index, int speak);
int sr_focus_first(sr_runtime *rt);
int sr_focus_last(sr_runtime *rt);
int sr_move(sr_runtime *rt, int delta);
int sr_move_page(sr_runtime *rt, int direction);
int sr_move_role(sr_runtime *rt, sr_role role, int direction);
int sr_handle(sr_runtime *rt, sr_command command);
int sr_announce_focus(sr_runtime *rt);
int sr_repeat(sr_runtime *rt);
int sr_help(sr_runtime *rt);
int sr_where_am_i(sr_runtime *rt);
int sr_say(sr_runtime *rt, const char *text, sr_speech_priority priority);
void sr_stop_speech(sr_runtime *rt);

const sr_node *sr_current(const sr_runtime *rt);
const char *sr_role_name(sr_role role);

#endif
