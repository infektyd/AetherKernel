"""S39 NET-021: post-el0 shell commands attested on certificate must be probe_shelled."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

# Fifteen print* commands attested on bootcert/certificate but missing probe_shell
# before S39 (xhci already had probe_shell @ net-iterate.sh:388).
POST_EL0_PROBE_COMMANDS = (
    ("syscall", 'probe_shell "syscall" "^syscall ok=1 version=48 .*"'),
    ("uaccess", 'probe_shell "uaccess" "^uaccess ok=1 version=49"'),
    ("usermode", 'probe_shell "usermode" "^usermode ok=1 version=50 fault_contained=1"'),
    ("process", 'probe_shell "process" "^process ok=1 version=51 .*"'),
    ("loader", 'probe_shell "loader" "^loader ok=1 version=52"'),
    ("multiprocess", 'probe_shell "multiprocess" "^multiprocess ok=1 version=53"'),
    ("sdhci", 'probe_shell "sdhci" "^sdhci ok=1 version=54 .*"'),
    ("card", 'probe_shell "card" "^card ok=1 version=55 .*"'),
    ("block", 'probe_shell "block" "^block ok=1 version=56 .*"'),
    ("fat32", 'probe_shell "fat32" "^fat32 ok=1 version=57 file=config.txt .*"'),
    ("mailbox", 'probe_shell "mailbox" "^mailbox ok=1 version=58 .*"'),
    ("framebuf", 'probe_shell "framebuf" "^framebuf ok=1 version=59 .*"'),
    ("console", 'probe_shell "console" "^console ok=1 version=60 .* display=0"'),
    ("pcie", 'probe_shell "pcie" "^pcie ok=1 version=61 .*"'),
    ("vl805", 'probe_shell "vl805" "^vl805 ok=1 version=62 .*"'),
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_s39_net021_post_el0_probe_shells_present() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    for cmd, probe_line in POST_EL0_PROBE_COMMANDS:
        assert f'probe shell: {cmd}' in net_iterate, f"missing probe comment for {cmd}"
        assert probe_line in net_iterate, f"missing probe_shell for {cmd}"


def test_s39_net021_help_lists_post_el0_probes() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    help_line = [
        line
        for line in net_iterate.splitlines()
        if line.strip().startswith('echo "shell probes:')
    ][0]

    for cmd, _ in POST_EL0_PROBE_COMMANDS:
        assert f" {cmd}" in help_line or help_line.rstrip('"').endswith(cmd)


def test_s39_net021_xhci_probe_unchanged() -> None:
    """xhci was already probe_shelled; NET-021 must not regress it."""
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    assert (
        'probe_shell "xhci" "^xhci ok=1 version=63 hciversion=0x100 ports=5 slots=32 scratch=31"'
        in net_iterate
    )


def test_s39_net021_vmm_asplit_el0_probes_unchanged() -> None:
    """S32/S35 locked regexes — do not weaken."""
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    assert 'probe_shell "vmm" "^vmm ok=1 version=50 pt=.* selftest=.*"' in net_iterate
    assert 'probe_shell "asplit" "^asplit ok=1 version=46"' in net_iterate
    assert 'probe_shell "el0" "^el0 ok=1 version=47"' in net_iterate
