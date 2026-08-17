// Runtime V106: SDHCI ACMD51 SEND_SCR after GENET.
// 8-byte ADTC. Fail-closed unless SCR_STRUCTURE is 0, SD_SPEC<=3, and
// 4-bit bus is advertised. Restores 512-byte block length. No FAT write.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Does not arm or reset the watchdog. No large stack.

#include "Support.h"

static int sdscr_probed;
static int sdscr_ok_val;
static unsigned int sdscr_spec_val;
static unsigned int sdscr_bus_val;

int kernel_sdscr_selftest(void) {
    unsigned int spec = 0u;
    unsigned int bus = 0u;

    if (sdscr_probed) return sdscr_ok_val;
    sdscr_probed = 1;
    sdscr_ok_val = 0;
    sdscr_spec_val = 0;
    sdscr_bus_val = 0;

    if (kernel_sdhci_card_scr(&spec, &bus) == 0) {
        sdscr_spec_val = spec;
        sdscr_bus_val = bus;
        return 0;
    }
    sdscr_spec_val = spec;
    sdscr_bus_val = bus;
    if (spec <= 3u && (bus & 4u) != 0u) {
        sdscr_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdscr_ok(void)   { return sdscr_ok_val; }
unsigned int kernel_sdscr_spec(void) { return sdscr_spec_val; }
unsigned int kernel_sdscr_bus(void)  { return sdscr_bus_val; }
