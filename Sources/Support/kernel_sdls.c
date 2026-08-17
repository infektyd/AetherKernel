// Runtime V86: list FAT32 root short-name entries after GENET.
// EPIC H: count 8.3 names. ok=1 only if files >= 2u and CONFIG.TXT is
// present and one other name was seen. Do not execute at EL0
// (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Uses the shared 512 B FAT scratch, not core0 stack.

#include "Support.h"

static int sdls_probed;
static int sdls_ok_val;
static unsigned int sdls_files_val;
static unsigned int sdls_config_val;
static unsigned int sdls_other_val;

int kernel_sdls_selftest(void) {
    if (sdls_probed) return sdls_ok_val;
    sdls_probed = 1;
    sdls_ok_val = 0;
    sdls_files_val = 0;
    sdls_config_val = 0;
    sdls_other_val = 0;

    unsigned int files = 0u;
    unsigned int config = 0u;
    (void)kernel_sdhci_fat32_listdir(&files, &config);
    sdls_files_val = files;
    sdls_config_val = config;
    sdls_other_val = kernel_sdhci_fat32_list_other();
    if (files >= 2u && config == 1u && sdls_other_val != 0u) {
        sdls_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdls_ok(void)     { return sdls_ok_val; }
unsigned int kernel_sdls_files(void)  { return sdls_files_val; }
unsigned int kernel_sdls_config(void) { return sdls_config_val; }
unsigned int kernel_sdls_other(void)  { return sdls_other_val; }
