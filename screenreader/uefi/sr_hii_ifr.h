#ifndef SR_HII_IFR_H
#define SR_HII_IFR_H

#include "sr_core.h"

typedef int (*sr_hii_string_resolver)(void *ctx,
                                      sr_u16 string_id,
                                      char *out,
                                      sr_u32 out_cap);

typedef struct {
    sr_u64 value;
    const char *label;
    sr_u8 width;
    sr_u8 flags;
} sr_hii_option;

typedef struct {
    sr_u16 question_id;
    sr_u16 varstore_id;
    sr_u16 varstore_info;
    sr_u16 option_start;
    sr_u8 option_count;
    sr_u8 opcode;
    sr_u8 question_flags;
    sr_u8 oneof_flags;
} sr_hii_binding;

typedef struct {
    sr_node *nodes;
    sr_u32 node_capacity;
    sr_u32 node_count;
    char *string_arena;
    sr_u32 string_capacity;
    sr_u32 string_used;
    sr_hii_string_resolver resolve_string;
    void *resolve_ctx;
    sr_hii_binding *bindings;
    sr_u32 binding_capacity;
    sr_hii_option *options;
    sr_u32 option_capacity;
    sr_u32 option_count;
    sr_u32 malformed_opcodes;
    sr_u32 dropped_nodes;
} sr_hii_model;

void sr_hii_model_init(sr_hii_model *model,
                       sr_node *nodes,
                       sr_u32 node_capacity,
                       char *string_arena,
                       sr_u32 string_capacity,
                       sr_hii_string_resolver resolver,
                       void *resolver_ctx);

/*
 * Parse one EFI_HII_PACKAGE_FORMS package including its four-byte package
 * header. Appends nodes to model and never allocates.
 */
void sr_hii_model_attach_metadata(sr_hii_model *model,
                                  sr_hii_binding *bindings,
                                  sr_u32 binding_capacity,
                                  sr_hii_option *options,
                                  sr_u32 option_capacity);

int sr_hii_parse_forms_package(sr_hii_model *model,
                               const sr_u8 *package,
                               sr_u32 package_bytes);

int sr_hii_apply_raw_value(sr_hii_model *model,
                           sr_u32 node_index,
                           sr_u64 raw_value);

#endif
