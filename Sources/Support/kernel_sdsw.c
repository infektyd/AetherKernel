// Runtime V110: SDHCI CMD6 SWITCH_FUNC check (mode 0) after GENET.
// 64-byte ADTC. Not ACMD6. Fail-closed unless group-1 default access
// mode is advertised. Restore 512-byte blocks. Shared 512 B scratch
// — not the 4 KiB core0 stack. No FAT write. No bus-width change.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Does not arm or reset the watchdog.

#include "Support.h"

static int sdsw_probed;
static int sdsw_ok_val;
static unsigned int sdsw_mode_val;
static unsigned int sdsw_grp1_val;

int kernel_sdsw_selftest(void) {
    unsigned int mode = 0u;
    unsigned int grp1 = 0u;

    if (sdsw_probed) return sdsw_ok_val;
    sdsw_probed = 1;
    sdsw_ok_val = 0;
    sdsw_mode_val = 0;
    sdsw_grp1_val = 0;

    if (kernel_sdhci_card_switch(&mode, &grp1) == 0) {
        sdsw_mode_val = mode;
        sdsw_grp1_val = grp1;
        return 0;
    }
    sdsw_mode_val = mode;
    sdsw_grp1_val = grp1;
    if ((grp1 & 1u) != 0u) {
        sdsw_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdsw_ok(void)   { return sdsw_ok_val; }
unsigned int kernel_sdsw_mode(void) { return sdsw_mode_val; }
unsigned int kernel_sdsw_grp1(void) { return sdsw_grp1_val; }
