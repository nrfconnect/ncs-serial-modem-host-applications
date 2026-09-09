# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

from utils.console import strip_ansi, strip_console_noise

# A log line as the shell emits it: colour, prompt, reset, cursor back, erase.
SHELL_WRAPPED_LINE = (
    "\x1b[1;32muart:~$ \x1b[m\x1b[8D\x1b[J"
    "[00:00:06.659,161] \x1b[0m<inf> main: Cloud connected\x1b[0m"
)


def test_strip_ansi_removes_escapes_and_carriage_returns() -> None:
    assert strip_ansi("\x1b[1;32mOK\x1b[m\r\n") == "OK\n"


def test_strip_ansi_keeps_the_shell_prompt() -> None:
    assert strip_ansi(SHELL_WRAPPED_LINE) == (
        "uart:~$ [00:00:06.659,161] <inf> main: Cloud connected"
    )


def test_strip_console_noise_leaves_only_the_log_line() -> None:
    assert strip_console_noise(SHELL_WRAPPED_LINE) == (
        "[00:00:06.659,161] <inf> main: Cloud connected"
    )


def test_strip_console_noise_leaves_plain_text_untouched() -> None:
    line = "[00:00:02.510,586] <inf> main: state_running_entry"

    assert strip_console_noise(line) == line


def test_strip_console_noise_is_idempotent() -> None:
    once = strip_console_noise(SHELL_WRAPPED_LINE)

    assert strip_console_noise(once) == once


def test_strip_console_noise_empties_a_bare_prompt_redraw() -> None:
    assert strip_console_noise("\x1b[1;32muart:~$ \x1b[m\x1b[8D\x1b[J").strip() == ""
