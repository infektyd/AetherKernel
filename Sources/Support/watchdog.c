#include "Support.h"

//===----------------------------------------------------------------------===//
// AetherKernel — BCM2711 watchdog / power-manager reset.
//
// The PM block drives the SoC watchdog. We use it two ways:
//   - watchdog_reset_now(): reboot the board immediately (firmware reloads
//     kernel8.img). The building block for an autonomous flash/reboot loop.
//   - watchdog_arm_seconds()/watchdog_pet_seconds(): a hang detector — if the
//     kernel stops petting it, the board auto-reboots instead of dying silently.
//
// Registers (low-peripheral mode, peripheral base 0xFE000000; PM at +0x100000):
//   PM_RSTC 0xFE10001C, PM_RSTS 0xFE100020, PM_WDOG 0xFE100024.
// Every PM write needs the password 0x5A in bits [31:24]. The WDOG timeout is a
// 20-bit count of watchdog ticks; 1 second ≈ (1 << 16) ticks (max ~15 s).
//===----------------------------------------------------------------------===//

#define PM_RSTC 0xFE10001CUL
#define PM_RSTS 0xFE100020UL
#define PM_WDOG 0xFE100024UL

#define PM_PASSWORD              0x5A000000U
#define PM_RSTC_WRCFG_CLR        0xFFFFFFCFU  // clear the WRCFG field (bits [5:4])
#define PM_RSTC_WRCFG_FULL_RESET 0x00000020U
#define PM_WDOG_TIME_MASK        0x000FFFFFU  // 20-bit timeout

// Partition number lives in PM_RSTS bits {0,2,4,6,8,10}; 0 = boot normally.
#define PM_RSTS_PARTITION_BITS   0x00000555U

static unsigned long watchdog_resets;
static unsigned long watchdog_arms;
static unsigned long watchdog_pets;
static unsigned long watchdog_disables;

// Arm the watchdog to perform a full reset after `ticks` watchdog ticks.
static void wdog_start(unsigned int ticks) {
    // Force boot partition 0 so the firmware reloads kernel8.img as usual.
    unsigned int rsts = mmio_read32(PM_RSTS) & ~PM_RSTS_PARTITION_BITS;
    mmio_write32(PM_RSTS, PM_PASSWORD | rsts);

    mmio_write32(PM_WDOG, PM_PASSWORD | (ticks & PM_WDOG_TIME_MASK));

    unsigned int rstc = (mmio_read32(PM_RSTC) & PM_RSTC_WRCFG_CLR)
                        | PM_RSTC_WRCFG_FULL_RESET;
    mmio_write32(PM_RSTC, PM_PASSWORD | rstc);
}

// Reboot the board now (~150 us out; effectively immediate).
void watchdog_reset_now(void) {
    unsigned long flags = irq_save();
    watchdog_resets++;
    irq_restore(flags);
    wdog_start(10);
}

// Arm a hang-detector: the board reboots after `seconds` unless re-armed
// (watchdog_pet_seconds) or cancelled (watchdog_disable) first.
void watchdog_arm_seconds(unsigned int seconds) {
    unsigned long flags = irq_save();
    watchdog_arms++;
    irq_restore(flags);
    wdog_start(seconds << 16);
}

// Re-arm (pet) the watchdog to push the deadline out again.
void watchdog_pet_seconds(unsigned int seconds) {
    unsigned long flags = irq_save();
    watchdog_pets++;
    irq_restore(flags);
    wdog_start(seconds << 16);
}

// Cancel a pending reset (clear the WRCFG full-reset configuration).
void watchdog_disable(void) {
    unsigned long flags = irq_save();
    watchdog_disables++;
    irq_restore(flags);

    unsigned int rstc = mmio_read32(PM_RSTC) & PM_RSTC_WRCFG_CLR;
    mmio_write32(PM_RSTC, PM_PASSWORD | rstc);
}

unsigned long watchdog_reset_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = watchdog_resets;
    irq_restore(flags);
    return count;
}

unsigned long watchdog_arm_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = watchdog_arms;
    irq_restore(flags);
    return count;
}

unsigned long watchdog_pet_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = watchdog_pets;
    irq_restore(flags);
    return count;
}

unsigned long watchdog_disable_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = watchdog_disables;
    irq_restore(flags);
    return count;
}
