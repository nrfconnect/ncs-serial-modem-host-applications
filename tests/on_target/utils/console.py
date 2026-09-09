# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Cleanup of Zephyr console output captured over a serial port."""

from __future__ import annotations

import re

# The shell colours its output and moves the cursor around, and it reprints its
# prompt whenever a log line interrupts it, so a bare `OK` reaches the capture as
# `\x1b[1;32muart:~$ \x1b[m\x1b[8D\x1b[JOK`.
_ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")
_SHELL_PROMPT = re.compile(r"uart:~\$\s*")


def strip_ansi(text: str) -> str:
    """Remove ANSI escape sequences and carriage returns for line-oriented parsing."""
    return _ANSI_ESCAPE.sub("", text).replace("\r", "")


def strip_console_noise(text: str) -> str:
    """Remove ANSI escape sequences and reprinted shell prompts from *text*."""
    return _SHELL_PROMPT.sub("", strip_ansi(text))
