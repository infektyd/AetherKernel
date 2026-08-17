// Runtime V96: VideoCore GET_TEMPERATURE after GENET.
// Millidegrees from mailbox tag 0x00030006. Fail-closed if the call fails
// or the value is outside (0, 120000). Honest token reports the raw value.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit.

#include "Support.h"
#include <stdint.h>

#define MBOXT_TEMP_MIN 1U
#define MBOXT_TEMP_MAX 119999U

static int mboxt_probed;
static int mboxt_ok_val;
static unsigned int mboxt_temp_val;

int kernel_mboxt_selftest(void) {
    if (mboxt_probed) return mboxt_ok_val;
    mboxt_probed = 1;
    mboxt_ok_val = 0;
    mboxt_temp_val = 0;

    if (kernel_vc_mbox_selftest() == 0) return 0;

    unsigned int temp = 0;
    if (kernel_vc_mbox_get_temp(&temp) == 0) return 0;
    mboxt_temp_val = temp;

    if (temp >= MBOXT_TEMP_MIN && temp <= MBOXT_TEMP_MAX) {
        mboxt_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_mboxt_ok(void)   { return mboxt_ok_val; }
unsigned int kernel_mboxt_temp(void) { return mboxt_temp_val; }
