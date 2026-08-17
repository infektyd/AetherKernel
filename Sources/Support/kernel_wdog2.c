// Runtime V98: PM watchdog remaining-tick readback after GENET.
// Arm 8s, read PM_WDOG ticks, disable immediately. Fail-closed if remaining
// is outside (0, 8<<16] or WRCFG full-reset does not clear. Never reset_now.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit.

#include "Support.h"
#include <stdint.h>

#define WDOG2_ARM_SECONDS 8U
#define WDOG2_REMAIN_MIN  1U
#define WDOG2_REMAIN_MAX  (WDOG2_ARM_SECONDS << 16)

static int wdog2_probed;
static int wdog2_ok_val;
static int wdog2_armed_val;
static int wdog2_off_val;
static unsigned int wdog2_remain_val;

int kernel_wdog2_selftest(void) {
    if (wdog2_probed) return wdog2_ok_val;
    wdog2_probed = 1;
    wdog2_ok_val = 0;
    wdog2_armed_val = 0;
    wdog2_off_val = 0;
    wdog2_remain_val = 0;

    watchdog_disable();
    if (watchdog_full_reset_armed()) return 0;

    watchdog_arm_seconds(WDOG2_ARM_SECONDS);
    wdog2_remain_val = watchdog_remaining_ticks();
    wdog2_armed_val = watchdog_full_reset_armed();

    watchdog_disable();
    wdog2_off_val = watchdog_full_reset_armed() ? 0 : 1;

    if (wdog2_remain_val >= WDOG2_REMAIN_MIN &&
        wdog2_remain_val <= WDOG2_REMAIN_MAX &&
        wdog2_armed_val == 1 &&
        wdog2_off_val == 1) {
        wdog2_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_wdog2_ok(void)     { return wdog2_ok_val; }
int          kernel_wdog2_armed(void)  { return wdog2_armed_val; }
int          kernel_wdog2_off(void)    { return wdog2_off_val; }
unsigned int kernel_wdog2_remain(void) { return wdog2_remain_val; }
