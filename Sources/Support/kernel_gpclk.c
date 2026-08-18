// Runtime V144: BCM2711 GPCLK0 clock enable after GENET.
// New unused CM block (CM_GP0 0xFE101070). Fail-closed OSC+ENAB
// write+readback + leftover restore. No pin-mux. No output claim.
// UART2  after GENET hung AXI/UART — do not retry UART2-5.
// Do not probe BSC0 after GENET (AXI hang, UART dies).
// Do not assert SPI0 TA after GENET (hung boot).
// CM_SMI 0xFE1010B0 after GENET hung AXI/UART (4.9W) — do not retry CM_SMI or SMI CS.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit.

#include "Support.h"
#include <stdint.h>

#define CM_GP0CTL 0xFE101070UL
#define CM_GP0DIV 0xFE101074UL
#define CM32(addr) (*(volatile uint32_t *)(addr))

#define CM_PASSWD  0x5A000000U
#define CM_SRC_OSC 1U
#define CM_ENAB    (1U << 4)
#define CM_BUSY    (1U << 7)
#define CM_SRC_MASK 0x0FU
#define CM_PROG     (CM_ENAB | CM_SRC_MASK)

#define GPCLK_DIVI       50U
#define GPCLK_WAIT_TICKS 108000UL /* ~2ms @ 54MHz CNTPCT */

static int gpclk_probed;
static int gpclk_ok_val;
static unsigned int gpclk_clk_val;
static unsigned int gpclk_restore_val;

static int gpclk_wait_busy(unsigned int want) {
    unsigned long start = read_cntpct();
    while ((read_cntpct() - start) < GPCLK_WAIT_TICKS) {
        unsigned int ctl = CM32(CM_GP0CTL);
        if (ctl == 0xFFFFFFFFU) return 0;
        if (want) {
            if ((ctl & CM_BUSY) != 0U) return 1;
        } else if ((ctl & CM_BUSY) == 0U) {
            return 1;
        }
    }
    return 0;
}

int kernel_gpclk_selftest(void) {
    if (gpclk_probed) return gpclk_ok_val;
    gpclk_probed = 1;
    gpclk_ok_val = 0;
    gpclk_clk_val = 0;
    gpclk_restore_val = 0;

    unsigned int saved_cm_ctl = CM32(CM_GP0CTL);
    unsigned int saved_cm_div = CM32(CM_GP0DIV);
    if (saved_cm_ctl == 0xFFFFFFFFU || saved_cm_div == 0xFFFFFFFFU) {
        return 0;
    }

    CM32(CM_GP0CTL) = CM_PASSWD | (saved_cm_ctl & 0x0FU);
    if (gpclk_wait_busy(0) == 0) {
        CM32(CM_GP0CTL) = CM_PASSWD | (saved_cm_ctl & 0xFFU);
        return 0;
    }

    CM32(CM_GP0DIV) = CM_PASSWD | (GPCLK_DIVI << 12);
    CM32(CM_GP0CTL) = CM_PASSWD | CM_ENAB | CM_SRC_OSC;
    if (gpclk_wait_busy(1) != 0) gpclk_clk_val = 1;

    CM32(CM_GP0CTL) = CM_PASSWD | (saved_cm_ctl & 0x0FU);
    (void)gpclk_wait_busy(0);
    CM32(CM_GP0DIV) = CM_PASSWD | (saved_cm_div & 0x00FFFFFFU);
    CM32(CM_GP0CTL) = CM_PASSWD | (saved_cm_ctl & 0xFFU);

    if ((CM32(CM_GP0CTL) & CM_PROG) == (saved_cm_ctl & CM_PROG) &&
        (CM32(CM_GP0DIV) & 0x00FFFFFFU) == (saved_cm_div & 0x00FFFFFFU)) {
        gpclk_restore_val = 1;
    }

    if (gpclk_clk_val == 1U && gpclk_restore_val == 1U) {
        gpclk_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_gpclk_ok(void)      { return gpclk_ok_val; }
unsigned int kernel_gpclk_clk(void)     { return gpclk_clk_val; }
unsigned int kernel_gpclk_restore(void) { return gpclk_restore_val; }
