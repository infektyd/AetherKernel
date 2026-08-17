// Runtime V109: SDHCI CMD18 READ_MULTIPLE_BLOCK after GENET.
// Two 512 B blocks from LBA 0. Fail-closed MBR 0xAA55 on block 0.
// AUTO_CMD12 stop. Restore single-block length. Shared 512 B scratch
// — not the 4 KiB core0 stack. No FAT write. No bus-width change.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Does not arm or reset the watchdog.

#include "Support.h"

static int sdmb_probed;
static int sdmb_ok_val;
static unsigned int sdmb_blocks_val;
static unsigned int sdmb_mbr_val;

int kernel_sdmb_selftest(void) {
    unsigned int blocks = 0u;
    unsigned int mbr = 0u;

    if (sdmb_probed) return sdmb_ok_val;
    sdmb_probed = 1;
    sdmb_ok_val = 0;
    sdmb_blocks_val = 0;
    sdmb_mbr_val = 0;

    if (kernel_sdhci_card_multiblock(&blocks, &mbr) == 0) {
        sdmb_blocks_val = blocks;
        sdmb_mbr_val = mbr;
        return 0;
    }
    sdmb_blocks_val = blocks;
    sdmb_mbr_val = mbr;
    if (blocks == 2u && mbr == 1u) {
        sdmb_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdmb_ok(void)     { return sdmb_ok_val; }
unsigned int kernel_sdmb_blocks(void) { return sdmb_blocks_val; }
unsigned int kernel_sdmb_mbr(void)    { return sdmb_mbr_val; }
