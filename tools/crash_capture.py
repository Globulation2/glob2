#!/usr/bin/env python3
"""Run Glob2 processes with reproducible crash diagnostics.

The tournament runners used to retain stdout but not the process identity or
termination signal.  This module also enables core dumps, records the exact
command/environment, and gathers platform crash evidence after an abnormal
exit.
"""

from __future__ import annotations

import json
import os
import platform
import re
import shlex
import shutil
import signal
import subprocess
import time
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Mapping, Sequence


TELEMETRY_MARKERS = ("MAXIMA_TELEMETRY\t", "NICOWAR_")


@dataclass(frozen=True)
class CapturedProcess:
    output: str
    returncode: int | None
    pid: int | None
    timed_out: bool
    interrupted: bool
    wall_seconds: float
    crash: dict[str, object] | None = None

    def result_fields(self) -> dict[str, object]:
        fields: dict[str, object] = {
            "process_pid": self.pid,
            "termination_signal": signal_name(self.returncode),
        }
        if self.crash is not None:
            fields.update(
                {
                    "failure_kind": self.crash["failure_kind"],
                    "crash_artifact": self.crash.get("artifact_dir", ""),
                    "crash": self.crash,
                }
            )
        return fields


def signal_name(returncode: int | None) -> str:
    if returncode is None or returncode >= 0:
        return ""
    try:
        return signal.Signals(-returncode).name
    except ValueError:
        return f"SIGNAL_{-returncode}"


def _safe_label(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_.-]+", "-", value).strip("-.")
    return cleaned or "glob2"


def _core_enabled_command(command: Sequence[str]) -> list[str]:
    if os.name != "posix":
        return list(command)
    # The shell immediately execs Glob2, so the reported PID remains the game
    # PID.  This avoids Python preexec_fn, which is unsafe in threaded runners.
    return [
        "/bin/sh",
        "-c",
        'ulimit -c unlimited 2>/dev/null || true; exec "$@"',
        "glob2-crash-capture",
        *command,
    ]


def _kill_process_group(process: subprocess.Popen[str], requested_signal: int) -> None:
    if process.poll() is not None:
        return
    try:
        if os.name == "posix":
            os.killpg(process.pid, requested_signal)
        elif requested_signal == signal.SIGTERM:
            process.terminate()
        else:
            process.kill()
    except ProcessLookupError:
        pass


def _diagnostic_report_dirs() -> list[Path]:
    if platform.system() != "Darwin":
        return []
    return [
        Path.home() / "Library/Logs/DiagnosticReports",
        Path("/Library/Logs/DiagnosticReports"),
    ]


def _recent_macos_reports(process_name: str, started_epoch: float) -> list[Path]:
    reports: list[Path] = []
    wanted = process_name.casefold()
    for directory in _diagnostic_report_dirs():
        try:
            candidates = directory.glob("*.ips")
            for candidate in candidates:
                try:
                    if wanted in candidate.name.casefold() and candidate.stat().st_mtime >= started_epoch - 1:
                        reports.append(candidate)
                except OSError:
                    continue
        except OSError:
            continue
    return sorted(set(reports))


