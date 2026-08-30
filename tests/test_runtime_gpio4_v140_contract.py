"""EPIC G V140: GPIO42 rising-edge detect after GENET. Fail-closed GPEDS. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_gpio4_rising_edge_detect_fail_closed_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_gpio4.c")
    gpio2 = read_repo("Sources/Support/kernel_gpio2.c")
    gpio_ro = read_repo("Sources/Support/kernel_gpio.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    roadmap = read_repo("docs/ROADMAP.md")

    assert "0xFE200000" in load
    assert "GPREN1" in load
    assert "GPEDS1" in load
    assert "0x50" in load  # GPREN1; 0x4C is GPREN0
    assert "0x44" in load  # GPEDS1; 0x40 is GPEDS0
    assert "42" in load
    assert "kernel_gpio2_selftest" in load
    assert "int           kernel_gpio4_selftest(void);" in support
    assert "unsigned int  kernel_gpio4_pin(void);" in support
    assert "unsigned int  kernel_gpio4_rise(void);" in support
    assert "unsigned int  kernel_gpio4_restore(void);" in support
    assert 'uartPuts("runtime v140: GPIO rising-edge detect\\n")' in app
    assert 'uartPuts("gpio4 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 140" not in app
    assert "func printGpio4()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "gpio4")' in shell
    assert ",pwm3,gpio4" in shell
    assert 'grep -qa "gpio4 ok=1 version=140 pin=42 rise="' in iterate
    assert 'probe_shell "gpio4" "^gpio4 ok=1 version=140 pin=42 rise="' in iterate
    assert 'grep -q "gpio4 ok=1 version=140 pin=42 rise="' in doctor
    assert "V140 GPIO42 rising-edge detect" in roadmap
    assert "kernel_gpio4_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    assert "watchdog_reset_now" not in load
    # Event-detect mechanism, not another FSEL/PUP/ALT0 clone.
    assert "GPFSEL1" not in load
    assert "PUP_PDN" not in load
    assert "0xE4" not in load
    assert "ALT0" not in load
    assert "GPREN" not in gpio2
    assert "GPEDS" not in gpio2
    assert "G32(GPFSEL0) =" not in gpio_ro
    assert "GPSET" not in gpio_ro
    assert "RBUF_64B_EN" not in load
    assert "DMA_TX_DO_CSUM" not in load
    assert "CMD_LCL_LOOP_EN" not in load
    assert "EXT_RGMII_OOB_CTRL" not in load
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_gpio4_selftest" in after
