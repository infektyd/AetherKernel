// Runtime V95: BCM2711 system timer C3 match after GENET.
// ARM owns C1/C3. Do not write C0/C2 (GPU) or C1 (V92). Clear M3 only.
// Park C3 after the match so it does not keep asserting.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit.

#include "Support.h"
#include <stdint.h>

#define STIMER_BASE 0xFE003000UL
#define G32(off) (*(volatile uint32_t *)(STIMER_BASE + (unsigned long)(off)))

#define ST_CS  0x00U
#define ST_CLO 0x04U
#define ST_C3  0x18U

#define ST_CS_M3           (1U << 3)
#define STIMER3_DELTA      200U          /* 200us @ 1MHz CLO */
#define STIMER3_WAIT_TICKS 108000UL      /* ~2ms @ 54MHz CNTPCT */
#define STIMER3_PARK       0x40000000U   /* ~1074s @ 1MHz */

static int stimer3_probed;
static int stimer3_ok_val;
static unsigned int stimer3_chan_val;
static unsigned int stimer3_match_val;

int kernel_stimer3_selftest(void) {
    if (stimer3_probed) return stimer3_ok_val;
    stimer3_probed = 1;
    stimer3_ok_val = 0;
    stimer3_chan_val = 3;
    stimer3_match_val = 0;

    if (kernel_stimer_selftest() == 0) return 0;

    G32(ST_CS) = ST_CS_M3;
    unsigned int clo = G32(ST_CLO);
    if (clo == 0xFFFFFFFFU) return 0;
    G32(ST_C3) = clo + STIMER3_DELTA;

    unsigned long start = read_cntpct();
    while ((read_cntpct() - start) < STIMER3_WAIT_TICKS) {
        if ((G32(ST_CS) & ST_CS_M3) != 0U) {
            stimer3_match_val = 1;
            break;
        }
    }

    G32(ST_CS) = ST_CS_M3;
    G32(ST_C3) = G32(ST_CLO) + STIMER3_PARK;

    if (stimer3_match_val == 1U) {
        stimer3_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_stimer3_ok(void)    { return stimer3_ok_val; }
unsigned int kernel_stimer3_chan(void)  { return stimer3_chan_val; }
unsigned int kernel_stimer3_match(void) { return stimer3_match_val; }
