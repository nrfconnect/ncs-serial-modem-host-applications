# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
from __future__ import annotations

import threading
import time
from pathlib import Path

import serial

from utils.logger import get_logger

logger = get_logger()

DEFAULT_UART_TIMEOUT = 60 * 30


def _ordinal_suffix(value: int) -> str:
    if 10 <= value % 100 <= 20:
        return "th"
    return {1: "st", 2: "nd", 3: "rd"}.get(value % 10, "th")


class Uart:
    def __init__(
        self,
        port: str,
        *,
        timeout: int = DEFAULT_UART_TIMEOUT,
        baudrate: int = 115200,
        log_path: Path | None = None,
        serial_timeout: float = 1.0,
    ) -> None:
        self.port = port
        self.baudrate = baudrate
        self.serial_timeout = serial_timeout
        self.log_path = log_path
        self.log = ""
        self.whole_log = ""
        self._log_lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._watchdog = threading.Timer(timeout, self._timeout_stop)
        self._thread.start()
        self._watchdog.start()

    def _append_line(self, line: str) -> None:
        with self._log_lock:
            self.log = self.log + "\n" + line
            self.whole_log = self.whole_log + "\n" + line
        if self.log_path is not None:
            with self.log_path.open("a", encoding="utf-8", errors="replace") as log_file:
                log_file.write(line + "\n")
                log_file.flush()

    def snapshot_log(self) -> str:
        with self._log_lock:
            return self.whole_log

    def _reader(self) -> None:
        with serial.Serial(
            self.port,
            baudrate=self.baudrate,
            timeout=self.serial_timeout,
        ) as ser:
            # nRF DK debuggers tri-state UART lines until the host asserts DTR.
            ser.dtr = True
            ser.rts = True

            if ser.in_waiting:
                logger.warning(
                    "Serial port %s had %d buffered bytes; discarding before capture",
                    self.port,
                    ser.in_waiting,
                )
                ser.reset_input_buffer()

            line = ""
            while not self._stop.is_set():
                try:
                    data = ser.read(1)
                except serial.SerialException as exc:
                    logger.error("Serial read failed on %s: %s", self.port, exc)
                    time.sleep(0.5)
                    continue

                if not data:
                    continue

                try:
                    char = data.decode("utf-8")
                except UnicodeDecodeError:
                    continue

                line += char
                if char != "\n":
                    continue

                self._append_line(line.strip())
                line = ""

    def wait_for_substring(
        self,
        needle: str,
        *,
        timeout: float = 900.0,
        poll_interval: float = 1.0,
    ) -> str:
        """Block until *needle* appears in the captured log."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            captured = self.snapshot_log()
            if needle in captured:
                for line in captured.splitlines():
                    if needle in line:
                        return line
                return needle
            time.sleep(poll_interval)
        raise TimeoutError(
            f"Timed out after {timeout:.0f}s waiting for serial log line containing {needle!r}"
        )

    def wait_for_nth_occurrence(
        self,
        needle: str,
        count: int,
        *,
        timeout: float = 900.0,
        poll_interval: float = 1.0,
    ) -> str:
        """Block until *needle* appears *count* times in the captured log."""
        if count < 1:
            raise ValueError(f"count must be >= 1, got {count}")

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            captured = self.snapshot_log()
            seen = 0
            start = 0
            while True:
                index = captured.find(needle, start)
                if index < 0:
                    break
                seen += 1
                if seen == count:
                    tail = captured[index:]
                    for line in tail.splitlines():
                        if needle in line:
                            return line
                    return needle
                start = index + len(needle)
            time.sleep(poll_interval)
        raise TimeoutError(
            f"Timed out after {timeout:.0f}s waiting for {count} occurrences of "
            f"{needle!r} in serial log"
        )

    def wait_for_substring_after(
        self,
        needle: str,
        *,
        after: str,
        timeout: float = 900.0,
        poll_interval: float = 1.0,
    ) -> str:
        """Block until *needle* appears in the log after the first *after* marker."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            captured = self.snapshot_log()
            marker_index = captured.find(after)
            if marker_index >= 0:
                tail = captured[marker_index + len(after):]
                if needle in tail:
                    for line in tail.splitlines():
                        if needle in line:
                            return line
                    return needle
            time.sleep(poll_interval)
        raise TimeoutError(
            f"Timed out after {timeout:.0f}s waiting for serial log line containing "
            f"{needle!r} after {after!r}"
        )

    def wait_for_substring_after_nth(
        self,
        needle: str,
        *,
        after: str,
        after_count: int,
        timeout: float = 900.0,
        poll_interval: float = 1.0,
    ) -> str:
        """Block until *needle* appears after the *after_count* occurrence of *after*."""
        if after_count < 1:
            raise ValueError(f"after_count must be >= 1, got {after_count}")

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            captured = self.snapshot_log()
            start = 0
            seen_after = 0
            marker_index = -1
            while True:
                index = captured.find(after, start)
                if index < 0:
                    break
                seen_after += 1
                if seen_after == after_count:
                    marker_index = index
                    break
                start = index + len(after)

            if marker_index >= 0:
                tail = captured[marker_index + len(after):]
                if needle in tail:
                    for line in tail.splitlines():
                        if needle in line:
                            return line
                    return needle
            time.sleep(poll_interval)
        raise TimeoutError(
            f"Timed out after {timeout:.0f}s waiting for serial log line containing "
            f"{needle!r} after the {after_count}{_ordinal_suffix(after_count)} "
            f"occurrence of {after!r}"
        )

    def stop(self) -> None:
        self._watchdog.cancel()
        self._stop.set()
        self._thread.join(timeout=5)
        if self._thread.is_alive():
            logger.warning("UART reader thread did not stop within timeout")

    def _timeout_stop(self) -> None:
        logger.error("UART capture timed out on %s", self.port)
        self.stop()
