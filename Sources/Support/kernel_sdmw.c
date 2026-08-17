// Runtime V111: SDHCI CMD25 WRITE_MULTIPLE_BLOCK after GENET.
// Two 512 B blocks into a free FAT span. Fail-closed CMD17 readback.
// FAT/dir unchanged. Restore single-block length. Shared 512 B scratch
// — not the 4 KiB core0 stack. No named-file write. No bus-width change.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Does not arm or reset the watchdog.

#include "Support.h"

static int sdmw_probed;
static int sdmw_ok_val;
static unsigned int sdmw_match_val;
static unsigned int sdmw_blocks_val;
static unsigned int sdmw_clus_val;

int kernel_sdmw_selftest(void) {
    unsigned int blocks = 0u;
    unsigned int match = 0u;
    unsigned int clus = 0u;

    if (sdmw_probed) return sdmw_ok_val;
    sdmw_probed = 1;
    sdmw_ok_val = 0;
    sdmw_match_val = 0;
    sdmw_blocks_val = 0;
    sdmw_clus_val = 0;

    if (kernel_sdhci_card_multiwrite(&blocks, &match, &clus) == 0) {
        sdmw_blocks_val = blocks;
        sdmw_match_val = match;
        sdmw_clus_val = clus;
        return 0;
    }
    sdmw_blocks_val = blocks;
    sdmw_match_val = match;
    sdmw_clus_val = clus;
    if (blocks == 2u && match == 1u && clus >= 2u) {
        sdmw_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdmw_ok(void)     { return sdmw_ok_val; }
unsigned int kernel_sdmw_match(void)  { return sdmw_match_val; }
unsigned int kernel_sdmw_blocks(void) { return sdmw_blocks_val; }
unsigned int kernel_sdmw_clus(void)   { return sdmw_clus_val; }
