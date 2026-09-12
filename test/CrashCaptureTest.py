#!/usr/bin/env python3

from __future__ import annotations

import json
import signal
import sys
import tempfile
import unittest
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import crash_capture


class CrashCaptureTest(unittest.TestCase):
    def test_live_output_is_readable_before_process_exits(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "output.log"
            release = root / "release"
            code = (
                "import pathlib,time\nprint('telemetry ready',flush=True)\n"
                f"while not pathlib.Path({str(release)!r}).exists(): time.sleep(.01)\n"
            )
            with ThreadPoolExecutor(max_workers=1) as executor:
                future = executor.submit(crash_capture.run_process,
                    [sys.executable, "-c", code], cwd=ROOT, timeout=5,
                    enable_core_dumps=False, live_output_path=output)
                try:
                    deadline = time.monotonic() + 3
                    while time.monotonic() < deadline:
                        if output.exists() and "telemetry ready" in output.read_text():
                            break
                        time.sleep(.01)
                    self.assertIn("telemetry ready", output.read_text())
                    self.assertFalse(future.done())
                finally:
                    release.touch()
                captured = future.result(timeout=5)
                self.assertEqual(captured.returncode, 0)
                self.assertEqual(captured.output, output.read_text())

    def test_nonzero_exit_creates_reproducible_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            captured = crash_capture.run_process(
                [sys.executable, "-c", "print('before failure'); raise SystemExit(7)"],
                cwd=ROOT,
                timeout=10,
                crash_root=root,
                label="unit test",
                enable_core_dumps=False,
            )
            self.assertEqual(captured.returncode, 7)
            self.assertIsNotNone(captured.crash)
            bundle = Path(str(captured.crash["artifact_dir"]))
            self.assertTrue((bundle / "crash.json").is_file())
            self.assertTrue((bundle / "reproduce.sh").is_file())
            self.assertIn("before failure", (bundle / "output.log").read_text())
            metadata = json.loads((bundle / "crash.json").read_text())
            self.assertEqual(metadata["failure_kind"], "nonzero_exit")
            self.assertEqual(metadata["returncode"], 7)
            self.assertEqual(metadata["pid"], captured.pid)

    @unittest.skipIf(sys.platform == "win32", "POSIX signal semantics")
    def test_signal_name_is_preserved(self):
        captured = crash_capture.run_process(
            ["/bin/sh", "-c", "kill -TERM $$"],
            cwd=ROOT,
            timeout=10,
            enable_core_dumps=False,
        )
        self.assertEqual(captured.returncode, -signal.SIGTERM)
        self.assertEqual(captured.result_fields()["termination_signal"], "SIGTERM")

    def test_timeout_is_not_mislabeled_as_a_crash(self):
        with tempfile.TemporaryDirectory() as directory:
            captured = crash_capture.run_process(
                [sys.executable, "-c", "import time; time.sleep(5)"],
                cwd=ROOT,
                timeout=0.05,
                crash_root=Path(directory),
                enable_core_dumps=False,
            )
            self.assertTrue(captured.timed_out)
            self.assertIsNone(captured.crash)
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_lldb_crash_transcript_recovers_target_pid_and_signal(self):
        output = """Process 43294 launched: '/tmp/glob2' (arm64)
Process 43294 stopped
* thread #1, stop reason = signal SIGABRT
"""
        returncode, pid = crash_capture._debugger_target_result(output, 0, 40000)
        self.assertEqual(returncode, -signal.SIGABRT)
        self.assertEqual(pid, 43294)


if __name__ == "__main__":
    unittest.main()
