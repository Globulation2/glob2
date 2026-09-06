#!/usr/bin/env python3
"""Run real host/join/ready/leave/rejoin clients on loopback (no public YOG)."""
import argparse
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path, default=Path("output/lan-session-test"))
    args = parser.parse_args()
    binary = args.binary.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.setdefault("SDL_VIDEODRIVER", "dummy")
    env.setdefault("SDL_AUDIODRIVER", "dummy")
    host_log = output / "host.log"
    join_log = output / "join.log"
    with host_log.open("w") as host_out, join_log.open("w") as join_out:
        host = subprocess.Popen(
            [str(binary), "host", "127.0.0.1", "2", str(output / "host")],
            stdout=host_out, stderr=subprocess.STDOUT, env=env,
        )
        try:
            deadline = time.monotonic() + 20
            while "HOST roster=1" not in host_log.read_text():
                if host.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("Host did not publish a lobby")
                time.sleep(0.1)
            joined = subprocess.run(
                [str(binary), "join", "127.0.0.1", "2", str(output / "join")],
                stdout=join_out, stderr=subprocess.STDOUT, env=env, timeout=100,
            )
            if joined.returncode != 0:
                raise RuntimeError(f"Join/transfer/leave test exited {joined.returncode}")
            if host.wait(timeout=15) != 0:
                raise RuntimeError("Host roster/readiness verification failed")
        except (RuntimeError, subprocess.TimeoutExpired) as error:
            print(f"LAN session regression FAIL: {error}")
            print(host_log.read_text())
            print(join_log.read_text())
            return 1
        finally:
            if host.poll() is None:
                host.terminate()
                try:
                    host.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    host.kill()
                    host.wait()
    print(host_log.read_text())
    print(join_log.read_text())
    print("LAN session regression PASS: two ready/join/leave cycles and exact map downloads")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
