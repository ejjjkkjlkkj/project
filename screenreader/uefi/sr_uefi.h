#ifndef SR_UEFI_H
#define SR_UEFI_H

#include "sr_core.h"

typedef sr_u64 sr_efi_status;
typedef void *sr_efi_handle;

typedef struct {
    sr_u32 data1;
    sr_u16 data2;
    sr_u16 data3;
    sr_u8 data4[8];
} sr_efi_guid;

typedef sr_efi_status (*sr_locate_protocol_fn)(
    const sr_efi_guid *protocol,
    void *registration,
    void **interface_out);

static inline void *sr_uefi_boot_services(void *system_table) {
    if (!system_table) return 0;
    return *(void **)((sr_u8 *)system_table + 0x60u);
}

static inline sr_locate_protocol_fn sr_uefi_locate_protocol(void *system_table) {
    void *bs = sr_uefi_boot_services(system_table);
    if (!bs) return 0;
    return *(sr_locate_protocol_fn *)((sr_u8 *)bs + 0x140u);
}

#endif
