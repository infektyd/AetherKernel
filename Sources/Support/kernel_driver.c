#include "Support.h"

//===----------------------------------------------------------------------===//
// Runtime V24 fixed driver registry.
//
// This is metadata and observability for the drivers AetherKernel already has:
// UART0, CNTP, GIC, and watchdog. It does not rewrite those drivers or put any
// allocation on their hot paths. Driver records are fixed and object-backed so
// Swift/host tooling can reason about handles, INTIDs, bases, and counters.
//===----------------------------------------------------------------------===//

#define KERNEL_DRIVER_CAPACITY_VALUE 4U
#define UART0_BASE_ADDR 0xFE201000UL
#define GICD_BASE_ADDR  0xFF841000UL
#define WATCHDOG_BASE_ADDR 0xFE10001CUL
#define CNTP_INTID_VALUE 30U
#define UART0_INTID_VALUE 153U

typedef struct kernel_driver_record {
    unsigned int active;
    unsigned int object_id;
    unsigned int state;
    unsigned int intid;
    unsigned long base;
    unsigned int caps;
    const unsigned char *name;
    unsigned int name_len;
} kernel_driver_record;

static kernel_driver_record drivers[KERNEL_DRIVER_CAPACITY_VALUE];
static unsigned int driver_initialized;
static unsigned int driver_count_value;

static const unsigned char driver_uart0_name[] = "uart0";
static const unsigned char driver_cntp_name[] = "cntp";
static const unsigned char driver_gic_name[] = "gic";
static const unsigned char driver_watchdog_name[] = "watchdog";

static unsigned int bytes_len(const unsigned char *name) {
    unsigned int n = 0;
    while (name[n] != '\0') {
        n++;
    }
    return n;
}

static void clear_drivers_unsafe(void) {
    for (unsigned int i = 0; i < KERNEL_DRIVER_CAPACITY_VALUE; i++) {
        drivers[i].active = 0;
        drivers[i].object_id = 0;
        drivers[i].state = 0;
        drivers[i].intid = 0;
        drivers[i].base = 0;
        drivers[i].caps = 0;
        drivers[i].name = 0;
        drivers[i].name_len = 0;
    }
    driver_count_value = 0;
}

static void driver_register_unsafe(unsigned int driver_id,
                                   unsigned int object_id,
                                   const unsigned char *name,
                                   unsigned int intid,
                                   unsigned long base,
                                   unsigned int caps) {
    if (driver_id >= KERNEL_DRIVER_CAPACITY_VALUE || object_id == 0) {
        return;
    }

    if (!drivers[driver_id].active) {
        driver_count_value++;
    }
    drivers[driver_id].active = 1;
    drivers[driver_id].object_id = object_id;
    drivers[driver_id].state = KERNEL_DRIVER_STATE_READY;
    drivers[driver_id].intid = intid;
    drivers[driver_id].base = base;
    drivers[driver_id].caps = caps;
    drivers[driver_id].name = name;
    drivers[driver_id].name_len = bytes_len(name);
}

static unsigned int register_driver_object(const unsigned char *name, unsigned int caps) {
    (void)caps;
    return kernel_object_register(KERNEL_OBJECT_KIND_DRIVER,
                                  KERNEL_OBJECT_FLAG_ACTIVE,
                                  name,
                                  bytes_len(name));
}

void kernel_driver_registry_init(void) {
    unsigned long flags = irq_save();
    clear_drivers_unsafe();
    driver_initialized = 1;
    irq_restore(flags);

    unsigned int driver_caps = KERNEL_OBJECT_CAP_INSPECT | KERNEL_OBJECT_CAP_CONTROL;
    unsigned int uart0_object = register_driver_object(driver_uart0_name, driver_caps);
    unsigned int cntp_object = register_driver_object(driver_cntp_name, driver_caps);
    unsigned int gic_object = register_driver_object(driver_gic_name, driver_caps);
    unsigned int watchdog_object = register_driver_object(driver_watchdog_name, driver_caps);

    flags = irq_save();
    driver_register_unsafe(KERNEL_DRIVER_ID_UART0, uart0_object, driver_uart0_name,
                           UART0_INTID_VALUE, UART0_BASE_ADDR, driver_caps);
    driver_register_unsafe(KERNEL_DRIVER_ID_CNTP, cntp_object, driver_cntp_name,
                           CNTP_INTID_VALUE, 0, driver_caps);
    driver_register_unsafe(KERNEL_DRIVER_ID_GIC, gic_object, driver_gic_name,
                           0, GICD_BASE_ADDR, driver_caps);
    driver_register_unsafe(KERNEL_DRIVER_ID_WATCHDOG, watchdog_object, driver_watchdog_name,
                           0, WATCHDOG_BASE_ADDR, driver_caps);
    irq_restore(flags);
}

static void ensure_initialized(void) {
    if (!driver_initialized) {
        kernel_driver_registry_init();
    }
}

static kernel_driver_record *driver_at(unsigned int driver_id) {
    if (driver_id >= KERNEL_DRIVER_CAPACITY_VALUE || !drivers[driver_id].active) {
        return 0;
    }
    return &drivers[driver_id];
}

unsigned int kernel_driver_count(void) {
    unsigned long flags = irq_save();
    ensure_initialized();
    unsigned int count = driver_count_value;
    irq_restore(flags);
    return count;
}

