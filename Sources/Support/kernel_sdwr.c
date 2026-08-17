// Runtime V102: SDHCI CMD24 write of a free FAT32 cluster after GENET.
// Writes one 512-byte pattern to a cluster whose FAT entry is 0, then
// CMD17 readback. Does not update FAT or directory. Does not execute.
// Shared 512 B scratch in kernel_sdhci.c — not the 4 KiB core0 stack.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Does not arm or reset the watchdog.

#include "Support.h"

static int sdwr_probed;
static int sdwr_ok_val;
static int sdwr_match_val;
static unsigned int sdwr_clus_val;
static unsigned int sdwr_bytes_val;

int kernel_sdwr_selftest(void) {
    unsigned int clus = 0u;
    unsigned int match = 0u;

    if (sdwr_probed) return sdwr_ok_val;
    sdwr_probed = 1;
    sdwr_ok_val = 0;
    sdwr_match_val = 0;
    sdwr_clus_val = 0;
    sdwr_bytes_val = 512u;

    if (kernel_sdhci_fat32_write_free(&clus, &match) == 0) {
        sdwr_clus_val = clus;
        sdwr_match_val = (int)match;
        return 0;
    }
    sdwr_clus_val = clus;
    sdwr_match_val = (int)match;
    if (match == 1u && clus >= 2u) {
        sdwr_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdwr_ok(void)    { return sdwr_ok_val; }
int          kernel_sdwr_match(void) { return sdwr_match_val; }
unsigned int kernel_sdwr_clus(void)  { return sdwr_clus_val; }
unsigned int kernel_sdwr_bytes(void) { return sdwr_bytes_val; }