def _macos_log(pid: int, started_at: datetime) -> str:
    if platform.system() != "Darwin" or not Path("/usr/bin/log").exists():
        return ""
    local_start = started_at.astimezone() - timedelta(seconds=2)
    local_end = datetime.now().astimezone() + timedelta(seconds=2)
    predicate = f"(processIdentifier == {pid}) OR (eventMessage CONTAINS[c] \"{pid}\")"
    try:
        result = subprocess.run(
            [
                "/usr/bin/log", "show", "--style", "compact",
                "--start", local_start.strftime("%Y-%m-%d %H:%M:%S"),
                "--end", local_end.strftime("%Y-%m-%d %H:%M:%S"),
                "--predicate", predicate,
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=15,
        )
        return result.stdout
    except (OSError, subprocess.TimeoutExpired):
        return ""


def _find_core(pid: int) -> Path | None:
    candidates = [Path(f"/cores/core.{pid}"), Path.cwd() / f"core.{pid}"]
    for candidate in candidates:
        try:
            if candidate.is_file():
                return candidate
        except OSError:
            continue
    return None


def _symbolicate_core(binary: str, core: Path) -> str:
    debugger = shutil.which("lldb")
    if debugger is None:
        return "LLDB is not installed; retain the core file for later analysis.\n"
    try:
        result = subprocess.run(
            [debugger, "--batch", "-o", "thread backtrace all",
             "-c", str(core), binary],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=60,
        )
        return result.stdout
    except (OSError, subprocess.TimeoutExpired) as error:
        return f"Could not run LLDB: {error}\n"


def _debugger_command(command: Sequence[str]) -> list[str] | None:
    debugger = shutil.which("lldb")
    if debugger is None:
        return None
    return [
        debugger, "--batch", "-o", "run",
        "-k", "thread backtrace all", "--", *command,
    ]


def _debugger_target_result(
    output: str, launcher_returncode: int | None, launcher_pid: int | None
) -> tuple[int | None, int | None]:
    launched = re.findall(r"Process (\d+) launched:", output)
    target_pid = int(launched[-1]) if launched else launcher_pid
    stopped = re.findall(r"stop reason = signal (SIG[A-Z0-9]+)", output)
    if stopped:
        member = getattr(signal, stopped[-1], None)
        if member is not None:
            return -int(member), target_pid
        return 1, target_pid
    exited = re.findall(r"Process \d+ exited with status = (-?\d+)", output)
    if exited:
        return int(exited[-1]), target_pid
    return launcher_returncode, target_pid


def _write_reproducer(
    path: Path,
    command: Sequence[str],
    cwd: Path,
    environment: Mapping[str, str],
) -> None:
    lines = ["#!/bin/sh", "set -eu", "ulimit -c unlimited 2>/dev/null || true"]
    lines.append(f"cd {shlex.quote(str(cwd))}")
    for key in sorted(environment):
        if key.startswith("GLOB2_"):
            lines.append(f"export {key}={shlex.quote(environment[key])}")
    lines.append(f"exec {shlex.join(command)}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    path.chmod(0o755)


def _write_crash_bundle(
    crash_root: Path,
    label: str,
    command: Sequence[str],
    cwd: Path,
    environment: Mapping[str, str],
    output: str,
    returncode: int,
    pid: int,
    started_at: datetime,
    started_epoch: float,
    wall_seconds: float,
    debugger_attached: bool,
    debugger_rerun_timeout: float | None,
) -> dict[str, object]:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    bundle = crash_root / f"{_safe_label(label)}-{stamp}-pid{pid}"
    bundle.mkdir(parents=True, exist_ok=False)
    failure_kind = "signal" if returncode < 0 else "nonzero_exit"
    telemetry_tail = [
        line for line in output.splitlines()
        if line.startswith(TELEMETRY_MARKERS)
    ][-25:]
    metadata: dict[str, object] = {
        "schema_version": 1,
        "captured_at": datetime.now(timezone.utc).isoformat(),
        "started_at": started_at.isoformat(),
        "wall_seconds": round(wall_seconds, 3),
        "platform": platform.platform(),
        "command": list(command),
        "cwd": str(cwd),
        "pid": pid,
        "returncode": returncode,
        "failure_kind": failure_kind,
        "signal": signal_name(returncode),
        "debugger_attached": debugger_attached,
        "telemetry_tail": telemetry_tail,
        "artifact_dir": str(bundle),
    }
    (bundle / "output.log").write_text(output, encoding="utf-8")
    _write_reproducer(bundle / "reproduce.sh", command, cwd, environment)

    core = None
    reports: list[Path] = []
    if returncode < 0 and not debugger_attached:
        deadline = time.monotonic() + 4.0
        while time.monotonic() < deadline:
            core = _find_core(pid)
            reports = _recent_macos_reports(Path(command[0]).name, started_epoch)
            if core is not None or reports:
                break
            time.sleep(0.1)

    if core is not None:
        retained_core = bundle / core.name
        try:
            os.link(core, retained_core)
            metadata["core_file"] = str(retained_core)
        except OSError:
            metadata["core_file"] = str(core)
        (bundle / "backtrace.txt").write_text(
            _symbolicate_core(command[0], core), encoding="utf-8"
        )
    elif debugger_attached:
        (bundle / "backtrace.txt").write_text(output, encoding="utf-8")

    copied_reports = []
    for report in reports:
        destination = bundle / report.name
        try:
            shutil.copy2(report, destination)
            copied_reports.append(str(destination))
        except OSError:
            continue
    if copied_reports:
        metadata["system_crash_reports"] = copied_reports

    if returncode < 0 and core is None and not debugger_attached:
        debugger_command = _debugger_command(command)
        if debugger_command is not None:
            rerun = run_process(
                command,
                cwd=cwd,
                timeout=debugger_rerun_timeout,
                environment=environment,
                debugger=True,
                enable_core_dumps=False,
            )
            rerun_path = bundle / "debugger-rerun.log"
            rerun_path.write_text(rerun.output, encoding="utf-8")
            reproduced = rerun.returncode is not None and rerun.returncode < 0
            metadata["debugger_rerun"] = {
                "reproduced": reproduced,
                "returncode": rerun.returncode,
                "signal": signal_name(rerun.returncode),
                "pid": rerun.pid,
                "log": str(rerun_path),
            }
            if reproduced:
                (bundle / "backtrace.txt").write_text(
                    rerun.output, encoding="utf-8"
                )

    system_log = _macos_log(pid, started_at) if returncode < 0 else ""
    if system_log:
        (bundle / "system.log").write_text(system_log, encoding="utf-8")
        metadata["system_log"] = str(bundle / "system.log")
    (bundle / "crash.json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
    )
    return metadata


def run_process(
    command: Sequence[str],
    *,
    cwd: Path,
    timeout: float | None,
    environment: Mapping[str, str] | None = None,
    crash_root: Path | None = None,
    label: str = "glob2",
    enable_core_dumps: bool = True,
    debugger: bool = False,
    live_output_path: Path | None = None,
) -> CapturedProcess:
    """Run a process and create a crash bundle for every abnormal exit."""
    command = [str(part) for part in command]
    cwd = Path(cwd).resolve()
    child_environment = dict(environment or os.environ)
    debugger_command = _debugger_command(command) if debugger else None
    debugger_attached = debugger_command is not None
    launch_command = debugger_command or list(command)
    if enable_core_dumps:
        launch_command = _core_enabled_command(launch_command)
    started_at = datetime.now(timezone.utc)
    started_epoch = time.time()
    started = time.monotonic()
    process: subprocess.Popen[str] | None = None
    output = ""
    timed_out = False
    interrupted = False
    live_output = None
    try:
        if live_output_path is not None:
            live_output_path = Path(live_output_path).resolve()
            live_output_path.parent.mkdir(parents=True, exist_ok=True)
            live_output = live_output_path.open("w", encoding="utf-8")
        process = subprocess.Popen(
            launch_command,
            cwd=cwd,
            env=child_environment,
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=live_output if live_output is not None else subprocess.PIPE,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            output, _ = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            _kill_process_group(process, signal.SIGTERM)
            try:
                output, _ = process.communicate(timeout=2)
            except subprocess.TimeoutExpired:
                _kill_process_group(process, signal.SIGKILL)
                output, _ = process.communicate()
        except KeyboardInterrupt:
            interrupted = True
            _kill_process_group(process, signal.SIGINT)
            try:
                output, _ = process.communicate(timeout=2)
            except subprocess.TimeoutExpired:
                _kill_process_group(process, signal.SIGKILL)
                output, _ = process.communicate()
    except OSError as error:
        output = str(error)
    finally:
        if live_output is not None:
            live_output.close()
            output = live_output_path.read_text(encoding="utf-8", errors="replace") + (output or "")
    wall_seconds = time.monotonic() - started
    returncode = process.returncode if process is not None else None
    result_pid = process.pid if process is not None else None
    if debugger_attached:
        returncode, result_pid = _debugger_target_result(output, returncode, result_pid)
    crash = None
    if (
        crash_root is not None
        and process is not None
        and returncode not in (None, 0)
        and not timed_out
        and not interrupted
    ):
        try:
            crash = _write_crash_bundle(
                Path(crash_root), label, command, cwd, child_environment,
                output, returncode, result_pid or process.pid, started_at,
                started_epoch, wall_seconds, debugger_attached, timeout,
            )
        except OSError as error:
            crash = {
                "failure_kind": "capture_error",
                "signal": signal_name(returncode),
                "error": str(error),
                "artifact_dir": "",
            }
    return CapturedProcess(
        output=output,
        returncode=returncode,
        pid=result_pid,
        timed_out=timed_out,
        interrupted=interrupted,
        wall_seconds=round(wall_seconds, 3),
        crash=crash,
    )
