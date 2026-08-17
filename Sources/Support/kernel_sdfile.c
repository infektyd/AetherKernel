// Runtime V87: load a second FAT32 root file after GENET.
// Prefer cmdline.txt, then issue.txt, else the first small regular file.
// Skip directories (OVERLAYS). Not CONFIG.TXT. No EL0 execute
// (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Uses the shared 512 B FAT scratch, not core0 stack.

#include "Support.h"

static int sdfile_probed;
static int sdfile_ok_val;
static unsigned int sdfile_name_val;
static unsigned int sdfile_bytes_val;
static unsigned int sdfile_sum_val;

int kernel_sdfile_selftest(void) {
    if (sdfile_probed) return sdfile_ok_val;
    sdfile_probed = 1;
    sdfile_ok_val = 0;
    sdfile_name_val = 0;
    sdfile_bytes_val = 0;
    sdfile_sum_val = 0;

    unsigned int name = 0u;
    unsigned int bytes = 0u;
    unsigned int sum = 0u;
    if (kernel_sdhci_fat32_read_second(&name, &bytes, &sum) == 0) return 0;
    sdfile_name_val = name;
    sdfile_bytes_val = bytes;
    sdfile_sum_val = sum;
    if (bytes > 0u && name != 0u && name != 0x434F4E46u) {
        sdfile_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdfile_ok(void)    { return sdfile_ok_val; }
unsigned int kernel_sdfile_name(void)  { return sdfile_name_val; }
unsigned int kernel_sdfile_bytes(void) { return sdfile_bytes_val; }
unsigned int kernel_sdfile_sum(void)   { return sdfile_sum_val; }
