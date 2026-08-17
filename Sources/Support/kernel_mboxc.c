// Runtime V97: VideoCore GET_CLOCK_RATE (ARM) after GENET.
// Hz from mailbox tag 0x00030002, clock id 3 (CLK_ARM). Fail-closed if the
// call fails, the echoed id mismatches, or Hz is outside (100000000, 3000000000].
// Honest token reports the raw rate. No EL0 enter (I-abort esr=0xbf000002
// elr=0x100002000 after GENET DMA). No boot event emit.

#include "Support.h"
#include <stdint.h>

#define MBOXC_CLK_ARM 3U
#define MBOXC_HZ_MIN  100000001U
#define MBOXC_HZ_MAX  3000000000U

static int mboxc_probed;
static int mboxc_ok_val;
static unsigned int mboxc_clk_val;
static unsigned int mboxc_hz_val;

int kernel_mboxc_selftest(void) {
    if (mboxc_probed) return mboxc_ok_val;
    mboxc_probed = 1;
    mboxc_ok_val = 0;
    mboxc_clk_val = MBOXC_CLK_ARM;
    mboxc_hz_val = 0;

    if (kernel_vc_mbox_selftest() == 0) return 0;

    unsigned int hz = 0;
    if (kernel_vc_mbox_get_clock_rate(MBOXC_CLK_ARM, &hz) == 0) return 0;
    mboxc_hz_val = hz;

    if (hz >= MBOXC_HZ_MIN && hz <= MBOXC_HZ_MAX) {
        mboxc_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_mboxc_ok(void)  { return mboxc_ok_val; }
unsigned int kernel_mboxc_clk(void) { return mboxc_clk_val; }
unsigned int kernel_mboxc_hz(void)  { return mboxc_hz_val; }
