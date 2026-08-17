// Runtime V112: SDHCI CMD23 SET_BLOCK_COUNT then CMD18 after GENET.
// Two 512 B blocks from LBA 0. Fail-closed MBR 0xAA55 on block 0.
// No AUTO_CMD12. Restore single-block length. Shared 512 B scratch
// — not the 4 KiB core0 stack. No FAT write. No bus-width change.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Does not arm or reset the watchdog.

#include "Support.h"

static int sdbc_probed;
static int sdbc_ok_val;
static unsigned int sdbc_count_val;
static unsigned int sdbc_mbr_val;

int kernel_sdbc_selftest(void) {
    unsigned int count = 0u;
    unsigned int mbr = 0u;

    if (sdbc_probed) return sdbc_ok_val;
    sdbc_probed = 1;
    sdbc_ok_val = 0;
    sdbc_count_val = 0;
    sdbc_mbr_val = 0;

    if (kernel_sdhci_card_blockcount(&count, &mbr) == 0) {
        sdbc_count_val = count;
        sdbc_mbr_val = mbr;
        return 0;
    }
    sdbc_count_val = count;
    sdbc_mbr_val = mbr;
    if (count == 2u && mbr == 1u) {
        sdbc_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdbc_ok(void)    { return sdbc_ok_val; }
unsigned int kernel_sdbc_count(void) { return sdbc_count_val; }
unsigned int kernel_sdbc_mbr(void)   { return sdbc_mbr_val; }
