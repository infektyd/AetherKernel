// Runtime V103: FAT32 create/link of dedicated scratch AETHER.TMP after GENET.
// Allocates one free cluster, marks EOC, writes pattern, writes one root
// dirent. Fail-closed if AETHER.TMP exists as a different file. Does not
// execute. Shared 512 B scratch in kernel_sdhci.c — not the 4 KiB core0 stack.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Does not arm or reset the watchdog.

#include "Support.h"

static int sdmk_probed;
static int sdmk_ok_val;
static int sdmk_match_val;
static int sdmk_created_val;
static unsigned int sdmk_name_val;

int kernel_sdmk_selftest(void) {
    unsigned int name = 0u;
    unsigned int created = 0u;
    unsigned int match = 0u;

    if (sdmk_probed) return sdmk_ok_val;
    sdmk_probed = 1;
    sdmk_ok_val = 0;
    sdmk_match_val = 0;
    sdmk_created_val = 0;
    sdmk_name_val = 0;

    if (kernel_sdhci_fat32_create_scratch(&name, &created, &match) == 0) {
        sdmk_name_val = name;
        sdmk_created_val = (int)created;
        sdmk_match_val = (int)match;
        return 0;
    }
    sdmk_name_val = name;
    sdmk_created_val = (int)created;
    sdmk_match_val = (int)match;
    if (match == 1u && name == 0x41455448u) {
        sdmk_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdmk_ok(void)      { return sdmk_ok_val; }
int          kernel_sdmk_match(void)   { return sdmk_match_val; }
int          kernel_sdmk_created(void) { return sdmk_created_val; }
unsigned int kernel_sdmk_name(void)    { return sdmk_name_val; }
