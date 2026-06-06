#include "Support.h"

//===----------------------------------------------------------------------===//
// Runtime V13 fixed mailbox queues.
//
// Bounded UInt64 FIFOs with stable counters. This layer deliberately owns only
// storage and accounting; Swift tasks decide when to await/retry.
//===----------------------------------------------------------------------===//

#define KERNEL_MAILBOX_CAPACITY_VALUE       4U
#define KERNEL_MAILBOX_QUEUE_CAPACITY_VALUE 8U

typedef struct kernel_mailbox_record {
    unsigned int active;
    unsigned int object_id;
    const unsigned char *name;
    unsigned int name_len;
    unsigned int head;
    unsigned int tail;
    unsigned int depth;
    unsigned long queue[KERNEL_MAILBOX_QUEUE_CAPACITY_VALUE];
    unsigned long sent;
    unsigned long received;
    unsigned long drops;
    unsigned int last_error;
} kernel_mailbox_record;

static kernel_mailbox_record mailboxes[KERNEL_MAILBOX_CAPACITY_VALUE];
static unsigned int mailbox_initialized;
static unsigned int mailbox_count_value;

static void mailbox_record_error(kernel_mailbox_record *mailbox, unsigned int error) {
    if (mailbox) {
        mailbox->last_error = error;
    }
}

static kernel_mailbox_record *mailbox_at(unsigned int mailbox_id) {
    if (mailbox_id >= KERNEL_MAILBOX_CAPACITY_VALUE ||
        !mailboxes[mailbox_id].active) {
        return 0;
    }
    return &mailboxes[mailbox_id];
}

static void clear_mailbox_queue_unsafe(kernel_mailbox_record *mailbox) {
    mailbox->head = 0;
    mailbox->tail = 0;
    mailbox->depth = 0;
    mailbox->last_error = KERNEL_MAILBOX_ERROR_NONE;
    for (unsigned int i = 0; i < KERNEL_MAILBOX_QUEUE_CAPACITY_VALUE; i++) {
        mailbox->queue[i] = 0;
    }
}

static void clear_registry_unsafe(void) {
    for (unsigned int i = 0; i < KERNEL_MAILBOX_CAPACITY_VALUE; i++) {
        mailboxes[i].active = 0;
        mailboxes[i].object_id = 0;
        mailboxes[i].name = 0;
        mailboxes[i].name_len = 0;
        mailboxes[i].sent = 0;
        mailboxes[i].received = 0;
        mailboxes[i].drops = 0;
        clear_mailbox_queue_unsafe(&mailboxes[i]);
    }
    mailbox_count_value = 0;
}

void kernel_mailbox_registry_init(void) {
    unsigned long flags = irq_save();
    clear_registry_unsafe();
    mailbox_initialized = 1;
    irq_restore(flags);
}

unsigned int kernel_mailbox_register(unsigned int mailbox_id,
                                     const unsigned char *name,
                                     unsigned int name_len) {
    if (mailbox_id >= KERNEL_MAILBOX_CAPACITY_VALUE) {
        kernel_panic("kernel-mailbox-bad-id");
    }

    unsigned int object_id = kernel_object_register(KERNEL_OBJECT_KIND_MAILBOX,
                                                    KERNEL_OBJECT_FLAG_ACTIVE,
                                                    name,
                                                    name_len);

    unsigned long flags = irq_save();
    if (!mailbox_initialized) {
        clear_registry_unsafe();
        mailbox_initialized = 1;
    }
    if (!mailboxes[mailbox_id].active) {
        mailbox_count_value++;
    }
    mailboxes[mailbox_id].active = 1;
    mailboxes[mailbox_id].object_id = object_id;
    mailboxes[mailbox_id].name = name;
    mailboxes[mailbox_id].name_len = name_len;
    mailboxes[mailbox_id].sent = 0;
    mailboxes[mailbox_id].received = 0;
    mailboxes[mailbox_id].drops = 0;
    clear_mailbox_queue_unsafe(&mailboxes[mailbox_id]);
    irq_restore(flags);
    return object_id;
}

unsigned int kernel_mailbox_count(void) {
    unsigned long flags = irq_save();
    unsigned int count = mailbox_count_value;
    irq_restore(flags);
    return count;
}

unsigned int kernel_mailbox_capacity(void) {
    return KERNEL_MAILBOX_CAPACITY_VALUE;
}

unsigned int kernel_mailbox_queue_capacity(void) {
    return KERNEL_MAILBOX_QUEUE_CAPACITY_VALUE;
}

