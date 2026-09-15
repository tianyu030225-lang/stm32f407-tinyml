#!/usr/bin/env python3
"""Capture the STM32 text protocol without inventing or repairing samples.

The only non-standard dependency is ``pyserial``.  The board emits one
``D,...`` row per sample; structurally valid rows are written immediately to
one CSV per ``session_id``/``label`` pair.  Invalid rows are rejected and
counted.  A partial line is never completed with a guessed value.  ``R`` rows
use the firmware's integer ``confidence_milli`` field in the inclusive range
0..1000; they are validated but not written to the sample CSV.
Each capture directory is write-once: existing metadata or session CSV files    
are rejected so a board restart cannot silently mix acquisitions.
The optional ``--start-label`` and ``--windows`` arguments can send a dataset
command and stop after a fixed number of complete windows while keeping the
serial port exclusively open.  Active capture requires a matching START reply
within five seconds and accepts only the requested label and its first session.
On exit it stops only a capture it confirmed starting.  Without --start-label,
the tool only listens, including when --windows ends the capture.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, TextIO, Tuple

try:
    import serial  # type: ignore
except ImportError:  # pragma: no cover - exercised when the optional dependency is absent
    serial = None  # type: ignore


CSV_HEADER = [
    "session_id",
    "label",
    "window_seq",
    "sample_index",
    "tick_ms",
    "ax_raw",
    "ay_raw",
    "az_raw",
]
TOKEN_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}\Z")
UINT_RE = re.compile(r"[0-9]+\Z")
INT_RE = re.compile(r"-?[0-9]+\Z")
EXPECTED_AXIS_ORDER = ["ax_raw", "ay_raw", "az_raw"]
PROTOCOL_VERSION = 1
RAW_SCALE_G_PER_LSB = 1.0 / 8192.0
EXPECTED_METADATA = {
    "schema_version": 2,
    "protocol_version": PROTOCOL_VERSION,
    "format": "D,session_id,label,window_seq,sample_index,tick_ms,ax_raw,ay_raw,az_raw",
    "axis_order": EXPECTED_AXIS_ORDER,
    "window_size": 128,
    "sample_rate_hz": 100,
    "raw_dtype": "int16",
    "sensor": "MPU6050",
    "accel_range_g": 4,
    "accel_lsb_per_g": 8192,
    "raw_scale_g_per_lsb": RAW_SCALE_G_PER_LSB,
    "sensor_config": "current_compiled_configuration",
    "source": "STM32 USART1 text protocol",
}
CAPTURE_LABELS = ("STABLE", "VIBRATION", "IMPACT")
START_ACK_TIMEOUT_SECONDS = 5.0
COMMAND_TIMEOUT_SECONDS = 1.0


def _safe_token(value: str, field: str) -> str:
    """Return a path-safe protocol token, rejecting rather than rewriting it."""

    if not TOKEN_RE.fullmatch(value):
        raise ValueError(
            "%s must match [A-Za-z0-9][A-Za-z0-9_.-]{0,63}; refusing unsafe path token"
            % field
        )
    return value


def _parse_uint(value: str, field: str, maximum: int) -> int:
    if not UINT_RE.fullmatch(value):
        raise ValueError("%s is not a non-negative integer" % field)
    parsed = int(value, 10)
    if parsed > maximum:
        raise ValueError("%s is outside 0..%d" % (field, maximum))
    return parsed


def _parse_int16(value: str, field: str) -> int:
    if not INT_RE.fullmatch(value):
        raise ValueError("%s is not an integer" % field)
    parsed = int(value, 10)
    if parsed < -32768 or parsed > 32767:
        raise ValueError("%s is outside int16 range" % field)
    return parsed


class CsvSink:
    """Create one canonical CSV for each safe session/label pair."""

    def __init__(self, root: Path) -> None:
        self.root = root.resolve()
        self._handles: Dict[Tuple[str, str], Tuple[TextIO, csv.writer]] = {}
        self.paths: List[str] = []

    def _path_for(self, session_id: str, label: str) -> Path:
        _safe_token(session_id, "session_id")
        _safe_token(label, "label")
        candidate = (self.root / ("session_%s__label_%s.csv" % (session_id, label))).resolve()
        try:
            candidate.relative_to(self.root)
        except ValueError:
            raise ValueError("refusing output path outside output directory")
        return candidate

    def _open(self, session_id: str, label: str) -> Tuple[TextIO, csv.writer]:
        key = (session_id, label)
        if key in self._handles:
            return self._handles[key]

        path = self._path_for(session_id, label)
        if path.exists():
            raise RuntimeError(
                "%s already exists; refusing to mix a new capture with an existing session" % path
            )

        try:
            handle = path.open("x", newline="", encoding="utf-8")
        except FileExistsError:
            raise RuntimeError(
                "%s appeared during capture setup; refusing to mix sessions" % path
            )
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(CSV_HEADER)
        handle.flush()
        self._handles[key] = (handle, writer)
        self.paths.append(str(path))
        return handle, writer

    def write(self, row: List[object]) -> None:
        session_id = str(row[0])
        label = str(row[1])
        handle, writer = self._open(session_id, label)
        writer.writerow(row)
        handle.flush()

    def close(self) -> None:
        for handle, _writer in self._handles.values():
            handle.close()


def ensure_metadata(root: Path) -> Path:
    """Create or check the fixed schema metadata, never silently replace it."""

    try:
        root.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        raise RuntimeError("cannot create output directory: %s" % exc)
    metadata_path = (root / "dataset_metadata.json").resolve()
    try:
        metadata_path.relative_to(root.resolve())
    except ValueError:
        raise RuntimeError("refusing metadata path outside output directory")

    if metadata_path.exists() or any(root.glob("session_*.csv")):
        raise RuntimeError(
            "%s already contains capture artifacts; choose a new output directory" % root
        )

    try:
        with metadata_path.open("x", encoding="utf-8") as handle:
            json.dump(EXPECTED_METADATA, handle, indent=2, ensure_ascii=True)
            handle.write("\n")
    except OSError as exc:
        raise RuntimeError("cannot create metadata: %s" % exc)
    return metadata_path


def _new_stats() -> Dict[str, object]:
    return {
        "lines_seen": 0,
        "blank_lines": 0,
        "d_lines": 0,
        "r_lines": 0,
        "ok_lines": 0,
        "err_lines": 0,
        "unknown_lines": 0,
        "bad_rows": 0,
        "bad_d_rows": 0,
        "bad_r_rows": 0,
        "sequence_errors": 0,
        "sequence_gaps": 0,
        "rows_written": 0,
        "completed_windows": 0,
        "incomplete_windows": 0,
        "truncated_lines": 0,
        "oversize_lines": 0,
        "serial_disconnect": False,
        "capture_error": False,
        "start_confirmed": False,
        "stop_sent": False,
        "stop_error": None,
        "stop_reason": "unknown",
    }


class ProtocolParser:
    def __init__(self, sink: CsvSink, stats: Dict[str, object]) -> None:
        self.sink = sink
        self.stats = stats
        # Keep every observed window so an incomplete window is reported at EOF.
        self.windows: Dict[Tuple[str, str, int], Dict[str, object]] = {}
        self.latest_seq: Dict[Tuple[str, str], int] = {}

    def _bad(self, kind: str) -> None:
        self.stats["bad_rows"] = int(self.stats["bad_rows"]) + 1
        key = "bad_%s_rows" % kind
        self.stats[key] = int(self.stats[key]) + 1

    def handle(self, line: str) -> None:
        self.stats["lines_seen"] = int(self.stats["lines_seen"]) + 1
        if not line:
            self.stats["blank_lines"] = int(self.stats["blank_lines"]) + 1
            return

        kind = line.split(",", 1)[0]
        if kind == "D":
            self.stats["d_lines"] = int(self.stats["d_lines"]) + 1
            self._handle_d(line)
        elif kind == "R":
            self.stats["r_lines"] = int(self.stats["r_lines"]) + 1
            self._handle_r(line)
        elif line == "OK" or line.startswith("OK "):
            self.stats["ok_lines"] = int(self.stats["ok_lines"]) + 1
        elif line == "ERR" or line.startswith("ERR "):
            self.stats["err_lines"] = int(self.stats["err_lines"]) + 1
        else:
            self.stats["unknown_lines"] = int(self.stats["unknown_lines"]) + 1

    def _handle_d(self, line: str) -> None:
        fields = line.split(",")
        if len(fields) != 9:
            self._bad("d")
            return
        try:
            session_id = _safe_token(fields[1], "session_id")
            label = _safe_token(fields[2], "label")
            window_seq = _parse_uint(fields[3], "window_seq", 0xFFFFFFFF)
            sample_index = _parse_uint(fields[4], "sample_index", 127)
            tick_ms = _parse_uint(fields[5], "tick_ms", 0xFFFFFFFF)
            ax_raw = _parse_int16(fields[6], "ax_raw")
            ay_raw = _parse_int16(fields[7], "ay_raw")
            az_raw = _parse_int16(fields[8], "az_raw")
        except ValueError:
            self._bad("d")
            return

        stream = (session_id, label)
        previous = self.latest_seq.get(stream)
        if previous is None:
            self.latest_seq[stream] = window_seq
        elif window_seq < previous:
            self.stats["sequence_errors"] = int(self.stats["sequence_errors"]) + 1
            self._bad("d")
            return
        elif window_seq > previous:
            if window_seq > previous + 1:
                self.stats["sequence_gaps"] = int(self.stats["sequence_gaps"]) + (
                    window_seq - previous - 1
                )
            self.latest_seq[stream] = window_seq

        key = (session_id, label, window_seq)
        state = self.windows.setdefault(key, {"next_sample": 0, "complete": False})
        expected = int(state["next_sample"])
        if state["complete"] or sample_index != expected:
            self.stats["sequence_errors"] = int(self.stats["sequence_errors"]) + 1
            self._bad("d")
            return

        self.sink.write(
            [
                session_id,
                label,
                window_seq,
                sample_index,
                tick_ms,
                ax_raw,
                ay_raw,
                az_raw,
            ]
        )
        self.stats["rows_written"] = int(self.stats["rows_written"]) + 1
        state["next_sample"] = expected + 1
        if state["next_sample"] == 128:
            state["complete"] = True
            self.stats["completed_windows"] = int(self.stats["completed_windows"]) + 1

    def _handle_r(self, line: str) -> None:
        fields = line.split(",")
        if len(fields) != 6:
            self._bad("r")
            return
        try:
            _parse_uint(fields[1], "window_seq", 0xFFFFFFFF)
            _safe_token(fields[2], "class")
            _parse_uint(fields[3], "confidence_milli", 1000)
            _parse_uint(fields[4], "latency_us", 0xFFFFFFFF)
            if not fields[5] or len(fields[5]) > 64 or "\n" in fields[5]:
                raise ValueError("invalid alarm_flags")
        except (TypeError, ValueError):
            self._bad("r")

    def finish(self) -> None:
        self.stats["incomplete_windows"] = sum(
            1 for state in self.windows.values() if not state["complete"]
        )


def _send_command(board: "serial.Serial", command: str) -> None:
    payload = (command.rstrip("\r\n") + "\r\n").encode("ascii")
    deadline = time.monotonic() + COMMAND_TIMEOUT_SECONDS
    if board.write(payload) != len(payload):
        raise RuntimeError("serial command was only partially written")
    # pyserial.flush() has no timeout on Windows; bound the drain as well as
    # write() so cleanup cannot wait forever on a disconnected or stuck port.
    while board.out_waiting:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RuntimeError("serial command drain timed out")
        time.sleep(min(0.01, remaining))


def capture(
    port: str,
    baud: int,
    timeout: float,
    max_line_bytes: int,
    sink: CsvSink,
    start_label: Optional[str] = None,
    stop_after_windows: Optional[int] = None,
) -> Dict[str, object]:
    if serial is None:
        raise RuntimeError("pyserial is required; install it with `python -m pip install pyserial`")

    stats = _new_stats()
    parser = ProtocolParser(sink, stats)
    pending = bytearray()
    discarding_oversize = False
    board = None
    owns_capture = False
    startup_deadline = None
    capture_session = None
    try:
        board = serial.Serial(
            port=port,
            baudrate=baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=min(timeout, START_ACK_TIMEOUT_SECONDS) if start_label is not None else timeout,
            write_timeout=COMMAND_TIMEOUT_SECONDS,
        )
        stats["stop_reason"] = "running"
        if start_label is not None:
            _send_command(board, "DATASET START %s" % start_label)
            startup_deadline = time.monotonic() + START_ACK_TIMEOUT_SECONDS
        while True:
            if startup_deadline is not None and time.monotonic() >= startup_deadline:
                stats["capture_error"] = True
                stats["stop_reason"] = "start_timeout: no matching DATASET START acknowledgment"
                break
            try:
                chunk = board.read(256)
            except serial.SerialException as exc:
                stats["serial_disconnect"] = True
                stats["stop_reason"] = "serial_exception: %s" % exc
                break
            if not chunk:
                continue
            pending.extend(chunk)
            while True:
                if discarding_oversize:
                    newline = pending.find(b"\n")
                    if newline < 0:
                        pending.clear()
                        break
                    del pending[: newline + 1]
                    discarding_oversize = False
                    continue

                newline = pending.find(b"\n")
                if newline < 0:
                    if len(pending) > max_line_bytes:
                        stats["oversize_lines"] = int(stats["oversize_lines"]) + 1
                        stats["truncated_lines"] = int(stats["truncated_lines"]) + 1
                        pending.clear()
                        discarding_oversize = True
                    break

                raw_line = bytes(pending[:newline])
                del pending[: newline + 1]
                if len(raw_line) > max_line_bytes:
                    stats["oversize_lines"] = int(stats["oversize_lines"]) + 1
                    stats["truncated_lines"] = int(stats["truncated_lines"]) + 1
                    continue
                try:
                    text_line = raw_line.decode("utf-8", errors="strict").rstrip("\r")
                except UnicodeDecodeError:
                    stats["lines_seen"] = int(stats["lines_seen"]) + 1
                    stats["bad_rows"] = int(stats["bad_rows"]) + 1
                    continue
                if start_label is not None and not owns_capture:
                    if text_line == "OK DATASET START %s" % start_label:
                        owns_capture = True
                        stats["start_confirmed"] = True
                        startup_deadline = None
                        board.timeout = timeout
                    elif text_line == "ERR" or text_line.startswith(("ERR ", "OK DATASET START ")):
                        parser.handle(text_line)
                        stats["capture_error"] = True
                        stats["stop_reason"] = "start_rejected: %s" % text_line
                        break
                    else:
                        # The board may still be exporting an older capture.
                        # None of its D rows belong to this requested start.
                        if not text_line.startswith("D,"):
                            parser.handle(text_line)
                        continue
                fields = text_line.split(",")
                if owns_capture and fields[0] == "D" and len(fields) == 9:
                    if fields[2] != start_label or (
                        capture_session is not None and fields[1] != capture_session
                    ):
                        stats["capture_error"] = True
                        stats["stop_reason"] = "capture_changed: unexpected dataset label or session"
                        # Do not stop a session that no longer belongs to us.
                        owns_capture = False
                        break
                rows_before = int(stats["rows_written"])
                parser.handle(text_line)
                if owns_capture and capture_session is None and int(stats["rows_written"]) > rows_before:
                    capture_session = fields[1]
                if (
                    stop_after_windows is not None
                    and int(stats["completed_windows"]) >= stop_after_windows
                ):
                    stats["stop_reason"] = "window_limit"
                    break
            if stats["stop_reason"] == "window_limit" or stats["capture_error"]:
                break
    except KeyboardInterrupt:
        stats["stop_reason"] = "keyboard_interrupt"
    except (OSError, ValueError) as exc:
        stats["stop_reason"] = "open_error: %s" % exc
        raise RuntimeError(str(exc))
    finally:
        if pending and not discarding_oversize:
            stats["truncated_lines"] = int(stats["truncated_lines"]) + 1
        if board is not None:
            try:
                if owns_capture:
                    try:
                        _send_command(board, "DATASET STOP")
                        stats["stop_sent"] = True
                    except (OSError, ValueError, RuntimeError) as exc:
                        stats["capture_error"] = True
                        stats["stop_error"] = str(exc)
            finally:
                try:
                    board.close()
                except (OSError, ValueError) as exc:
                    stats["capture_error"] = True
                    stats["close_error"] = str(exc)
        parser.finish()
    return stats


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="serial port, for example COM5 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=0.5)
    parser.add_argument("--max-line-bytes", type=int, default=512)
    parser.add_argument(
        "--start-label",
        choices=CAPTURE_LABELS,
        help="send DATASET START <label> immediately after opening the port",
    )
    parser.add_argument(
        "--windows",
        dest="stop_after_windows",
        type=int,
        help="finish after this many complete windows; stop the board only if --start-label started this capture",
    )
    return parser


def main(argv: Optional[List[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    if (
        args.baud <= 0
        or args.timeout <= 0
        or args.max_line_bytes < 32
        or (args.stop_after_windows is not None and args.stop_after_windows <= 0)
    ):
        print("baud, timeout, max-line-bytes and windows must be positive", file=sys.stderr)
        return 2
    try:
        ensure_metadata(args.output_dir)
        sink = CsvSink(args.output_dir)
        try:
            stats = capture(
                args.port,
                args.baud,
                args.timeout,
                args.max_line_bytes,
                sink,
                args.start_label,
                args.stop_after_windows,
            )
        finally:
            sink.close()
    except RuntimeError as exc:
        print("capture not started: %s" % exc, file=sys.stderr)
        return 2

    stats["output_dir"] = str(args.output_dir.resolve())
    stats["csv_files"] = sink.paths
    print(json.dumps(stats, ensure_ascii=False, indent=2, sort_keys=True))
    return 2 if stats["serial_disconnect"] or stats["capture_error"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
