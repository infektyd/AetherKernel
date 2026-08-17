// Runtime V115: FAT32 FAT-mirror compare after GENET.
// First sector of FAT copy 0 must match copy 1. Fail-closed fats>=2.
// No FAT write. No named boot file.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Does not arm or reset the watchdog.

#include "Support.h"

static int sdfm_probed;
static int sdfm_ok_val;
static unsigned int sdfm_match_val;
static unsigned int sdfm_fats_val;

int kernel_sdfm_selftest(void) {
    unsigned int match = 0u;
    unsigned int fats = 0u;

    if (sdfm_probed) return sdfm_ok_val;
    sdfm_probed = 1;
    sdfm_ok_val = 0;
    sdfm_match_val = 0;
    sdfm_fats_val = 0;

    if (kernel_sdhci_fat32_mirror(&match, &fats) == 0) {
        sdfm_match_val = match;
        sdfm_fats_val = fats;
        return 0;
    }
    sdfm_match_val = match;
    sdfm_fats_val = fats;
    if (match == 1u && fats >= 2u) {
        sdfm_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdfm_ok(void)    { return sdfm_ok_val; }
unsigned int kernel_sdfm_match(void) { return sdfm_match_val; }
unsigned int kernel_sdfm_fats(void)  { return sdfm_fats_val; }
