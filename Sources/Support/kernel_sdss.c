// Runtime V107: SDHCI ACMD13 SD_STATUS after GENET.
// 64-byte ADTC. Fail-closed unless SD_CARD_TYPE is SD (0) or SDHC/SDXC
// (1). Restores 512-byte block length. Shared 512 B scratch — not the
// 4 KiB core0 stack. No FAT write. No EL0 enter (I-abort esr=0xbf000002
// elr=0x100002000 after GENET DMA). No boot event emit. Does not arm
// or reset the watchdog.

#include "Support.h"

static int sdss_probed;
static int sdss_ok_val;
static unsigned int sdss_type_val;
static unsigned int sdss_class_val;

int kernel_sdss_selftest(void) {
    unsigned int type = 0u;
    unsigned int cls = 0u;

    if (sdss_probed) return sdss_ok_val;
    sdss_probed = 1;
    sdss_ok_val = 0;
    sdss_type_val = 0;
    sdss_class_val = 0;

    if (kernel_sdhci_card_sd_status(&type, &cls) == 0) {
        sdss_type_val = type;
        sdss_class_val = cls;
        return 0;
    }
    sdss_type_val = type;
    sdss_class_val = cls;
    if (type <= 1u) {
        sdss_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdss_ok(void)    { return sdss_ok_val; }
unsigned int kernel_sdss_type(void)  { return sdss_type_val; }
unsigned int kernel_sdss_class(void) { return sdss_class_val; }
