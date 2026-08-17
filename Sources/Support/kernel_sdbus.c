// Runtime V108: SDHCI ACMD6 SET_BUS_WIDTH after GENET.
// Switch card+host to 4-bit, fail-closed MBR 0xAA55, restore 1-bit.
// Shared 512 B scratch — not the 4 KiB core0 stack. No FAT write.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Does not arm or reset the watchdog.

#include "Support.h"

static int sdbus_probed;
static int sdbus_ok_val;
static unsigned int sdbus_bits_val;
static unsigned int sdbus_host_val;
static unsigned int sdbus_mbr_val;

int kernel_sdbus_selftest(void) {
    unsigned int bits = 0u;
    unsigned int host = 0u;
    unsigned int mbr = 0u;

    if (sdbus_probed) return sdbus_ok_val;
    sdbus_probed = 1;
    sdbus_ok_val = 0;
    sdbus_bits_val = 0;
    sdbus_host_val = 0;
    sdbus_mbr_val = 0;

    if (kernel_sdhci_card_bus_width(&bits, &host, &mbr) == 0) {
        sdbus_bits_val = bits;
        sdbus_host_val = host;
        sdbus_mbr_val = mbr;
        return 0;
    }
    sdbus_bits_val = bits;
    sdbus_host_val = host;
    sdbus_mbr_val = mbr;
    if (bits == 4u && host == 4u && mbr == 1u) {
        sdbus_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdbus_ok(void)   { return sdbus_ok_val; }
unsigned int kernel_sdbus_bits(void) { return sdbus_bits_val; }
unsigned int kernel_sdbus_host(void) { return sdbus_host_val; }
unsigned int kernel_sdbus_mbr(void)  { return sdbus_mbr_val; }
