// Runtime V90: load FAT32 root issue.txt by 8.3 name after GENET.
// Fail-closed if missing. Do not fall back to another file. Do not execute.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Uses the shared 512 B FAT scratch, not core0 stack.

#include "Support.h"

static int sdiss_probed;
static int sdiss_ok_val;
static int sdiss_present_val;
static unsigned int sdiss_name_val;
static unsigned int sdiss_bytes_val;
static unsigned int sdiss_sum_val;

int kernel_sdiss_selftest(void) {
    if (sdiss_probed) return sdiss_ok_val;
    sdiss_probed = 1;
    sdiss_ok_val = 0;
    sdiss_present_val = 0;
    sdiss_name_val = 0;
    sdiss_bytes_val = 0;
    sdiss_sum_val = 0;

    unsigned int name = 0u;
    unsigned int bytes = 0u;
    unsigned int sum = 0u;
    if (kernel_sdhci_fat32_read_issue(&name, &bytes, &sum) == 0) return 0;
    sdiss_present_val = 1;
    sdiss_name_val = name;
    sdiss_bytes_val = bytes;
    sdiss_sum_val = sum;
    if (bytes > 0u && name == 0x49535355u) {
        sdiss_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdiss_ok(void)      { return sdiss_ok_val; }
int          kernel_sdiss_present(void) { return sdiss_present_val; }
unsigned int kernel_sdiss_name(void)    { return sdiss_name_val; }
unsigned int kernel_sdiss_bytes(void)   { return sdiss_bytes_val; }
unsigned int kernel_sdiss_sum(void)     { return sdiss_sum_val; }
