#!/usr/bin/env python3
"""Launch the optimized GUI with telemetry and automatic crash capture."""

from __future__ import annotations

import argparse
import json
import os
import sys
from datetime import datetime, timezone
from pathlib import Path

import crash_capture


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=root / "build/src/glob2")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--no-telemetry", action="store_true")
    parser.add_argument(
        "--no-debugger",
        action="store_true",
        help="Do not keep LLDB attached to the GUI process",
    )
    parser.add_argument("--timeout", type=float, help="Optional wall-clock limit in seconds")
    args, game_args = parser.parse_known_args()
    if game_args[:1] == ["--"]:
        game_args = game_args[1:]

    binary = args.binary if args.binary.is_absolute() else root / args.binary
    binary = binary.resolve()
    if not binary.is_file():
        print(f"GUI binary not found: {binary}", file=sys.stderr)
        print("Build it with: scons release=1 -j4 build/src/glob2", file=sys.stderr)
        return 2
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output_dir = args.output_dir or root / "tournament-results/manual-runs" / stamp
    if not output_dir.is_absolute():
        output_dir = root / output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    command = [str(binary), *game_args]
    if not args.no_telemetry and "-nicowar-telemetry" not in command:
        command.append("-nicowar-telemetry")
    print(f"Launching Glob2; output is being captured in {output_dir}", flush=True)
    captured = crash_capture.run_process(
        command,
        cwd=root,
        timeout=args.timeout,
        environment=os.environ.copy(),
        crash_root=output_dir / "crashes",
        label="manual-gui",
        debugger=not args.no_debugger,
        live_output_path=output_dir / "output.log",
    )
    (output_dir / "output.log").write_text(captured.output, encoding="utf-8")
    run_record = {
        "command": command,
        "pid": captured.pid,
        "returncode": captured.returncode,
        "termination_signal": crash_capture.signal_name(captured.returncode),
        "timed_out": captured.timed_out,
        "interrupted": captured.interrupted,
        "wall_seconds": captured.wall_seconds,
        "crash": captured.crash,
    }
    (output_dir / "run.json").write_text(
        json.dumps(run_record, indent=2) + "\n", encoding="utf-8"
    )
    if captured.crash:
        print(f"Glob2 crashed; diagnostics: {captured.crash.get('artifact_dir')}", file=sys.stderr)
    elif captured.timed_out:
        print("Glob2 exceeded the requested timeout", file=sys.stderr)
    elif captured.interrupted:
        print("Glob2 was interrupted", file=sys.stderr)
    else:
        print(f"Glob2 exited with status {captured.returncode}", flush=True)
    return 124 if captured.timed_out else int(captured.returncode or 0)


if __name__ == "__main__":
    raise SystemExit(main())
