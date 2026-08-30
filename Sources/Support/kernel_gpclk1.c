// Runtime V146: BCM2711 GPCLK1 clock enable after GENET.
// New unused CM block (CM_GP1 0xFE101078). Fail-closed OSC+ENAB
// write+readback + leftover restore. No pin-mux. No output claim.
// V145 SMI clock manager after GENET hung AXI/UART — do not retry SMI.
// UART2 after GENET hung AXI/UART — do not retry UART2-5.
// Do not probe BSC0 after GENET (AXI hang, UART dies).
// Do not assert SPI0 TA after GENET (hung boot).
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit.

#include "Support.h"
#include <stdint.h>

#define CM_GP1CTL 0xFE101078UL
#define CM_GP1DIV 0xFE10107CUL
#define CM32(addr) (*(volatile uint32_t *)(addr))

#define CM_PASSWD  0x5A000000U
#define CM_SRC_OSC 1U
#define CM_ENAB    (1U << 4)
#define CM_BUSY    (1U << 7)
#define CM_SRC_MASK 0x0FU
#define CM_PROG     (CM_ENAB | CM_SRC_MASK)

#define GPCLK1_DIVI       50U
#define GPCLK1_WAIT_TICKS 108000UL /* ~2ms @ 54MHz CNTPCT */

static int gpclk1_probed;
static int gpclk1_ok_val;
static unsigned int gpclk1_clk_val;
static unsigned int gpclk1_restore_val;

static int gpclk1_wait_busy(unsigned int want) {
    unsigned long start = read_cntpct();
    while ((read_cntpct() - start) < GPCLK1_WAIT_TICKS) {
        unsigned int ctl = CM32(CM_GP1CTL);
        if (ctl == 0xFFFFFFFFU) return 0;
        if (want) {
            if ((ctl & CM_BUSY) != 0U) return 1;
        } else if ((ctl & CM_BUSY) == 0U) {
            return 1;
        }
    }
    return 0;
}

int kernel_gpclk1_selftest(void) {
    if (gpclk1_probed) return gpclk1_ok_val;
    gpclk1_probed = 1;
    gpclk1_ok_val = 0;
    gpclk1_clk_val = 0;
    gpclk1_restore_val = 0;

    unsigned int saved_cm_ctl = CM32(CM_GP1CTL);
    unsigned int saved_cm_div = CM32(CM_GP1DIV);
    if (saved_cm_ctl == 0xFFFFFFFFU || saved_cm_div == 0xFFFFFFFFU) {
        return 0;
    }

    CM32(CM_GP1CTL) = CM_PASSWD | (saved_cm_ctl & 0x0FU);
    if (gpclk1_wait_busy(0) == 0) {
        CM32(CM_GP1CTL) = CM_PASSWD | (saved_cm_ctl & 0xFFU);
        return 0;
    }

    CM32(CM_GP1DIV) = CM_PASSWD | (GPCLK1_DIVI << 12);
    CM32(CM_GP1CTL) = CM_PASSWD | CM_ENAB | CM_SRC_OSC;
    if (gpclk1_wait_busy(1) != 0) gpclk1_clk_val = 1;

    CM32(CM_GP1CTL) = CM_PASSWD | (saved_cm_ctl & 0x0FU);
    (void)gpclk1_wait_busy(0);
    CM32(CM_GP1DIV) = CM_PASSWD | (saved_cm_div & 0x00FFFFFFU);
    CM32(CM_GP1CTL) = CM_PASSWD | (saved_cm_ctl & 0xFFU);

    if ((CM32(CM_GP1CTL) & CM_PROG) == (saved_cm_ctl & CM_PROG) &&
        (CM32(CM_GP1DIV) & 0x00FFFFFFU) == (saved_cm_div & 0x00FFFFFFU)) {
        gpclk1_restore_val = 1;
    }

    if (gpclk1_clk_val == 1U && gpclk1_restore_val == 1U) {
        gpclk1_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_gpclk1_ok(void)      { return gpclk1_ok_val; }
unsigned int kernel_gpclk1_clk(void)     { return gpclk1_clk_val; }
unsigned int kernel_gpclk1_restore(void) { return gpclk1_restore_val; }
