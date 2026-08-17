// Runtime V114: FAT32 backup boot sector after GENET.
// BPB_BkBootSec copy must match primary BPB scalars + trail 0xAA55.
// No FAT write. No named boot file.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Does not arm or reset the watchdog.

#include "Support.h"

static int sdfb_probed;
static int sdfb_ok_val;
static unsigned int sdfb_match_val;
static unsigned int sdfb_sec_val;

int kernel_sdfb_selftest(void) {
    unsigned int match = 0u;
    unsigned int sec = 0u;

    if (sdfb_probed) return sdfb_ok_val;
    sdfb_probed = 1;
    sdfb_ok_val = 0;
    sdfb_match_val = 0;
    sdfb_sec_val = 0;

    if (kernel_sdhci_fat32_backup(&match, &sec) == 0) {
        sdfb_match_val = match;
        sdfb_sec_val = sec;
        return 0;
    }
    sdfb_match_val = match;
    sdfb_sec_val = sec;
    if (match == 1u && sec != 0u) {
        sdfb_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdfb_ok(void)    { return sdfb_ok_val; }
unsigned int kernel_sdfb_match(void) { return sdfb_match_val; }
unsigned int kernel_sdfb_sec(void)   { return sdfb_sec_val; }