unsigned int kernel_driver_capacity(void) {
    return KERNEL_DRIVER_CAPACITY_VALUE;
}

unsigned int kernel_driver_object_id(unsigned int driver_id) {
    unsigned long flags = irq_save();
    ensure_initialized();
    kernel_driver_record *driver = driver_at(driver_id);
    unsigned int value = driver ? driver->object_id : 0;
    irq_restore(flags);
    return value;
}

unsigned long kernel_driver_handle(unsigned int driver_id) {
    unsigned int object_id = kernel_driver_object_id(driver_id);
    if (object_id == 0) {
        return KERNEL_OBJECT_HANDLE_INVALID;
    }
    return kernel_object_make_handle(object_id - 1U,
                                     KERNEL_OBJECT_CAP_INSPECT | KERNEL_OBJECT_CAP_CONTROL);
}

unsigned int kernel_driver_name_len(unsigned int driver_id) {
    unsigned long flags = irq_save();
    ensure_initialized();
    kernel_driver_record *driver = driver_at(driver_id);
    unsigned int value = driver ? driver->name_len : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_driver_name_byte(unsigned int driver_id, unsigned int offset) {
    unsigned long flags = irq_save();
    ensure_initialized();
    kernel_driver_record *driver = driver_at(driver_id);
    unsigned int value = 0;
    if (driver && offset < driver->name_len) {
        value = (unsigned int)(unsigned char)driver->name[offset];
    }
    irq_restore(flags);
    return value;
}

unsigned int kernel_driver_state(unsigned int driver_id) {
    unsigned long flags = irq_save();
    ensure_initialized();
    kernel_driver_record *driver = driver_at(driver_id);
    unsigned int value = driver ? driver->state : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_driver_intid(unsigned int driver_id) {
    unsigned long flags = irq_save();
    ensure_initialized();
    kernel_driver_record *driver = driver_at(driver_id);
    unsigned int value = driver ? driver->intid : 0;
    irq_restore(flags);
    return value;
}

unsigned long kernel_driver_base(unsigned int driver_id) {
    unsigned long flags = irq_save();
    ensure_initialized();
    kernel_driver_record *driver = driver_at(driver_id);
    unsigned long value = driver ? driver->base : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_driver_caps(unsigned int driver_id) {
    unsigned long flags = irq_save();
    ensure_initialized();
    kernel_driver_record *driver = driver_at(driver_id);
    unsigned int value = driver ? driver->caps : 0;
    irq_restore(flags);
    return value;
}

unsigned long kernel_driver_irq_count(unsigned int driver_id) {
    if (driver_id == KERNEL_DRIVER_ID_UART0) {
        return kernel_irq_uart0_count();
    }
    if (driver_id == KERNEL_DRIVER_ID_CNTP) {
        return kernel_irq_cntp_count();
    }
    if (driver_id == KERNEL_DRIVER_ID_GIC) {
        return kernel_irq_total_count();
    }
    return 0;
}

unsigned long kernel_driver_error_count(unsigned int driver_id) {
    if (driver_id == KERNEL_DRIVER_ID_UART0) {
        return uart_rx_overflow_count();
    }
    if (driver_id == KERNEL_DRIVER_ID_GIC) {
        return kernel_irq_unknown_count();
    }
    return 0;
}

unsigned long kernel_driver_operation_count(unsigned int driver_id) {
    if (driver_id == KERNEL_DRIVER_ID_UART0) {
        return uart_rx_ring_count();
    }
    if (driver_id == KERNEL_DRIVER_ID_CNTP) {
        return kernel_timer_active_count();
    }
    if (driver_id == KERNEL_DRIVER_ID_GIC) {
        return kernel_irq_total_count();
    }
    if (driver_id == KERNEL_DRIVER_ID_WATCHDOG) {
        return watchdog_reset_count() + watchdog_arm_count() +
               watchdog_pet_count() + watchdog_disable_count();
    }
    return 0;
}

int kernel_driver_registry_selftest(void) {
    ensure_initialized();
    if (kernel_driver_capacity() != KERNEL_DRIVER_CAPACITY_VALUE ||
        kernel_driver_count() != KERNEL_DRIVER_CAPACITY_VALUE) {
        return 0;
    }

    for (unsigned int i = 0; i < KERNEL_DRIVER_CAPACITY_VALUE; i++) {
        if (kernel_driver_object_id(i) == 0 ||
            kernel_driver_handle(i) == KERNEL_OBJECT_HANDLE_INVALID ||
            kernel_driver_name_len(i) == 0 ||
            kernel_driver_state(i) != KERNEL_DRIVER_STATE_READY ||
            kernel_driver_caps(i) == 0) {
            return 0;
        }
    }

    return kernel_driver_intid(KERNEL_DRIVER_ID_UART0) == UART0_INTID_VALUE &&
           kernel_driver_intid(KERNEL_DRIVER_ID_CNTP) == CNTP_INTID_VALUE &&
           kernel_driver_base(KERNEL_DRIVER_ID_UART0) == UART0_BASE_ADDR &&
           kernel_driver_base(KERNEL_DRIVER_ID_GIC) == GICD_BASE_ADDR &&
           kernel_driver_base(KERNEL_DRIVER_ID_WATCHDOG) == WATCHDOG_BASE_ADDR;
}
