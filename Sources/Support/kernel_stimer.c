// Runtime V84: BCM2711 system timer register probe.
// Read-only. Do not write CS or C0-C3 (GPU uses C0/C2).
// CLO must advance. chans=1 if all four compare slots are live (not all-ones).
// No boot event emit. No EL0 enter after GENET DMA
// (I-abort esr=0xbf000002 elr=0x100002000).

#include "Support.h"
#include <stdint.h>

#define STIMER_BASE 0xFE003000UL

#define G32(off) (*(volatile uint32_t *)(STIMER_BASE + (unsigned long)(off)))

#define ST_CS  0x00U
#define ST_CLO 0x04U
#define ST_CHI 0x08U
#define ST_C0  0x0CU
#define ST_C1  0x10U
#define ST_C2  0x14U
#define ST_C3  0x18U

#define STIMER_CLO_WAIT_TICKS 5400UL /* ~100us @ 54MHz; CLO is 1MHz */

static int stimer_probed;
static int stimer_ok_val;
static unsigned int stimer_clo_val;
static unsigned int stimer_chi_val;
static unsigned int stimer_chans_val;

int kernel_stimer_selftest(void) {
    if (stimer_probed) return stimer_ok_val;
    stimer_probed = 1;
    stimer_ok_val = 0;
    stimer_clo_val = 0;
    stimer_chi_val = 0;
    stimer_chans_val = 0;

    unsigned int cs0 = G32(ST_CS);
    unsigned int cs1 = G32(ST_CS);
    unsigned int clo0 = G32(ST_CLO);
    unsigned int chi0 = G32(ST_CHI);
    unsigned int c0 = G32(ST_C0);
    unsigned int c1 = G32(ST_C1);
    unsigned int c2 = G32(ST_C2);
    unsigned int c3 = G32(ST_C3);

    if (cs0 == 0xFFFFFFFFU || cs0 != cs1) return 0;
    if (clo0 == 0xFFFFFFFFU) return 0;
    if (chi0 == 0xFFFFFFFFU) return 0;
    // CS bits [31:4] are reserved RAZ.
    if ((cs0 & 0xFFFFFFF0U) != 0U) return 0;

    unsigned long start = read_cntpct();
    while ((read_cntpct() - start) < STIMER_CLO_WAIT_TICKS) {
    }
    unsigned int clo1 = G32(ST_CLO);
    unsigned int chi1 = G32(ST_CHI);
    stimer_clo_val = clo1;
    stimer_chi_val = chi1;

    // Non-vacuity: the 1MHz free-running counter moved.
    if (clo1 == clo0 && chi1 == chi0) return 0;

    if (c0 == 0xFFFFFFFFU || c1 == 0xFFFFFFFFU ||
        c2 == 0xFFFFFFFFU || c3 == 0xFFFFFFFFU) {
        return 0;
    }
    stimer_chans_val = 1;
    stimer_ok_val = 1;
    return 1;
}

int          kernel_stimer_ok(void)    { return stimer_ok_val; }
unsigned int kernel_stimer_clo(void)   { return stimer_clo_val; }
unsigned int kernel_stimer_chi(void)   { return stimer_chi_val; }
unsigned int kernel_stimer_chans(void) { return stimer_chans_val; }
