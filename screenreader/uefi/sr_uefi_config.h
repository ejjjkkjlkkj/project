#ifndef SR_UEFI_CONFIG_H
#define SR_UEFI_CONFIG_H

#include "sr_hii_ifr.h"

typedef struct {
    sr_u32 refresh_attempts;
    sr_u32 refresh_successes;
    sr_u32 commit_attempts;
    sr_u32 commit_successes;
    sr_u32 routing_failures;
} sr_uefi_config_stats;

typedef struct {
    void *system_table;
    sr_hii_model *model;
    void *routing;
    sr_uefi_config_stats stats;
} sr_uefi_config;

int sr_uefi_config_init(sr_uefi_config *config,
                        void *system_table,
                        sr_hii_model *model);

int sr_uefi_config_refresh_value(sr_uefi_config *config,
                                 sr_u32 node_index);

sr_u32 sr_uefi_config_refresh_all(sr_uefi_config *config);

int sr_uefi_config_set_raw_value(sr_uefi_config *config,
                                 sr_u32 node_index,
                                 sr_u64 raw_value);

int sr_uefi_config_adjust(sr_uefi_config *config,
                          sr_u32 node_index,
                          int direction);

#endif
