#ifndef SR_UEFI_HII_H
#define SR_UEFI_HII_H

#include "sr_hii_ifr.h"

typedef struct {
    sr_u32 package_lists_seen;
    sr_u32 forms_packages_seen;
    sr_u32 strings_resolved;
    sr_u32 strings_failed;
} sr_uefi_hii_stats;

int sr_uefi_collect_hii(void *system_table,
                        sr_hii_model *model,
                        sr_uefi_hii_stats *stats);

#endif
