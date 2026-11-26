# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
from __future__ import annotations

import re

# The shell colours output, moves the cursor and reprints its prompt after log
# lines, so a bare `OK` arrives as `\x1b[1;32muart:~$ \x1b[m\x1b[8D\x1b[JOK`.
_ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")
_SHELL_PROMPT = re.compile(r"uart:~\$\s*")


def strip_ansi(text: str) -> str:
    """Remove ANSI escape sequences and carriage returns for line-oriented parsing."""
    return _ANSI_ESCAPE.sub("", text).replace("\r", "")


def strip_console_noise(text: str) -> str:
    """Remove ANSI escape sequences and reprinted shell prompts from *text*."""
    return _SHELL_PROMPT.sub("", strip_ansi(text))