unsigned int kernel_mailbox_object_id(unsigned int mailbox_id) {
    unsigned long flags = irq_save();
    kernel_mailbox_record *mailbox = mailbox_at(mailbox_id);
    unsigned int value = mailbox ? mailbox->object_id : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_mailbox_depth(unsigned int mailbox_id) {
    unsigned long flags = irq_save();
    kernel_mailbox_record *mailbox = mailbox_at(mailbox_id);
    unsigned int value = mailbox ? mailbox->depth : 0;
    irq_restore(flags);
    return value;
}

unsigned long kernel_mailbox_sent_count(unsigned int mailbox_id) {
    unsigned long flags = irq_save();
    kernel_mailbox_record *mailbox = mailbox_at(mailbox_id);
    unsigned long value = mailbox ? mailbox->sent : 0;
    irq_restore(flags);
    return value;
}

unsigned long kernel_mailbox_received_count(unsigned int mailbox_id) {
    unsigned long flags = irq_save();
    kernel_mailbox_record *mailbox = mailbox_at(mailbox_id);
    unsigned long value = mailbox ? mailbox->received : 0;
    irq_restore(flags);
    return value;
}

unsigned long kernel_mailbox_drop_count(unsigned int mailbox_id) {
    unsigned long flags = irq_save();
    kernel_mailbox_record *mailbox = mailbox_at(mailbox_id);
    unsigned long value = mailbox ? mailbox->drops : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_mailbox_last_error(unsigned int mailbox_id) {
    unsigned long flags = irq_save();
    kernel_mailbox_record *mailbox = mailbox_at(mailbox_id);
    unsigned int value = mailbox ? mailbox->last_error : KERNEL_MAILBOX_ERROR_BAD_ID;
    irq_restore(flags);
    return value;
}

int kernel_mailbox_send_u64(unsigned int mailbox_id, unsigned long value) {
    unsigned long flags = irq_save();
    kernel_mailbox_record *mailbox = mailbox_at(mailbox_id);
    if (!mailbox) {
        irq_restore(flags);
        return 0;
    }
    if (mailbox->depth >= KERNEL_MAILBOX_QUEUE_CAPACITY_VALUE) {
        mailbox->drops++;
        mailbox_record_error(mailbox, KERNEL_MAILBOX_ERROR_FULL);
        irq_restore(flags);
        return 0;
    }

    mailbox->queue[mailbox->tail] = value;
    mailbox->tail = (mailbox->tail + 1U) % KERNEL_MAILBOX_QUEUE_CAPACITY_VALUE;
    mailbox->depth++;
    mailbox->sent++;
    mailbox_record_error(mailbox, KERNEL_MAILBOX_ERROR_NONE);
    irq_restore(flags);
    return 1;
}

int kernel_mailbox_recv_u64(unsigned int mailbox_id, unsigned long *out) {
    unsigned long flags = irq_save();
    kernel_mailbox_record *mailbox = mailbox_at(mailbox_id);
    if (!mailbox) {
        irq_restore(flags);
        return 0;
    }
    if (mailbox->depth == 0) {
        mailbox_record_error(mailbox, KERNEL_MAILBOX_ERROR_EMPTY);
        irq_restore(flags);
        return 0;
    }

    unsigned long value = mailbox->queue[mailbox->head];
    mailbox->queue[mailbox->head] = 0;
    mailbox->head = (mailbox->head + 1U) % KERNEL_MAILBOX_QUEUE_CAPACITY_VALUE;
    mailbox->depth--;
    mailbox->received++;
    mailbox_record_error(mailbox, KERNEL_MAILBOX_ERROR_NONE);
    if (out) {
        *out = value;
    }
    irq_restore(flags);
    return 1;
}

void kernel_mailbox_clear(unsigned int mailbox_id) {
    unsigned long flags = irq_save();
    kernel_mailbox_record *mailbox = mailbox_at(mailbox_id);
    if (mailbox) {
        clear_mailbox_queue_unsafe(mailbox);
    }
    irq_restore(flags);
}

unsigned int kernel_mailbox_name_len(unsigned int mailbox_id) {
    unsigned long flags = irq_save();
    kernel_mailbox_record *mailbox = mailbox_at(mailbox_id);
    unsigned int value = mailbox ? mailbox->name_len : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_mailbox_name_byte(unsigned int mailbox_id, unsigned int offset) {
    unsigned long flags = irq_save();
    kernel_mailbox_record *mailbox = mailbox_at(mailbox_id);
    unsigned int value = 0;
    if (mailbox && offset < mailbox->name_len) {
        value = (unsigned int)mailbox->name[offset];
    }
    irq_restore(flags);
    return value;
}

int kernel_mailbox_selftest(void) {
    const unsigned int mailbox_id = 1U;
    unsigned long value = 0;

    if (!mailbox_at(mailbox_id)) {
        return 0;
    }
    kernel_mailbox_clear(mailbox_id);
    if (!kernel_mailbox_send_u64(mailbox_id, 0x13UL)) {
        return 0;
    }
    if (kernel_mailbox_depth(mailbox_id) != 1U) {
        return 0;
    }
    if (!kernel_mailbox_recv_u64(mailbox_id, &value)) {
        return 0;
    }
    if (value != 0x13UL || kernel_mailbox_depth(mailbox_id) != 0U) {
        return 0;
    }
    return 1;
}
