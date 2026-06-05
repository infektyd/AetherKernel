#include "Support.h"

#define UART_SHELL_BUFFER_CAPACITY 80U

static unsigned char shell_buffer[UART_SHELL_BUFFER_CAPACITY];
static unsigned int shell_buffer_len = 0;

void uart_shell_buffer_clear(void) {
    shell_buffer_len = 0;
}

unsigned int uart_shell_buffer_count(void) {
    return shell_buffer_len;
}

int uart_shell_buffer_append(unsigned int byte) {
    if (shell_buffer_len >= UART_SHELL_BUFFER_CAPACITY) {
        return 0;
    }

    shell_buffer[shell_buffer_len] = (unsigned char)(byte & 0xFFU);
    shell_buffer_len++;
    return 1;
}

unsigned int uart_shell_buffer_get(unsigned int index) {
    if (index >= shell_buffer_len) {
        return 0;
    }
    return shell_buffer[index];
}
