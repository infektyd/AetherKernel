// Runtime V89: load one regular file from FAT32 overlays/ after GENET.
// First short-name file with 0 < size <= 65536. Skip directories.
// No EL0 execute (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Uses the shared 512 B FAT scratch, not core0 stack.

#include "Support.h"

static int sdovf_probed;
static int sdovf_ok_val;
static unsigned int sdovf_name_val;
static unsigned int sdovf_bytes_val;
static unsigned int sdovf_sum_val;

int kernel_sdovf_selftest(void) {
    if (sdovf_probed) return sdovf_ok_val;
    sdovf_probed = 1;
    sdovf_ok_val = 0;
    sdovf_name_val = 0;
    sdovf_bytes_val = 0;
    sdovf_sum_val = 0;

    unsigned int name = 0u;
    unsigned int bytes = 0u;
    unsigned int sum = 0u;
    if (kernel_sdhci_fat32_read_overlay(&name, &bytes, &sum) == 0) return 0;
    sdovf_name_val = name;
    sdovf_bytes_val = bytes;
    sdovf_sum_val = sum;
    if (bytes > 0u && name != 0u && name != 0x4F564552u) {
        sdovf_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdovf_ok(void)    { return sdovf_ok_val; }
unsigned int kernel_sdovf_name(void)  { return sdovf_name_val; }
unsigned int kernel_sdovf_bytes(void) { return sdovf_bytes_val; }
unsigned int kernel_sdovf_sum(void)   { return sdovf_sum_val; }
