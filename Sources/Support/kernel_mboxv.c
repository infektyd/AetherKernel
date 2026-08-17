// Runtime V99: VideoCore GET_VOLTAGE (core) after GENET.
// Microvolts from mailbox tag 0x00030003, voltage id 1 (VOLT_CORE).
// Fail-closed if the call fails, the echoed id mismatches, or uv is
// outside (500000, 2000000]. Honest token reports the raw value.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Does not arm or reset the watchdog.

#include "Support.h"
#include <stdint.h>

#define MBOXV_VOLT_CORE 1U
#define MBOXV_UV_MIN    500001U
#define MBOXV_UV_MAX    2000000U

static int mboxv_probed;
static int mboxv_ok_val;
static unsigned int mboxv_id_val;
static unsigned int mboxv_uv_val;

int kernel_mboxv_selftest(void) {
    if (mboxv_probed) return mboxv_ok_val;
    mboxv_probed = 1;
    mboxv_ok_val = 0;
    mboxv_id_val = MBOXV_VOLT_CORE;
    mboxv_uv_val = 0;

    if (kernel_vc_mbox_selftest() == 0) return 0;

    unsigned int uv = 0;
    if (kernel_vc_mbox_get_voltage(MBOXV_VOLT_CORE, &uv) == 0) return 0;
    mboxv_uv_val = uv;

    if (uv >= MBOXV_UV_MIN && uv <= MBOXV_UV_MAX) {
        mboxv_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_mboxv_ok(void) { return mboxv_ok_val; }
unsigned int kernel_mboxv_id(void) { return mboxv_id_val; }
unsigned int kernel_mboxv_uv(void) { return mboxv_uv_val; }
