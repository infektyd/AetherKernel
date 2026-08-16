"""Shared helpers for historical COMMANDS_V* contract honesty (S58).

Historical per-version command lists must appear in order as a subsequence of
the live ``SHELL_COMMAND_LIST`` and of ``COMMANDS_V45``. Only V45 locks the
full live list byte-for-byte in shell, doctor, and net-iterate greps.
"""

from __future__ import annotations

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
UART_SHELL_PATH = "Sources/Application/UARTShell.swift"


def normalize_commands_str(commands: str) -> str:
    return commands.replace("\n", "").replace(" ", "")


def command_tokens(commands: str) -> list[str]:
    normalized = normalize_commands_str(commands)
    if not normalized.startswith("commands="):
        raise ValueError(f"expected commands= prefix, got {normalized[:40]!r}")
    return normalized[len("commands=") :].split(",")


def read_uart_shell() -> str:
    return (ROOT / UART_SHELL_PATH).read_text()


def live_shell_command_list_str() -> str:
    shell = read_uart_shell()
    match = re.search(
        r'let SHELL_COMMAND_LIST\s*=\s*\n?\s*"([^"]+)"',
        shell,
        re.DOTALL,
    )
    if not match:
        raise AssertionError("SHELL_COMMAND_LIST literal not found in UARTShell.swift")
    return match.group(1)


def live_shell_command_tokens() -> list[str]:
    return command_tokens(live_shell_command_list_str())


def assert_commands_era_subsequence(haystack: list[str], era_tokens: list[str], *, label: str, haystack_name: str) -> None:
    index = 0
    for token in era_tokens:
        while index < len(haystack) and haystack[index] != token:
            index += 1
        assert index < len(haystack), (
            f"{label}: era token {token!r} missing from ordered {haystack_name}"
        )
        index += 1


def assert_commands_era_subsequence_of_live(commands_era: str, *, label: str) -> None:
    """Every era command appears in order within live ``SHELL_COMMAND_LIST``."""
    era_tokens = command_tokens(commands_era)
    live_tokens = live_shell_command_tokens()
    assert_commands_era_subsequence(live_tokens, era_tokens, label=label, haystack_name="SHELL_COMMAND_LIST")


def assert_commands_era_subsequence_of_ceiling(
    commands_era: str,
    commands_ceiling: str,
    *,
    label: str,
) -> None:
    """Every era command appears in order within the live ceiling string."""
    era_tokens = command_tokens(commands_era)
    ceiling_tokens = command_tokens(commands_ceiling)
    assert_commands_era_subsequence(ceiling_tokens, era_tokens, label=label, haystack_name="ceiling")


def assert_commands_era_in_netboot_sources(
    commands_era: str,
    commands_ceiling: str,
    *sources: str,
    label: str,
) -> None:
    """Era is an ordered subsequence of ceiling; netboot greps lock the ceiling string."""
    assert_commands_era_subsequence_of_ceiling(commands_era, commands_ceiling, label=label)
    ceiling_norm = normalize_commands_str(commands_ceiling)
    for index, source in enumerate(sources):
        assert ceiling_norm in normalize_commands_str(source), (
            f"{label}: live command ceiling missing from netboot source[{index}]"
        )


# Back-compat aliases used by generated contract tests.
assert_commands_era_prefix_of_live = assert_commands_era_subsequence_of_live
