import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_support_declares_uart_rx_irq_driver_api() -> None:
    support = read_repo("Sources/Support/include/Support.h")

    for symbol in (
        "uart_rx_irq_init",
        "uart_rx_irq_enable",
        "uart_rx_irq_service",
        "uart_rx_ring_read_byte",
        "uart_rx_ring_count",
        "uart_rx_ring_capacity",
        "uart_rx_overflow_count",
    ):
        assert symbol in support


def test_uart_rx_irq_driver_uses_fixed_ring_and_no_irq_allocation() -> None:
    driver = read_repo("Sources/Support/uart_rx_irq.c")

    assert "UART_RX_RING_CAPACITY" in driver
    assert "uart_rx_ring" in driver
    assert "uart_rx_irq_service" in driver
    assert "malloc(" not in driver
    assert "free(" not in driver
    assert "swift_" not in driver


def test_swift_uart_async_byte_wait_surface_exists() -> None:
    uart_async = read_repo("Sources/Application/UARTRX.swift")

    assert "func uartReadByteAsync() async -> UInt8" in uart_async
    assert "UART RX PANIC: waiter already active" in uart_async
    assert "uart_rx_ring_read_byte" in uart_async


def test_uart_shell_no_longer_polls_rx_with_timer_sleep() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    assert "await uartReadByteAsync()" in shell
    assert "await timerSleepMillis(25)" not in shell
    assert "func pollUartShell()" not in shell


def test_irq_handler_routes_confirmed_uart0_intid() -> None:
    irq = read_repo("Sources/Application/IRQHandler.swift")
    gic = read_repo("Sources/Application/GIC.swift")

    assert "UART0_GIC_INTID" in irq
    assert "serviceUartRxIrq()" in irq
    assert "UART0_GIC_INTID" in gic
    assert "gicEnableInterrupt(UART0_GIC_INTID" in gic


def test_reboot_responses_are_drained_before_watchdog_reset() -> None:
    uart = read_repo("Sources/Application/UART.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")

    assert "func uartDrainTx()" in uart
    assert "FR_BUSY" in uart
    assert "uartDrainTx()" in shell
    assert shell.index("uartDrainTx()") < shell.index("watchdog_reset_now()")


def test_uart0_spi_is_targeted_to_cpu0() -> None:
    gic = read_repo("Sources/Application/GIC.swift")

    assert "GICD_ITARGETSR" in gic
    assert "gicSetTargetCpu0" in gic
    assert "gicSetTargetCpu0(intid)" in gic


def test_runtime_v4_preserves_runtime_v3_shell_contract_and_updates_net_iterate_gate() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    net_iterate = read_repo("net-iterate.sh")

    for marker in (
        "shell ready commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot",
        "status uptime_ms=",
        "heap total=",
        "queues ready=",
        "task fast count=",
        "shell reboot reason=command",
        "b == 0x72 || b == 0x52",
    ):
        assert marker in shell

    assert "runtime v4: irq-backed uart shell" in net_iterate
    assert "runtime v5: diagnostics shell" in net_iterate
    assert "shell ready commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot" in net_iterate
