// Runtime V85: reload config.txt from the SD FAT32 root after GENET.
// EPIC H first slice: load at runtime. Do not execute the file at EL0
// (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// match=1 only if bytes+checksum equal the V57 read. No boot event emit.

#include "Support.h"

static int sdload_probed;
static int sdload_ok_val;
static unsigned int sdload_match_val;
static unsigned int sdload_bytes_val;
static unsigned int sdload_sum_val;

int kernel_sdload_selftest(void) {
    if (sdload_probed) return sdload_ok_val;
    sdload_probed = 1;
    sdload_ok_val = 0;
    sdload_match_val = 0;
    sdload_bytes_val = 0;
    sdload_sum_val = 0;

    if (kernel_sdhci_fat32_selftest() == 0) return 0;
    unsigned int expect_bytes = (unsigned int)kernel_sdhci_fat32_bytes();
    unsigned int expect_sum = (unsigned int)kernel_sdhci_fat32_checksum();
    if (expect_bytes == 0U) return 0;

    if (kernel_sdhci_fat32_read() == 0) return 0;
    sdload_bytes_val = (unsigned int)kernel_sdhci_fat32_bytes();
    sdload_sum_val = (unsigned int)kernel_sdhci_fat32_checksum();
    if (sdload_bytes_val != expect_bytes) return 0;
    if (sdload_sum_val != expect_sum) return 0;

    sdload_match_val = 1;
    sdload_ok_val = 1;
    return 1;
}

int          kernel_sdload_ok(void)     { return sdload_ok_val; }
unsigned int kernel_sdload_match(void)  { return sdload_match_val; }
unsigned int kernel_sdload_bytes(void)  { return sdload_bytes_val; }
unsigned int kernel_sdload_sum(void)    { return sdload_sum_val; }
