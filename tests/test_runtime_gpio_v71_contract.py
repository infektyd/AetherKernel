"""EPIC G V71: BCM2711 GPIO register probe. Read-only. No boot event emit."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_gpio_is_read_only_2711_register_probe() -> None:
    gpio = read_repo("Sources/Support/kernel_gpio.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    genet = read_repo("Sources/Support/kernel_genet.c")

    assert "0xFE200000" in gpio
    assert "0xE4" in gpio  # GPIO_PUP_PDN_CNTRL_REG0, 2711-only
    assert "int           kernel_gpio_selftest(void);" in support
    assert 'uartPuts("runtime v71: GPIO register probe\\n")' in app
    assert 'uartPuts("gpio ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 71" not in app
    assert "func printGpio()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "gpio")' in shell
    assert ",xhci,genet,genet2,genet3,genet4,gpio" in shell
    assert 'grep -qa "gpio ok=1 version=71 fsel=.* pup=.* uart="' in iterate
    assert 'grep -qa "runtime v71: GPIO register probe"' in iterate
    assert 'probe_shell "gpio" "^gpio ok=1 version=71 fsel=.* pup=.* uart=1"' in iterate
    assert 'grep -qa "genet4 ok=1 version=70 serial=.* mbox=.* mac="' in iterate
    assert "sched12" in iterate
    assert "kernel_gpio_selftest" not in scheduler
    assert "i < 200000" not in gpio
    # Read-only: no GPFSEL/PUP writes, no IRQ, no DMA, no GENET RX enable.
    assert "G32(GPFSEL0) =" not in gpio
    assert "G32(GPFSEL1) =" not in gpio
    assert "G32(PUP_PDN0) =" not in gpio
    assert "alloc_dma" not in gpio
    assert "CMD_RX_EN)" in genet
