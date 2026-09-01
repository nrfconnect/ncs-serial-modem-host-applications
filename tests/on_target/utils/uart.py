# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
from __future__ import annotations

import codecs
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
        self._log_lock = threading.Lock()
        self._serial: serial.Serial | None = None
        self._serial_open = threading.Event()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._watchdog = threading.Timer(timeout, self._timeout_stop)
        self._thread.start()
        self._watchdog.start()

    def _append_lines(self, lines: list[str]) -> None:
        if not lines:
            return

        blob = "".join(line + "\n" for line in lines)
        with self._log_lock:
            self.log += blob
            self.whole_log += blob
        if self.log_path is not None:
            with self.log_path.open("a", encoding="utf-8", errors="replace") as log_file:
                log_file.write(blob)
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

            self._serial = ser
            self._serial_open.set()

            # Buffers a character whose bytes straddle two reads, which a plain
            # bytes.decode() per read would turn into replacement characters.
            decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
            pending = ""
            while not self._stop.is_set():
                try:
                    # Drain whatever is buffered. One syscall per byte cannot
                    # keep up with the modem console at 1 Mbaud once debug
                    # logging is on, and the kernel buffer then overflows.
                    data = ser.read(max(ser.in_waiting, 1))
                except serial.SerialException as exc:
                    logger.error("Serial read failed on %s: %s", self.port, exc)
                    time.sleep(0.5)
                    continue

                if not data:
                    continue

                pending += decoder.decode(data)

                lines = pending.split("\n")
                pending = lines.pop()
                self._append_lines([line.strip() for line in lines])

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

    def write_line(self, text: str, *, timeout: float = 30.0) -> None:
        """Write *text* plus CRLF to the captured port, without pausing capture.

        Lets a test drive the Zephyr shell while the log keeps streaming, so
        waits that already matched earlier output stay valid.
        """
        if not self._serial_open.wait(timeout):
            raise TimeoutError(
                f"Timed out after {timeout:.0f}s waiting for {self.port} to open"
            )

        ser = self._serial
        if ser is None:
            raise RuntimeError(f"Capture on {self.port} is no longer running")

        ser.write(f"{text}\r\n".encode("utf-8"))
        ser.flush()

    def stop(self) -> None:
        self._watchdog.cancel()
        self._stop.set()
        self._thread.join(timeout=5)
        if self._thread.is_alive():
            logger.warning("UART reader thread did not stop within timeout")
        self._serial_open.clear()
        self._serial = None

    def _timeout_stop(self) -> None:
        logger.error("UART capture timed out on %s", self.port)
        self.stop()
