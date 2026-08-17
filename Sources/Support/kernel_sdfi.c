// Runtime V113: FAT32 FSInfo sector parse after GENET.
// Lead 0x41615252 + struct 0x61417272 + trail 0xAA55.
// Reports FSI_Free_Count honestly. No FAT write. No named boot file.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Does not arm or reset the watchdog.

#include "Support.h"

static int sdfi_probed;
static int sdfi_ok_val;
static unsigned int sdfi_lead_val;
static unsigned int sdfi_struct_val;
static unsigned int sdfi_free_val;

int kernel_sdfi_selftest(void) {
    unsigned int lead = 0u;
    unsigned int st = 0u;
    unsigned int free_cnt = 0u;

    if (sdfi_probed) return sdfi_ok_val;
    sdfi_probed = 1;
    sdfi_ok_val = 0;
    sdfi_lead_val = 0;
    sdfi_struct_val = 0;
    sdfi_free_val = 0;

    if (kernel_sdhci_fat32_fsinfo(&lead, &st, &free_cnt) == 0) {
        sdfi_lead_val = lead;
        sdfi_struct_val = st;
        sdfi_free_val = free_cnt;
        return 0;
    }
    sdfi_lead_val = lead;
    sdfi_struct_val = st;
    sdfi_free_val = free_cnt;
    if (lead == 1u && st == 1u) {
        sdfi_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdfi_ok(void)     { return sdfi_ok_val; }
unsigned int kernel_sdfi_lead(void)   { return sdfi_lead_val; }
unsigned int kernel_sdfi_struct(void) { return sdfi_struct_val; }
unsigned int kernel_sdfi_free(void)   { return sdfi_free_val; }
