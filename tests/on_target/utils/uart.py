# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
from __future__ import annotations

import threading
import time
from pathlib import Path

import serial

from utils.logger import get_logger

logger = get_logger()

DEFAULT_UART_TIMEOUT = 60 * 30


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
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._watchdog = threading.Timer(timeout, self._timeout_stop)
        self._thread.start()
        self._watchdog.start()

    def _append_line(self, line: str) -> None:
        self.log = self.log + "\n" + line
        self.whole_log = self.whole_log + "\n" + line
        if self.log_path is not None:
            with self.log_path.open("a", encoding="utf-8", errors="replace") as log_file:
                log_file.write(line + "\n")
                log_file.flush()

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
            if needle in self.whole_log:
                for line in self.whole_log.splitlines():
                    if needle in line:
                        return line
                return needle
            time.sleep(poll_interval)
        raise TimeoutError(
            f"Timed out after {timeout:.0f}s waiting for serial log line containing {needle!r}"
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
