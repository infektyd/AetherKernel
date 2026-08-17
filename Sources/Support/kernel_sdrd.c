// Runtime V104: re-read FAT32 AETHER.TMP by name after GENET.
// Lookup only. Fail-closed if missing or foreign pattern. Does not
// create, write FAT/dir, or execute. Shared 512 B scratch — not the
// 4 KiB core0 stack. No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000
// after GENET DMA). No boot event emit. Does not arm or reset the watchdog.

#include "Support.h"

static int sdrd_probed;
static int sdrd_ok_val;
static int sdrd_match_val;
static int sdrd_present_val;
static unsigned int sdrd_name_val;

int kernel_sdrd_selftest(void) {
    unsigned int name = 0u;
    unsigned int present = 0u;
    unsigned int match = 0u;

    if (sdrd_probed) return sdrd_ok_val;
    sdrd_probed = 1;
    sdrd_ok_val = 0;
    sdrd_match_val = 0;
    sdrd_present_val = 0;
    sdrd_name_val = 0;

    if (kernel_sdhci_fat32_read_scratch(&name, &present, &match) == 0) {
        sdrd_name_val = name;
        sdrd_present_val = (int)present;
        sdrd_match_val = (int)match;
        return 0;
    }
    sdrd_name_val = name;
    sdrd_present_val = (int)present;
    sdrd_match_val = (int)match;
    if (match == 1u && present == 1u && name == 0x41455448u) {
        sdrd_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdrd_ok(void)      { return sdrd_ok_val; }
int          kernel_sdrd_match(void)   { return sdrd_match_val; }
int          kernel_sdrd_present(void) { return sdrd_present_val; }
unsigned int kernel_sdrd_name(void)    { return sdrd_name_val; }
