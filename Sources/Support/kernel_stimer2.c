// Runtime V92: BCM2711 system timer C1 match after GENET.
// ARM owns C1/C3. Do not write C0/C2 (GPU). Clear M1 only (CS bit 1).
// Park C1 after the match so it does not keep asserting.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit.

#include "Support.h"
#include <stdint.h>

#define STIMER_BASE 0xFE003000UL
#define G32(off) (*(volatile uint32_t *)(STIMER_BASE + (unsigned long)(off)))

#define ST_CS  0x00U
#define ST_CLO 0x04U
#define ST_C1  0x10U

#define ST_CS_M1           (1U << 1)
#define STIMER2_DELTA      200U          /* 200us @ 1MHz CLO */
#define STIMER2_WAIT_TICKS 108000UL      /* ~2ms @ 54MHz CNTPCT */
#define STIMER2_PARK       0x40000000U   /* ~1074s @ 1MHz */

static int stimer2_probed;
static int stimer2_ok_val;
static unsigned int stimer2_chan_val;
static unsigned int stimer2_match_val;

int kernel_stimer2_selftest(void) {
    if (stimer2_probed) return stimer2_ok_val;
    stimer2_probed = 1;
    stimer2_ok_val = 0;
    stimer2_chan_val = 1;
    stimer2_match_val = 0;

    if (kernel_stimer_selftest() == 0) return 0;

    G32(ST_CS) = ST_CS_M1;
    unsigned int clo = G32(ST_CLO);
    if (clo == 0xFFFFFFFFU) return 0;
    G32(ST_C1) = clo + STIMER2_DELTA;

    unsigned long start = read_cntpct();
    while ((read_cntpct() - start) < STIMER2_WAIT_TICKS) {
        if ((G32(ST_CS) & ST_CS_M1) != 0U) {
            stimer2_match_val = 1;
            break;
        }
    }

    G32(ST_CS) = ST_CS_M1;
    G32(ST_C1) = G32(ST_CLO) + STIMER2_PARK;

    if (stimer2_match_val == 1U) {
        stimer2_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_stimer2_ok(void)    { return stimer2_ok_val; }
unsigned int kernel_stimer2_chan(void)  { return stimer2_chan_val; }
unsigned int kernel_stimer2_match(void) { return stimer2_match_val; }
