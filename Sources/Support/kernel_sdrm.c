// Runtime V116: FAT32 scratch unlink after GENET.
// Marks the dedicated scratch dirent 0xE5 and frees its cluster.
// Fail-closed unless the V103 512-byte 0xA1030000 payload matches.
// No boot-file names. No EL0 enter (I-abort esr=0xbf000002
// elr=0x100002000 after GENET DMA). No boot event emit.
// Does not arm or reset the watchdog.

#include "Support.h"

static int sdrm_probed;
static int sdrm_ok_val;
static unsigned int sdrm_deleted_val;
static unsigned int sdrm_present_val;

int kernel_sdrm_selftest(void) {
    unsigned int deleted = 0u;
    unsigned int present = 0u;

    if (sdrm_probed) return sdrm_ok_val;
    sdrm_probed = 1;
    sdrm_ok_val = 0;
    sdrm_deleted_val = 0;
    sdrm_present_val = 0;

    if (kernel_sdhci_fat32_unlink_scratch(&deleted, &present) == 0) {
        sdrm_deleted_val = deleted;
        sdrm_present_val = present;
        return 0;
    }
    sdrm_deleted_val = deleted;
    sdrm_present_val = present;
    if (deleted == 1u && present == 0u) {
        sdrm_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdrm_ok(void)      { return sdrm_ok_val; }
unsigned int kernel_sdrm_deleted(void) { return sdrm_deleted_val; }
unsigned int kernel_sdrm_present(void) { return sdrm_present_val; }
