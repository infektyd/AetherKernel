// Runtime V105: SDHCI CMD13 SEND_STATUS after GENET.
// Read-only card status. Fail-closed unless CURRENT_STATE is TRAN (4)
// and READY_FOR_DATA is set. No data phase, no FAT write, no EL0 enter
// (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA). No boot
// event emit. Does not arm or reset the watchdog. No large stack buffers.

#include "Support.h"

static int sdst_probed;
static int sdst_ok_val;
static int sdst_ready_val;
static unsigned int sdst_state_val;
static unsigned int sdst_rca_val;

int kernel_sdst_selftest(void) {
    unsigned int state = 0u;
    unsigned int ready = 0u;
    unsigned int rca = 0u;

    if (sdst_probed) return sdst_ok_val;
    sdst_probed = 1;
    sdst_ok_val = 0;
    sdst_ready_val = 0;
    sdst_state_val = 0;
    sdst_rca_val = 0;

    if (kernel_sdhci_card_status(&state, &ready, &rca) == 0) {
        sdst_state_val = state;
        sdst_ready_val = (int)ready;
        sdst_rca_val = rca;
        return 0;
    }
    sdst_state_val = state;
    sdst_ready_val = (int)ready;
    sdst_rca_val = rca;
    if (state == 4u && ready == 1u && rca != 0u) {
        sdst_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdst_ok(void)    { return sdst_ok_val; }
int          kernel_sdst_ready(void) { return sdst_ready_val; }
unsigned int kernel_sdst_state(void) { return sdst_state_val; }
unsigned int kernel_sdst_rca(void)   { return sdst_rca_val; }
